#pragma once

#include "woort.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    WOORT_NODISCARD bool woort_JIT_Asmjit_bootup(void);
    void woort_JIT_Asmjit_shutdown(void);

    void* woort_JIT_Asmjit_get_runtime(void);

#ifdef __cplusplus
}
#endif