#pragma once

/*
woort_value_types.h
*/

#include "woort.h"

#include "woort_gc_units_types.h"
#ifndef __cplusplus
#   include "woort_atomic.h"
#endif

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t woort_BoxedValue;
typedef uint64_t woort_BoxedFloat63;
typedef uint64_t woort_BoxedInt62;
typedef uint64_t woort_BoxedBool;

typedef struct woort_GCString woort_GCString;
typedef struct woort_GCMap woort_GCMap;
typedef struct woort_GCVec woort_GCVec;
typedef struct woort_GCStruct woort_GCStruct;
typedef struct woort_GCClosure woort_GCClosure;
typedef struct woort_GCHandle woort_GCHandle;

typedef union woort_DynBox
{
    woort_BoxedValue m_boxed;

}woort_DynBox;

typedef enum woort_CallWay
{
    /* 一个脚本中的函数调用了另一个（本地的）脚本函数 */
    WOORT_CALL_WAY_NEAR,

    /* 调用了另一个代码环境下的脚本函数，返回时需要额外检查 */
    WOORT_CALL_WAY_FAR,

    /* 此调用是由 native 层发起的，返回时需要中断解释器执行 */
    WOORT_CALL_WAY_FROM_NATIVE,

} woort_CallWay;

typedef struct woort_RetBP
{
    woort_CallWay   m_way;
    uint32_t        m_bp_offset;

} woort_RetBP;

union woort_Value
{
    woort_Value*            m_pvalue;
    woort_GCUnit*           m_gcinstance;
    woort_Int               m_integer;
    woort_Real              m_real;
    const woort_GCString*   m_string;
    woort_GCVec*            m_vec;
    woort_GCMap*            m_map;
    woort_GCStruct*         m_struct;
    const woort_GCClosure*  m_closure;
    const woort_GCHandle*   m_gchandle;

    const woort_Bytecode*   m_script_function;
    woort_NativeFunction    m_native_function;
    woort_JitFunction       m_jit_function;

    woort_DynBox            m_dynamic;

    woort_RetBP             m_ret_bp;
    const void*             m_ret_addr;

#ifndef __cplusplus
    woort_AtomicInt64       m_atomic_i64;
#endif
};

#ifdef __cplusplus
static_assert
#else
_Static_assert(sizeof(woort_AtomicInt64) == sizeof(woort_Int),
    "_Atomic int64_t must be 8 bytes; otherwise the C union diverges from C++");
_Static_assert
#endif
(sizeof(woort_Value) == sizeof(uint64_t),
    "woort_Value must stay 8 bytes in both C and C++ (cross-TU ABI invariant)");

/* 扩展装箱类型：用于存储超出内联范围的整数或浮点数 */
typedef struct woort_BoxedExValue
{
    woort_GCUnit m_unit;
    bool m_is_int;
    union
    {
        woort_Real m_real;
        woort_Int m_int;
    };
} woort_BoxedExValue;
