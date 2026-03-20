/*
 * woort_ir_arith.c
 * 
 * IR 算术和比较指令实现。
 */

#include "woort_ir_internal.h"

/*******************************************************************************
 * 整数算术指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_ADDI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_ADDI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_SUBI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_SUBI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MULI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MULI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_DIVI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_DIVI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MODI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MODI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_NEGI(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_NEGI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 整数比较指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LTI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LTI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_GTI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_GTI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_LEI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LEI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_GEI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_GEI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_EQI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_EQI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_NEI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_NEI, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 实数算术指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_ADDR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_ADDR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_SUBR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_SUBR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MULR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MULR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_DIVR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_DIVR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_MODR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_MODR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_NEGR(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_NEGR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 实数比较指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LTR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LTR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_GTR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_GTR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_LER(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LER, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_GER(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_GER, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_EQR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_EQR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_NER(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_NER, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 字符串算术指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_ADDS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_ADDS, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 字符串比较指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LTS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LTS, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_GTS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_GTS, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_LES(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LES, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_GES(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_GES, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_EQS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_EQS, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_NES(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_NES, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

/*******************************************************************************
 * 逻辑运算指令
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_LAND(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LAND, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_LOR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs)
{
    if (!block || !lhs || !rhs)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LOR, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = lhs;
    inst->m_operand1 = rhs;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}

const woort_IRValue* woort_IRBlock_LNOT(
    woort_IRBlock* block,
    const woort_IRValue* value)
{
    if (!block || !value)
    {
        return NULL;
    }
    
    woort_IRInstruction* inst = _woort_IRInstruction_create(
        WOORT_IR_INST_LNOT, block);
    if (!inst)
    {
        return NULL;
    }
    
    inst->m_operand0 = value;
    
    _woort_IRBlock_add_instruction(block, inst);
    
    return (const woort_IRValue*)inst;
}
