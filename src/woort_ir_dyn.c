/*
 * woort_ir_dyn.c
 * 
 * IR 动态类型和索引访问指令实现。
 */

#include "woort_ir_internal.h"

/*******************************************************************************
 * 动态类型指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_BOXDYN(
    woort_IRBlock* block,
    woort_IRValue_TypeTag type_tag,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_BOXDYN, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    inst->m_extra_type_tag = type_tag;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_UNBOXDYN(
    woort_IRBlock* block,
    woort_IRValue_TypeTag type_tag,
    const woort_IRValue* dynbox)
{
    if (!block || !dynbox)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_UNBOXDYN, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = dynbox;
    inst->m_extra_type_tag = type_tag;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_CHECKDYN(
    woort_IRBlock* block,
    woort_IRValue_TypeTag type_tag,
    const woort_IRValue* dynbox)
{
    if (!block || !dynbox)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_CHECKDYN, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = dynbox;
    inst->m_extra_type_tag = type_tag;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 向量索引访问指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LDIDXVEC(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index)
{
    if (!block || !vec || !index)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LDIDXVEC, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = vec;
    inst->m_operand1 = index;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

bool woort_IRBlock_STIDXVEC(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index,
    const woort_IRValue* value)
{
    if (!block || !vec || !index || !value)
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_STIDXVEC, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_operand0 = vec;
    inst->m_operand1 = index;
    inst->m_operand2 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return true;
}

const woort_IRValue* woort_IRBlock_LDIDXVECX(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index)
{
    if (!block || !vec || !index)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LDIDXVECX, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = vec;
    inst->m_operand1 = index;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

bool woort_IRBlock_STIDXVECX(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index,
    const woort_IRValue* value)
{
    if (!block || !vec || !index || !value)
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_STIDXVECX, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_operand0 = vec;
    inst->m_operand1 = index;
    inst->m_operand2 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return true;
}

/*******************************************************************************
 * 映射索引访问指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LDIDXMAP(
    woort_IRBlock* block,
    const woort_IRValue* map,
    woort_IRValue_TypeTag key_type,
    const woort_IRValue* key)
{
    if (!block || !map || !key)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LDIDXMAP, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = map;
    inst->m_operand1 = key;
    inst->m_extra_type_tag = key_type;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

bool woort_IRBlock_STIDXMAP(
    woort_IRBlock* block,
    const woort_IRValue* map,
    woort_IRValue_TypeTag key_type,
    const woort_IRValue* key,
    const woort_IRValue* value)
{
    if (!block || !map || !key || !value)
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_STIDXMAP, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_operand0 = map;
    inst->m_operand1 = key;
    inst->m_operand2 = value;
    inst->m_extra_type_tag = key_type;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return true;
}

/*******************************************************************************
 * 结构体字段访问指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LDIDSTRUCT(
    woort_IRBlock* block,
    const woort_IRValue* struct_val,
    size_t field_index)
{
    if (!block || !struct_val)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LDIDSTRUCT, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = struct_val;
    inst->m_extra_size = field_index;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

bool woort_IRBlock_STIDSTRUCT(
    woort_IRBlock* block,
    const woort_IRValue* struct_val,
    size_t field_index,
    const woort_IRValue* value)
{
    if (!block || !struct_val || !value)
    {
        return false;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_STIDSTRUCT, block);
    if (!inst)
    {
        return false;
    }
    
    inst->m_operand0 = struct_val;
    inst->m_operand1 = value;
    inst->m_extra_size = field_index;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return true;
}

/*******************************************************************************
 * 字符串索引访问指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LDIDSTRING(
    woort_IRBlock* block,
    const woort_IRValue* str,
    const woort_IRValue* index)
{
    if (!block || !str || !index)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LDIDSTRING, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = str;
    inst->m_operand1 = index;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}
