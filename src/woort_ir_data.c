/*
 * woort_ir_data.c
 * 
 * IR 数据构造、类型转换和解包指令实现。
 */

#include "woort_ir_internal.h"

/*******************************************************************************
 * 数据构造指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_MKVEC(
    woort_IRBlock* block,
    size_t element_count)
{
    if (!block)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MKVEC, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_extra_size = element_count;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MKMAP(
    woort_IRBlock* block,
    size_t entry_count)
{
    if (!block)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MKMAP, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_extra_size = entry_count;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MKSTRUCT(
    woort_IRBlock* block,
    size_t field_count)
{
    if (!block)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MKSTRUCT, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_extra_size = field_count;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MKCLOSURE(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t capture_count)
{
    if (!block)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MKCLOSURE, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_extra_global_index = func_index;
    inst->m_extra_size = capture_count;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 类型转换指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_CASTI_TO_R(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_CASTI_TO_R, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_CASTR_TO_I(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_CASTR_TO_I, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_CASTI_TO_S(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_CASTI_TO_S, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_CASTR_TO_S(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_CASTR_TO_S, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 解包指令
 ******************************************************************************/

bool woort_IRBlock_UNPACKVEC(
    woort_IRBlock* block,
    const woort_IRValue* vec)
{
    if (!block || !vec)
    {
        return false;
    }
    
    /* TODO: 实现向量解包指令生成 */
    return true;
}

bool woort_IRBlock_UNPACKVECX(
    woort_IRBlock* block,
    const woort_IRValue* vec)
{
    if (!block || !vec)
    {
        return false;
    }
    
    /* TODO: 实现扩展向量解包指令生成 */
    return true;
}

bool woort_IRBlock_UNPACKSTRUCT(
    woort_IRBlock* block,
    const woort_IRValue* struct_val)
{
    if (!block || !struct_val)
    {
        return false;
    }
    
    /* TODO: 实现结构体解包指令生成 */
    return true;
}
