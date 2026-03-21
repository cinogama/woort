/*
 * woort_ir_codegen.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_compiler.h"
#include "woort_opcode_builder.h"
#include "woort_opcode.h"
#include "woort_codeenv.h"

#include <stdlib.h>
#include <string.h>

/*
 * 字节码发射器
 */
typedef struct woort_IREmitter
{
    uint32_t* m_code;
    uint32_t m_code_count;
    uint32_t m_code_capacity;
    
    woort_Value* m_data;
    uint32_t m_data_count;
    uint32_t m_data_capacity;
    
    char m_error[256];
    bool m_has_error;
} woort_IREmitter;

#define WOORT_IR_EMITTER_INITIAL_CODE_CAPACITY 256
#define WOORT_IR_EMITTER_INITIAL_DATA_CAPACITY 64

WOORT_NODISCARD static bool _woort_ir_emitter_init(woort_IREmitter* emitter)
{
    emitter->m_code = (uint32_t*)malloc(sizeof(uint32_t) * WOORT_IR_EMITTER_INITIAL_CODE_CAPACITY);
    if (emitter->m_code == NULL)
    {
        return false;
    }
    
    emitter->m_data = (woort_Value*)malloc(sizeof(woort_Value) * WOORT_IR_EMITTER_INITIAL_DATA_CAPACITY);
    if (emitter->m_data == NULL)
    {
        free(emitter->m_code);
        return false;
    }
    
    emitter->m_code_count = 0;
    emitter->m_code_capacity = WOORT_IR_EMITTER_INITIAL_CODE_CAPACITY;
    emitter->m_data_count = 0;
    emitter->m_data_capacity = WOORT_IR_EMITTER_INITIAL_DATA_CAPACITY;
    emitter->m_error[0] = '\0';
    emitter->m_has_error = false;
    
    return true;
}

static void _woort_ir_emitter_drop(woort_IREmitter* emitter)
{
    if (emitter->m_code != NULL)
    {
        free(emitter->m_code);
    }
    if (emitter->m_data != NULL)
    {
        free(emitter->m_data);
    }
}

WOORT_NODISCARD static bool _woort_ir_emitter_ensure_code_capacity(woort_IREmitter* emitter, uint32_t needed)
{
    if (emitter->m_code_count + needed <= emitter->m_code_capacity)
    {
        return true;
    }
    
    uint32_t new_capacity = emitter->m_code_capacity;
    while (new_capacity < emitter->m_code_count + needed)
    {
        new_capacity *= 2;
    }
    
    uint32_t* new_code = (uint32_t*)realloc(emitter->m_code, sizeof(uint32_t) * new_capacity);
    if (new_code == NULL)
    {
        return false;
    }
    
    emitter->m_code = new_code;
    emitter->m_code_capacity = new_capacity;
    return true;
}

WOORT_NODISCARD static bool _woort_ir_emitter_emit(woort_IREmitter* emitter, uint32_t bytecode)
{
    if (!_woort_ir_emitter_ensure_code_capacity(emitter, 1))
    {
        return false;
    }
    
    emitter->m_code[emitter->m_code_count++] = bytecode;
    return true;
}

WOORT_NODISCARD static bool _woort_ir_emitter_emit_ext(woort_IREmitter* emitter, uint32_t bytecode, uint32_t ext)
{
    if (!_woort_ir_emitter_ensure_code_capacity(emitter, 2))
    {
        return false;
    }
    
    emitter->m_code[emitter->m_code_count++] = bytecode;
    emitter->m_code[emitter->m_code_count++] = ext;
    return true;
}

static uint32_t _woort_ir_emitter_current_pos(woort_IREmitter* emitter)
{
    return emitter->m_code_count;
}

static void _woort_ir_emitter_patch_at(woort_IREmitter* emitter, uint32_t pos, uint32_t value)
{
    if (pos < emitter->m_code_count)
    {
        emitter->m_code[pos] = value;
    }
}

/*
 * 跳转目标记录
 */
typedef struct woort_IRJumpTarget
{
    woort_IRBlock* m_block;
    uint32_t m_code_pos;
} woort_IRJumpTarget;

typedef struct woort_IRJumpPatch
{
    uint32_t m_patch_pos;
    woort_IRBlock* m_target_block;
} woort_IRJumpPatch;

/*
 * 代码生成上下文
 */
typedef struct woort_IRCodeGenCtx
{
    woort_IRCompiler* m_compiler;
    woort_IRFunction* m_func;
    woort_IREmitter m_emitter;
    
    int32_t* m_value_to_slot;
    
    woort_IRBlock* m_current_block;
    
    woort_IRJumpTarget* m_block_targets;
    uint32_t m_block_target_count;
    
    woort_IRJumpPatch* m_jump_patches;
    uint32_t m_jump_patch_count;
    uint32_t m_jump_patch_capacity;
    
} woort_IRCodeGenCtx;

WOORT_NODISCARD static bool _woort_ir_codegen_ctx_init(
    woort_IRCodeGenCtx* ctx,
    woort_IRCompiler* compiler,
    woort_IRFunction* func,
    int32_t* value_to_slot)
{
    ctx->m_compiler = compiler;
    ctx->m_func = func;
    ctx->m_value_to_slot = value_to_slot;
    ctx->m_current_block = NULL;
    
    if (!_woort_ir_emitter_init(&ctx->m_emitter))
    {
        return false;
    }
    
    ctx->m_block_targets = (woort_IRJumpTarget*)calloc(func->m_block_count, sizeof(woort_IRJumpTarget));
    if (ctx->m_block_targets == NULL)
    {
        _woort_ir_emitter_drop(&ctx->m_emitter);
        return false;
    }
    ctx->m_block_target_count = 0;
    
    ctx->m_jump_patches = (woort_IRJumpPatch*)malloc(sizeof(woort_IRJumpPatch) * func->m_block_count * 2);
    if (ctx->m_jump_patches == NULL)
    {
        free(ctx->m_block_targets);
        _woort_ir_emitter_drop(&ctx->m_emitter);
        return false;
    }
    ctx->m_jump_patch_count = 0;
    ctx->m_jump_patch_capacity = func->m_block_count * 2;
    
    return true;
}

static void _woort_ir_codegen_ctx_drop(woort_IRCodeGenCtx* ctx)
{
    _woort_ir_emitter_drop(&ctx->m_emitter);
    if (ctx->m_block_targets != NULL)
    {
        free(ctx->m_block_targets);
    }
    if (ctx->m_jump_patches != NULL)
    {
        free(ctx->m_jump_patches);
    }
}

static void _woort_ir_codegen_record_block_target(woort_IRCodeGenCtx* ctx, woort_IRBlock* block, uint32_t code_pos)
{
    if (block->m_index < ctx->m_func->m_block_count)
    {
        ctx->m_block_targets[block->m_index].m_block = block;
        ctx->m_block_targets[block->m_index].m_code_pos = code_pos;
    }
}

static void _woort_ir_codegen_add_jump_patch(woort_IRCodeGenCtx* ctx, uint32_t patch_pos, woort_IRBlock* target_block)
{
    if (ctx->m_jump_patch_count < ctx->m_jump_patch_capacity)
    {
        ctx->m_jump_patches[ctx->m_jump_patch_count].m_patch_pos = patch_pos;
        ctx->m_jump_patches[ctx->m_jump_patch_count].m_target_block = target_block;
        ctx->m_jump_patch_count++;
    }
}

static void _woort_ir_codegen_apply_patches(woort_IRCodeGenCtx* ctx)
{
    for (uint32_t i = 0; i < ctx->m_jump_patch_count; ++i)
    {
        woort_IRJumpPatch* patch = &ctx->m_jump_patches[i];
        if (patch->m_target_block->m_index < ctx->m_func->m_block_count)
        {
            uint32_t target_pos = ctx->m_block_targets[patch->m_target_block->m_index].m_code_pos;
            uint32_t instr = ctx->m_emitter.m_code[patch->m_patch_pos];
            
            uint32_t opcode = instr & WOORT_BYTECODE_OP6_MASK;
            if (opcode == WOORT_MAKE_BYTECODE(OP6, WOORT_OPCODE_JFWD))
            {
                instr = (instr & ~WOORT_BYTECODE_MABC26_MASK) | WOORT_MAKE_BYTECODE(MABC26, target_pos);
            }
            else if ((instr & WOORT_BYTECODE_OP6_MASK) == WOORT_MAKE_BYTECODE(OP6, WOORT_OPCODE_JFWDCND))
            {
                instr = (instr & ~WOORT_BYTECODE_BC16_MASK) | WOORT_MAKE_BYTECODE(BC16, target_pos - patch->m_patch_pos - 1);
            }
            else
            {
                instr = (instr & ~WOORT_BYTECODE_MABC26_MASK) | WOORT_MAKE_BYTECODE(MABC26, target_pos);
            }
            
            ctx->m_emitter.m_code[patch->m_patch_pos] = instr;
        }
    }
}

/*
 * 获取值的栈偏移
 */
WOORT_NODISCARD static int32_t _woort_ir_codegen_get_slot(woort_IRCodeGenCtx* ctx, const woort_IRValue* val)
{
    if (val == NULL)
    {
        return 0;
    }
    if (val->m_index >= ctx->m_func->m_next_value_index)
    {
        return 0;
    }
    return ctx->m_value_to_slot[val->m_index];
}

/*
 * 检查栈偏移是否在 8 位范围内
 */
static bool _woort_ir_slot_fits_i8(int32_t slot)
{
    return slot >= -128 && slot <= 127;
}

/*
 * 检查栈偏移是否在 16 位范围内
 */
static bool _woort_ir_slot_fits_i16(int32_t slot)
{
    return slot >= -32768 && slot <= 32767;
}

/*
 * 检查全局索引是否在 18 位范围内
 */
static bool _woort_ir_global_fits_u18(uint32_t global_idx)
{
    return global_idx <= 0x3FFFF;
}

/*
 * 检查全局索引是否在 24 位范围内
 */
static bool _woort_ir_global_fits_u24(uint32_t global_idx)
{
    return global_idx <= 0xFFFFFF;
}

/*
 * 检查全局索引是否在 26 位范围内
 */
static bool _woort_ir_global_fits_u26(uint32_t global_idx)
{
    return global_idx <= 0x3FFFFFF;
}

/*
 * 发射 LOAD 指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_load(
    woort_IRCodeGenCtx* ctx,
    uint32_t global_idx,
    int32_t dest_slot)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (_woort_ir_global_fits_u18(global_idx) && _woort_ir_slot_fits_i8(dest_slot))
    {
        return _woort_ir_emitter_emit(e, woort_OpCode_LOAD(global_idx, dest_slot));
    }
    else if (_woort_ir_slot_fits_i16(dest_slot))
    {
        return _woort_ir_emitter_emit_ext(e, 
            woort_OpCode_LOADEX(dest_slot),
            global_idx);
    }
    else
    {
        return false;
    }
}

/*
 * 发射 PUSH 指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_push(
    woort_IRCodeGenCtx* ctx,
    int32_t src_slot)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (_woort_ir_slot_fits_i16(src_slot))
    {
        return _woort_ir_emitter_emit(e, woort_OpCode_PUSHSCHK(src_slot));
    }
    else
    {
        return false;
    }
}

/*
 * 发射压入常量指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_push_const(
    woort_IRCodeGenCtx* ctx,
    uint32_t global_idx)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (_woort_ir_global_fits_u24(global_idx))
    {
        return _woort_ir_emitter_emit(e, woort_OpCode_PUSHCCHK(global_idx));
    }
    else
    {
        return _woort_ir_emitter_emit_ext(e,
            woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_PUSHCHK, 3),
            global_idx);
    }
}

/*
 * 发射二元运算指令（整数）
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_binop_i(
    woort_IRCodeGenCtx* ctx,
    woort_IRInstrKind kind,
    int32_t a_slot,
    int32_t b_slot,
    int32_t c_slot)
{
    woort_IREmitter* e = &ctx->m_emitter;
    uint32_t opcode = 0;
    uint32_t mode = 0;
    
    switch (kind)
    {
        case WOORT_IR_INSTR_ADD_I: opcode = WOORT_OPCODE_OPIASMD; mode = 0; break;
        case WOORT_IR_INSTR_SUB_I: opcode = WOORT_OPCODE_OPIASMD; mode = 1; break;
        case WOORT_IR_INSTR_MUL_I: opcode = WOORT_OPCODE_OPIASMD; mode = 2; break;
        case WOORT_IR_INSTR_DIV_I: opcode = WOORT_OPCODE_OPIASMD; mode = 3; break;
        case WOORT_IR_INSTR_MOD_I: opcode = WOORT_OPCODE_OPIONLG; mode = 0; break;
        case WOORT_IR_INSTR_LT_I:  opcode = WOORT_OPCODE_OPIONLG; mode = 2; break;
        case WOORT_IR_INSTR_GT_I:  opcode = WOORT_OPCODE_OPIONLG; mode = 3; break;
        case WOORT_IR_INSTR_LE_I:  opcode = WOORT_OPCODE_OPISREN; mode = 0; break;
        case WOORT_IR_INSTR_GE_I:  opcode = WOORT_OPCODE_OPISREN; mode = 1; break;
        case WOORT_IR_INSTR_EQ_I:  opcode = WOORT_OPCODE_OPISREN; mode = 2; break;
        case WOORT_IR_INSTR_NE_I:  opcode = WOORT_OPCODE_OPISREN; mode = 3; break;
        default: return false;
    }
    
    if (_woort_ir_slot_fits_i8(a_slot) && _woort_ir_slot_fits_i8(b_slot) && _woort_ir_slot_fits_i8(c_slot))
    {
        return _woort_ir_emitter_emit(e, 
            woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, opcode, mode, a_slot, b_slot, c_slot));
    }
    else
    {
        return false;
    }
}

/*
 * 发射一元运算指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_unop(
    woort_IRCodeGenCtx* ctx,
    woort_IRInstrKind kind,
    int32_t a_slot,
    int32_t result_slot)
{
    woort_IREmitter* e = &ctx->m_emitter;
    uint32_t opcode = 0;
    uint32_t mode = 1;
    
    switch (kind)
    {
        case WOORT_IR_INSTR_NEG_I: opcode = WOORT_OPCODE_OPIONLG; break;
        case WOORT_IR_INSTR_NEG_R: opcode = WOORT_OPCODE_OPRONLG; break;
        default: return false;
    }
    
    if (_woort_ir_slot_fits_i8(a_slot) && _woort_ir_slot_fits_i16(result_slot))
    {
        return _woort_ir_emitter_emit(e,
            woort_OpCodeFormal_cons(OP6_M2_A8_BC16, opcode, mode, a_slot, result_slot));
    }
    else
    {
        return false;
    }
}

/*
 * 发射 CALL 指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_call(
    woort_IRCodeGenCtx* ctx,
    woort_IRInstrKind kind,
    uint32_t func_idx,
    uint32_t argc,
    int32_t result_slot,
    bool has_result)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (!_woort_ir_global_fits_u26(func_idx))
    {
        return false;
    }
    
    switch (kind)
    {
        case WOORT_IR_INSTR_CALLNWO:
            if (!_woort_ir_emitter_emit(e, woort_OpCode_CALLNWO(func_idx)))
            {
                return false;
            }
            break;
        case WOORT_IR_INSTR_CALLNFP:
            if (!_woort_ir_emitter_emit(e, woort_OpCode_CALLNFP(func_idx)))
            {
                return false;
            }
            break;
        case WOORT_IR_INSTR_CALLNJIT:
            if (!_woort_ir_emitter_emit(e, woort_OpCode_CALLNJIT(func_idx)))
            {
                return false;
            }
            break;
        default:
            return false;
    }
    
    if (has_result)
    {
        if (!_woort_ir_slot_fits_i16(result_slot))
        {
            return false;
        }
        if (!_woort_ir_emitter_emit(e, woort_OpCode_RESULT(argc, result_slot)))
        {
            return false;
        }
    }
    else
    {
        if (!_woort_ir_emitter_emit(e, woort_OpCode_POPR(argc)))
        {
            return false;
        }
    }
    
    return true;
}

/*
 * 发射返回指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_ret(
    woort_IRCodeGenCtx* ctx,
    int32_t val_slot,
    bool has_value)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (has_value)
    {
        if (_woort_ir_slot_fits_i16(val_slot))
        {
            return _woort_ir_emitter_emit(e, woort_OpCode_RETVS(val_slot));
        }
        else
        {
            return false;
        }
    }
    else
    {
        return _woort_ir_emitter_emit(e, woort_OpCode_RET());
    }
}

/*
 * 发射无条件跳转
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_br(
    woort_IRCodeGenCtx* ctx,
    woort_IRBlock* target)
{
    woort_IREmitter* e = &ctx->m_emitter;
    woort_IRFunction* func = ctx->m_func;
    woort_IRBlock* current = ctx->m_current_block;
    
    for (uint32_t i = 0; i < func->m_phi_count; ++i)
    {
        woort_IRPHI* phi = func->m_phis[i];
        if (phi->m_block != target)
        {
            continue;
        }
        
        const woort_IRValue* incoming_value = NULL;
        for (uint32_t j = 0; j < phi->m_incoming_count; ++j)
        {
            if (phi->m_incomings[j].m_from_block == current)
            {
                incoming_value = phi->m_incomings[j].m_value;
                break;
            }
        }
        
        if (incoming_value == NULL)
        {
            continue;
        }
        
        int32_t src_slot = _woort_ir_codegen_get_slot(ctx, incoming_value);
        int32_t dst_slot = _woort_ir_codegen_get_slot(ctx, &phi->m_value);
        
        if (src_slot != dst_slot)
        {
            if (_woort_ir_slot_fits_i8(dst_slot) && _woort_ir_slot_fits_i16(src_slot))
            {
                if (!_woort_ir_emitter_emit(e, woort_OpCode_MOVLD(dst_slot, src_slot)))
                {
                    return false;
                }
            }
            else if (_woort_ir_slot_fits_i8(src_slot) && _woort_ir_slot_fits_i16(dst_slot))
            {
                if (!_woort_ir_emitter_emit(e, woort_OpCode_MOVST(src_slot, dst_slot)))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    
    uint32_t patch_pos = _woort_ir_emitter_current_pos(e);
    
    if (!_woort_ir_emitter_emit(e, woort_OpCode_JFWD(0)))
    {
        return false;
    }
    
    _woort_ir_codegen_add_jump_patch(ctx, patch_pos, target);
    
    return true;
}

/*
 * 发射条件跳转
 * 
 * 由于 JFWDLT 等指令使用相对偏移（8位），我们改用：
 * 1. 比较指令生成布尔结果
 * 2. JFWDNZ 进行条件跳转
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_br_cmp(
    woort_IRCodeGenCtx* ctx,
    woort_IRInstrKind kind,
    int32_t a_slot,
    int32_t b_slot,
    woort_IRBlock* true_block,
    woort_IRBlock* false_block)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (!_woort_ir_slot_fits_i8(a_slot) || !_woort_ir_slot_fits_i8(b_slot))
    {
        return false;
    }
    
    int32_t result_slot = a_slot;
    
    uint32_t cmp_opcode = WOORT_OPCODE_OPIASMD;
    uint32_t cmp_mode = 0;
    
    switch (kind)
    {
        case WOORT_IR_INSTR_BR_LT: 
            cmp_opcode = WOORT_OPCODE_OPIONLG; 
            cmp_mode = 2; 
            break;
        case WOORT_IR_INSTR_BR_GT: 
            cmp_opcode = WOORT_OPCODE_OPIONLG; 
            cmp_mode = 3; 
            break;
        case WOORT_IR_INSTR_BR_LE: 
            cmp_opcode = WOORT_OPCODE_OPISREN; 
            cmp_mode = 0; 
            break;
        case WOORT_IR_INSTR_BR_GE: 
            cmp_opcode = WOORT_OPCODE_OPISREN; 
            cmp_mode = 1; 
            break;
        case WOORT_IR_INSTR_BR_EQ: 
            cmp_opcode = WOORT_OPCODE_OPISREN; 
            cmp_mode = 2; 
            break;
        case WOORT_IR_INSTR_BR_NE: 
            cmp_opcode = WOORT_OPCODE_OPISREN; 
            cmp_mode = 3; 
            break;
        default: 
            return false;
    }
    
    if (!_woort_ir_emitter_emit(e, 
        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, cmp_opcode, cmp_mode, a_slot, b_slot, result_slot)))
    {
        return false;
    }
    
    uint32_t patch_pos = _woort_ir_emitter_current_pos(e);
    
    if (!_woort_ir_emitter_emit(e, woort_OpCode_JFWDNZ(result_slot, 0)))
    {
        return false;
    }
    
    _woort_ir_codegen_add_jump_patch(ctx, patch_pos, true_block);
    
    if (!_woort_ir_codegen_emit_br(ctx, false_block))
    {
        return false;
    }
    
    return true;
}

/*
 * 发射布尔条件跳转
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_br_cond(
    woort_IRCodeGenCtx* ctx,
    int32_t cond_slot,
    woort_IRBlock* true_block,
    woort_IRBlock* false_block)
{
    woort_IREmitter* e = &ctx->m_emitter;
    
    if (!_woort_ir_slot_fits_i8(cond_slot))
    {
        return false;
    }
    
    uint32_t patch_pos = _woort_ir_emitter_current_pos(e);
    
    if (!_woort_ir_emitter_emit(e, woort_OpCode_JFWDNZ(cond_slot, 0)))
    {
        return false;
    }
    
    _woort_ir_codegen_add_jump_patch(ctx, patch_pos, true_block);
    
    if (!_woort_ir_codegen_emit_br(ctx, false_block))
    {
        return false;
    }
    
    return true;
}

/*
 * 发射指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_instr(
    woort_IRCodeGenCtx* ctx,
    woort_IRInstr* instr)
{
    int32_t a_slot, b_slot, c_slot, result_slot;
    uint32_t global_idx;
    
    switch (instr->m_kind)
    {
        case WOORT_IR_INSTR_LOAD_CONST:
        case WOORT_IR_INSTR_LOAD:
            global_idx = (instr->m_kind == WOORT_IR_INSTR_LOAD_CONST) 
                ? instr->m_op.m_load_const.m_global_idx 
                : instr->m_op.m_load.m_global_idx;
            result_slot = _woort_ir_codegen_get_slot(ctx, instr->m_result);
            return _woort_ir_codegen_emit_load(ctx, global_idx, result_slot);
            
        case WOORT_IR_INSTR_STORE:
            global_idx = instr->m_op.m_store.m_global_idx;
            a_slot = _woort_ir_codegen_get_slot(ctx, instr->m_op.m_store.m_val);
            return _woort_ir_emitter_emit(&ctx->m_emitter, woort_OpCode_STORE(global_idx, a_slot));
            
        case WOORT_IR_INSTR_ADD_I:
        case WOORT_IR_INSTR_SUB_I:
        case WOORT_IR_INSTR_MUL_I:
        case WOORT_IR_INSTR_DIV_I:
        case WOORT_IR_INSTR_MOD_I:
        case WOORT_IR_INSTR_LT_I:
        case WOORT_IR_INSTR_LE_I:
        case WOORT_IR_INSTR_GT_I:
        case WOORT_IR_INSTR_GE_I:
        case WOORT_IR_INSTR_EQ_I:
        case WOORT_IR_INSTR_NE_I:
            a_slot = _woort_ir_codegen_get_slot(ctx, instr->m_op.m_binop.m_a);
            b_slot = _woort_ir_codegen_get_slot(ctx, instr->m_op.m_binop.m_b);
            c_slot = _woort_ir_codegen_get_slot(ctx, instr->m_result);
            return _woort_ir_codegen_emit_binop_i(ctx, instr->m_kind, a_slot, b_slot, c_slot);
            
        case WOORT_IR_INSTR_NEG_I:
        case WOORT_IR_INSTR_NEG_R:
            a_slot = _woort_ir_codegen_get_slot(ctx, instr->m_op.m_unop.m_a);
            result_slot = _woort_ir_codegen_get_slot(ctx, instr->m_result);
            return _woort_ir_codegen_emit_unop(ctx, instr->m_kind, a_slot, result_slot);
            
        case WOORT_IR_INSTR_PUSH:
            a_slot = _woort_ir_codegen_get_slot(ctx, instr->m_op.m_push.m_val);
            return _woort_ir_codegen_emit_push(ctx, a_slot);
            
        case WOORT_IR_INSTR_CALLNWO:
        case WOORT_IR_INSTR_CALLNFP:
        case WOORT_IR_INSTR_CALLNJIT:
            result_slot = _woort_ir_codegen_get_slot(ctx, instr->m_result);
            return _woort_ir_codegen_emit_call(ctx, instr->m_kind,
                instr->m_op.m_call_imm.m_func_idx,
                instr->m_op.m_call_imm.m_argc,
                result_slot,
                instr->m_result != NULL);
                
        default:
            return true;
    }
}

/*
 * 发射终止指令
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_terminator(
    woort_IRCodeGenCtx* ctx,
    woort_IRInstr* terminator)
{
    int32_t a_slot, b_slot, cond_slot, val_slot;
    
    switch (terminator->m_kind)
    {
        case WOORT_IR_INSTR_BR:
            return _woort_ir_codegen_emit_br(ctx, terminator->m_op.m_br.m_target);
            
        case WOORT_IR_INSTR_BR_LT:
        case WOORT_IR_INSTR_BR_LE:
        case WOORT_IR_INSTR_BR_GT:
        case WOORT_IR_INSTR_BR_GE:
        case WOORT_IR_INSTR_BR_EQ:
        case WOORT_IR_INSTR_BR_NE:
            a_slot = _woort_ir_codegen_get_slot(ctx, terminator->m_op.m_br_cmp.m_a);
            b_slot = _woort_ir_codegen_get_slot(ctx, terminator->m_op.m_br_cmp.m_b);
            return _woort_ir_codegen_emit_br_cmp(ctx, terminator->m_kind, a_slot, b_slot,
                terminator->m_op.m_br_cmp.m_true_block,
                terminator->m_op.m_br_cmp.m_false_block);
            
        case WOORT_IR_INSTR_BR_COND:
            cond_slot = _woort_ir_codegen_get_slot(ctx, terminator->m_op.m_br_cond.m_cond);
            return _woort_ir_codegen_emit_br_cond(ctx, cond_slot,
                terminator->m_op.m_br_cond.m_true_block,
                terminator->m_op.m_br_cond.m_false_block);
            
        case WOORT_IR_INSTR_RET:
            val_slot = _woort_ir_codegen_get_slot(ctx, terminator->m_op.m_ret.m_val);
            return _woort_ir_codegen_emit_ret(ctx, val_slot, true);
            
        case WOORT_IR_INSTR_RET_VOID:
            return _woort_ir_codegen_emit_ret(ctx, 0, false);
            
        default:
            return false;
    }
}

/*
 * 发射基本块
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_block(
    woort_IRCodeGenCtx* ctx,
    woort_IRBlock* block)
{
    ctx->m_current_block = block;
    
    _woort_ir_codegen_record_block_target(ctx, block, _woort_ir_emitter_current_pos(&ctx->m_emitter));
    
    for (uint32_t i = 0; i < block->m_instr_count; ++i)
    {
        if (!_woort_ir_codegen_emit_instr(ctx, &block->m_instrs[i]))
        {
            return false;
        }
    }
    
    if (block->m_has_terminator)
    {
        if (!_woort_ir_codegen_emit_terminator(ctx, &block->m_terminator))
        {
            return false;
        }
    }
    
    return true;
}

/*
 * 发射函数
 */
WOORT_NODISCARD static bool _woort_ir_codegen_emit_function(
    woort_IRCodeGenCtx* ctx)
{
    woort_IREmitter* e = &ctx->m_emitter;
    woort_IRFunction* func = ctx->m_func;
    
    int32_t min_slot = 1;
    for (uint32_t i = 0; i < func->m_next_value_index; ++i)
    {
        int32_t slot = ctx->m_value_to_slot[i];
        if (slot != INT32_MAX && slot <= 0 && slot < min_slot)
        {
            min_slot = slot;
        }
    }
    
    int32_t stack_size = (min_slot <= 0) ? -min_slot + 1 : 0;
    
    if (stack_size > 0)
    {
        if (stack_size <= 0xFFFFFF)
        {
            if (!_woort_ir_emitter_emit(e, woort_OpCode_PUSHRCHK(stack_size)))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    
    for (uint32_t i = 0; i < func->m_block_count; ++i)
    {
        if (!_woort_ir_codegen_emit_block(ctx, func->m_blocks[i]))
        {
            return false;
        }
    }
    
    _woort_ir_codegen_apply_patches(ctx);
    
    return true;
}

/*
 * 完整代码生成
 */
WOORT_NODISCARD bool _woort_ir_codegen(woort_IRCompiler* compiler, woort_CodeEnv** out_codeenv)
{
    uint32_t total_code_count = 0;
    uint32_t total_data_count = compiler->m_global_count;
    
    for (uint32_t func_idx = 0; func_idx < compiler->m_function_count; ++func_idx)
    {
        woort_IRFunction* func = compiler->m_functions[func_idx];
        
        uint32_t func_instr_count = 0;
        for (uint32_t block_idx = 0; block_idx < func->m_block_count; ++block_idx)
        {
            func_instr_count += func->m_blocks[block_idx]->m_instr_count;
        }
        total_code_count += func->m_block_count * 32 + func_instr_count + 16;
    }
    
    if (total_code_count == 0)
    {
        total_code_count = 16;
    }
    
    uint32_t* all_code = (uint32_t*)malloc(sizeof(uint32_t) * total_code_count);
    if (all_code == NULL)
    {
        _woort_ir_compiler_set_error(compiler, "Failed to allocate code buffer");
        return false;
    }
    
    uint32_t code_offset = 0;
    
    for (uint32_t func_idx = 0; func_idx < compiler->m_function_count; ++func_idx)
    {
        woort_IRFunction* func = compiler->m_functions[func_idx];
        
        int32_t* value_to_slot = (int32_t*)malloc(sizeof(int32_t) * func->m_next_value_index);
        if (value_to_slot == NULL)
        {
            free(all_code);
            _woort_ir_compiler_set_error(compiler, "Failed to allocate value_to_slot");
            return false;
        }
        
        int32_t next_local_slot = -1;
        for (uint32_t i = 0; i < func->m_next_value_index; ++i)
        {
            value_to_slot[i] = INT32_MAX;
        }
        
        for (uint32_t i = 0; i < func->m_param_count; ++i)
        {
            uint32_t idx = func->m_params[i].m_index;
            value_to_slot[idx] = (int32_t)(3 + i);
        }
        
        int32_t local_count = 0;
        for (uint32_t block_idx = 0; block_idx < func->m_block_count; ++block_idx)
        {
            woort_IRBlock* block = func->m_blocks[block_idx];
            for (uint32_t instr_idx = 0; instr_idx < block->m_instr_count; ++instr_idx)
            {
                woort_IRInstr* instr = &block->m_instrs[instr_idx];
                if (instr->m_result != NULL)
                {
                    uint32_t idx = instr->m_result->m_index;
                    if (idx < func->m_next_value_index && value_to_slot[idx] == INT32_MAX)
                    {
                        value_to_slot[idx] = -local_count;
                        local_count++;
                    }
                }
            }
        }
        
        for (uint32_t phi_idx = 0; phi_idx < func->m_phi_count; ++phi_idx)
        {
            woort_IRPHI* phi = func->m_phis[phi_idx];
            uint32_t idx = phi->m_value.m_index;
            if (idx < func->m_next_value_index && value_to_slot[idx] == INT32_MAX)
            {
                value_to_slot[idx] = -local_count;
                local_count++;
            }
        }
        
        woort_IRCodeGenCtx ctx;
        if (!_woort_ir_codegen_ctx_init(&ctx, compiler, func, value_to_slot))
        {
            free(value_to_slot);
            free(all_code);
            _woort_ir_compiler_set_error(compiler, "Failed to init codegen context");
            return false;
        }
        
        if (!_woort_ir_codegen_emit_function(&ctx))
        {
            _woort_ir_codegen_ctx_drop(&ctx);
            free(value_to_slot);
            free(all_code);
            _woort_ir_compiler_set_error(compiler, "Code generation failed for function %u", func_idx);
            return false;
        }
        
        if (ctx.m_emitter.m_code_count > 0)
        {
            if (code_offset + ctx.m_emitter.m_code_count > total_code_count)
            {
                uint32_t new_capacity = total_code_count * 2;
                while (new_capacity < code_offset + ctx.m_emitter.m_code_count)
                {
                    new_capacity *= 2;
                }
                uint32_t* new_code = (uint32_t*)realloc(all_code, sizeof(uint32_t) * new_capacity);
                if (new_code == NULL)
                {
                    _woort_ir_codegen_ctx_drop(&ctx);
                    free(value_to_slot);
                    free(all_code);
                    _woort_ir_compiler_set_error(compiler, "Failed to expand code buffer");
                    return false;
                }
                all_code = new_code;
                total_code_count = new_capacity;
            }
            
            memcpy(all_code + code_offset, ctx.m_emitter.m_code, 
                sizeof(uint32_t) * ctx.m_emitter.m_code_count);
            code_offset += ctx.m_emitter.m_code_count;
        }
        
        _woort_ir_codegen_ctx_drop(&ctx);
        free(value_to_slot);
    }
    
    woort_CodeEnv* codeenv;
    if (!woort_CodeEnv_create(all_code, code_offset, total_data_count, &codeenv))
    {
        free(all_code);
        _woort_ir_compiler_set_error(compiler, "Failed to create CodeEnv");
        return false;
    }
    
    free(all_code);
    *out_codeenv = codeenv;
    return true;
}