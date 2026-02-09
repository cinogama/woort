#pragma once

/*
woort_value.h
*/

#include "woort.h"

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int64_t woort_Integer;
typedef double woort_Real;
typedef uint64_t woort_DynBox;
typedef uint32_t woort_Bytecode;

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

typedef enum woort_DynBox_ValueType
{
    WOORT_DYNBOX_VALUE_TYPE_GCINSTANCE = 0,
    WOORT_DYNBOX_VALUE_TYPE_EXTERNED,
    WOORT_DYNBOX_VALUE_TYPE_INTEGER,
    WOORT_DYNBOX_VALUE_TYPE_REAL,
    WOORT_DYNBOX_VALUE_TYPE_BOOL,
    WOORT_DYNBOX_VALUE_TYPE_SCRIPT_FUNCTION,
    WOORT_DYNBOX_VALUE_TYPE_NATIVE_FUNCTION,
    WOORT_DYNBOX_VALUE_TYPE_JIT_FUNCTION,

    // Use this flag for pack/unpack/check:
    //  * WOORT_DYNBOX_VALUE_TYPE_SCRIPT_FUNCTION
    //  * WOORT_DYNBOX_VALUE_TYPE_NATIVE_FUNCTION
    //  * WOORT_DYNBOX_VALUE_TYPE_JIT_FUNCTION
    WOORT_DYNBOX_VALUE_TYPE_RUTIME_FUNCTION,

    // Externed:
    WOORT_DYNBOX_VALUE_TYPE_EXTERN_NIL,
    WOORT_DYNBOX_VALUE_TYPE_EXTERN_STRING,
    WOORT_DYNBOX_VALUE_TYPE_EXTERN_STRUCT,
    WOORT_DYNBOX_VALUE_TYPE_EXTERN_VEC,
    WOORT_DYNBOX_VALUE_TYPE_EXTERN_MAP,
    WOORT_DYNBOX_VALUE_TYPE_EXTERN_CLOSURE,


} woort_DynBox_ValueType;

/*
            H                           L
INTEGER:    | ValueStorage 62 | 1 | 0 |
REAL:       |   ValueStorage 63   | 1 |


*/

typedef union woort_Value
{
    void*                   m_gcinstance;
    woort_Integer           m_integer;
    woort_Real              m_real;
    const woort_Bytecode*   m_script_function;
    woort_NativeFunction    m_native_or_jit_function;
    woort_RuntimeFunction   m_runtime_function;

    woort_DynBox            m_dynamic;

    woort_RetBP             m_ret_bp;
    const void* m_ret_addr;

}woort_Value;

_Static_assert(sizeof(woort_Value) == sizeof(woort_value),
    "woort_Value and woort_value must have the same size");

void woort_DynBox_box(
    woort_DynBox_ValueType type,
    woort_Value val,
    woort_DynBox* modifing_box);

WOORT_NODISCARD bool woort_DynBox_check(
    woort_DynBox_ValueType expected_type,
    woort_DynBox box);

WOORT_NODISCARD bool woort_DynBox_try_unbox(
    woort_DynBox_ValueType expected_type,
    woort_DynBox box,
    woort_Value* out_val);

#define woort_RuntimeFunction_kind(function) (      \
    (woort_RuntimeFunction_Kind)(                           \
        ((woort_RuntimeFunction)(function)) >> 62))

#define woort_RuntimeFunction_target(function) (    \
    (void*)(                                        \
        ((woort_RuntimeFunction)(function))         \
            & 0x3fff'ffff'ffff'ffffull))    

#define woort_RuntimeFunction_pack(kind, target)    \
    (woort_RuntimeFunction)(                        \
        ((uint64_t)kind << 62)                      \
            | (uint64_t)(target))
