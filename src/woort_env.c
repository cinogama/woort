#include "woort.h"

#include "woort_env.h"
#include "woort_log.h"
#include "woort_utf8.h"
#include "woort_diagnosis.h"
#include "woort_platform.h"

#include <stdlib.h>
#include <string.h>
#include <locale.h>

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   include <windows.h>
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   include <stdio.h>
#endif

/* ================================================================
 * Locale name per platform
 * ================================================================ */

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   define WOORT_DEFAULT_LOCALE_NAME ".UTF-8"
#elif defined(WOORT_PLATFORM_OS_APPLE)
#   define WOORT_DEFAULT_LOCALE_NAME "en_US.UTF-8"
#else
#   define WOORT_DEFAULT_LOCALE_NAME "C.UTF-8"
#endif

/* ================================================================
 * Windows console readline constants
 * ================================================================ */

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#       define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#   endif

/* Initial buffer size for ReadConsoleW */
#   define WOORT_CONSOLE_READ_INITIAL_SIZE 256
#endif

const char* woort_env_locale_name(void)
{
    return WOORT_DEFAULT_LOCALE_NAME;
}

/* ================================================================
 * _woort_env_bootup / _woort_env_shutdown (internal)
 * ================================================================ */

void _woort_env_bootup(void)
{
    /* Enable ANSI/VT terminal processing on Windows console */
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    {
        HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (console_handle != INVALID_HANDLE_VALUE)
        {
            DWORD console_mode = 0;
            if (GetConsoleMode(console_handle, &console_mode))
            {
                SetConsoleMode(console_handle,
                    console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
        }
    }
#endif

    /* Set C locale to UTF-8 */
    if (NULL == setlocale(LC_CTYPE, WOORT_DEFAULT_LOCALE_NAME))
    {
        WOORT_DEBUG("Unable to initialize locale: bad locale type `%s`.",
            WOORT_DEFAULT_LOCALE_NAME);
    }
}

void _woort_env_shutdown(void)
{
    /* Nothing to clean up currently. */
}

/* ================================================================
 * woort_console_readline / woort_console_freeline (public)
 * ================================================================ */

#if defined(WOORT_PLATFORM_OS_WINDOWS)

WOORT_NODISCARD /* OPTIONAL */ char* woort_console_readline(void)
{
    wchar_t* wbuf;
    size_t wbuf_cap;
    size_t wbuf_used;
    DWORD read_count;
    HANDLE console_in;

    wbuf_cap = WOORT_CONSOLE_READ_INITIAL_SIZE;
    wbuf = (wchar_t*)malloc(wbuf_cap * sizeof(wchar_t));
    if (wbuf == NULL)
        return NULL;

    console_in = GetStdHandle(STD_INPUT_HANDLE);
    wbuf_used = 0;

    for (;;)
    {
        DWORD space_left = (DWORD)(wbuf_cap - wbuf_used - 1);
        if (!ReadConsoleW(console_in, wbuf + wbuf_used,
            space_left, &read_count, NULL))
        {
            DWORD err = GetLastError();
            free(wbuf);

            if (err == ERROR_OPERATION_ABORTED)
            {
                /* Ctrl+C pressed, return empty line */
                char* empty = (char*)malloc(1);
                if (empty != NULL)
                    empty[0] = '\0';
                return empty;
            }

            /* Real error */
            return NULL;
        }

        if (read_count == 0)
        {
            /* EOF */
            free(wbuf);
            return NULL;
        }

        wbuf_used += (size_t)read_count;
        wbuf[wbuf_used] = L'\0';

        /* Check for complete line (ends with \n) */
        if (wbuf_used > 0 && wbuf[wbuf_used - 1] == L'\n')
            break;

        /* Grow buffer if needed */
        if (wbuf_used >= wbuf_cap - 1)
        {
            size_t new_cap = wbuf_cap * 2;
            /* OPTIONAL */ wchar_t* new_buf = (wchar_t*)realloc(
                wbuf, new_cap * sizeof(wchar_t));
            if (new_buf == NULL)
            {
                free(wbuf);
                return NULL;
            }
            wbuf = new_buf;
            wbuf_cap = new_cap;
        }
    }

    /* Strip trailing \r\n */
    while (wbuf_used > 0 &&
        (wbuf[wbuf_used - 1] == L'\n' || wbuf[wbuf_used - 1] == L'\r'))
    {
        wbuf_used--;
    }
    wbuf[wbuf_used] = L'\0';

    /* Convert UTF-16 to UTF-8, filtering \r */
    {
        size_t u8len;
        char* result = woort_u16strtou8(
            (const char16_t*)wbuf, wbuf_used, &u8len);
        free(wbuf);

        if (result == NULL)
            return NULL;

        /* Strip \r from within the string */
        {
            char* src = result;
            char* dst = result;
            while (*src != '\0')
            {
                if (*src != '\r')
                {
                    *dst = *src;
                    dst++;
                }
                src++;
            }
            *dst = '\0';
        }

        return result;
    }
}

#else /* POSIX (Linux / macOS) */

WOORT_NODISCARD /* OPTIONAL */ char* woort_console_readline(void)
{
    char* line = NULL;
    size_t len = 0;
    ssize_t nread;

    nread = getline(&line, &len, stdin);
    if (nread < 0)
    {
        free(line);
        return NULL;
    }

    /* Strip trailing \n (and \r) */
    while (nread > 0 &&
        (line[nread - 1] == '\n' || line[nread - 1] == '\r'))
    {
        line[nread - 1] = '\0';
        nread--;
    }

    return line;
}

#endif

void woort_free(/* OPTIONAL */ void* buf)
{
    free(buf);
}
