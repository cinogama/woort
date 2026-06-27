#pragma once

#include "woort.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    WOORT_NODISCARD bool woort_JIT_Asmjit_bootup(void);
    void woort_JIT_Asmjit_shutdown(void);

    void* woort_JIT_Asmjit_get_runtime(void);

#define WOORT_VM_MAX_JIT_CALL_DEPTH 128
    
    extern const int32_t WOORT_VM_OFFSETOF_JIT_CALL_DEPTH;
    extern const int32_t WOORT_VM_OFFSETOF_IP;
    extern const int32_t WOORT_VM_OFFSETOF_SP;
    extern const int32_t WOORT_VM_OFFSETOF_SB;
    extern const int32_t WOORT_VM_OFFSETOF_ENV;
    extern const int32_t WOORT_VM_OFFSETOF_STACK;
    extern const int32_t WOORT_VM_OFFSETOF_STACK_END;
#ifdef __cplusplus
}
#endif