#pragma once

/*
woort_value.h
*/

#include "woort.h"

#include "woort_diagnosis.h"
#include "woort_gc_units.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int64_t woort_Int;
typedef double woort_Real;
typedef uint32_t woort_Bytecode;

typedef uint64_t woort_BoxedFloat63;
typedef uint64_t woort_BoxedInt62;
typedef uint64_t woort_BoxedBool;

typedef struct woort_GCString woort_GCString;
typedef struct woort_GCMap woort_GCMap;
typedef struct woort_GCVec woort_GCVec;
typedef struct woort_GCStruct woort_GCStruct;
typedef struct woort_GCClosure woort_GCClosure;

typedef union woort_DynBox
{
    woort_BoxedFloat63 m_boxed_real;
    woort_BoxedInt62 m_boxed_int;
    woort_BoxedBool m_boxed_bool;
    struct woort_BoxedExValue* m_boxed_ex;
    woort_GCUnit* m_boxed_gc_unit;

}woort_DynBox;

typedef enum woort_RuntimeFunction_Kind
{
    WOORT_RUNTIME_FUNCTION_KIND_CLOSURE = 0,
    WOORT_RUNTIME_FUNCTION_KIND_SCRIPT,
    WOORT_RUNTIME_FUNCTION_KIND_NATIVE,
    WOORT_RUNTIME_FUNCTION_KIND_JIT,

}woort_RuntimeFunction_Kind;

typedef uint64_t woort_RuntimeFunction;

typedef enum woort_CallWay
{
    // 一个脚本中的函数调用了另一个（本地的）脚本函数
    WOORT_CALL_WAY_NEAR,

    // 调用了另一个代码环境下的脚本函数，返回时需要额外检查
    WOORT_CALL_WAY_FAR,

    // 此调用是由 native 层发起的，返回时需要中断解释器执行
    WOORT_CALL_WAY_FROM_NATIVE,

} woort_CallWay;
typedef struct woort_RetBP
{
    woort_CallWay   m_way;
    uint32_t        m_bp_offset;

} woort_RetBP;

typedef union woort_Value
{
    woort_GCUnit*           m_gcinstance;
    woort_Int               m_integer;
    woort_Real              m_real;
    const woort_GCString*   m_string;
    woort_GCVec*            m_vec;
    woort_GCMap*            m_map;
    woort_GCStruct*         m_struct;
    woort_GCClosure*        m_closure;

    const woort_Bytecode*   m_script_function;
    woort_NativeFunction    m_native_or_jit_function;
    woort_RuntimeFunction   m_runtime_function;

    woort_DynBox            m_dynamic;

    woort_RetBP             m_ret_bp;
    const void* m_ret_addr;

}woort_Value;

_Static_assert(sizeof(woort_Value) == sizeof(woort_value),
    "woort_Value and woort_value must have the same size");

typedef enum woort_BoxValueType
{
    WOORT_BOX_VALUE_TYPE_GCUNIT = 0b000,
    WOORT_BOX_VALUE_TYPE_REAL = 0b001,
    WOORT_BOX_VALUE_TYPE_INT = 0b010,
    WOORT_BOX_VALUE_TYPE_BOOL = 0b100,

    ////
} woort_BoxValueType;

#define woort_RuntimeFunction_kind(function) (      \
    (woort_RuntimeFunction_Kind)(                   \
        ((woort_RuntimeFunction)(function)) >> 62))

#define woort_RuntimeFunction_target(function) (    \
    (void*)(                                        \
        ((woort_RuntimeFunction)(function))         \
            & 0x3fff'ffff'ffff'ffffull))    

#define woort_RuntimeFunction_pack(kind, target)    \
    (woort_RuntimeFunction)(                        \
        ((uint64_t)kind << 62)                      \
            | (uint64_t)(target))

void woort_DynBox_box(
    woort_Value val, woort_BoxValueType type, woort_DynBox* out_val);

WOORT_NODISCARD bool woort_DynBox_check(
    woort_DynBox val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type);

WOORT_NODISCARD bool woort_Value_unbox(
    woort_Value val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type,
    woort_Value* out_val);
