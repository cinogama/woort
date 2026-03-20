/*
 * woort_ir_block.c
 * 
 * IR 基本块接口实现：控制流和返回指令。
 */

#include "woort_ir_internal.h"

#include <string.h>

/*******************************************************************************
 * 控制流指令
 ******************************************************************************/

bool woort_IRBlock_br(
    woort_IRBlock* block,
    woort_IRBlock* target)
{
    if (!block || !target)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_BR;
    block->m_terminator.m_data.m_br.m_target = target;
    
    _woort_IRBlock_add_successor(block, target);
    
    return true;
}

bool woort_IRBlock_condbr_less_then(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !lhs || !rhs || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_LESS_THEN;
    block->m_terminator.m_data.m_condbr.m_lhs = lhs;
    block->m_terminator.m_data.m_condbr.m_rhs = rhs;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_greater_then(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !lhs || !rhs || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_GREATER_THEN;
    block->m_terminator.m_data.m_condbr.m_lhs = lhs;
    block->m_terminator.m_data.m_condbr.m_rhs = rhs;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_less_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !lhs || !rhs || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_LESS_EQUAL;
    block->m_terminator.m_data.m_condbr.m_lhs = lhs;
    block->m_terminator.m_data.m_condbr.m_rhs = rhs;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_greater_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !lhs || !rhs || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_GREATER_EQUAL;
    block->m_terminator.m_data.m_condbr.m_lhs = lhs;
    block->m_terminator.m_data.m_condbr.m_rhs = rhs;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !lhs || !rhs || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_EQUAL;
    block->m_terminator.m_data.m_condbr.m_lhs = lhs;
    block->m_terminator.m_data.m_condbr.m_rhs = rhs;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_not_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !lhs || !rhs || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_NOT_EQUAL;
    block->m_terminator.m_data.m_condbr.m_lhs = lhs;
    block->m_terminator.m_data.m_condbr.m_rhs = rhs;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_true(
    woort_IRBlock* block,
    const woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !cond || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_TRUE;
    block->m_terminator.m_data.m_condbr.m_lhs = cond;
    block->m_terminator.m_data.m_condbr.m_rhs = NULL;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

bool woort_IRBlock_condbr_false(
    woort_IRBlock* block,
    const woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    if (!block || !cond || !then_block || !else_block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_CONDBR;
    block->m_terminator.m_data.m_condbr.m_cond_kind = WOORT_IR_CONDBR_FALSE;
    block->m_terminator.m_data.m_condbr.m_lhs = cond;
    block->m_terminator.m_data.m_condbr.m_rhs = NULL;
    block->m_terminator.m_data.m_condbr.m_then_block = then_block;
    block->m_terminator.m_data.m_condbr.m_else_block = else_block;
    
    _woort_IRBlock_add_successor(block, then_block);
    _woort_IRBlock_add_successor(block, else_block);
    
    return true;
}

/*******************************************************************************
 * 返回指令
 ******************************************************************************/

bool woort_IRBlock_ret(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_RET;
    block->m_terminator.m_data.m_ret.m_value = value;
    
    return true;
}

bool woort_IRBlock_ret_void(woort_IRBlock* block)
{
    if (!block)
    {
        return false;
    }
    
    if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
    {
        return false;
    }
    
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_RET_VOID;
    block->m_terminator.m_data.m_ret.m_value = NULL;
    
    return true;
}

/*******************************************************************************
 * IRBlock 工具接口
 ******************************************************************************/

woort_IRFunction* woort_IRBlock_get_function(const woort_IRBlock* block)
{
    if (!block)
    {
        return NULL;
    }
    return block->m_function;
}

bool woort_IRBlock_is_terminated(const woort_IRBlock* block)
{
    if (!block)
    {
        return false;
    }
    return block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE;
}

size_t woort_IRBlock_get_predecessor_count(const woort_IRBlock* block)
{
    if (!block)
    {
        return 0;
    }
    return block->m_predecessors.m_size;
}

size_t woort_IRBlock_get_successor_count(const woort_IRBlock* block)
{
    if (!block)
    {
        return 0;
    }
    return block->m_successors.m_size;
}
