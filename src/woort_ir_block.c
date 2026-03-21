/*
 * woort_ir_block.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_block.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WOORT_IR_INITIAL_INSTR_CAPACITY 32
#define WOORT_IR_INITIAL_PRED_CAPACITY 8
#define WOORT_IR_INITIAL_SUCC_CAPACITY 8

WOORT_NODISCARD bool _woort_ir_block_init(
    woort_IRBlock** out_block,
    woort_IRFunction* func,
    uint32_t index,
    bool is_entry)
{
    assert(out_block != NULL);
    assert(func != NULL);

    woort_IRBlock* block = (woort_IRBlock*)malloc(sizeof(woort_IRBlock));
    if (block == NULL)
    {
        return false;
    }

    block->m_func = func;
    block->m_index = index;
    block->m_is_entry = is_entry;

    /* Instructions */
    block->m_instrs = (woort_IRInstr*)malloc(sizeof(woort_IRInstr) * WOORT_IR_INITIAL_INSTR_CAPACITY);
    if (block->m_instrs == NULL)
    {
        free(block);
        return false;
    }
    block->m_instr_count = 0;
    block->m_instr_capacity = WOORT_IR_INITIAL_INSTR_CAPACITY;

    /* Terminator */
    memset(&block->m_terminator, 0, sizeof(woort_IRInstr));
    block->m_has_terminator = false;

    /* Parameters (none for now) */
    block->m_params = NULL;
    block->m_param_count = 0;

    /* Predecessors */
    block->m_predecessors = (woort_IRBlock**)malloc(sizeof(woort_IRBlock*) * WOORT_IR_INITIAL_PRED_CAPACITY);
    if (block->m_predecessors == NULL)
    {
        free(block->m_instrs);
        free(block);
        return false;
    }
    block->m_predecessor_count = 0;
    block->m_predecessor_capacity = WOORT_IR_INITIAL_PRED_CAPACITY;

    /* Successors */
    block->m_successors = (woort_IRBlock**)malloc(sizeof(woort_IRBlock*) * WOORT_IR_INITIAL_SUCC_CAPACITY);
    if (block->m_successors == NULL)
    {
        free(block->m_predecessors);
        free(block->m_instrs);
        free(block);
        return false;
    }
    block->m_successor_count = 0;
    block->m_successor_capacity = WOORT_IR_INITIAL_SUCC_CAPACITY;

    *out_block = block;
    return true;
}

void _woort_ir_block_drop(woort_IRBlock* block)
{
    if (block == NULL)
    {
        return;
    }

    if (block->m_instrs != NULL)
    {
        for (uint32_t i = 0; i < block->m_instr_count; ++i)
        {
            if (block->m_instrs[i].m_result != NULL)
            {
                free(block->m_instrs[i].m_result);
            }
        }
        free(block->m_instrs);
    }
    if (block->m_params != NULL)
    {
        free(block->m_params);
    }
    if (block->m_predecessors != NULL)
    {
        free(block->m_predecessors);
    }
    if (block->m_successors != NULL)
    {
        free(block->m_successors);
    }

    free(block);
}

WOORT_NODISCARD woort_IRInstr* _woort_ir_block_append_instr(woort_IRBlock* block)
{
    assert(block != NULL);

    if (block->m_instr_count >= block->m_instr_capacity)
    {
        uint32_t new_capacity = block->m_instr_capacity * 2;
        woort_IRInstr* new_instrs = (woort_IRInstr*)realloc(
            block->m_instrs,
            sizeof(woort_IRInstr) * new_capacity);
        if (new_instrs == NULL)
        {
            return NULL;
        }
        block->m_instrs = new_instrs;
        block->m_instr_capacity = new_capacity;
    }

    woort_IRInstr* instr = &block->m_instrs[block->m_instr_count];
    memset(instr, 0, sizeof(woort_IRInstr));
    block->m_instr_count++;

    return instr;
}

void _woort_ir_block_add_successor(woort_IRBlock* block, woort_IRBlock* successor)
{
    assert(block != NULL);
    assert(successor != NULL);

    if (block->m_successor_count >= block->m_successor_capacity)
    {
        uint32_t new_capacity = block->m_successor_capacity * 2;
        woort_IRBlock** new_successors = (woort_IRBlock**)realloc(
            block->m_successors,
            sizeof(woort_IRBlock*) * new_capacity);
        if (new_successors == NULL)
        {
            return;
        }
        block->m_successors = new_successors;
        block->m_successor_capacity = new_capacity;
    }

    block->m_successors[block->m_successor_count] = successor;
    block->m_successor_count++;
}

void _woort_ir_block_add_predecessor(woort_IRBlock* block, woort_IRBlock* predecessor)
{
    assert(block != NULL);
    assert(predecessor != NULL);

    if (block->m_predecessor_count >= block->m_predecessor_capacity)
    {
        uint32_t new_capacity = block->m_predecessor_capacity * 2;
        woort_IRBlock** new_predecessors = (woort_IRBlock**)realloc(
            block->m_predecessors,
            sizeof(woort_IRBlock*) * new_capacity);
        if (new_predecessors == NULL)
        {
            return;
        }
        block->m_predecessors = new_predecessors;
        block->m_predecessor_capacity = new_capacity;
    }

    block->m_predecessors[block->m_predecessor_count] = predecessor;
    block->m_predecessor_count++;
}

static WOORT_NODISCARD woort_IRValue* _woort_ir_block_create_result_value(woort_IRBlock* block, woort_IRInstr* instr)
{
    woort_IRValue* val = (woort_IRValue*)malloc(sizeof(woort_IRValue));
    if (val == NULL)
    {
        return NULL;
    }

    val->m_defining_block = block;
    val->m_index = block->m_func->m_next_value_index++;
    val->m_defining_instr = instr;

    return val;
}

/*
 * ============================================================
 * 常量/全局加载
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_load_const(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_idx)
{
    assert(block != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_LOAD_CONST;
    instr->m_op.m_load_const.m_global_idx = global_idx;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LOAD(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_idx)
{
    assert(block != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_LOAD;
    instr->m_op.m_load.m_global_idx = global_idx;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

void woort_IRBlock_STORE(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_idx,
    const woort_IRValue* val)
{
    assert(block != NULL);
    assert(val != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return;
    }

    instr->m_kind = WOORT_IR_INSTR_STORE;
    instr->m_op.m_store.m_global_idx = global_idx;
    instr->m_op.m_store.m_val = val;
    instr->m_result = NULL;
}

/*
 * ============================================================
 * 算术运算 - 整数
 * ============================================================
 */

#define DEFINE_BINOP_I(name, kind) \
    WOORT_NODISCARD const woort_IRValue* woort_IRBlock_##name(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b) \
    { \
        assert(block != NULL); \
        assert(a != NULL); \
        assert(b != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return NULL; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_binop.m_a = a; \
        instr->m_op.m_binop.m_b = b; \
        instr->m_result = _woort_ir_block_create_result_value(block, instr); \
        \
        return instr->m_result; \
    }

DEFINE_BINOP_I(ADD_I, WOORT_IR_INSTR_ADD_I)
DEFINE_BINOP_I(SUB_I, WOORT_IR_INSTR_SUB_I)
DEFINE_BINOP_I(MUL_I, WOORT_IR_INSTR_MUL_I)
DEFINE_BINOP_I(DIV_I, WOORT_IR_INSTR_DIV_I)
DEFINE_BINOP_I(MOD_I, WOORT_IR_INSTR_MOD_I)

#define DEFINE_UNOP_I(name, kind) \
    WOORT_NODISCARD const woort_IRValue* woort_IRBlock_##name(woort_IRBlock* block, const woort_IRValue* a) \
    { \
        assert(block != NULL); \
        assert(a != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return NULL; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_unop.m_a = a; \
        instr->m_result = _woort_ir_block_create_result_value(block, instr); \
        \
        return instr->m_result; \
    }

DEFINE_UNOP_I(NEG_I, WOORT_IR_INSTR_NEG_I)

/*
 * ============================================================
 * 算术运算 - 实数
 * ============================================================
 */

DEFINE_BINOP_I(ADD_R, WOORT_IR_INSTR_ADD_R)
DEFINE_BINOP_I(SUB_R, WOORT_IR_INSTR_SUB_R)
DEFINE_BINOP_I(MUL_R, WOORT_IR_INSTR_MUL_R)
DEFINE_BINOP_I(DIV_R, WOORT_IR_INSTR_DIV_R)
DEFINE_BINOP_I(MOD_R, WOORT_IR_INSTR_MOD_R)
DEFINE_UNOP_I(NEG_R, WOORT_IR_INSTR_NEG_R)

/*
 * ============================================================
 * 算术运算 - 字符串
 * ============================================================
 */

DEFINE_BINOP_I(ADD_S, WOORT_IR_INSTR_ADD_S)

/*
 * ============================================================
 * 比较运算 - 整数
 * ============================================================
 */

DEFINE_BINOP_I(LT_I, WOORT_IR_INSTR_LT_I)
DEFINE_BINOP_I(LE_I, WOORT_IR_INSTR_LE_I)
DEFINE_BINOP_I(GT_I, WOORT_IR_INSTR_GT_I)
DEFINE_BINOP_I(GE_I, WOORT_IR_INSTR_GE_I)
DEFINE_BINOP_I(EQ_I, WOORT_IR_INSTR_EQ_I)
DEFINE_BINOP_I(NE_I, WOORT_IR_INSTR_NE_I)

/*
 * ============================================================
 * 比较运算 - 实数
 * ============================================================
 */

DEFINE_BINOP_I(LT_R, WOORT_IR_INSTR_LT_R)
DEFINE_BINOP_I(LE_R, WOORT_IR_INSTR_LE_R)
DEFINE_BINOP_I(GT_R, WOORT_IR_INSTR_GT_R)
DEFINE_BINOP_I(GE_R, WOORT_IR_INSTR_GE_R)
DEFINE_BINOP_I(EQ_R, WOORT_IR_INSTR_EQ_R)
DEFINE_BINOP_I(NE_R, WOORT_IR_INSTR_NE_R)

/*
 * ============================================================
 * 比较运算 - 字符串
 * ============================================================
 */

DEFINE_BINOP_I(LT_S, WOORT_IR_INSTR_LT_S)
DEFINE_BINOP_I(LE_S, WOORT_IR_INSTR_LE_S)
DEFINE_BINOP_I(GT_S, WOORT_IR_INSTR_GT_S)
DEFINE_BINOP_I(GE_S, WOORT_IR_INSTR_GE_S)
DEFINE_BINOP_I(EQ_S, WOORT_IR_INSTR_EQ_S)
DEFINE_BINOP_I(NE_S, WOORT_IR_INSTR_NE_S)

/*
 * ============================================================
 * 比较运算 - 布尔
 * ============================================================
 */

DEFINE_BINOP_I(EQ_B, WOORT_IR_INSTR_EQ_B)
DEFINE_BINOP_I(NE_B, WOORT_IR_INSTR_NE_B)

/*
 * ============================================================
 * 比较运算 - 动态类型
 * ============================================================
 */

DEFINE_BINOP_I(EQ_X, WOORT_IR_INSTR_EQ_X)
DEFINE_BINOP_I(NE_X, WOORT_IR_INSTR_NE_X)

/*
 * ============================================================
 * 逻辑运算
 * ============================================================
 */

DEFINE_BINOP_I(LAND, WOORT_IR_INSTR_LAND)
DEFINE_BINOP_I(LOR, WOORT_IR_INSTR_LOR)
DEFINE_UNOP_I(LNOT, WOORT_IR_INSTR_LNOT)

/*
 * ============================================================
 * 类型转换
 * ============================================================
 */

DEFINE_UNOP_I(ITOR, WOORT_IR_INSTR_ITOR)
DEFINE_UNOP_I(RTOI, WOORT_IR_INSTR_RTOI)
DEFINE_UNOP_I(ITOS, WOORT_IR_INSTR_ITOS)
DEFINE_UNOP_I(STOI, WOORT_IR_INSTR_STOI)
DEFINE_UNOP_I(STOR, WOORT_IR_INSTR_STOR)
DEFINE_UNOP_I(RTOS, WOORT_IR_INSTR_RTOS)

/*
 * ============================================================
 * 容器构造
 * ============================================================
 */

#define DEFINE_MKCONTAINER(name, kind) \
    WOORT_NODISCARD const woort_IRValue* woort_IRBlock_##name(woort_IRBlock* block, uint32_t n) \
    { \
        assert(block != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return NULL; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_mkcontainer.m_count = n; \
        instr->m_result = _woort_ir_block_create_result_value(block, instr); \
        \
        return instr->m_result; \
    }

DEFINE_MKCONTAINER(MKVEC, WOORT_IR_INSTR_MKVEC)
DEFINE_MKCONTAINER(MKMAP, WOORT_IR_INSTR_MKMAP)
DEFINE_MKCONTAINER(MKSTRUCT, WOORT_IR_INSTR_MKSTRUCT)

/*
 * ============================================================
 * 索引加载
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXVEC(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx)
{
    assert(block != NULL);
    assert(vec != NULL);
    assert(idx != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_LDIDXVEC;
    instr->m_op.m_ldidx.m_container = vec;
    instr->m_op.m_ldidx.m_idx = idx;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXVECX(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx)
{
    assert(block != NULL);
    assert(vec != NULL);
    assert(idx != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_LDIDXVECX;
    instr->m_op.m_ldidx.m_container = vec;
    instr->m_op.m_ldidx.m_idx = idx;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDSTRUCT(woort_IRBlock* block, const woort_IRValue* struct_val, uint32_t field_idx)
{
    assert(block != NULL);
    assert(struct_val != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_LDIDSTRUCT;
    instr->m_op.m_ldidstruct.m_container = struct_val;
    instr->m_op.m_ldidstruct.m_field_idx = field_idx;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDSTRING(woort_IRBlock* block, const woort_IRValue* str, const woort_IRValue* idx)
{
    assert(block != NULL);
    assert(str != NULL);
    assert(idx != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_LDIDSTRING;
    instr->m_op.m_ldidx.m_container = str;
    instr->m_op.m_ldidx.m_idx = idx;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

#define DEFINE_LDIDXDICT(name, kind) \
    WOORT_NODISCARD const woort_IRValue* woort_IRBlock_##name(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key) \
    { \
        assert(block != NULL); \
        assert(dict != NULL); \
        assert(key != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return NULL; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_ldidxdict.m_dict = dict; \
        instr->m_op.m_ldidxdict.m_key = key; \
        instr->m_result = _woort_ir_block_create_result_value(block, instr); \
        \
        return instr->m_result; \
    }

DEFINE_LDIDXDICT(LDIDXDICT_I, WOORT_IR_INSTR_LDIDXDICT_I)
DEFINE_LDIDXDICT(LDIDXDICT_R, WOORT_IR_INSTR_LDIDXDICT_R)
DEFINE_LDIDXDICT(LDIDXDICT_B, WOORT_IR_INSTR_LDIDXDICT_B)
DEFINE_LDIDXDICT(LDIDXDICT_X, WOORT_IR_INSTR_LDIDXDICT_X)

/*
 * ============================================================
 * 索引存储 - 向量
 * ============================================================
 */

#define DEFINE_STIDXVEC(name, kind) \
    void woort_IRBlock_##name(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx, const woort_IRValue* val) \
    { \
        assert(block != NULL); \
        assert(vec != NULL); \
        assert(idx != NULL); \
        assert(val != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_stidx.m_container = vec; \
        instr->m_op.m_stidx.m_idx = idx; \
        instr->m_op.m_stidx.m_val = val; \
        instr->m_result = NULL; \
    }

DEFINE_STIDXVEC(STIDXVEC_I, WOORT_IR_INSTR_STIDXVEC_I)
DEFINE_STIDXVEC(STIDXVEC_R, WOORT_IR_INSTR_STIDXVEC_R)
DEFINE_STIDXVEC(STIDXVEC_B, WOORT_IR_INSTR_STIDXVEC_B)
DEFINE_STIDXVEC(STIDXVEC_X, WOORT_IR_INSTR_STIDXVEC_X)

/*
 * ============================================================
 * 索引存储 - 结构体
 * ============================================================
 */

void woort_IRBlock_STIDSTRUCT(woort_IRBlock* block, const woort_IRValue* struct_val, uint32_t field_idx, const woort_IRValue* val)
{
    assert(block != NULL);
    assert(struct_val != NULL);
    assert(val != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return;
    }

    instr->m_kind = WOORT_IR_INSTR_STIDSTRUCT;
    instr->m_op.m_stidstruct.m_container = struct_val;
    instr->m_op.m_stidstruct.m_field_idx = field_idx;
    instr->m_op.m_stidstruct.m_val = val;
    instr->m_result = NULL;
}

/*
 * ============================================================
 * 索引存储 - 字典
 * ============================================================
 */

#define DEFINE_STIDXDICT(name, kind) \
    void woort_IRBlock_##name(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val) \
    { \
        assert(block != NULL); \
        assert(dict != NULL); \
        assert(key != NULL); \
        assert(val != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_stidxdict.m_dict = dict; \
        instr->m_op.m_stidxdict.m_key = key; \
        instr->m_op.m_stidxdict.m_val = val; \
        instr->m_result = NULL; \
    }

DEFINE_STIDXDICT(STIDXDICT_II, WOORT_IR_INSTR_STIDXDICT_II)
DEFINE_STIDXDICT(STIDXDICT_IR, WOORT_IR_INSTR_STIDXDICT_IR)
DEFINE_STIDXDICT(STIDXDICT_IB, WOORT_IR_INSTR_STIDXDICT_IB)
DEFINE_STIDXDICT(STIDXDICT_IX, WOORT_IR_INSTR_STIDXDICT_IX)
DEFINE_STIDXDICT(STIDXDICT_RI, WOORT_IR_INSTR_STIDXDICT_RI)
DEFINE_STIDXDICT(STIDXDICT_RR, WOORT_IR_INSTR_STIDXDICT_RR)
DEFINE_STIDXDICT(STIDXDICT_RB, WOORT_IR_INSTR_STIDXDICT_RB)
DEFINE_STIDXDICT(STIDXDICT_RX, WOORT_IR_INSTR_STIDXDICT_RX)
DEFINE_STIDXDICT(STIDXDICT_BI, WOORT_IR_INSTR_STIDXDICT_BI)
DEFINE_STIDXDICT(STIDXDICT_BR, WOORT_IR_INSTR_STIDXDICT_BR)
DEFINE_STIDXDICT(STIDXDICT_BB, WOORT_IR_INSTR_STIDXDICT_BB)
DEFINE_STIDXDICT(STIDXDICT_BX, WOORT_IR_INSTR_STIDXDICT_BX)
DEFINE_STIDXDICT(STIDXDICT_XI, WOORT_IR_INSTR_STIDXDICT_XI)
DEFINE_STIDXDICT(STIDXDICT_XR, WOORT_IR_INSTR_STIDXDICT_XR)
DEFINE_STIDXDICT(STIDXDICT_XB, WOORT_IR_INSTR_STIDXDICT_XB)
DEFINE_STIDXDICT(STIDXDICT_XX, WOORT_IR_INSTR_STIDXDICT_XX)

/*
 * ============================================================
 * 闭包
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKCLOSURE(woort_IRBlock* block, woort_IRGlobalIndex func_idx, uint32_t capture_count)
{
    assert(block != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return NULL;
    }

    instr->m_kind = WOORT_IR_INSTR_MKCLOSURE;
    instr->m_op.m_mkclosure.m_func_idx = func_idx;
    instr->m_op.m_mkclosure.m_capture_count = capture_count;
    instr->m_result = _woort_ir_block_create_result_value(block, instr);

    return instr->m_result;
}

/*
 * ============================================================
 * 函数调用
 * ============================================================
 */

void woort_IRBlock_PUSH(woort_IRBlock* block, const woort_IRValue* val)
{
    assert(block != NULL);
    assert(val != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return;
    }

    instr->m_kind = WOORT_IR_INSTR_PUSH;
    instr->m_op.m_push.m_val = val;
    instr->m_result = NULL;
}

#define DEFINE_CALL_IMM(name, kind) \
    WOORT_NODISCARD bool woort_IRBlock_##name( \
        woort_IRBlock* block, \
        woort_IRGlobalIndex func_idx, \
        uint32_t argc, \
        const woort_IRValue** out_result) \
    { \
        assert(block != NULL); \
        \
        woort_IRInstr* instr = _woort_ir_block_append_instr(block); \
        if (instr == NULL) \
        { \
            return false; \
        } \
        \
        instr->m_kind = kind; \
        instr->m_op.m_call_imm.m_func_idx = func_idx; \
        instr->m_op.m_call_imm.m_argc = argc; \
        \
        if (out_result != NULL) \
        { \
            instr->m_result = _woort_ir_block_create_result_value(block, instr); \
            *out_result = instr->m_result; \
        } \
        else \
        { \
            instr->m_result = NULL; \
        } \
        \
        return true; \
    }

DEFINE_CALL_IMM(CALLNWO, WOORT_IR_INSTR_CALLNWO)
DEFINE_CALL_IMM(CALLNFP, WOORT_IR_INSTR_CALLNFP)
DEFINE_CALL_IMM(CALLNJIT, WOORT_IR_INSTR_CALLNJIT)

WOORT_NODISCARD bool woort_IRBlock_CALL(
    woort_IRBlock* block,
    const woort_IRValue* func,
    uint32_t argc,
    const woort_IRValue** out_result)
{
    assert(block != NULL);
    assert(func != NULL);

    woort_IRInstr* instr = _woort_ir_block_append_instr(block);
    if (instr == NULL)
    {
        return false;
    }

    instr->m_kind = WOORT_IR_INSTR_CALL;
    instr->m_op.m_call.m_func = func;
    instr->m_op.m_call.m_argc = argc;

    if (out_result != NULL)
    {
        instr->m_result = _woort_ir_block_create_result_value(block, instr);
        *out_result = instr->m_result;
    }
    else
    {
        instr->m_result = NULL;
    }

    return true;
}

/*
 * ============================================================
 * 终止指令
 * ============================================================
 */

void woort_IRBlock_br(woort_IRBlock* block, woort_IRBlock* target)
{
    assert(block != NULL);
    assert(target != NULL);
    assert(!block->m_has_terminator);

    block->m_terminator.m_kind = WOORT_IR_INSTR_BR;
    block->m_terminator.m_op.m_br.m_target = target;
    block->m_has_terminator = true;

    _woort_ir_block_add_successor(block, target);
    _woort_ir_block_add_predecessor(target, block);
}

#define DEFINE_BR_CMP(name, kind) \
    void woort_IRBlock_##name( \
        woort_IRBlock* block, \
        const woort_IRValue* a, \
        const woort_IRValue* b, \
        woort_IRBlock* true_block, \
        woort_IRBlock* false_block) \
    { \
        assert(block != NULL); \
        assert(a != NULL); \
        assert(b != NULL); \
        assert(true_block != NULL); \
        assert(false_block != NULL); \
        assert(!block->m_has_terminator); \
        \
        block->m_terminator.m_kind = kind; \
        block->m_terminator.m_op.m_br_cmp.m_a = a; \
        block->m_terminator.m_op.m_br_cmp.m_b = b; \
        block->m_terminator.m_op.m_br_cmp.m_true_block = true_block; \
        block->m_terminator.m_op.m_br_cmp.m_false_block = false_block; \
        block->m_has_terminator = true; \
        \
        _woort_ir_block_add_successor(block, true_block); \
        _woort_ir_block_add_successor(block, false_block); \
        _woort_ir_block_add_predecessor(true_block, block); \
        _woort_ir_block_add_predecessor(false_block, block); \
    }

DEFINE_BR_CMP(br_lt, WOORT_IR_INSTR_BR_LT)
DEFINE_BR_CMP(br_le, WOORT_IR_INSTR_BR_LE)
DEFINE_BR_CMP(br_gt, WOORT_IR_INSTR_BR_GT)
DEFINE_BR_CMP(br_ge, WOORT_IR_INSTR_BR_GE)
DEFINE_BR_CMP(br_eq, WOORT_IR_INSTR_BR_EQ)
DEFINE_BR_CMP(br_ne, WOORT_IR_INSTR_BR_NE)

void woort_IRBlock_br_cond(
    woort_IRBlock* block,
    const woort_IRValue* cond,
    woort_IRBlock* true_block,
    woort_IRBlock* false_block)
{
    assert(block != NULL);
    assert(cond != NULL);
    assert(true_block != NULL);
    assert(false_block != NULL);
    assert(!block->m_has_terminator);

    block->m_terminator.m_kind = WOORT_IR_INSTR_BR_COND;
    block->m_terminator.m_op.m_br_cond.m_cond = cond;
    block->m_terminator.m_op.m_br_cond.m_true_block = true_block;
    block->m_terminator.m_op.m_br_cond.m_false_block = false_block;
    block->m_has_terminator = true;

    _woort_ir_block_add_successor(block, true_block);
    _woort_ir_block_add_successor(block, false_block);
    _woort_ir_block_add_predecessor(true_block, block);
    _woort_ir_block_add_predecessor(false_block, block);
}

void woort_IRBlock_ret(woort_IRBlock* block, const woort_IRValue* val)
{
    assert(block != NULL);
    assert(val != NULL);
    assert(!block->m_has_terminator);

    block->m_terminator.m_kind = WOORT_IR_INSTR_RET;
    block->m_terminator.m_op.m_ret.m_val = val;
    block->m_has_terminator = true;
}

void woort_IRBlock_ret_void(woort_IRBlock* block)
{
    assert(block != NULL);
    assert(!block->m_has_terminator);

    block->m_terminator.m_kind = WOORT_IR_INSTR_RET_VOID;
    block->m_has_terminator = true;
}
