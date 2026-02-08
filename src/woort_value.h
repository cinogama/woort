#pragma once

/*
woort_value.h
*/

#include <stdint.h>
#include <stddef.h>

typedef int64_t woort_Integer;
typedef double woort_Real;

typedef enum woort_FunctionType
{
    WOORT_FUNCTION_TYPE_SCRIPT,
    WOORT_FUNCTION_TYPE_NATIVE,
    WOORT_FUNCTION_TYPE_JIT,

}woort_FunctionType;
typedef struct woort_Function
{
    int64_t    m_type : 2;
    int64_t    m_address : 62;

}woort_Function;

typedef enum woort_CallWay
{
    // 一个脚本中的函数调用了另一个（本地的）脚本函数
    WOORT_CALL_WAY_NEAR,

    // 调用了另一个代码环境下的脚本函数，返回时需要额外检查
    WOORT_CALL_WAY_FAR,

    // 此调用是由 native 层发起的，返回时需要终止解释器执行
    WOORT_CALL_WAY_FROM_NATIVE,
} woort_CallWay;
typedef struct woort_RetBP
{
    woort_CallWay   m_way;
    uint32_t        m_bp_offset;

} woort_RetBP;

typedef union woort_Value
{
    woort_Integer   m_integer;
    woort_Real      m_real;
    woort_Function  m_function;
    woort_RetBP     m_ret_bp;
    const void*     m_ret_addr;

}woort_Value;

_Static_assert(sizeof(woort_Value) == sizeof(woort_value), 
    "woort_Value and woort_value must have the same size");
