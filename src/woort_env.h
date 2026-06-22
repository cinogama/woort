#pragma once

/*
woort_env.h
*/

#include <stdbool.h>
#include <stddef.h>

void _woort_env_bootup(void);
void _woort_env_shutdown(void);

/*
 * Internal UTF-8 console input byte stream.
 *
 * On Windows a real console is read with ReadConsoleW (UTF-16) and converted
 * to UTF-8 so non-ASCII input (e.g. CJK) is handled correctly; redirected
 * stdin (pipe/file) and all POSIX platforms fall back to the C stdin, whose
 * bytes are already UTF-8 in this runtime's locale. Used by the built-in
 * input functions (read_i / read_r / read_s / readline).
 */
int woort_conin_getc(void);                 /* next UTF-8 byte (0-255), or EOF */
int woort_conin_ungetc(int ch);             /* push back one byte (1-deep)     */
/* Reads one line as a NUL-terminated malloc'd UTF-8 string (use woort_free).
 * Returns NULL on end-of-file with nothing read. *out_len receives the byte
 * length (excluding the NUL terminator) when non-NULL. */
WOORT_NODISCARD /* OPTIONAL */ char* woort_conin_readline(size_t* out_len);
