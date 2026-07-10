#include "woort_path.h"

#include "woort_diagnosis.h"
#include "woort_utf8.h"
#include "woort_platform.h"
#include "woort_spin.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   include <windows.h>
#elif defined(WOORT_PLATFORM_OS_APPLE)
#   include <mach-o/dyld.h>
#   include <sys/param.h>
#   include <unistd.h>
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   include <unistd.h>
#endif

#define WOORT_MAX_EXE_PATH_LEN 16384

static char* g_exe_path_cache = NULL;
static woort_RWSpinlock g_exe_path_lock;

void _woort_path_bootup(void)
{
    g_exe_path_cache = NULL;
    woort_rwspinlock_init(&g_exe_path_lock);
}

void _woort_path_shutdown(void)
{
    woort_rwspinlock_deinit(&g_exe_path_lock);

    free(g_exe_path_cache);
    g_exe_path_cache = NULL;
}

WOORT_NODISCARD static bool _woort_path_build_exe_cache(void)
{
    char* full_path = NULL;
    bool perm_fail = false;

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    {
        wchar_t wbuf[WOORT_MAX_EXE_PATH_LEN];
        DWORD len = GetModuleFileNameW(NULL, wbuf, WOORT_MAX_EXE_PATH_LEN);
        if (len == 0 || len >= WOORT_MAX_EXE_PATH_LEN)
            perm_fail = true;
        else
        {
            size_t u8len;
            char* u8path = woort_u16strtou8(
                (const char16_t*)wbuf, (size_t)len, &u8len);
            if (u8path == NULL)
                return false;   /* OOM: retryable */
            full_path = u8path;
        }
    }
#elif defined(WOORT_PLATFORM_OS_APPLE)
    {
        char buf[WOORT_MAX_EXE_PATH_LEN];
        uint32_t size = WOORT_MAX_EXE_PATH_LEN;
        if (_NSGetExecutablePath(buf, &size) != 0)
            perm_fail = true;
        else
        {
            char resolved[WOORT_MAX_EXE_PATH_LEN];
            const char* src = buf;
            if (realpath(buf, resolved) != NULL)
                src = resolved;
            full_path = (char*)malloc(strlen(src) + 1);
            if (full_path != NULL)
                strcpy(full_path, src);
            /* else: OOM, falls through */
        }
    }
#elif defined(WOORT_PLATFORM_OS_POSIX)
    {
        char buf[WOORT_MAX_EXE_PATH_LEN];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len < 0 || (size_t)len >= sizeof(buf) - 1)
            perm_fail = true;
        else
        {
            buf[len] = '\0';
            full_path = (char*)malloc((size_t)len + 1);
            if (full_path != NULL)
                strcpy(full_path, buf);
            /* else: OOM, falls through */
        }
    }
#else
    perm_fail = true;
#endif

    if (perm_fail)
    {
        /* Cache an empty string so we don't retry the OS on every call. */
        g_exe_path_cache = (char*)malloc(1);
        if (g_exe_path_cache == NULL)
            return false;   /* OOM: retryable */
        g_exe_path_cache[0] = '\0';
        return false;
    }

    if (full_path == NULL)
        return false;   /* OOM: retryable */

    /* In-place: the directory part is always shorter than full_path, so it
       fits within full_path's own buffer. full_path is then kept as the
       cache -- no second allocation and no separate normalization. */
    (void)woort_get_file_loc(full_path, full_path, strlen(full_path) + 1);
    g_exe_path_cache = full_path;
    return true;
}

WOORT_NODISCARD bool woort_set_exe_path(const char* path)
{
    const size_t len = strlen(path);
    char* const new_exe_path = (char*)malloc(len + 1);

    if (new_exe_path == NULL)
        return false;

    strcpy(new_exe_path, path);

    woort_rwspinlock_write_lock(&g_exe_path_lock);

    char* const old_exe_path = g_exe_path_cache;
    g_exe_path_cache = new_exe_path;

    woort_rwspinlock_write_unlock(&g_exe_path_lock);

    free(old_exe_path);

    return true;
}

WOORT_NODISCARD size_t woort_exe_path(char* buf, size_t bufsz)
{
    /* Fast path: cache already built, read under read-lock. */
    woort_rwspinlock_read_lock(&g_exe_path_lock);
    if (g_exe_path_cache != NULL)
    {
        const size_t len = strlen(g_exe_path_cache);
        if (bufsz != 0)
        {
            assert(buf != NULL);
            const size_t copy = (len < bufsz) ? len : bufsz - 1;
            memcpy(buf, g_exe_path_cache, copy);
            buf[copy] = '\0';
        }
        woort_rwspinlock_read_unlock(&g_exe_path_lock);
        return len;
    }
    woort_rwspinlock_read_unlock(&g_exe_path_lock);

    /* Slow path: build cache under write-lock. */
    woort_rwspinlock_write_lock(&g_exe_path_lock);
    /* Double-check: another thread may have built it while we waited. */
    if (g_exe_path_cache == NULL)
    {
        if (!_woort_path_build_exe_cache())
        {
            woort_rwspinlock_write_unlock(&g_exe_path_lock);
            return 0;
        }
    }

    const size_t len = strlen(g_exe_path_cache);
    if (bufsz != 0)
    {
        assert(buf != NULL);
        const size_t copy = (len < bufsz) ? len : bufsz - 1;
        memcpy(buf, g_exe_path_cache, copy);
        buf[copy] = '\0';
    }
    woort_rwspinlock_write_unlock(&g_exe_path_lock);
    return len;
}

WOORT_NODISCARD size_t woort_work_path(char* buf, size_t bufsz)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    {
        wchar_t wbuf[WOORT_MAX_EXE_PATH_LEN];
        DWORD wlen = GetCurrentDirectoryW(WOORT_MAX_EXE_PATH_LEN, wbuf);
        if (wlen == 0 || wlen >= WOORT_MAX_EXE_PATH_LEN)
            return 0;

        size_t u8len;
        char* u8 = woort_u16strtou8(
            (const char16_t*)wbuf, (size_t)wlen, &u8len);
        if (u8 == NULL)
            return 0;

        woort_normalize_path(u8);

        if (bufsz != 0)
        {
            assert(buf != NULL);

            size_t copy = (u8len < bufsz) ? u8len : bufsz - 1;
            memcpy(buf, u8, copy);
            buf[copy] = '\0';
        }
        free(u8);
        return u8len;
    }
#elif defined(WOORT_PLATFORM_OS_POSIX)
    {
        char tmp[WOORT_MAX_EXE_PATH_LEN];
        if (getcwd(tmp, sizeof(tmp)) == NULL)
            return 0;

        woort_normalize_path(tmp);
        size_t len = strlen(tmp);

        if (bufsz != 0)
        {
            assert(buf != NULL);

            size_t copy = (len < bufsz) ? len : bufsz - 1;
            memcpy(buf, tmp, copy);
            buf[copy] = '\0';
        }
        return len;
    }
#else
    (void)buf;
    (void)bufsz;
    return 0;
#endif
}

WOORT_NODISCARD bool woort_set_work_path(const char* path)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    {
        size_t wlen;
        char16_t* wpath = woort_u8strtou16(path, strlen(path), &wlen);
        if (wpath == NULL)
            return false;

        BOOL ok = SetCurrentDirectoryW((const wchar_t*)wpath);
        free(wpath);
        return ok != 0;
    }
#elif defined(WOORT_PLATFORM_OS_POSIX)
    {
        return chdir(path) == 0;
    }
#else
    (void)path;
    return false;
#endif
}

WOORT_NODISCARD size_t woort_get_file_loc(
    const char* path, char* buf, size_t bufsz)
{
    if (path == NULL)
    {
        if (bufsz != 0)
            buf[0] = '\0';
        return 0;
    }

    /* Locate the last directory separator (after normalization). Normalization
       only turns '\\' into '/' on Windows, so the separator position in the raw
       path is the same as in the normalized result. */
    const char* last = strrchr(path, '/');
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    const char* lastbs = strrchr(path, '\\');
    if (lastbs > last)
        last = lastbs;
#endif
    const size_t result_len = (last != NULL) ? (size_t)(last - path) : 0;

    if (bufsz != 0)
    {
        assert(buf != NULL);

        const size_t copy = (result_len < bufsz) ? result_len : bufsz - 1;
        if (buf != path)
            memcpy(buf, path, copy);
        buf[copy] = '\0';
        woort_normalize_path(buf);
    }

    return result_len;
}

void woort_normalize_path(char* path)
{
    if (path == NULL)
        return;

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    for (char* p = path; *p != '\0'; p++)
    {
        if (*p == '\\')
            *p = '/';
    }

    if (path[0] != '\0' && path[1] == ':')
    {
        if (path[0] >= 'a' && path[0] <= 'z')
            path[0] = (char)(path[0] - 'a' + 'A');
    }
#else
    (void)path;
#endif
}
