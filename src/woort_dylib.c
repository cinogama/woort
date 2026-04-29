#include "woort_dylib.h"
#include "woort_path.h"

#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_hashmap.h"
#include "woort_util.h"
#include "woort_threads.h"
#include "woort_utf8.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <assert.h>

/* ================================================================
 * Platform abstraction
 * ================================================================ */

#ifndef WOORT_DYLIB_DISABLED

#if defined(_WIN32) || defined(_WIN64)

#   define WOORT_DYLIB_EXT ".dll"

static void* _woort_dylib_os_loadlib(const char* path)
{
    assert(path != NULL);

    size_t wlen;
    char16_t* wpath = woort_u8strtou16(path, strlen(path), &wlen);
    if (wpath == NULL)
        return NULL;

    void* handle = (void*)LoadLibraryExW(
        (const wchar_t*)wpath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    free(wpath);
    return handle;
}

static void* _woort_dylib_os_loadfunc(void* handle, const char* name)
{
    return (void*)GetProcAddress((HMODULE)handle, name);
}

static void _woort_dylib_os_freelib(void* handle)
{
    FreeLibrary((HMODULE)handle);
}

#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__MACH__)

#   if defined(__APPLE__)
#       define WOORT_DYLIB_EXT ".dylib"
#   else
#       define WOORT_DYLIB_EXT ".so"
#   endif

static void* _woort_dylib_os_loadlib(const char* path)
{
    assert(path == NULL);

    return dlopen(path, RTLD_LAZY);
}

static void* _woort_dylib_os_loadfunc(void* handle, const char* name)
{
    return dlsym(handle, name);
}

static void _woort_dylib_os_freelib(void* handle)
{
    dlclose(handle);
}

#endif /* platform */

/* ================================================================
 * Helpers
 * ================================================================ */

static bool _woort_dylib_file_exists(const char* path)
{
    assert(path != NULL);

    struct stat st;
    return stat(path, &st) == 0;
}

static char* _woort_dylib_build_search_path(const char* dir, const char* name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    char* result = (char*)malloc(dir_len + 1 + name_len + 1);
    if (result == NULL)
        return NULL;
    memcpy(result, dir, dir_len);
    result[dir_len] = '/';
    memcpy(result + dir_len + 1, name, name_len);
    result[dir_len + 1 + name_len] = '\0';
    return result;
}

static char* _woort_dylib_append_ext(const char* name, const char* ext)
{
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    char* result = (char*)malloc(name_len + ext_len + 1);
    if (result == NULL)
        return NULL;
    memcpy(result, name, name_len);
    memcpy(result + name_len, ext, ext_len);
    result[name_len + ext_len] = '\0';
    return result;
}

static void* _woort_dylib_try_open_lib(const char* path)
{
    if (_woort_dylib_file_exists(path))
    {
        void* handle = _woort_dylib_os_loadlib(path);
        if (handle == NULL)
        {
            WOORT_DEBUG("Failed to load library '%s', broken file or "
                "failed to load dependence?", path);
        }
        return handle;
    }
    return NULL;
}

#endif /* WOORT_DYLIB_DISABLED */

/* ================================================================
 * Global named library registry
 * ================================================================ */

static woort_HashMap/* const char*, woort_Dylib* */          g_named_libs;
static woort_RecursiveMutex* g_named_libs_mx;

bool _woort_dylib_bootup(void)
{
    if (!woort_recursive_mutex_create(&g_named_libs_mx))
        return false;

    woort_hashmap_init(
        &g_named_libs,
        sizeof(const char*),
        sizeof(woort_Dylib*),
        woort_util_cstr_hash,
        woort_util_cstr_equal);

    return true;
}

static void _woort_dylib_registry_remove(woort_Dylib* dylib);

static void _woort_dylib_free_fake_function_list(woort_ExternLibFunc* fake_libs)
{
    woort_ExternLibFunc* f = fake_libs;
    while (f->m_name != NULL)
    {
        free((void*)f->m_name);
        ++f;
    }
    free(fake_libs);
}

static bool _woort_dylib_resolved_funcs_foreach_free(
    const void* key,
    void* value,
    /* OPTIONAL */ void* user_data)
{
    (void)key;
    (void)user_data;
    free(*(char**)value);
    return true;
}

static void _woort_dylib_try_record_resolved(
    woort_Dylib* lib,
    const char* funcname,
    void* func_addr)
{
    if (func_addr == NULL)
        return;

    /* Copy name outside the lock to minimize critical section */
    size_t name_len = strlen(funcname);
    char* name_copy = (char*)malloc(name_len + 1);
    if (name_copy == NULL)
        return;
    memcpy(name_copy, funcname, name_len + 1);

    woort_rwspinlock_write_lock(&lib->m_resolved_lock);
    {
        woort_hashmap_Result res = woort_hashmap_insert(
            &lib->m_resolved_funcs, &func_addr, &name_copy);
        if (res != WOORT_HASHMAP_RESULT_OK)
            free(name_copy);
    }
    woort_rwspinlock_write_unlock(&lib->m_resolved_lock);
}

static void _woort_dylib_close(woort_Dylib* dylib)
{
#ifndef WOORT_DYLIB_DISABLED
    if (dylib->m_native_handle != NULL)
        _woort_dylib_os_freelib(dylib->m_native_handle);
#endif

    if (dylib->m_fake_funcs != NULL)
    {
        _woort_dylib_free_fake_function_list(dylib->m_fake_funcs);
    }

    (void)woort_hashmap_foreach(&dylib->m_resolved_funcs,
        _woort_dylib_resolved_funcs_foreach_free, NULL);
    woort_hashmap_deinit(&dylib->m_resolved_funcs);
    woort_rwspinlock_deinit(&dylib->m_resolved_lock);

    free(dylib->m_name);
    free(dylib);
}

static bool _woort_dylib_shutdown_foreach_callback(
    const void* key,
    void* value,
    /* OPTIONAL */ void* user_data)
{
    const char* name = *(const char**)key;

    woort_log("WOORT: Unloading library '%s' during shutdown.\n", name);
    woort_Dylib* const dylib = *(woort_Dylib**)value;

    woort_DylibLeaveFunc const leave =
        woort_dylib_load_func(dylib, "woort_lib_leave");
    if (leave != NULL)
        leave();

    _woort_dylib_close(dylib);

    return true;
}

void _woort_dylib_shutdown(void)
{
    woort_recursive_mutex_lock(g_named_libs_mx);

    if (g_named_libs.m_size > 0)
    {
        const size_t remaining = g_named_libs.m_size;

        /* Collect dylib pointers safely via foreach
           (no removal during iteration avoids corrupting the linked list). */
        woort_hashmap_foreach(&g_named_libs,
            _woort_dylib_shutdown_foreach_callback, NULL);

        woort_log("WOORT: %zu library(s) loaded by 'woort_dylib_load' "
            "unloaded during shutdown.\n", remaining);
    }

    woort_recursive_mutex_unlock(g_named_libs_mx);

    woort_hashmap_deinit(&g_named_libs);
    woort_recursive_mutex_destroy(g_named_libs_mx);
    g_named_libs_mx = NULL;
}

static woort_Dylib* _woort_dylib_registry_find(const char* name)
{
    void* value_storage = NULL;
    if (woort_hashmap_find(&g_named_libs, (const void*)&name, &value_storage))
        return *(woort_Dylib**)value_storage;
    return NULL;
}

static void _woort_dylib_registry_insert(woort_Dylib* dylib)
{
    woort_Dylib* ptr = dylib;
    (void)woort_hashmap_insert(
        &g_named_libs, (const void*)&dylib->m_name, &ptr);

    woort_DylibEntryFunc const entry =
        woort_dylib_load_func(dylib, "woort_lib_entry");

    if (entry != NULL)
        entry(dylib);
}

static void _woort_dylib_registry_remove(woort_Dylib* dylib)
{
    (void)woort_hashmap_remove(
        &g_named_libs, (const void*)&dylib->m_name);
}

/* ================================================================
 * Public API
 * ================================================================ */

WOORT_NODISCARD /* OPTIONAL */ woort_Dylib* woort_dylib_fake(
    const char* libname,
    const woort_ExternLibFunc* funcs,
    /* OPTIONAL */ woort_Dylib* dependence_dylib)
{
    if (libname == NULL || funcs == NULL)
        return NULL;

    woort_recursive_mutex_lock(g_named_libs_mx);

    if (_woort_dylib_registry_find(libname) != NULL)
    {
        woort_recursive_mutex_unlock(g_named_libs_mx);
        return NULL;
    }

    woort_Dylib* dylib = (woort_Dylib*)malloc(sizeof(woort_Dylib));
    if (dylib == NULL)
    {
        woort_recursive_mutex_unlock(g_named_libs_mx);
        return NULL;
    }

    dylib->m_native_handle = NULL;

    dylib->m_dependenced = dependence_dylib;
    if (dylib->m_dependenced != NULL)
        woort_dylib_keep(dylib->m_dependenced);

    dylib->m_name = (char*)malloc(strlen(libname) + 1);
    if (dylib->m_name == NULL)
    {
        free(dylib);
        woort_recursive_mutex_unlock(g_named_libs_mx);
        return NULL;
    }
    strcpy(dylib->m_name, libname);
    woort_atomic_store_explicit(&dylib->m_use_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    woort_rwspinlock_init(&dylib->m_resolved_lock);
    woort_hashmap_init(
        &dylib->m_resolved_funcs,
        sizeof(void*),
        sizeof(const char*),
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    /* Deep copy the funcs array */
    {
        size_t count = 0;
        const woort_ExternLibFunc* f = funcs;
        while (f->m_name != NULL)
        {
            ++count;
            ++f;
        }

        woort_ExternLibFunc* copy = (woort_ExternLibFunc*)malloc(
            (count + 1) * sizeof(woort_ExternLibFunc));
        if (copy == NULL)
        {
            free(dylib->m_name);
            free(dylib);
            woort_recursive_mutex_unlock(g_named_libs_mx);
            return NULL;
        }

        for (size_t i = 0; i < count; ++i)
        {
            size_t name_len = strlen(funcs[i].m_name);
            copy[i].m_name = (const char*)malloc(name_len + 1);
            if (copy[i].m_name == NULL)
            {
                for (size_t j = 0; j < i; ++j)
                    free((void*)copy[j].m_name);
                free(copy);
                free(dylib->m_name);
                free(dylib);
                woort_recursive_mutex_unlock(g_named_libs_mx);
                return NULL;
            }
            memcpy((void*)copy[i].m_name, funcs[i].m_name, name_len + 1);
            copy[i].m_func_addr = funcs[i].m_func_addr;
        }

        /* Sentinel */
        copy[count].m_name = NULL;
        copy[count].m_func_addr = NULL;

        dylib->m_fake_funcs = copy;
    }

    if (dependence_dylib != NULL)
    {
        woort_atomic_fetch_add_explicit(
            &dependence_dylib->m_use_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }

    _woort_dylib_registry_insert(dylib);

    woort_recursive_mutex_unlock(g_named_libs_mx);
    return dylib;
}

WOORT_NODISCARD /* OPTIONAL */ woort_Dylib* woort_dylib_load(
    const char* libname,
    const char* path,
    /* OPTIONAL */ const char* script_path,
    bool panic_when_fail)
{
#ifndef WOORT_DYLIB_DISABLED
    woort_recursive_mutex_lock(g_named_libs_mx);

    /* Already loaded? */
    woort_Dylib* const existing = _woort_dylib_registry_find(libname);
    if (existing != NULL)
    {
        woort_dylib_keep(existing);
        woort_recursive_mutex_unlock(g_named_libs_mx);
        return existing;
    }

    void* native_handle = NULL;

    char* name_with_ext = _woort_dylib_append_ext(path, WOORT_DYLIB_EXT);
    if (name_with_ext == NULL)
    {
        woort_recursive_mutex_unlock(g_named_libs_mx);
        if (panic_when_fail)
            woort_panic(WOORT_PANIC_USER, "Failed to load library: out of memory.");
        return NULL;
    }

    /* 1) Try from script_path */
    if (script_path != NULL)
    {
        char* script_dir = woort_get_file_loc(script_path);
        if (script_dir != NULL)
        {
            char* try_path = script_dir[0] != '\0'
                ? _woort_dylib_build_search_path(script_dir, name_with_ext)
                : NULL;
            free(script_dir);

            if (try_path != NULL)
            {
                native_handle = _woort_dylib_try_open_lib(try_path);
                free(try_path);
            }
        }
    }

    /* 2) Try from work_path */
    if (native_handle == NULL)
    {
        char* wd = woort_work_path();
        if (wd != NULL)
        {
            char* try_path = _woort_dylib_build_search_path(wd, name_with_ext);
            if (try_path != NULL)
            {
                native_handle = _woort_dylib_try_open_lib(try_path);
                free(try_path);
            }
            free(wd);
        }
    }

    /* 3) Try from exe_path */
    if (native_handle == NULL)
    {
        char* ed = woort_exe_path();
        if (ed != NULL)
        {
            char* try_path = _woort_dylib_build_search_path(ed, name_with_ext);
            if (try_path != NULL)
            {
                native_handle = _woort_dylib_try_open_lib(try_path);
                free(try_path);
            }
            free(ed);
        }
    }

    free(name_with_ext);

    /* 4) Try path as-is (without extension appended) */
    if (native_handle == NULL)
    {
        native_handle = _woort_dylib_try_open_lib(path);
    }

    /* 5) OS default search (only if no script_path was given) */
    if (native_handle == NULL && script_path == NULL)
    {
        native_handle = _woort_dylib_os_loadlib(path);
    }

    if (native_handle == NULL)
    {
        woort_recursive_mutex_unlock(g_named_libs_mx);
        if (panic_when_fail)
            woort_panic(WOORT_PANIC_USER, "Failed to load library '%s'.", libname);
        return NULL;
    }

    /* Create dylib entry */
    woort_Dylib* dylib = (woort_Dylib*)malloc(sizeof(woort_Dylib));
    if (dylib == NULL)
    {
        _woort_dylib_os_freelib(native_handle);
        woort_recursive_mutex_unlock(g_named_libs_mx);
        if (panic_when_fail)
            woort_panic(WOORT_PANIC_USER, "Failed to load library: out of memory.");
        return NULL;
    }

    dylib->m_native_handle = native_handle;
    dylib->m_fake_funcs = NULL;
    dylib->m_dependenced = NULL;
    dylib->m_name = (char*)malloc(strlen(libname) + 1);
    if (dylib->m_name == NULL)
    {
        _woort_dylib_os_freelib(native_handle);
        free(dylib);
        woort_recursive_mutex_unlock(g_named_libs_mx);
        if (panic_when_fail)
            woort_panic(WOORT_PANIC_USER, "Failed to load library: out of memory.");
        return NULL;
    }
    strcpy(dylib->m_name, libname);
    woort_atomic_store_explicit(&dylib->m_use_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    woort_rwspinlock_init(&dylib->m_resolved_lock);
    woort_hashmap_init(
        &dylib->m_resolved_funcs,
        sizeof(void*),
        sizeof(const char*),
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    _woort_dylib_registry_insert(dylib);

    woort_recursive_mutex_unlock(g_named_libs_mx);

    return dylib;
#else /* WOORT_DYLIB_DISABLED */
    (void)libname;
    (void)path;
    (void)script_path;
    if (panic_when_fail)
        woort_panic(WOORT_PANIC_USER, "Dynamic library loading not supported on this platform.");
    return NULL;
#endif
}

WOORT_NODISCARD /* OPTIONAL */ void* woort_dylib_load_func(
    woort_Dylib* lib,
    const char* funcname)
{
    if (lib->m_fake_funcs != NULL)
    {
        const woort_ExternLibFunc* f = lib->m_fake_funcs;
        while (f->m_name != NULL)
        {
            if (strcmp(f->m_name, funcname) == 0)
            {
                _woort_dylib_try_record_resolved(lib, funcname, f->m_func_addr);
                return f->m_func_addr;
            }
            f++;
        }
        return NULL;
    }

#ifndef WOORT_DYLIB_DISABLED
    if (lib->m_native_handle != NULL)
    {
        void* result = _woort_dylib_os_loadfunc(lib->m_native_handle, funcname);
        _woort_dylib_try_record_resolved(lib, funcname, result);
        return result;
    }
#endif

    return NULL;
}

WOORT_NODISCARD /* OPTIONAL */ const char* woort_dylib_get_func_name(
    woort_Dylib* lib,
    void* func_addr)
{
    if (lib == NULL || func_addr == NULL)
        return NULL;

    const char* name = NULL;
    woort_rwspinlock_read_lock(&lib->m_resolved_lock);
    {
        void* value_addr;
        if (woort_hashmap_find(&lib->m_resolved_funcs, &func_addr, &value_addr))
            name = *(const char**)value_addr;
    }
    woort_rwspinlock_read_unlock(&lib->m_resolved_lock);
    return name;
}

void woort_dylib_unload(woort_Dylib* lib, woort_DylibUnloadMethod method)
{
    bool should_free = false;

    if ((method & WOORT_DYLIB_UNREF) != 0)
    {
        if (woort_atomic_fetch_sub_explicit(
            &lib->m_use_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED) == 1)
        {
            should_free = true;
        }
    }

    if ((method & WOORT_DYLIB_BURY) != 0
        || should_free)
    {
        woort_recursive_mutex_lock(g_named_libs_mx);

        _woort_dylib_registry_remove(lib);
        if (should_free)
        {
            woort_DylibLeaveFunc const leave =
                woort_dylib_load_func(lib, "woort_lib_leave");

            if (leave != NULL)
                leave();
        }

        woort_recursive_mutex_unlock(g_named_libs_mx);
    }

    if (should_free)
    {
        /* Release dependency */
        if (lib->m_dependenced != NULL)
            woort_dylib_unload(lib->m_dependenced, WOORT_DYLIB_UNREF);

        _woort_dylib_close(lib);
    }
}

void woort_dylib_keep(woort_Dylib* lib)
{
    woort_atomic_fetch_add_explicit(
        &lib->m_use_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
}
