#pragma once

#include "woort.h"

#include "woort_value_types.h"

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
    extern const int32_t WOORT_VM_OFFSETOF_CHECK_REQUEST_MASK;

    void woort_JIT_GC_mixed_write_barrier_value(
        woort_Value* modified_value, woort_Value src_value);

    WOORT_NODISCARD woort_GCVec* woort_JIT_make_vec(
        woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_GCMap* woort_JIT_make_map(
        woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_GCStruct* woort_JIT_make_struct(
        woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_GCStruct* woort_JIT_make_union(
        woort_Int idx, const woort_Value* src);

    WOORT_NODISCARD woort_GCClosure* woort_JIT_make_closure(
        const woort_GCClosure* tmpl, woort_Value* sp, size_t count);

    WOORT_NODISCARD const woort_Bytecode* woort_JIT_CodeEnv_codes(const woort_CodeEnv* cenv);
    WOORT_NODISCARD size_t woort_JIT_CodeEnv_constant_count(const woort_CodeEnv* cenv);
    WOORT_NODISCARD const woort_Value* woort_JIT_CodeEnv_static_data(const woort_CodeEnv* cenv);

    WOORT_NODISCARD const woort_GCString* woort_GCString_from_integer(woort_Int value);
    WOORT_NODISCARD const woort_GCString* woort_GCString_from_real(woort_Real value);

    WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(const char* str, size_t len);

    WOORT_NODISCARD woort_Int woort_GCString_to_integer(const woort_GCString* str);
    WOORT_NODISCARD woort_Real woort_GCString_to_real(const woort_GCString* str);
    WOORT_NODISCARD woort_Int woort_JIT_GCString_to_bool(const woort_GCString* str);
    WOORT_NODISCARD const woort_GCString* woort_JIT_GCString_from_bool(woort_Int value);

    WOORT_NODISCARD woort_Int woort_JIT_serialize_vec(woort_Value* dst, woort_GCVec* src);
    WOORT_NODISCARD woort_Int woort_JIT_serialize_map(woort_Value* dst, woort_GCMap* src);

    WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_JIT_request_handler(woort_VMRuntime* vm);
    WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_JIT_stack_overflow_handler(woort_VMRuntime* vm);

    WOORT_NODISCARD const woort_Bytecode* woort_JIT_next_bytecode(const woort_Bytecode* bc);

#ifdef __cplusplus
}
#endif