#include "woort_path.h"

#include "woort_diagnosis.h"
#include "woort_utf8.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32) || defined(_WIN64)
#   include <windows.h>
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#   include <sys/param.h>
#   include <unistd.h>
#elif defined(__unix__) || defined(__unix)
#   include <unistd.h>
#endif

#define WOORT_MAX_EXE_PATH_LEN 16384

static char* g_exe_path_cache = NULL;

void _woort_path_bootup(void)
{
    g_exe_path_cache = NULL;
}

void _woort_path_shutdown(void)
{
    free(g_exe_path_cache);
    g_exe_path_cache = NULL;
}

WOORT_NODISCARD char* woort_exe_path(void)
{
    if (g_exe_path_cache != NULL)
    {
        char* copy = (char*)malloc(strlen(g_exe_path_cache) + 1);
        if (copy != NULL)
            strcpy(copy, g_exe_path_cache);
        return copy;
    }

    char* full_path = NULL;

#if defined(_WIN32) || defined(_WIN64)
    {
        wchar_t wbuf[WOORT_MAX_EXE_PATH_LEN];
        DWORD len = GetModuleFileNameW(NULL, wbuf, WOORT_MAX_EXE_PATH_LEN);
        if (len == 0 || len >= WOORT_MAX_EXE_PATH_LEN)
            woort_panic(WOORT_PANIC_USER, "Failed to get executable path.");

        size_t u8len;
        char* u8path = woort_u16strtou8(
            (const char16_t*)wbuf, (size_t)len, &u8len);
        if (u8path == NULL)
            woort_panic(WOORT_PANIC_USER, "Failed to convert executable path to UTF-8.");

        full_path = u8path;
    }
#elif defined(__APPLE__)
    {
        char buf[WOORT_MAX_EXE_PATH_LEN];
        uint32_t size = WOORT_MAX_EXE_PATH_LEN;
        if (_NSGetExecutablePath(buf, &size) != 0)
            woort_panic(WOORT_PANIC_USER, "Failed to get executable path.");

        char resolved[WOORT_MAX_EXE_PATH_LEN];
        if (realpath(buf, resolved) != NULL)
        {
            full_path = (char*)malloc(strlen(resolved) + 1);
            if (full_path != NULL)
                strcpy(full_path, resolved);
        }
        else
        {
            full_path = (char*)malloc(strlen(buf) + 1);
            if (full_path != NULL)
                strcpy(full_path, buf);
        }
    }
#elif defined(__unix__) || defined(__unix)
    {
        char buf[WOORT_MAX_EXE_PATH_LEN];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len < 0 || (size_t)len >= sizeof(buf) - 1)
            woort_panic(WOORT_PANIC_USER, "Failed to get executable path.");

        buf[len] = '\0';
        full_path = (char*)malloc((size_t)len + 1);
        if (full_path != NULL)
            strcpy(full_path, buf);
    }
#else
    woort_panic(WOORT_PANIC_USER, "Exe path not supported on this platform.");
#endif

    if (full_path == NULL)
        woort_panic(WOORT_PANIC_USER, "Failed to get executable path.");

    woort_normalize_path(full_path);

    g_exe_path_cache = woort_get_file_loc(full_path);
    free(full_path);

    if (g_exe_path_cache != NULL)
    {
        char* copy = (char*)malloc(strlen(g_exe_path_cache) + 1);
        if (copy != NULL)
            strcpy(copy, g_exe_path_cache);
        return copy;
    }
    return NULL;
}

WOORT_NODISCARD char* woort_work_path(void)
{
#if defined(_WIN32) || defined(_WIN64)
    {
        wchar_t wbuf[WOORT_MAX_EXE_PATH_LEN];
        DWORD len = GetCurrentDirectoryW(WOORT_MAX_EXE_PATH_LEN, wbuf);
        if (len == 0 || len >= WOORT_MAX_EXE_PATH_LEN)
        {
            wbuf[0] = L'\0';
            len = 0;
        }

        size_t u8len;
        char* result = woort_u16strtou8(
            (const char16_t*)wbuf, (size_t)len, &u8len);
        woort_normalize_path(result);
        return result;
    }
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__MACH__)
    {
        char buf[WOORT_MAX_EXE_PATH_LEN];
        if (getcwd(buf, sizeof(buf)) == NULL)
            woort_panic(WOORT_PANIC_USER, "Failed to get current working directory.");

        char* result = (char*)malloc(strlen(buf) + 1);
        if (result != NULL)
            strcpy(result, buf);
        woort_normalize_path(result);
        return result;
    }
#else
    woort_panic(WOORT_PANIC_USER, "Work path not supported on this platform.");
    return NULL;
#endif
}

bool woort_set_work_path(const char* path)
{
#if defined(_WIN32) || defined(_WIN64)
    {
        size_t wlen;
        char16_t* wpath = woort_u8strtou16(path, strlen(path), &wlen);
        if (wpath == NULL)
            return false;

        BOOL ok = SetCurrentDirectoryW((const wchar_t*)wpath);
        free(wpath);
        return ok != 0;
    }
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__MACH__)
    {
        return chdir(path) == 0;
    }
#else
    (void)path;
    return false;
#endif
}

WOORT_NODISCARD char* woort_get_file_loc(const char* path)
{
    if (path == NULL)
        return NULL;

    char* buf = (char*)malloc(strlen(path) + 1);
    if (buf == NULL)
        return NULL;
    strcpy(buf, path);
    woort_normalize_path(buf);

    char* last = strrchr(buf, '/');
    if (last != NULL)
        *last = '\0';
    else
        buf[0] = '\0';

    return buf;
}

void woort_normalize_path(char* path)
{
    if (path == NULL)
        return;

#if defined(_WIN32) || defined(_WIN64)
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
