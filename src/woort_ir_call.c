/*
 * woort_ir_call.c
 * 
 * IR 函数调用和栈操作指令实现。
 */

#include "woort_ir_internal.h"

/*******************************************************************************
 * 栈操作指令
 ******************************************************************************/

bool woort_IRBlock_PUSH(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return false;
    }
    
    if (woort_IRBlock_is_terminated(block))
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        block->m_function->m_compiler, WOORT_IR_INST_PUSH, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return true;
}

bool woort_IRBlock_PUSH_const(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_index)
{
    if (!block)
    {
        return false;
    }
    
    if (woort_IRBlock_is_terminated(block))
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        block->m_function->m_compiler, WOORT_IR_INST_PUSH_CONST, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_extra_global_index = global_index;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return true;
}

/*******************************************************************************
 * 函数调用指令
 ******************************************************************************/

bool woort_IRBlock_CALL(
    woort_IRBlock* block,
    const woort_IRValue* callee,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result)
{
    if (!block || !callee)
    {
        return false;
    }
    
    if (woort_IRBlock_is_terminated(block))
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        block->m_function->m_compiler, WOORT_IR_INST_CALL, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_operand0 = callee;
    inst->m_extra_size = argc;
    inst->m_need_result = (out_result != NULL);
    
    _woort_IRBlock_add_instruction(block, inst);
    
    if (out_result)
    {
        *out_result = (const woort_IRValue*)inst;
    }
    
    return true;
}

bool woort_IRBlock_CALLNWO(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result)
{
    if (!block)
    {
        return false;
    }
    
    if (woort_IRBlock_is_terminated(block))
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        block->m_function->m_compiler, WOORT_IR_INST_CALLNWO, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_extra_global_index = func_index;
    inst->m_extra_size = argc;
    inst->m_need_result = (out_result != NULL);
    
    _woort_IRBlock_add_instruction(block, inst);
    
    if (out_result)
    {
        *out_result = (const woort_IRValue*)inst;
    }
    
    return true;
}

bool woort_IRBlock_CALLNFP(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result)
{
    if (!block)
    {
        return false;
    }
    
    if (woort_IRBlock_is_terminated(block))
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        block->m_function->m_compiler, WOORT_IR_INST_CALLNFP, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_extra_global_index = func_index;
    inst->m_extra_size = argc;
    inst->m_need_result = (out_result != NULL);
    
    _woort_IRBlock_add_instruction(block, inst);
    
    if (out_result)
    {
        *out_result = (const woort_IRValue*)inst;
    }
    
    return true;
}

bool woort_IRBlock_CALLNJIT(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result)
{
    if (!block)
    {
        return false;
    }
    
    if (woort_IRBlock_is_terminated(block))
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        block->m_function->m_compiler, WOORT_IR_INST_CALLNJIT, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_extra_global_index = func_index;
    inst->m_extra_size = argc;
    inst->m_need_result = (out_result != NULL);
    
    _woort_IRBlock_add_instruction(block, inst);
    
    if (out_result)
    {
        *out_result = (const woort_IRValue*)inst;
    }
    
    return true;
}
