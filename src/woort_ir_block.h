#pragma once

/*
woort_ir_block.h
*/

#include "woort_vector.h"
#include "woort_ir_operate.h"

typedef struct woort_IR_Block woort_IR_Block;

typedef struct woort_IR_Block_EndWay
{
    /* OPTIONAL */ woort_IR_Block* m_next_block;
    /* OPTIONAL */ woort_IR_Register* m_return_value;

}woort_IR_Block_EndWay;

/*
woort_IR_Block

Block 是若干 Op 的合集，是代码连续执行内容的最小单元（即，
一个 Block 内的代码总是按顺序执行，直到执行到块的尾部。

一个块只会以以下方式之一结束：
    * 条件跳转到其他块（或者返回），条件不满足时默认跳转到其他块（或者返回）
    * 直接跳转到其他块（或者返回）
*/
struct woort_IR_Block
{
    woort_Vector /* const woort_IR_Operate_base* */ m_codes;

    /* OPTIONAL */ woort_IR_Register* m_cond;
    woort_IR_Block_EndWay /* USELESS IF m_cond IS NONE */ m_cond_next;
    woort_IR_Block_EndWay m_normal_next;
};

