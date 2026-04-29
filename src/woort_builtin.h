#pragma once

#include "woort.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bootup/shutdown called from woort_init / woort_shutdown.
 * Registers the "woolang" fake library with built-in native functions.
 */
bool _woort_builtin_bootup(void);
void _woort_builtin_shutdown(void);

/*
 * Get the handle of the built-in "woolang" fake library.
 * Returns NULL if woort_init has not been called yet.
 */
WOORT_NODISCARD /* OPTIONAL */ woort_Dylib* woort_get_builtin_lib(void);

#ifdef __cplusplus
}
#endif
