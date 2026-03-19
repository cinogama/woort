#include "woort_ir_codegen.h"
#include "woort_ir_builder.h"
#include "../woort_opcode.h"
#include "../woort_opcode_formal.h"
#include "../woort_gc_string.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CODE_CAPACITY 256
#define INITIAL_CONST_CAPACITY 64
#define INITIAL_PATCH_CAPACITY 32

static bool _woort_ConstantPool_init(woort_ConstantPool* pool)
{
    pool->m_entries = (woort_Value*)malloc(sizeof(woort_Value) * INITIAL_CONST_CAPACITY);
    if (!pool->m_entries) return false;
    pool->m_count = 0;
    pool->m_capacity = INITIAL_CONST_CAPACITY;
    return true;
}

static void _woort_ConstantPool_cleanup(woort_ConstantPool* pool)
{
    free(pool->m_entries);
}

static uint32_t _woort_ConstantPool_add(woort_ConstantPool* pool, woort_Value value)
{
    if (pool->m_count >= pool->m_capacity)
    {
        uint32_t new_capacity = pool->m_capacity * 2;
        woort_Value* new_entries = (woort_Value*)realloc(pool->m_entries, sizeof(woort_Value) * new_capacity);
        if (!new_entries) return (uint32_t)-1;
        pool->m_entries = new_entries;
        pool->m_capacity = new_capacity;
    }
    pool->m_entries[pool->m_count] = value;
    return pool->m_count++;
}

static bool _woort_CodeEmitter_init(woort_CodeEmitter* emitter)
{
    emitter->m_code = (woort_Bytecode*)malloc(sizeof(woort_Bytecode) * INITIAL_CODE_CAPACITY);
    if (!emitter->m_code) return false;
    emitter->m_code_size = 0;
    emitter->m_code_capacity = INITIAL_CODE_CAPACITY;

    emitter->m_patches = malloc(sizeof(*emitter->m_patches) * INITIAL_PATCH_CAPACITY);
    if (!emitter->m_patches) return false;
    emitter->m_patch_count = 0;
    emitter->m_patch_capacity = INITIAL_PATCH_CAPACITY;

    emitter->m_block_offsets = NULL;
    emitter->m_block_count = 0;

    return true;
}

static void _woort_CodeEmitter_cleanup(woort_CodeEmitter* emitter)
{
    free(emitter->m_code);
    free(emitter->m_patches);
    free(emitter->m_block_offsets);
}

static void _woort_CodeEmitter_ensure_capacity(woort_CodeEmitter* emitter, uint32_t needed)
{
    if (emitter->m_code_size + needed <= emitter->m_code_capacity) return;
    uint32_t new_capacity = emitter->m_code_capacity * 2;
    while (new_capacity < emitter->m_code_size + needed)
    {
        new_capacity *= 2;
    }
    woort_Bytecode* new_code = (woort_Bytecode*)realloc(emitter->m_code, sizeof(woort_Bytecode) * new_capacity);
    if (new_code)
    {
        emitter->m_code = new_code;
        emitter->m_code_capacity = new_capacity;
    }
}

static void _woort_CodeEmitter_emit(woort_CodeEmitter* emitter, woort_Bytecode bytecode)
{
    _woort_CodeEmitter_ensure_capacity(emitter, 1);
    emitter->m_code[emitter->m_code_size++] = bytecode;
}

static void _woort_CodeEmitter_emit_ex(woort_CodeEmitter* emitter, woort_Bytecode bytecode, uint32_t ex)
{
    _woort_CodeEmitter_ensure_capacity(emitter, 2);
    emitter->m_code[emitter->m_code_size++] = bytecode;
    emitter->m_code[emitter->m_code_size++] = ex;
}

static void _woort_CodeEmitter_add_patch(woort_CodeEmitter* emitter, uint32_t inst_offset, uint32_t target_block_id, woort_PatchKind kind)
{
    if (emitter->m_patch_count >= emitter->m_patch_capacity)
    {
        uint32_t new_capacity = emitter->m_patch_capacity * 2;
        void* new_patches = realloc(emitter->m_patches, sizeof(*emitter->m_patches) * new_capacity);
        if (!new_patches) return;
        emitter->m_patches = new_patches;
        emitter->m_patch_capacity = new_capacity;
    }
    emitter->m_patches[emitter->m_patch_count].m_inst_offset = inst_offset;
    emitter->m_patches[emitter->m_patch_count].m_target_block_id = target_block_id;
    emitter->m_patches[emitter->m_patch_count].m_kind = kind;
    emitter->m_patch_count++;
}

static void _woort_CodeEmitter_apply_patches(woort_CodeEmitter* emitter)
{
    for (uint32_t i = 0; i < emitter->m_patch_count; ++i)
    {
        uint32_t inst_offset = emitter->m_patches[i].m_inst_offset;
        uint32_t target_block_id = emitter->m_patches[i].m_target_block_id;
        uint32_t target_offset = emitter->m_block_offsets[target_block_id];
        uint32_t relative = target_offset - inst_offset;
        woort_PatchKind kind = emitter->m_patches[i].m_kind;

        woort_Bytecode* inst = &emitter->m_code[inst_offset];
        uint32_t op6 = (*inst >> 26) & 0x3F;

        if (kind == WOORT_PATCH_KIND_MABC26)
        {
            *inst = woort_OpcodeFormal_OP6_MABC26_cons(op6, relative);
        }
        else
        {
            uint32_t m2 = (*inst >> 24) & 0x3;
            uint32_t a8 = (*inst >> 16) & 0xFF;
            *inst = woort_OpcodeFormal_OP6_M2_A8_BC16_cons(op6, m2, a8, relative);
        }
    }
}

static bool _woort_StackAllocator_init(woort_StackAllocator* alloc, uint32_t value_count)
{
    alloc->m_value_slots = (woort_StackSlotInfo*)calloc(value_count, sizeof(woort_StackSlotInfo));
    if (!alloc->m_value_slots) return false;
    alloc->m_value_count = value_count;
    alloc->m_max_local_offset = 0;
    alloc->m_max_sp_offset = 0;
    return true;
}

static void _woort_StackAllocator_cleanup(woort_StackAllocator* alloc)
{
    free(alloc->m_value_slots);
}

static int32_t _woort_StackAllocator_alloc_slot(woort_StackAllocator* alloc, uint32_t value_id)
{
    if (value_id >= alloc->m_value_count) return 0;
    if (alloc->m_value_slots[value_id].m_is_allocated)
    {
        return alloc->m_value_slots[value_id].m_offset;
    }

    alloc->m_max_local_offset--;
    alloc->m_value_slots[value_id].m_offset = alloc->m_max_local_offset;
    alloc->m_value_slots[value_id].m_is_allocated = true;

    return alloc->m_value_slots[value_id].m_offset;
}

static int32_t _woort_StackAllocator_get_slot(woort_StackAllocator* alloc, uint32_t value_id)
{
    if (value_id >= alloc->m_value_count) return 0;
    if (!alloc->m_value_slots[value_id].m_is_allocated)
    {
        return _woort_StackAllocator_alloc_slot(alloc, value_id);
    }
    return alloc->m_value_slots[value_id].m_offset;
}

static bool _woort_CodeEmitter_reset_for_function(woort_CodeEmitter* emitter, uint32_t block_count)
{
    if (emitter->m_block_count < block_count)
    {
        free(emitter->m_block_offsets);
        emitter->m_block_offsets = (uint32_t*)calloc(block_count, sizeof(uint32_t));
        if (!emitter->m_block_offsets) return false;
        emitter->m_block_count = block_count;
    }
    else
    {
        memset(emitter->m_block_offsets, 0, block_count * sizeof(uint32_t));
    }
    emitter->m_patch_count = 0;
    return true;
}

static bool _woort_CodeGen_function(
    woort_IRFunction* func,
    woort_CodeEmitter* emitter,
    woort_ConstantPool* const_pool,
    woort_StackAllocator* stack_alloc,
    const woort_Bytecode** function_entries)
{
    woort_IRBlock* block = func->m_block_list;
    while (block)
    {
        emitter->m_block_offsets[block->m_id] = emitter->m_code_size;

        if (block->m_phis)
        {
        }

        woort_IRInst* inst = block->m_first;
        while (inst)
        {
            switch (inst->m_op)
            {
                case WOORT_IR_OP_RET:
                {
                    if (inst->m_operand_count == 0)
                    {
                        _woort_CodeEmitter_emit(emitter,
                            woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0));
                    }
                    else
                    {
                        int32_t slot = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                        woort_Bytecode bc = woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_RET, 1, slot);
                        _woort_CodeEmitter_emit(emitter, bc);
                    }
                    break;
                }

                case WOORT_IR_OP_BR:
                {
                    woort_IRBlock* target = (woort_IRBlock*)inst->m_operands[0];
                    if (target->m_id == block->m_next->m_id)
                    {
                    }
                    else
                    {
                        _woort_CodeEmitter_emit(emitter,
                            woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JFWD, 0));
                        _woort_CodeEmitter_add_patch(emitter, emitter->m_code_size - 1, target->m_id, WOORT_PATCH_KIND_MABC26);
                    }
                    break;
                }

                case WOORT_IR_OP_COND_BR:
                {
                    int32_t cond_slot = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    woort_IRBlock* then_block = (woort_IRBlock*)inst->m_operands[1];
                    woort_IRBlock* else_block = (woort_IRBlock*)inst->m_operands[2];

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JFWDCND, 0, cond_slot, 0));
                    _woort_CodeEmitter_add_patch(emitter, emitter->m_code_size - 1, then_block->m_id, WOORT_PATCH_KIND_BC16);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JFWD, 0));
                    _woort_CodeEmitter_add_patch(emitter, emitter->m_code_size - 1, else_block->m_id, WOORT_PATCH_KIND_MABC26);
                    break;
                }

                case WOORT_IR_OP_ADD_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_SUB_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_MUL_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_DIV_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_ADD_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_SUB_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_MUL_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_DIV_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_MOD_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_NEG_R:
                {
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPRONLG, 1, val, result));
                    break;
                }

                case WOORT_IR_OP_LT_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_GT_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_LE_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_GE_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_EQ_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_NE_R:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_ADD_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_LT_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_GT_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_LE_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_GE_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_EQ_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_NE_S:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_LT_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_GT_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_MOD_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_NEG_I:
                {
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPIONLG, 1, val, result));
                    break;
                }

                case WOORT_IR_OP_LE_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_GE_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_EQ_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 2, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_NE_I:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 3, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_AND:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPLAONI, 0, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_OR:
                {
                    int32_t lhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t rhs = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPLAONI, 1, lhs, rhs, result));
                    break;
                }

                case WOORT_IR_OP_NOT:
                {
                    int32_t value = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPLAONI, 2, value, result));
                    break;
                }

                case WOORT_IR_OP_CONST_INT:
                {
                    int64_t val = (int64_t)(intptr_t)inst->m_operands[0];
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    woort_Value const_val;
                    const_val.m_integer = val;
                    uint32_t const_idx = _woort_ConstantPool_add(const_pool, const_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, const_idx, result));
                    break;
                }

                case WOORT_IR_OP_CONST_REAL:
                {
                    double val;
                    {
                        uint64_t bits = (uint64_t)(intptr_t)inst->m_operands[0];
                        memcpy(&val, &bits, sizeof(double));
                    }
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    woort_Value const_val;
                    const_val.m_real = val;
                    uint32_t const_idx = _woort_ConstantPool_add(const_pool, const_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, const_idx, result));
                    break;
                }

                case WOORT_IR_OP_CONST_BOOL:
                {
                    bool val = (inst->m_operands[0] != NULL);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    woort_Value const_val;
                    const_val.m_integer = val ? 1 : 0;
                    uint32_t const_idx = _woort_ConstantPool_add(const_pool, const_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, const_idx, result));
                    break;
                }

                case WOORT_IR_OP_CONST_NULL:
                {
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    woort_Value const_val;
                    const_val.m_gcinstance = NULL;
                    uint32_t const_idx = _woort_ConstantPool_add(const_pool, const_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, const_idx, result));
                    break;
                }

                case WOORT_IR_OP_CONST_FUNC:
                {
                    uint32_t func_id = (uint32_t)(uintptr_t)inst->m_operands[0];
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    woort_Value const_val;
                    const_val.m_script_function = function_entries[func_id];
                    uint32_t const_idx = _woort_ConstantPool_add(const_pool, const_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, const_idx, result));
                    break;
                }

                case WOORT_IR_OP_PARAM:
                {
                    uint32_t param_idx = (uint32_t)(uintptr_t)inst->m_operands[0];
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    int32_t param_slot = 3 + (int32_t)param_idx;
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_MOV, 0, param_slot, result));
                    break;
                }

                case WOORT_IR_OP_CALL:
                {
                    uint32_t arg_count = inst->m_operand_count - 1;

                    (void)arg_count;

                    int32_t func_slot = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CALL, 0, func_slot));

                    if (inst->m_result)
                    {
                        int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                        _woort_CodeEmitter_emit(emitter,
                            woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_RESULT, 0, result));
                    }
                    else
                    {
                        _woort_CodeEmitter_emit(emitter,
                            woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_RESULT, 0, 0));
                    }
                    break;
                }

                case WOORT_IR_OP_MKVEC:
                {
                    uint32_t count = inst->m_operand_count;
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONS, 0, count, result));
                    break;
                }

                case WOORT_IR_OP_MKMAP:
                {
                    uint32_t count = inst->m_operand_count;
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONS, 1, count, result));
                    break;
                }

                case WOORT_IR_OP_MKSTRUCT:
                {
                    uint32_t count = inst->m_operand_count;
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONS, 2, count, result));
                    break;
                }

                case WOORT_IR_OP_LDVEC:
                {
                    int32_t vec = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t idx = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 0, vec, idx, result));
                    break;
                }

                case WOORT_IR_OP_LDSTR:
                {
                    int32_t str = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t idx = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 3, str, idx, result));
                    break;
                }

                case WOORT_IR_OP_LDSTRUCT:
                {
                    uint32_t field_idx = (uint32_t)(uintptr_t)inst->m_operands[0];
                    int32_t st = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 2, field_idx, st, result));
                    break;
                }

                case WOORT_IR_OP_LDMAP_I:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 0, map, key, result));
                    break;
                }

                case WOORT_IR_OP_LDMAP_R:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 1, map, key, result));
                    break;
                }

                case WOORT_IR_OP_LDMAP_B:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 2, map, key, result));
                    break;
                }

                case WOORT_IR_OP_LDMAP_X:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 3, map, key, result));
                    break;
                }

                case WOORT_IR_OP_STVEC_I:
                {
                    int32_t vec = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t idx = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 0, vec, idx, val));
                    break;
                }

                case WOORT_IR_OP_STVEC_R:
                {
                    int32_t vec = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t idx = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 1, vec, idx, val));
                    break;
                }

                case WOORT_IR_OP_STVEC_B:
                {
                    int32_t vec = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t idx = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 2, vec, idx, val));
                    break;
                }

                case WOORT_IR_OP_STVEC_X:
                {
                    int32_t vec = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t idx = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 3, vec, idx, val));
                    break;
                }

                case WOORT_IR_OP_STSTRUCT:
                {
                    uint32_t field_idx = (uint32_t)(uintptr_t)inst->m_operands[0];
                    int32_t st = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MA10_B8_C8, WOORT_OPCODE_STIDSTRUCT, field_idx, st, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_I_I:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 0, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_I_R:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 1, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_I_B:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 2, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_I_X:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 3, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_R_I:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 0, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_R_R:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 1, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_R_B:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 2, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_R_X:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 3, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_B_I:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 0, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_B_R:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 1, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_B_B:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 2, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_B_X:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 3, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_X_I:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 0, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_X_R:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 1, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_X_B:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 2, map, key, val));
                    break;
                }

                case WOORT_IR_OP_STMAP_X_X:
                {
                    int32_t map = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t key = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[2]->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 3, map, key, val));
                    break;
                }

                case WOORT_IR_OP_CONST_STR:
                {
                    const char* str = (const char*)inst->m_operands[0];
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    const woort_GCString* gc_str = woort_GCString_make_string(str, 0);
                    woort_Value const_val;
                    const_val.m_string = gc_str;
                    uint32_t const_idx = _woort_ConstantPool_add(const_pool, const_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, const_idx, result));
                    break;
                }

                case WOORT_IR_OP_MKCLOSURE:
                {
                    uint32_t capture_count = inst->m_operand_count;
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);

                    woort_Value func_val;
                    func_val.m_gcinstance = NULL;
                    uint32_t func_idx = _woort_ConstantPool_add(const_pool, func_val);

                    _woort_CodeEmitter_emit_ex(emitter,
                        woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_MKCLOSURE, capture_count, result),
                        func_idx);
                    break;
                }

                case WOORT_IR_OP_CAST_I_TO_R:
                {
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 1, val, result));
                    break;
                }

                case WOORT_IR_OP_CAST_R_TO_I:
                {
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 1, val, result));
                    break;
                }

                case WOORT_IR_OP_BOX_DYN:
                {
                    uint32_t type_id = (uint32_t)(uintptr_t)inst->m_operands[0];
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 0, type_id, val, result));
                    break;
                }

                case WOORT_IR_OP_UNBOX_DYN:
                {
                    uint32_t type_id = (uint32_t)(uintptr_t)inst->m_operands[0];
                    int32_t val = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1]->m_id);
                    int32_t result = _woort_StackAllocator_alloc_slot(stack_alloc, inst->m_result->m_id);
                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 1, type_id, val, result));
                    break;
                }

                case WOORT_IR_OP_PHI:
                    break;

                default:
                    break;
            }

            inst = inst->m_next;
        }

        block = block->m_next;
    }

    return true;
}

WOORT_NODISCARD bool woort_IRModule_codegen(
    woort_IRModule* module,
    woort_IRCodegenResult* out_result)
{
    if (module->m_function_count == 0)
    {
        return false;
    }

    woort_CodeEmitter emitter;
    woort_ConstantPool const_pool;

    if (!_woort_CodeEmitter_init(&emitter))
    {
        return false;
    }

    if (!_woort_ConstantPool_init(&const_pool))
    {
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    const woort_Bytecode** function_entries = (const woort_Bytecode**)malloc(
        sizeof(const woort_Bytecode*) * module->m_function_count);
    if (!function_entries)
    {
        _woort_ConstantPool_cleanup(&const_pool);
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    for (uint32_t func_idx = 0; func_idx < module->m_function_count; ++func_idx)
    {
        woort_IRFunction* func = module->m_functions[func_idx];

        function_entries[func_idx] = emitter.m_code + emitter.m_code_size;

        if (!_woort_CodeEmitter_reset_for_function(&emitter, func->m_block_count))
        {
            free((void*)function_entries);
            _woort_ConstantPool_cleanup(&const_pool);
            _woort_CodeEmitter_cleanup(&emitter);
            return false;
        }

        woort_StackAllocator stack_alloc;
        if (!_woort_StackAllocator_init(&stack_alloc, func->m_next_value_id))
        {
            free((void*)function_entries);
            _woort_ConstantPool_cleanup(&const_pool);
            _woort_CodeEmitter_cleanup(&emitter);
            return false;
        }

        for (uint32_t i = 0; i < func->m_param_count; ++i)
        {
            stack_alloc.m_max_local_offset--;
        }

        if (!_woort_CodeGen_function(func, &emitter, &const_pool, &stack_alloc, function_entries))
        {
            _woort_StackAllocator_cleanup(&stack_alloc);
            free((void*)function_entries);
            _woort_ConstantPool_cleanup(&const_pool);
            _woort_CodeEmitter_cleanup(&emitter);
            return false;
        }

        _woort_CodeEmitter_apply_patches(&emitter);

        _woort_StackAllocator_cleanup(&stack_alloc);
    }

    uint32_t data_count = const_pool.m_count;
    woort_CodeEnv* codeenv;
    if (!woort_CodeEnv_create(
        emitter.m_code,
        emitter.m_code_size,
        data_count,
        &codeenv))
    {
        free((void*)function_entries);
        _woort_ConstantPool_cleanup(&const_pool);
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    for (uint32_t i = 0; i < const_pool.m_count; ++i)
    {
        codeenv->m_data_begin[i] = const_pool.m_entries[i];
    }

    const woort_Bytecode* old_code_begin = emitter.m_code;

    _woort_ConstantPool_cleanup(&const_pool);
    _woort_CodeEmitter_cleanup(&emitter);

    for (uint32_t i = 0; i < module->m_function_count; ++i)
    {
        function_entries[i] = codeenv->m_code_begin + (function_entries[i] - old_code_begin);
    }

    out_result->m_codeenv = codeenv;
    out_result->m_function_entries = function_entries;
    out_result->m_function_count = module->m_function_count;

    return true;
}