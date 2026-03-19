#pragma once

/*
woort_ir_function.h
*/

/*
woort_IR_Function

Function 是 Block 的合集，是寄存器分配等操作的基本单元。
*/

#include "woort_ir_operate.h"
#include "woort_ir_block.h"
#include "woort_linklist.h"

struct woort_IR_Function
{
    woort_LinkList /* woort_IR_Register */  m_allocated_registers;
};

woort_IR_Register* woort_IR_Function_load_const();
