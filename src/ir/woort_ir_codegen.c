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

static bool _woort_CodeEmitter_init(woort_CodeEmitter* emitter, uint32_t block_count)
{
    emitter->m_code = (woort_Bytecode*)malloc(sizeof(woort_Bytecode) * INITIAL_CODE_CAPACITY);
    if (!emitter->m_code) return false;
    emitter->m_code_size = 0;
    emitter->m_code_capacity = INITIAL_CODE_CAPACITY;

    emitter->m_patches = malloc(sizeof(*emitter->m_patches) * INITIAL_PATCH_CAPACITY);
    if (!emitter->m_patches) return false;
    emitter->m_patch_count = 0;
    emitter->m_patch_capacity = INITIAL_PATCH_CAPACITY;

    emitter->m_block_offsets = (uint32_t*)calloc(block_count, sizeof(uint32_t));
    if (!emitter->m_block_offsets) return false;
    emitter->m_block_count = block_count;

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

static void _woort_CodeEmitter_add_patch(woort_CodeEmitter* emitter, uint32_t inst_offset, uint32_t target_block_id)
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

        woort_Bytecode* inst = &emitter->m_code[inst_offset];
        uint32_t op6 = (*inst >> 26) & 0x3F;
        *inst = woort_OpcodeFormal_OP6_MABC26_cons(op6, relative);
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

static bool _woort_CodeGen_function(
    woort_IRFunction* func,
    woort_CodeEmitter* emitter,
    woort_ConstantPool* const_pool,
    woort_StackAllocator* stack_alloc)
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
                        _woort_CodeEmitter_add_patch(emitter, emitter->m_code_size - 1, target->m_id);
                    }
                    break;
                }

                case WOORT_IR_OP_COND_BR:
                {
                    int32_t cond_slot = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[0]->m_id);
                    woort_IRBlock* then_block = (woort_IRBlock*)inst->m_operands[1];
                    woort_IRBlock* else_block = (woort_IRBlock*)inst->m_operands[2];

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JBCKCND, 0, cond_slot, 0));
                    _woort_CodeEmitter_add_patch(emitter, emitter->m_code_size - 1, then_block->m_id);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JFWD, 0));
                    _woort_CodeEmitter_add_patch(emitter, emitter->m_code_size - 1, else_block->m_id);
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

                    for (uint32_t i = 0; i < arg_count; ++i)
                    {
                        int32_t arg_slot = _woort_StackAllocator_get_slot(stack_alloc, inst->m_operands[1 + i]->m_id);
                    }

                    woort_Value func_val;
                    func_val.m_native_or_jit_function = NULL;
                    uint32_t func_idx = _woort_ConstantPool_add(const_pool, func_val);

                    _woort_CodeEmitter_emit(emitter,
                        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, func_idx));

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
    woort_CodeEnv** out_codeenv)
{
    if (module->m_function_count == 0)
    {
        return false;
    }

    woort_CodeEmitter emitter;
    woort_ConstantPool const_pool;
    woort_StackAllocator stack_alloc;

    woort_IRFunction* first_func = module->m_functions[0];

    if (!_woort_CodeEmitter_init(&emitter, first_func->m_block_count))
    {
        return false;
    }

    if (!_woort_ConstantPool_init(&const_pool))
    {
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    if (!_woort_StackAllocator_init(&stack_alloc, first_func->m_next_value_id))
    {
        _woort_ConstantPool_cleanup(&const_pool);
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    for (uint32_t i = 0; i < first_func->m_param_count; ++i)
    {
        stack_alloc.m_max_local_offset--;
    }

    if (!_woort_CodeGen_function(first_func, &emitter, &const_pool, &stack_alloc))
    {
        _woort_StackAllocator_cleanup(&stack_alloc);
        _woort_ConstantPool_cleanup(&const_pool);
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    _woort_CodeEmitter_apply_patches(&emitter);

    uint32_t data_count = const_pool.m_count;
    if (!woort_CodeEnv_create(
        emitter.m_code,
        emitter.m_code_size,
        data_count,
        out_codeenv))
    {
        _woort_StackAllocator_cleanup(&stack_alloc);
        _woort_ConstantPool_cleanup(&const_pool);
        _woort_CodeEmitter_cleanup(&emitter);
        return false;
    }

    woort_CodeEnv* codeenv = *out_codeenv;
    for (uint32_t i = 0; i < const_pool.m_count; ++i)
    {
        codeenv->m_data_begin[i] = const_pool.m_entries[i];
    }

    _woort_StackAllocator_cleanup(&stack_alloc);
    _woort_ConstantPool_cleanup(&const_pool);
    _woort_CodeEmitter_cleanup(&emitter);

    return true;
}