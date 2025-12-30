#pragma once

/*
woort_value.h
*/

#include "stdint.h"

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

typedef union woort_Value
{
    woort_Integer   m_integer;
    woort_Real      m_real;
    woort_Function  m_function;

}woort_Value;