/*
 * woort_ir_codegen.c
 * 
 * IR 字节码生成核心实现。
 */

#include "woort_ir_internal.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_codeenv.h"
#include "woort_util.h"

#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * 寄存器分配
 ******************************************************************************/

static void _woort_IRRegAlloc_init(woort_IRRegAlloc* ra, int argument_count)
{
    woort_hashmap_init(&ra->m_value_to_slot, sizeof(const woort_IRValue*), sizeof(int),
        woort_util_ptr_hash, woort_util_ptr_equal);
    ra->m_next_local_slot = -1;
    ra->m_max_stack_depth = 0;
    ra->m_argument_count = argument_count;
    ra->m_push_count = 1;
}

static void _woort_IRRegAlloc_deinit(woort_IRRegAlloc* ra)
{
    woort_hashmap_deinit(&ra->m_value_to_slot);
}

static int _woort_IRRegAlloc_get_slot(woort_IRRegAlloc* ra, const woort_IRValue* value)
{
    if (!value)
    {
        return 0;
    }
    
    void* value_ptr = NULL;
    if (woort_hashmap_find(&ra->m_value_to_slot, &value, &value_ptr))
    {
        return *(int*)value_ptr;
    }
    
    int slot = 0;
    if (value->m_kind == WOORT_IRVALUE_KIND_ARGUMENT)
    {
        slot = (int)(3 + value->m_data.m_argument_index);
        woort_hashmap_insert(&ra->m_value_to_slot, &value, &slot);
        return slot;
    }
    
    if (value->m_kind == WOORT_IRVALUE_KIND_INSTRUCTION)
    {
        slot = ra->m_next_local_slot;
        ra->m_next_local_slot--;
        
        woort_hashmap_insert(&ra->m_value_to_slot, &value, &slot);
        return slot;
    }
    
    return 0;
}

static int _woort_IRRegAlloc_alloc_temp(woort_IRRegAlloc* ra)
{
    int slot = ra->m_next_local_slot;
    ra->m_next_local_slot--;
    return slot;
}

static void _woort_IRRegAlloc_record_push(woort_IRRegAlloc* ra)
{
    ra->m_push_count++;
    if (ra->m_push_count > ra->m_max_stack_depth)
    {
        ra->m_max_stack_depth = ra->m_push_count;
    }
}
static void _woort_IRRegAlloc_record_pop(woort_IRRegAlloc* ra, int count)
{
    ra->m_push_count -= count;
    if (ra->m_push_count < 0)
    {
        ra->m_push_count = 0;
    }
}

/*******************************************************************************
 * 代码生成上下文
 ******************************************************************************/

bool _woort_IRCodeGenContext_init(
    woort_IRCodeGenContext* ctx,
    woort_IRCompiler* compiler)
{
    memset(ctx, 0, sizeof(woort_IRCodeGenContext));
    
    ctx->m_compiler = compiler;
    
    woort_vector_init(&ctx->m_bytecodes, sizeof(woort_Bytecode));
    woort_hashmap_init(&ctx->m_block_to_offset, sizeof(const woort_IRBlock*), sizeof(size_t),
        woort_util_ptr_hash, woort_util_ptr_equal);
    woort_vector_init(&ctx->m_fixups, sizeof(woort_IRFixupEntry));
    
    return true;
}
void _woort_IRCodeGenContext_deinit(woort_IRCodeGenContext* ctx)
{
    woort_vector_deinit(&ctx->m_bytecodes);
    woort_hashmap_deinit(&ctx->m_block_to_offset);
    woort_vector_deinit(&ctx->m_fixups);
}
static void _woort_IRCodeGen_emit_bytecode(woort_IRCodeGenContext* ctx, woort_Bytecode bc)
{
    woort_vector_push_back(&ctx->m_bytecodes, 1, &bc);
}
static size_t _woort_IRCodeGen_current_offset(const woort_IRCodeGenContext* ctx)
{
    return ctx->m_bytecodes.m_size;
}
static void _woort_IRCodeGen_record_block_offset(woort_IRCodeGenContext* ctx, const woort_IRBlock* block)
{
    size_t offset = ctx->m_bytecodes.m_size;
    woort_hashmap_insert(&ctx->m_block_to_offset, &block, &offset);
}
static void _woort_IRCodeGen_add_fixup(
    woort_IRCodeGenContext* ctx,
    size_t bytecode_offset,
    const woort_IRBlock* target,
    woort_Opcode opcode_kind,
    int mode)
{
    woort_IRFixupEntry entry;
    entry.m_bytecode_offset = bytecode_offset;
    entry.m_target_block = target;
    entry.m_opcode_kind = opcode_kind;
    entry.m_mode = mode;
    woort_vector_push_back(&ctx->m_fixups, 1, &entry);
}

static int _woort_IRCodeGen_get_or_load_slot(
    woort_IRCodeGenContext* ctx,
    const woort_IRValue* value)
{
    if (!value)
    {
        return 0;
    }
    
    woort_IRRegAlloc* ra = &ctx->m_reg_alloc;
    
    if (value->m_kind == WOORT_IRVALUE_KIND_CONST)
    {
        int slot = _woort_IRRegAlloc_alloc_temp(ra);
        
        woort_IRGlobalIndex idx = value->m_data.m_global_index;
        
        if (idx < (1 << 18) && slot >= -128 && slot <= 127)
        {
            woort_Bytecode bc = woort_OpCode_LOAD((uint32_t)idx, (int8_t)slot);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        else
        {
            woort_Bytecode bc = woort_OpCode_LOADEX((int16_t)slot);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            _woort_IRCodeGen_emit_bytecode(ctx, (woort_Bytecode)idx);
        }
        
        return slot;
    }
    
    return _woort_IRRegAlloc_get_slot(ra, value);
}

/*******************************************************************************
 * 指令翻译
 ******************************************************************************/

static bool _woort_IRCodeGen_emit_instruction(
    woort_IRCodeGenContext* ctx,
    const woort_IRInstruction* inst)
{
    woort_IRRegAlloc* ra = &ctx->m_reg_alloc;
    
    switch (inst->m_inst_kind)
    {
    case WOORT_IR_INST_PUSH:
        {
            const woort_IRValue* operand = inst->m_operand0;
            
            if (operand->m_kind == WOORT_IRVALUE_KIND_CONST)
            {
                woort_IRGlobalIndex idx = operand->m_data.m_global_index;
                if (idx < (1 << 24))
                {
                    woort_Bytecode bc = woort_OpCode_PUSHC((uint32_t)idx);
                    _woort_IRCodeGen_emit_bytecode(ctx, bc);
                }
                else
                {
                    woort_Bytecode bc = woort_OpCode_PUSHCEXT(idx);
                    _woort_IRCodeGen_emit_bytecode(ctx, bc);
                }
            }
            else
            {
                int slot = _woort_IRRegAlloc_get_slot(ra, operand);
                woort_Bytecode bc = woort_OpCode_PUSHS(slot);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            _woort_IRRegAlloc_record_push(ra);
        }
        break;
        
    case WOORT_IR_INST_PUSH_CONST:
        {
            woort_IRGlobalIndex idx = inst->m_extra_global_index;
            if (idx < (1 << 24))
            {
                woort_Bytecode bc = woort_OpCode_PUSHC((uint32_t)idx);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                woort_Bytecode bc = woort_OpCode_PUSHCEXT(idx);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            _woort_IRRegAlloc_record_push(ra);
        }
        break;
        
    case WOORT_IR_INST_CALLNWO:
        {
            woort_IRGlobalIndex idx = inst->m_extra_global_index;
            size_t argc = inst->m_extra_size;
            
            woort_Bytecode bc = woort_OpCode_CALLNWO((uint32_t)idx);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            
            if (inst->m_need_result)
            {
                int result_slot = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
                if (argc > 0)
                {
                    woort_Bytecode result_bc = woort_OpCode_RESULT((uint32_t)argc, result_slot);
                    _woort_IRCodeGen_emit_bytecode(ctx, result_bc);
                }
            }
            else if (argc > 0)
            {
                woort_Bytecode popr_bc = woort_OpCode_POPR((uint32_t)argc);
                _woort_IRCodeGen_emit_bytecode(ctx, popr_bc);
            }
            _woort_IRRegAlloc_record_pop(ra, (int)argc);
        }
        break;
        
    case WOORT_IR_INST_CALLNFP:
        {
            woort_IRGlobalIndex idx = inst->m_extra_global_index;
            size_t argc = inst->m_extra_size;
            
            woort_Bytecode bc = woort_OpCode_CALLNFP((uint32_t)idx);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            
            if (inst->m_need_result)
            {
                int result_slot = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
                if (argc > 0)
                {
                    woort_Bytecode result_bc = woort_OpCode_RESULT((uint32_t)argc, result_slot);
                    _woort_IRCodeGen_emit_bytecode(ctx, result_bc);
                }
            }
            else if (argc > 0)
            {
                woort_Bytecode popr_bc = woort_OpCode_POPR((uint32_t)argc);
                _woort_IRCodeGen_emit_bytecode(ctx, popr_bc);
            }
            _woort_IRRegAlloc_record_pop(ra, (int)argc);
        }
        break;
        
    case WOORT_IR_INST_CALLNJIT:
        {
            woort_IRGlobalIndex idx = inst->m_extra_global_index;
            size_t argc = inst->m_extra_size;
            
            woort_Bytecode bc = woort_OpCode_CALLNJIT((uint32_t)idx);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            
            if (inst->m_need_result)
            {
                int result_slot = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
                if (argc > 0)
                {
                    woort_Bytecode result_bc = woort_OpCode_RESULT((uint32_t)argc, result_slot);
                    _woort_IRCodeGen_emit_bytecode(ctx, result_bc);
                }
            }
            else if (argc > 0)
            {
                woort_Bytecode popr_bc = woort_OpCode_POPR((uint32_t)argc);
                _woort_IRCodeGen_emit_bytecode(ctx, popr_bc);
            }
            _woort_IRRegAlloc_record_pop(ra, (int)argc);
        }
        break;
        
    case WOORT_IR_INST_ADDI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= -0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_ADDI(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_ADDI(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_SUBI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_SUBI(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_SUBI(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_MULI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_MULI(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_MULI(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_DIVI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_DIVI(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_DIVI(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_MODI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_MODI(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_MODI(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_NEGI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256)
            {
                woort_Bytecode bc = woort_OpCode_NEGI(slot_a, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_NEGI(slot_a, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_ADDR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_ADDR(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_ADDR(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_SUBR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_SUBR(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_SUBR(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_MULR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_MULR(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_MULR(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_DIVR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_DIVR(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_DIVR(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_MODR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256 && slot_b >= 0 && slot_b < 256)
            {
                woort_Bytecode bc = woort_OpCode_MODR(slot_a, slot_b, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_MODR(slot_a, slot_b, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_NEGR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_dst >= 0 && slot_dst < 256 && slot_a >= 0 && slot_a < 256)
            {
                woort_Bytecode bc = woort_OpCode_NEGR(slot_a, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                int temp = _woort_IRRegAlloc_alloc_temp(ra);
                woort_Bytecode bc = woort_OpCode_NEGR(slot_a, temp);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
                woort_Bytecode mov_bc = woort_OpCode_MOVLD(temp, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, mov_bc);
            }
        }
        break;
        
    case WOORT_IR_INST_LTI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LTI(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_GTI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_GTI(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LEI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LEI(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_GEI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_GEI(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_EQI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_EQI(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_NEI:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_NEI(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LTR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LTR(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_GTR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_GTR(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LER:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LER(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_GER:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_GER(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_EQR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_EQR(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_NER:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_NER(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LAND:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LAND(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LOR:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_b = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LOR(slot_a, slot_b, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LNOT:
        {
            int slot_a = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LNOT(slot_a, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_MKVEC:
        {
            size_t count = inst->m_extra_size;
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (count < 256)
            {
                woort_Bytecode bc = woort_OpCode_MKVEC((uint8_t)count, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                woort_Bytecode bc = woort_OpCode_MKVECEXT(slot_dst, (uint32_t)count);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            _woort_IRRegAlloc_record_pop(ra, (int)count);
        }
        break;
        
    case WOORT_IR_INST_MKMAP:
        {
            size_t count = inst->m_extra_size;
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (count < 256)
            {
                woort_Bytecode bc = woort_OpCode_MKMAP((uint8_t)count, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                woort_Bytecode bc = woort_OpCode_MKMAPEXT(slot_dst, (uint32_t)count);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            _woort_IRRegAlloc_record_pop(ra, (int)count);
        }
        break;
        
    case WOORT_IR_INST_MKSTRUCT:
        {
            size_t count = inst->m_extra_size;
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (count < 256)
            {
                woort_Bytecode bc = woort_OpCode_MKSTRUCT((uint8_t)count, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            else
            {
                woort_Bytecode bc = woort_OpCode_MKSTRUCTEXT(slot_dst, (uint32_t)count);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
            _woort_IRRegAlloc_record_pop(ra, (int)count);
        }
        break;
        
    case WOORT_IR_INST_MKCLOSURE:
        {
            woort_IRGlobalIndex func_idx = inst->m_extra_global_index;
            size_t capture_count = inst->m_extra_size;
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_MKCLOSURE((uint32_t)capture_count, func_idx);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            _woort_IRRegAlloc_record_pop(ra, (int)capture_count);
        }
        break;
        
    case WOORT_IR_INST_CASTI_TO_R:
        {
            int slot_src = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_src >= 0 && slot_dst >= 0 && slot_src < 256 && slot_dst < 256)
            {
                woort_Bytecode bc = woort_OpCode_ITORST(slot_src, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
        }
        break;
        
    case WOORT_IR_INST_CASTR_TO_I:
        {
            int slot_src = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_src >= 0 && slot_dst >= 0 && slot_src < 256 && slot_dst < 256)
            {
                woort_Bytecode bc = woort_OpCode_RTOIST(slot_src, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
        }
        break;
        
    case WOORT_IR_INST_CASTI_TO_S:
        {
            int slot_src = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_src >= 0 && slot_dst >= 0 && slot_src < 256 && slot_dst < 256)
            {
                woort_Bytecode bc = woort_OpCode_ITOSST(slot_src, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
        }
        break;
        
    case WOORT_IR_INST_CASTR_TO_S:
        {
            int slot_src = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            if (slot_src >= 0 && slot_dst >= 0 && slot_src < 256 && slot_dst < 256)
            {
                woort_Bytecode bc = woort_OpCode_RTOSST(slot_src, slot_dst);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
        }
        break;
        
    case WOORT_IR_INST_LDIDXVEC:
        {
            int slot_vec = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_idx = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LDIDXVEC(slot_vec, slot_idx, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_STIDXVEC:
        {
            int slot_vec = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_idx = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            int slot_val = _woort_IRRegAlloc_get_slot(ra, inst->m_operand2);
            
            woort_Bytecode bc = woort_OpCode_STIDXVEC_I(slot_vec, slot_idx, slot_val);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_LDIDSTRUCT:
        {
            size_t field_idx = inst->m_extra_size;
            int slot_struct = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_dst = _woort_IRRegAlloc_get_slot(ra, (const woort_IRValue*)inst);
            
            woort_Bytecode bc = woort_OpCode_LDIDSTRUCT((uint8_t)field_idx, slot_struct, slot_dst);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    case WOORT_IR_INST_STIDSTRUCT:
        {
            size_t field_idx = inst->m_extra_size;
            int slot_struct = _woort_IRRegAlloc_get_slot(ra, inst->m_operand0);
            int slot_val = _woort_IRRegAlloc_get_slot(ra, inst->m_operand1);
            
            woort_Bytecode bc = woort_OpCode_STIDSTRUCT((uint32_t)field_idx, slot_struct, slot_val);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    default:
        break;
    }
    
    return true;
}

/*******************************************************************************
 * 终结指令翻译
 ******************************************************************************/

static bool _woort_IRCodeGen_emit_terminator(
    woort_IRCodeGenContext* ctx,
    const woort_IRBlock* block)
{
    const woort_IRTerminator* term = &block->m_terminator;
    woort_IRRegAlloc* ra = &ctx->m_reg_alloc;
    
    switch (term->m_kind)
    {
    case WOORT_IR_TERMINATOR_BR:
        {
            size_t bc_offset = _woort_IRCodeGen_current_offset(ctx);
            woort_Bytecode bc = woort_OpCode_JFWD(0);
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            
            _woort_IRCodeGen_add_fixup(ctx, bc_offset, 
                term->m_data.m_br.m_target, WOORT_OPCODE_JFWD, 0);
        }
        break;
        
    case WOORT_IR_TERMINATOR_CONDBR:
        {
            int slot_lhs = _woort_IRCodeGen_get_or_load_slot(ctx, term->m_data.m_condbr.m_lhs);
            int slot_rhs = _woort_IRCodeGen_get_or_load_slot(ctx, term->m_data.m_condbr.m_rhs);
            
            size_t bc_offset = _woort_IRCodeGen_current_offset(ctx);
            woort_Bytecode bc;
            
            switch (term->m_data.m_condbr.m_cond_kind)
            {
            case WOORT_IR_CONDBR_LESS_THEN:
                bc = woort_OpCode_JFWDLT(slot_lhs, slot_rhs, 0);
                break;
            case WOORT_IR_CONDBR_GREATER_THEN:
                bc = woort_OpCode_JFWDGT(slot_lhs, slot_rhs, 0);
                break;
            case WOORT_IR_CONDBR_LESS_EQUAL:
                bc = woort_OpCode_JFWDEL(slot_lhs, slot_rhs, 0);
                break;
            case WOORT_IR_CONDBR_GREATER_EQUAL:
                bc = woort_OpCode_JFWDEG(slot_lhs, slot_rhs, 0);
                break;
            case WOORT_IR_CONDBR_EQUAL:
                bc = woort_OpCode_JFWDEQ(slot_lhs, slot_rhs, 0);
                break;
            case WOORT_IR_CONDBR_NOT_EQUAL:
                bc = woort_OpCode_JFWDNEQ(slot_lhs, slot_rhs, 0);
                break;
            case WOORT_IR_CONDBR_TRUE:
                bc = woort_OpCode_JFWDNZ(slot_lhs, 0);
                break;
            case WOORT_IR_CONDBR_FALSE:
                bc = woort_OpCode_JFWDZ(slot_lhs, 0);
                break;
            default:
                bc = woort_OpCode_JFWD(0);
                break;
            }
            
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
            _woort_IRCodeGen_add_fixup(ctx, bc_offset,
                term->m_data.m_condbr.m_then_block, WOORT_OPCODE_JFWDCND, 
                (int)term->m_data.m_condbr.m_cond_kind);
            
            size_t else_offset = _woort_IRCodeGen_current_offset(ctx);
            woort_Bytecode else_bc = woort_OpCode_JFWD(0);
            _woort_IRCodeGen_emit_bytecode(ctx, else_bc);
            _woort_IRCodeGen_add_fixup(ctx, else_offset,
                term->m_data.m_condbr.m_else_block, WOORT_OPCODE_JFWD, 0);
        }
        break;
        
    case WOORT_IR_TERMINATOR_RET:
        {
            const woort_IRValue* ret_value = term->m_data.m_ret.m_value;
            if (ret_value && ret_value->m_kind == WOORT_IRVALUE_KIND_CONST)
            {
                woort_IRGlobalIndex idx = ret_value->m_data.m_global_index;
                if (idx < (1 << 24))
                {
                    woort_Bytecode bc = woort_OpCode_RETVC((uint32_t)idx);
                    _woort_IRCodeGen_emit_bytecode(ctx, bc);
                }
                else
                {
                    int slot = _woort_IRCodeGen_get_or_load_slot(ctx, ret_value);
                    woort_Bytecode bc = woort_OpCode_RETVS(slot);
                    _woort_IRCodeGen_emit_bytecode(ctx, bc);
                }
            }
            else
            {
                int slot = _woort_IRCodeGen_get_or_load_slot(ctx, ret_value);
                woort_Bytecode bc = woort_OpCode_RETVS(slot);
                _woort_IRCodeGen_emit_bytecode(ctx, bc);
            }
        }
        break;
        
    case WOORT_IR_TERMINATOR_RET_VOID:
        {
            woort_Bytecode bc = woort_OpCode_RET();
            _woort_IRCodeGen_emit_bytecode(ctx, bc);
        }
        break;
        
    default:
        break;
    }
    
    return true;
}

/*******************************************************************************
 * 跳转回填
 ******************************************************************************/

static bool _woort_IRCodeGen_resolve_fixups(woort_IRCodeGenContext* ctx)
{
    for (size_t i = 0; i < ctx->m_fixups.m_size; ++i)
    {
        woort_IRFixupEntry* entry = (woort_IRFixupEntry*)woort_vector_at(&ctx->m_fixups, i);
        
        void* value_ptr = NULL;
        if (!woort_hashmap_find(&ctx->m_block_to_offset, &entry->m_target_block, &value_ptr))
        {
            continue;
        }
        
        size_t target_offset = *(size_t*)value_ptr;
        
        woort_Bytecode* bc = (woort_Bytecode*)woort_vector_at(&ctx->m_bytecodes, entry->m_bytecode_offset);
        
        if (entry->m_opcode_kind == WOORT_OPCODE_JFWD)
        {
            if (target_offset == entry->m_bytecode_offset + 1)
            {
                *bc = woort_OpCode_NOP();
            }
            else
            {
                uint32_t abs_offset = (uint32_t)target_offset;
                *bc = (*bc & ~WOORT_BYTECODE_MABC26_MASK) | (abs_offset << WOORT_BYTECODE_MABC26_SHIFT);
            }
        }
        else
        {
            if (target_offset == entry->m_bytecode_offset + 1)
            {
                *bc = woort_OpCode_NOP();
            }
            else
            {
                int32_t rel_offset = (int32_t)target_offset - (int32_t)entry->m_bytecode_offset;
                *bc = (*bc & ~WOORT_BYTECODE_C8_MASK) | ((uint32_t)rel_offset << WOORT_BYTECODE_C8_SHIFT);
            }
        }
    }
    
    return true;
}
/*******************************************************************************
 * 函数编译
 ******************************************************************************/

bool _woort_IRCodeGen_compile_function(
    woort_IRCodeGenContext* ctx,
    woort_IRFunction* func)
{
    ctx->m_current_function = func;
    
    int arg_count = (int)func->m_argument_values.m_size;
    _woort_IRRegAlloc_init(&ctx->m_reg_alloc, arg_count);
    
    int stack_reserve = -(ctx->m_reg_alloc.m_next_local_slot);
    woort_Bytecode reserve_bc = woort_OpCode_PUSHRCHK((uint32_t)stack_reserve);
    _woort_IRCodeGen_emit_bytecode(ctx, reserve_bc);
    
    for (size_t i = 0; i < func->m_blocks.m_size; ++i)
    {
        woort_IRBlock* block = *(woort_IRBlock**)woort_vector_at(&func->m_blocks, i);
        
        _woort_IRCodeGen_record_block_offset(ctx, block);
        
        for (size_t j = 0; j < block->m_instructions.m_size; ++j)
        {
            woort_IRInstruction* inst = *(woort_IRInstruction**)woort_vector_at(&block->m_instructions, j);
            _woort_IRCodeGen_emit_instruction(ctx, inst);
        }
        
        if (block->m_terminator.m_kind != WOORT_IR_TERMINATOR_NONE)
        {
            _woort_IRCodeGen_emit_terminator(ctx, block);
        }
    }
    
    _woort_IRCodeGen_resolve_fixups(ctx);
    
    _woort_IRRegAlloc_deinit(&ctx->m_reg_alloc);
    
    return true;
}
/*******************************************************************************
 * CodeEnv 创建
 ******************************************************************************/

bool _woort_IRCodeGen_create_code_env(
    woort_IRCodeGenContext* ctx,
    /* OPTIONAL */ woort_CodeEnv** out_code_env)
{
    if (!out_code_env)
    {
        return false;
    }
    
    size_t bytecode_count = ctx->m_bytecodes.m_size;
    woort_Bytecode* bytecodes = (woort_Bytecode*)ctx->m_bytecodes.m_data;
    
    size_t* offset_map = (size_t*)malloc(bytecode_count * sizeof(size_t));
    if (!offset_map)
    {
        return false;
    }
    
    size_t new_count = 0;
    for (size_t i = 0; i < bytecode_count; ++i)
    {
        offset_map[i] = new_count;
        woort_Bytecode bc = bytecodes[i];
        uint32_t op6 = WOORT_BYTECODE(OP6, bc);
        if (op6 != WOORT_OPCODE_NOP)
        {
            new_count++;
        }
    }
    
    woort_Bytecode* new_bytecodes = (woort_Bytecode*)malloc(new_count * sizeof(woort_Bytecode));
    if (!new_bytecodes)
    {
        free(offset_map);
        return false;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < bytecode_count; ++i)
    {
        woort_Bytecode bc = bytecodes[i];
        uint32_t op6 = WOORT_BYTECODE(OP6, bc);
        
        if (op6 == WOORT_OPCODE_NOP)
        {
            continue;
        }
        
        if (op6 == WOORT_OPCODE_JFWD)
        {
            uint32_t old_target = WOORT_BYTECODE(MABC26, bc);
            uint32_t new_target = (uint32_t)offset_map[old_target];
            bc = woort_OpCode_JFWD(new_target);
        }
        else if (op6 == WOORT_OPCODE_JFWDCND)
        {
            uint32_t m2 = WOORT_BYTECODE(M2, bc);
            uint32_t a8 = WOORT_BYTECODE(A8, bc);
            
            if (m2 <= 1)
            {
                int16_t rel = (int16_t)WOORT_BYTECODE(BC16, bc);
                size_t old_target = i + rel;
                size_t new_target = offset_map[old_target];
                int32_t new_rel = (int32_t)new_target - (int32_t)j;
                
                if (m2 == 0)
                    bc = woort_OpCode_JFWDNZ((int8_t)a8, (uint16_t)new_rel);
                else
                    bc = woort_OpCode_JFWDZ((int8_t)a8, (uint16_t)new_rel);
            }
            else
            {
                int8_t rel = (int8_t)WOORT_BYTECODE(C8, bc);
                size_t old_target = i + rel;
                int32_t new_rel = (int32_t)offset_map[old_target] - (int32_t)j;
                
                uint32_t b8 = WOORT_BYTECODE(B8, bc);
                
                if (m2 == 2)
                    bc = woort_OpCode_JFWDEQ((int8_t)a8, (int8_t)b8, (int8_t)new_rel);
                else
                    bc = woort_OpCode_JFWDNEQ((int8_t)a8, (int8_t)b8, (int8_t)new_rel);
            }
        }
        else if (op6 == WOORT_OPCODE_JFDCMP)
        {
            int8_t rel = (int8_t)WOORT_BYTECODE(C8, bc);
            size_t old_target = i + rel;
            int32_t new_rel = (int32_t)offset_map[old_target] - (int32_t)j;
            
            uint32_t m2 = WOORT_BYTECODE(M2, bc);
            uint32_t a8 = WOORT_BYTECODE(A8, bc);
            uint32_t b8 = WOORT_BYTECODE(B8, bc);
            
            switch (m2)
            {
            case 0: bc = woort_OpCode_JFWDLT((int8_t)a8, (int8_t)b8, (int8_t)new_rel); break;
            case 1: bc = woort_OpCode_JFWDGT((int8_t)a8, (int8_t)b8, (int8_t)new_rel); break;
            case 2: bc = woort_OpCode_JFWDEL((int8_t)a8, (int8_t)b8, (int8_t)new_rel); break;
            case 3: bc = woort_OpCode_JFWDEG((int8_t)a8, (int8_t)b8, (int8_t)new_rel); break;
            }
        }
        
        new_bytecodes[j++] = bc;
    }
    
    bool result = woort_CodeEnv_create(
        new_bytecodes,
        new_count,
        ctx->m_compiler->m_global_count,
        out_code_env);
    
    free(new_bytecodes);
    free(offset_map);
    
    return result;
}
