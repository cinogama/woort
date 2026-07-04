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

    extern const int32_t WOORT_GCSTRUCT_OFFSETOF_DATAS;
    extern const int32_t WOORT_GCCLOSURE_OFFSETOF_SCRIPT_FUNCTION;
    extern const int32_t WOORT_GCCLOSURE_OFFSETOF_JIT_FUNCTION;
    extern const int32_t WOORT_GCCLOSURE_OFFSETOF_SIZE;
    extern const int32_t WOORT_GCCLOSURE_OFFSETOF_DATAS;

    void woort_JIT_GC_mixed_write_barrier_value(
        woort_Value* modified_value, woort_Value src_value);

    WOORT_NODISCARD woort_GCVec* woort_JIT_make_vec(
        woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_GCMap* woort_JIT_make_map(
        woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_GCStruct* woort_JIT_make_struct(
        woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_GCStruct* woort_JIT_make_union(
        woort_Int idx, woort_Value src);

    WOORT_NODISCARD woort_GCClosure* woort_JIT_make_closure(
        const woort_GCClosure* tmpl, woort_Value* sp, size_t count);

    WOORT_NODISCARD woort_Value* woort_JIT_make_pvalue(woort_Value src);

    WOORT_NODISCARD woort_BoxedValue woort_JIT_box_int_ex(woort_Int val);
    WOORT_NODISCARD woort_BoxedValue woort_JIT_box_real_ex(woort_Real val);

    WOORT_NODISCARD bool woort_JIT_unbox_int_ex(woort_BoxedValue val, woort_Int* out);
    WOORT_NODISCARD bool woort_JIT_unbox_real_ex(woort_BoxedValue val, woort_Real* out);

    WOORT_NODISCARD bool woort_JIT_unbox_gc(
        woort_BoxedValue val, woort_BoxValueType type, woort_Value* out);

    WOORT_NODISCARD woort_Value woort_JIT_unbox_dyn_no_check(woort_DynBox val);

    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_int(
        woort_GCMap* map, woort_Int key);
    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_real(
        woort_GCMap* map, woort_BoxedValue real_bits);
    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_bool(
        woort_GCMap* map, woort_Int key);
    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_dyn(
        woort_GCMap* map, woort_DynBox key);

    WOORT_NODISCARD bool woort_JIT_ldidstring(
        const woort_GCString* str, woort_Int idx, woort_Value* out);

    void woort_JIT_store_dynbox_int(woort_DynBox* dst, woort_Int val);
    void woort_JIT_store_dynbox_real(woort_DynBox* dst, woort_BoxedValue real_bits);
    void woort_JIT_store_dynbox_bool(woort_DynBox* dst, woort_Int val);
    void woort_JIT_store_dynbox_dyn(woort_DynBox* dst, woort_DynBox val);

    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_int(woort_GCMap* map, woort_Int key);
    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_real(woort_GCMap* map, woort_BoxedValue real_bits);
    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_bool(woort_GCMap* map, woort_Int key);
    /* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_dyn(woort_GCMap* map, woort_DynBox key);

    WOORT_NODISCARD bool woort_JIT_check_int_ex(woort_BoxedValue val);
    WOORT_NODISCARD bool woort_JIT_check_real_ex(woort_BoxedValue val);
    WOORT_NODISCARD bool woort_JIT_check_gc(
        woort_BoxedValue val, woort_BoxValueType type);

    WOORT_NODISCARD const woort_Bytecode* woort_JIT_CodeEnv_codes(const woort_CodeEnv* cenv);
    WOORT_NODISCARD size_t woort_JIT_CodeEnv_constant_count(const woort_CodeEnv* cenv);
    WOORT_NODISCARD const woort_Value* woort_JIT_CodeEnv_static_data(const woort_CodeEnv* cenv);

    typedef void (*woort_JIT_JumpTargetCallback)(
        const woort_Bytecode* target, void* user_data);

    void woort_JIT_pre_scan_jump_targets(
        const woort_CodeEnv* cenv,
        const woort_Bytecode* func_start,
        woort_JIT_JumpTargetCallback callback,
        void* user_data);

    WOORT_NODISCARD const woort_GCString* woort_GCString_from_integer(woort_Int value);
    WOORT_NODISCARD const woort_GCString* woort_GCString_from_real(woort_Real value);

    WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(const char* str, size_t len);

    /* 字符串拼接 / 字典序比较（供 ADDS/LTS.../CADDS 等 JIT 处理器调用） */
    WOORT_NODISCARD const woort_GCString* woort_GCString_add_string(
        const woort_GCString* a, const woort_GCString* b);
    WOORT_NODISCARD int woort_GCString_compare(const woort_GCString* a, const woort_GCString* b);

    WOORT_NODISCARD woort_Int woort_GCString_to_integer(const woort_GCString* str);
    WOORT_NODISCARD woort_Real woort_GCString_to_real(const woort_GCString* str);
    WOORT_NODISCARD woort_Int woort_JIT_GCString_to_bool(const woort_GCString* str);
    WOORT_NODISCARD const woort_GCString* woort_JIT_GCString_from_bool(woort_Int value);

    WOORT_NODISCARD const woort_GCString* woort_JIT_serialize_vec(woort_GCVec* src);
    WOORT_NODISCARD const woort_GCString* woort_JIT_serialize_map(woort_GCMap* src);

    /*
     * GC UnitProxy 全局对象（其地址供 JIT 后端内联类型分派时作立即数比较）。
     * 这些符号定义在各 woort_gc_*.c / woort_value.c 中，此处仅作 extern 声明，
     * 令 C++ JIT 实现可直接取地址生成内联比较代码。
     */
    extern const woort_GCUnitProxy WOORT_EX_BOX_PROXY;
    extern const woort_GCUnitProxy WOORT_GCSTRING_UNIT_PROXY;
    extern const woort_GCUnitProxy WOORT_GCVEC_UNIT_PROXY;
    extern const woort_GCUnitProxy WOORT_GCMAP_UNIT_PROXY;
    extern const woort_GCUnitProxy WOORT_GCSTRUCT_UNIT_PROXY;
    extern const woort_GCUnitProxy WOORT_GCHANDLE_UNIT_PROXY;
    extern const woort_GCUnitProxy WOORT_GCCLOSURE_UNIT_PROXY;

    WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_JIT_request_handler(woort_VMRuntime* vm);
    WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_JIT_stack_overflow_handler(woort_VMRuntime* vm);

    WOORT_NODISCARD const woort_Bytecode* woort_JIT_next_bytecode(const woort_Bytecode* bc);

#ifdef __cplusplus
}
#endif