#include "woort.h"

#include "woort_env.h"
#include "woort_log.h"
#include "woort_utf8.h"
#include "woort_diagnosis.h"
#include "woort_platform.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   include <windows.h>
#else
#   include <unistd.h>
#   include <errno.h>
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

#if defined(WOORT_PLATFORM_OS_WINDOWS)
/* Reset console-input static state; defined near the conin impl below. */
static void woort_conin_reset(void);
/* Reset the raw console byte-stream static state (woort_console_getc). */
static void woort_raw_reset(void);
#endif

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
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    woort_conin_reset();
    woort_raw_reset();
#else
    /* Nothing to clean up currently. */
#endif
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

/* ================================================================
 * woort_conin_getc / woort_conin_ungetc / woort_conin_readline
 *
 * Internal UTF-8 console input byte stream for the built-in input
 * functions. A real Windows console is read with ReadConsoleW (UTF-16)
 * and converted to UTF-8 (UNICODE -> UTF-8); redirected stdin and all
 * POSIX platforms use the C stdin directly (bytes are already UTF-8 in
 * this runtime's locale).
 * Console I/O is inherently single-stream, so the state below is a
 * process-global singleton with no locking, mirroring
 * woort_console_readline.
 * ================================================================ */

#if defined(WOORT_PLATFORM_OS_WINDOWS)

#   define WOORT_CONIN_CHUNK_WCHARS 256

static char*  s_conin_u8buf  = NULL;   /* pending UTF-8 bytes   */
static size_t s_conin_u8cap  = 0;      /* allocated bytes       */
static size_t s_conin_u8len  = 0;      /* valid bytes           */
static size_t s_conin_u8pos  = 0;      /* next byte to serve    */
static int    s_conin_ungot  = EOF;    /* 1-deep ungetc slot    */
static int    s_conin_is_console = -1; /* lazy: -1 unknown, 0/1 */

static int woort_conin_is_console_handle(void)
{
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    return (GetFileType(hin) == FILE_TYPE_CHAR) ? 1 : 0;
}

/* Read one UTF-16 chunk from the console, convert to UTF-8 and append it
 * to the pending buffer. Returns 1 if bytes became available, 0 on
 * end-of-file / unrecoverable error. */
static int woort_conin_refill(void)
{
    wchar_t wbuf[WOORT_CONIN_CHUNK_WCHARS];
    DWORD   read_count = 0;
    HANDLE  hin = GetStdHandle(STD_INPUT_HANDLE);
    size_t  u8len = 0;
    char*   u8;

    if (!ReadConsoleW(hin, wbuf, WOORT_CONIN_CHUNK_WCHARS, &read_count, NULL))
    {
        DWORD err = GetLastError();
        if (err == ERROR_OPERATION_ABORTED)
        {
            /* Ctrl+C: behave like an empty line (matches woort_console_readline). */
            wbuf[0] = L'\n';
            read_count = 1;
        }
        else
        {
            return 0; /* real error -> treat as EOF */
        }
    }

    if (read_count == 0)
        return 0; /* EOF */

    u8 = woort_u16strtou8((const char16_t*)wbuf, (size_t)read_count, &u8len);
    if (u8 == NULL)
        return 0; /* out of memory -> EOF-ish */

    /* Drop already-consumed prefix (compact in place). */
    if (s_conin_u8pos > 0)
    {
        if (s_conin_u8len > s_conin_u8pos)
            memmove(s_conin_u8buf, s_conin_u8buf + s_conin_u8pos,
                    s_conin_u8len - s_conin_u8pos);
        s_conin_u8len -= s_conin_u8pos;
        s_conin_u8pos = 0;
    }

    /* Ensure capacity for the new bytes plus a NUL. */
    if (s_conin_u8cap < s_conin_u8len + u8len + 1)
    {
        size_t newcap = (s_conin_u8len + u8len + 1) * 2;
        char*  nb = (char*)realloc(s_conin_u8buf, newcap);
        if (nb == NULL)
        {
            free(u8);
            return 0;
        }
        s_conin_u8buf = nb;
        s_conin_u8cap = newcap;
    }

    memcpy(s_conin_u8buf + s_conin_u8len, u8, u8len);
    s_conin_u8len += u8len;
    free(u8);
    return 1;
}

/* Free and re-initialize the console-input statics so a subsequent
 * _woort_env_bootup within the same process starts from a clean slate
 * (no leaked buffer, no leftover bytes, console-ness re-detected). */
static void woort_conin_reset(void)
{
    free(s_conin_u8buf);
    s_conin_u8buf      = NULL;
    s_conin_u8cap      = 0;
    s_conin_u8len      = 0;
    s_conin_u8pos      = 0;
    s_conin_ungot      = EOF;
    s_conin_is_console = -1;  /* re-detect on next use */
}

#endif /* WOORT_PLATFORM_OS_WINDOWS */

int woort_conin_getc(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    if (s_conin_ungot != EOF)
    {
        int c = s_conin_ungot;
        s_conin_ungot = EOF;
        return c;
    }

    if (s_conin_is_console < 0)
        s_conin_is_console = woort_conin_is_console_handle();

    if (s_conin_is_console)
    {
        if (s_conin_u8pos >= s_conin_u8len)
        {
            if (!woort_conin_refill())
                return EOF;
        }
        return (unsigned char)s_conin_u8buf[s_conin_u8pos++];
    }
    /* redirected stdin (pipe/file): byte passthrough (already UTF-8) */
#endif
    return getchar();
}

int woort_conin_ungetc(int ch)
{
    if (ch == EOF)
        return EOF;

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    if (s_conin_is_console < 0)
        s_conin_is_console = woort_conin_is_console_handle();

    if (s_conin_is_console)
    {
        s_conin_ungot = ch;
        return ch;
    }
#endif
    return ungetc(ch, stdin);
}

WOORT_NODISCARD /* OPTIONAL */ char* woort_conin_readline(size_t* out_len)
{
    char*  buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int    c;

    for (;;)
    {
        c = woort_conin_getc();
        if (c == EOF)
        {
            if (len == 0)
            {
                /* EOF with nothing read */
                free(buf);
                if (out_len != NULL)
                    *out_len = 0;
                return NULL;
            }
            break; /* EOF terminates the current (partial) line */
        }
        if (c == '\n')
            break;

        /* ensure room for the byte and a future NUL terminator */
        if (len + 2 > cap)
        {
            size_t newcap = (cap == 0) ? 64 : cap * 2;
            char*  nb;
            while (newcap < len + 2)
                newcap *= 2;
            nb = (char*)realloc(buf, newcap);
            if (nb == NULL)
            {
                free(buf);
                if (out_len != NULL)
                    *out_len = 0;
                return NULL;
            }
            buf = nb;
            cap = newcap;
        }
        buf[len++] = (char)c;
    }

    /* strip a trailing '\r' (CRLF consoles) */
    while (len > 0 && buf[len - 1] == '\r')
        len--;

    /* 确保至少 len+1 字节空间：空行（仅 '\n'）时 buf 仍为 NULL、cap==0，
     * 扩容分支从未执行，故在此为 NUL 终止符补足空间。正常情况下
     * cap >= len+1 已成立，realloc 分支为 no-op。 */
    if (len + 1 > cap)
    {
        char* nb = (char*)realloc(buf, len + 1);
        if (nb == NULL)
        {
            free(buf);
            if (out_len != NULL)
                *out_len = 0;
            return NULL;
        }
        buf = nb;
        cap = len + 1;
    }
    buf[len] = '\0';
    if (out_len != NULL)
        *out_len = len;
    return buf;
}

/* ================================================================
 * woort_stdin_isatty / woort_console_getc / woort_console_ungetc
 *
 * Public raw UTF-8 console byte stream for char-at-a-time consumers
 * (live line editors, key-event decoders). Independent from the
 * internal woort_conin_* stream used by the built-in input functions,
 * so the two never share buffered state.
 *
 * Windows: a real console is read with ReadConsoleW (UTF-16) and
 * converted to UTF-8 (UNICODE -> UTF-8); redirected stdin (pipe/file)
 * is byte passthrough. POSIX: read(2) directly (NOT stdio), so it is
 * safe under termios raw mode and never entangles with the libc stdin
 * buffer.
 *
 * A Ctrl+C interrupt on Windows (ReadConsoleW failing with
 * ERROR_OPERATION_ABORTED) is delivered as the byte 0x03 (ETX) so a key
 * decoder can map it to a "cancel" event.
 * Console I/O is inherently single-stream, so the state below is a
 * process-global singleton with no locking, mirroring woort_conin_*.
 * ================================================================ */

bool woort_stdin_isatty(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    return (hin != INVALID_HANDLE_VALUE
        && GetFileType(hin) == FILE_TYPE_CHAR) ? true : false;
#else
    return isatty(fileno(stdin)) ? true : false;
#endif
}

#if defined(WOORT_PLATFORM_OS_WINDOWS)

#   define WOORT_RAW_CHUNK_WCHARS 256

static char*  s_raw_u8buf      = NULL;  /* pending UTF-8 bytes         */
static size_t s_raw_u8cap      = 0;     /* allocated bytes             */
static size_t s_raw_u8len      = 0;     /* valid bytes                 */
static size_t s_raw_u8pos      = 0;     /* next byte to serve          */
static int    s_raw_is_console = -1;    /* lazy: -1 unknown, 0/1       */

static int woort_raw_is_console_handle(void)
{
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    return (GetFileType(hin) == FILE_TYPE_CHAR) ? 1 : 0;
}

/* Read one UTF-16 chunk from the console, convert to UTF-8 and append it
 * to the pending buffer. Returns 1 if bytes became available, 0 on
 * end-of-file / unrecoverable error. */
static int woort_raw_refill(void)
{
    wchar_t wbuf[WOORT_RAW_CHUNK_WCHARS];
    DWORD   read_count = 0;
    HANDLE  hin = GetStdHandle(STD_INPUT_HANDLE);
    size_t  u8len = 0;
    char*   u8;

    if (!ReadConsoleW(hin, wbuf, WOORT_RAW_CHUNK_WCHARS, &read_count, NULL))
    {
        if (GetLastError() == ERROR_OPERATION_ABORTED)
        {
            /* Ctrl+C: deliver ETX so a key decoder maps it to "cancel". */
            wbuf[0] = L'\x03';
            read_count = 1;
        }
        else
        {
            return 0; /* real error -> treat as EOF */
        }
    }

    if (read_count == 0)
        return 0; /* EOF */

    u8 = woort_u16strtou8((const char16_t*)wbuf, (size_t)read_count, &u8len);
    if (u8 == NULL)
        return 0; /* out of memory -> EOF-ish */

    /* Drop already-consumed prefix (compact in place). */
    if (s_raw_u8pos > 0)
    {
        if (s_raw_u8len > s_raw_u8pos)
            memmove(s_raw_u8buf, s_raw_u8buf + s_raw_u8pos,
                    s_raw_u8len - s_raw_u8pos);
        s_raw_u8len -= s_raw_u8pos;
        s_raw_u8pos = 0;
    }

    /* Ensure capacity for the new bytes plus a NUL. */
    if (s_raw_u8cap < s_raw_u8len + u8len + 1)
    {
        size_t newcap = (s_raw_u8len + u8len + 1) * 2;
        char*  nb = (char*)realloc(s_raw_u8buf, newcap);
        if (nb == NULL)
        {
            free(u8);
            return 0;
        }
        s_raw_u8buf = nb;
        s_raw_u8cap = newcap;
    }

    memcpy(s_raw_u8buf + s_raw_u8len, u8, u8len);
    s_raw_u8len += u8len;
    free(u8);
    return 1;
}

static void woort_raw_reset(void)
{
    free(s_raw_u8buf);
    s_raw_u8buf      = NULL;
    s_raw_u8cap      = 0;
    s_raw_u8len      = 0;
    s_raw_u8pos      = 0;
    s_raw_is_console = -1;  /* re-detect on next use */
}

#endif /* WOORT_PLATFORM_OS_WINDOWS */

/* 1-deep pushback slot shared by both backends. */
static int s_raw_ungot = EOF;

int woort_console_getc(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    if (s_raw_ungot != EOF)
    {
        int c = s_raw_ungot;
        s_raw_ungot = EOF;
        return c;
    }

    if (s_raw_is_console < 0)
        s_raw_is_console = woort_raw_is_console_handle();

    if (s_raw_is_console)
    {
        if (s_raw_u8pos >= s_raw_u8len)
        {
            if (!woort_raw_refill())
                return EOF;
        }
        return (unsigned char)s_raw_u8buf[s_raw_u8pos++];
    }
    /* redirected stdin (pipe/file): byte passthrough (already UTF-8) */
    return getchar();
#else
    /* POSIX: read(2) directly, NOT stdio, so termios raw mode is safe and
     * we never entangle with the libc stdin buffer. */
    if (s_raw_ungot != EOF)
    {
        int c = s_raw_ungot;
        s_raw_ungot = EOF;
        return c;
    }

    for (;;)
    {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue; /* interrupted by signal (e.g. SIGINT handled) */
            return EOF;
        }
        if (n == 0)
            return EOF; /* EOF */
        return (int)c;
    }
#endif
}

int woort_console_ungetc(int ch)
{
    if (ch == EOF)
        return EOF;

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    if (s_raw_is_console < 0)
        s_raw_is_console = woort_raw_is_console_handle();

    if (s_raw_is_console)
    {
        s_raw_ungot = ch;
        return ch;
    }
    return ungetc(ch, stdin);
#else
    /* 1-deep pushback over read(2). */
    s_raw_ungot = ch;
    return ch;
#endif
}
