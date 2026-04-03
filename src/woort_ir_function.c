/*
 * woort_ir_function.c
 *
 * IR 函数分析引擎实现。
 * 包含：虚拟寄存器/Label 分配、基本块切分、CFG 构建、
 *       活跃性分析、栈槽分配（线性扫描）、支配树 + 常量加载放置。
 */

#include "woort.h"

#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_ir_srcloc.h"
#include "woort_vector.h"
#include "woort_linklist.h"
#include "woort_bitset.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ========== 私有类型 ========== */

/*
 * 常量加载放置信息（放置在 block 的 m_const_loads 中）
 */
typedef struct _woort_ConstLoadInfo
{
    woort_IRConstantIndex m_const_index;
    int32_t m_stack_offset; /* 目标栈槽偏移 */
} _woort_ConstLoadInfo;

/*
 * 线性扫描用的活跃区间
 */
typedef struct _woort_LiveInterval
{
    uint32_t m_vreg_id;
    uint32_t m_start;          /* 第一次定义/使用的指令索引 */
    uint32_t m_end;            /* 最后一次使用的指令索引 */
    int32_t  m_assigned_slot;  /* 分配到的栈槽编号 */
} _woort_LiveInterval;

/* ========================================================================
 * 辅助函数：指令分类
 * ======================================================================== */

static bool _is_unconditional_jump(woort_IROp_Kind kind)
{
    return kind == WOORT_IROP_KIND_JMP;
}

static bool _is_conditional_jump(woort_IROp_Kind kind)
{
    switch (kind)
    {
    case WOORT_IROP_KIND_JCC:
    case WOORT_IROP_KIND_JCCZ:
    case WOORT_IROP_KIND_JCC_LT:
    case WOORT_IROP_KIND_JCC_LE:
    case WOORT_IROP_KIND_JCC_EQ:
    case WOORT_IROP_KIND_JCC_GT:
    case WOORT_IROP_KIND_JCC_GE:
    case WOORT_IROP_KIND_JCC_NE:
        return true;
    default:
        return false;
    }
}

static bool _is_jump_op(woort_IROp_Kind kind)
{
    return _is_unconditional_jump(kind) || _is_conditional_jump(kind);
}

static bool _is_return_op(woort_IROp_Kind kind)
{
    return kind == WOORT_IROP_KIND_RET || kind == WOORT_IROP_KIND_RET_VOID;
}

static bool _is_terminator(woort_IROp_Kind kind)
{
    return _is_jump_op(kind) || _is_return_op(kind);
}

/* ========================================================================
 * 辅助函数：CFG 边
 * ======================================================================== */

static bool _add_cfg_edge(
    woort_IRBlock* from_block, uint32_t from_idx,
    woort_IRBlock* to_block,   uint32_t to_idx)
{
    if (!woort_vector_push_back(&from_block->m_successors, 1, &to_idx))
        return false;
    if (!woort_vector_push_back(&to_block->m_predecessors, 1, &from_idx))
        return false;
    return true;
}

/* ========================================================================
 * 辅助函数：Bitset 操作
 * ======================================================================== */

/*
 * dst |= src ，返回是否有位发生改变
 */
static bool _bitset_union_into(woort_Bitset* dst, const woort_Bitset* src)
{
    assert(dst->m_word_count == src->m_word_count);
    bool changed = false;
    for (size_t i = 0; i < dst->m_word_count; ++i)
    {
        uint64_t old_val = dst->m_data[i];
        dst->m_data[i] |= src->m_data[i];
        if (dst->m_data[i] != old_val)
            changed = true;
    }
    return changed;
}

/*
 * dst = use | (out & ~def)
 * 返回 dst 是否与原来不同
 */
static bool _bitset_assign_live_in(
    woort_Bitset* dst,
    const woort_Bitset* use_set,
    const woort_Bitset* out_set,
    const woort_Bitset* def_set)
{
    assert(dst->m_word_count == use_set->m_word_count);
    assert(dst->m_word_count == out_set->m_word_count);
    assert(dst->m_word_count == def_set->m_word_count);

    bool changed = false;
    for (size_t i = 0; i < dst->m_word_count; ++i)
    {
        uint64_t new_val = use_set->m_data[i] | (out_set->m_data[i] & ~def_set->m_data[i]);
        if (dst->m_data[i] != new_val)
        {
            dst->m_data[i] = new_val;
            changed = true;
        }
    }
    return changed;
}

/* ========================================================================
 * 辅助函数：qsort 比较
 * ======================================================================== */

static int _compare_intervals_by_start(const void* a, const void* b)
{
    const _woort_LiveInterval* ia = (const _woort_LiveInterval*)a;
    const _woort_LiveInterval* ib = (const _woort_LiveInterval*)b;
    if (ia->m_start < ib->m_start) return -1;
    if (ia->m_start > ib->m_start) return 1;
    return 0;
}

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

void woort_IRFunction_init(woort_IRFunction* f, uint32_t param_count)
{
    f->m_param_count = param_count;

    woort_linklist_init(&f->m_ir_values, sizeof(woort_IRValue));
    f->m_next_vreg_id = 0;

    woort_linklist_init(&f->m_ir_labels, sizeof(woort_IRLabel));
    f->m_next_label_id = 0;

    woort_vector_init(&f->m_instructions, sizeof(woort_IROp));
    woort_vector_init(&f->m_blocks, sizeof(woort_IRBlock));

    f->m_code_offset = 0;
    f->m_code_length = 0;

    /* 源码位置支持 */
    woort_SourceLocationStack_init(&f->m_srcloc_stack);
    woort_vector_init(&f->m_source_locations, sizeof(woort_SourceLocation));
}

void woort_IRFunction_deinit(woort_IRFunction* f)
{
    /* 销毁所有 block */
    for (size_t i = 0; i < f->m_blocks.m_size; ++i)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, i);
        _woort_IRBlock_deinit(blk);
    }

    woort_linklist_deinit(&f->m_ir_values);
    woort_linklist_deinit(&f->m_ir_labels);
    woort_vector_deinit(&f->m_instructions);
    woort_vector_deinit(&f->m_blocks);

    /* 源码位置支持 */
    woort_SourceLocationStack_deinit(&f->m_srcloc_stack);
    woort_vector_deinit(&f->m_source_locations);
}

/* ========================================================================
 * 源码位置 API
 * ======================================================================== */

WOORT_NODISCARD bool woort_IRFunction_push_srcloc(
    woort_IRFunction* f,
    /* OPTIONAL */ const char* filepath,
    uint32_t begin_line,
    uint32_t begin_column,
    uint32_t end_line,
    uint32_t end_column)
{
    woort_SourceLocation loc;
    loc.m_filepath = filepath;
    loc.m_begin_line = begin_line;
    loc.m_begin_column = begin_column;
    loc.m_end_line = end_line;
    loc.m_end_column = end_column;

    return woort_SourceLocationStack_push(&f->m_srcloc_stack, &loc);
}

void woort_IRFunction_pop_srcloc(woort_IRFunction* f)
{
    woort_SourceLocationStack_pop(&f->m_srcloc_stack);
}

WOORT_NODISCARD uint32_t _woort_IRFunction_current_srcloc_index(
    woort_IRFunction* f)
{
    const woort_SourceLocation* top =
        woort_SourceLocationStack_top(&f->m_srcloc_stack);

    if (top == NULL)
        return WOORT_SRCLOC_INVALID_INDEX;

    /*
     * 在 m_source_locations 中去重查找。
     * 通常源码位置数量不多，线性扫描即可。
     */
    for (size_t i = 0; i < f->m_source_locations.m_size; ++i)
    {
        const woort_SourceLocation* existing =
            (const woort_SourceLocation*)woort_vector_at(
                &f->m_source_locations, i);

        if (woort_SourceLocation_equal(top, existing))
            return (uint32_t)i;
    }

    /* 未找到，追加新条目 */
    if (!woort_vector_push_back(&f->m_source_locations, 1, top))
        return WOORT_SRCLOC_INVALID_INDEX; /* OOM: 降级为无源码信息 */

    return (uint32_t)(f->m_source_locations.m_size - 1);
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_new_vreg(
    woort_IRFunction* f)
{
    woort_IRValue* v;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&v))
        return NULL;

    woort_IRValue_init_vreg(v, f->m_next_vreg_id);
    f->m_next_vreg_id++;
    return v;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f, uint32_t param_idx)
{
    assert(param_idx < f->m_param_count);

    woort_IRValue* v;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&v))
        return NULL;

    woort_IRValue_init_argument(v, f->m_next_vreg_id, param_idx);
    f->m_next_vreg_id++;
    return v;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRLabel* woort_IRFunction_new_label(
    woort_IRFunction* f)
{
    woort_IRLabel* label;
    if (!woort_linklist_emplace_back(&f->m_ir_labels, (void**)&label))
        return NULL;

    label->m_id = f->m_next_label_id;
    f->m_next_label_id++;
    label->m_bound = false;
    label->m_bind_index = 0;
    label->m_block_index = 0;
    return label;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* f, woort_IRConstantIndex idx)
{
    /*
     * 查找已有的 CONST vreg（同一 const_index 返回同一 IRValue*）
     */
    for (woort_IRValue* v = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
         v != NULL;
         v = (woort_IRValue*)woort_linklist_next(v))
    {
        if (v->m_source == WOORT_IRVALUE_SOURCE_CONST && v->m_const_idx == idx)
            return v;
    }

    /* 首次请求该 const_index，创建新的 CONST vreg */
    woort_IRValue* v;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&v))
        return NULL;

    woort_IRValue_init_const(v, f->m_next_vreg_id, idx);
    f->m_next_vreg_id++;
    return v;
}

/* ===================================================================
 * Phase 1: Label → 基本块切分 + CFG 构建
 * =================================================================== */

static bool _phase1_split_blocks_and_build_cfg(woort_IRFunction* f)
{
    const size_t instr_count = f->m_instructions.m_size;
    if (instr_count == 0)
        return true;

    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    /*
     * 第一趟：标记 block 起始点（leader）
     */
    bool* is_leader = (bool*)calloc(instr_count, sizeof(bool));
    if (is_leader == NULL)
        return false;

    /* 第一条指令始终是 leader */
    is_leader[0] = true;

    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* op = &instrs[i];

        /* Label 绑定点是 leader */
        if (op->m_op == WOORT_IROP_KIND_LABEL)
        {
            is_leader[i] = true;
        }

        /* 终结符之后的下一条指令也是 leader */
        if (_is_terminator(op->m_op))
        {
            if (i + 1 < instr_count)
                is_leader[i + 1] = true;
        }

        /* 跳转目标也是 leader（经由 label 的 bind_index） */
        if (_is_jump_op(op->m_op))
        {
            woort_IRLabel* target = op->m_jump_target;
            assert(target != NULL);
            assert(target->m_bound);
            assert((size_t)target->m_bind_index < instr_count);
            is_leader[target->m_bind_index] = true;
        }
    }

    /*
     * 第二趟：计算 block 数量，创建 block，建立 instr→block 映射
     */
    uint32_t block_count = 0;
    for (size_t i = 0; i < instr_count; ++i)
    {
        if (is_leader[i])
            block_count++;
    }

    /* 预分配 blocks vector */
    if (!woort_vector_resize(&f->m_blocks, block_count))
    {
        free(is_leader);
        return false;
    }

    /* instruction → block index 映射 */
    uint32_t* instr_to_block = (uint32_t*)malloc(instr_count * sizeof(uint32_t));
    if (instr_to_block == NULL)
    {
        free(is_leader);
        return false;
    }

    /* 初始化每个 block，设置 m_begin，同时填充 instr_to_block */
    {
        uint32_t blk_idx = 0;
        for (size_t i = 0; i < instr_count; ++i)
        {
            if (is_leader[i])
            {
                if (blk_idx > 0)
                {
                    /* 填充前一个 block 的 m_end */
                    woort_IRBlock* prev_blk = (woort_IRBlock*)woort_vector_at(
                        &f->m_blocks, blk_idx - 1);
                    prev_blk->m_end = (uint32_t)i;
                }

                woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, blk_idx);
                _woort_IRBlock_init(blk);
                blk->m_begin = (uint32_t)i;
                blk_idx++;
            }
            /* 当前指令的 block index = (blk_idx - 1) */
            instr_to_block[i] = blk_idx - 1;
        }
        assert(blk_idx == block_count);

        /* 最后一个 block 的 m_end */
        {
            woort_IRBlock* last_blk = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, block_count - 1);
            last_blk->m_end = (uint32_t)instr_count;
        }
    }

    free(is_leader);

    /* 为每个 label 设置 m_block_index */
    for (woort_IRLabel* label = (woort_IRLabel*)woort_linklist_iter(&f->m_ir_labels);
         label != NULL;
         label = (woort_IRLabel*)woort_linklist_next(label))
    {
        if (label->m_bound)
        {
            assert(label->m_bind_index < (uint32_t)instr_count);
            label->m_block_index = instr_to_block[label->m_bind_index];
        }
    }

    /*
     * 第三趟：构建 CFG 边
     */
    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);
        assert(blk->m_end > blk->m_begin);

        /* 最后一条指令 */
        uint32_t last_idx = blk->m_end - 1;
        woort_IROp* last_op = &instrs[last_idx];

        if (_is_unconditional_jump(last_op->m_op))
        {
            /* JMP: 仅目标边 */
            uint32_t target_blk = last_op->m_jump_target->m_block_index;
            woort_IRBlock* target_block = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, target_blk);
            if (!_add_cfg_edge(blk, b, target_block, target_blk))
            {
                free(instr_to_block);
                return false;
            }
        }
        else if (_is_conditional_jump(last_op->m_op))
        {
            /* 条件跳转: 目标边 + fallthrough 边 */
            uint32_t target_blk = last_op->m_jump_target->m_block_index;
            woort_IRBlock* target_block = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, target_blk);
            if (!_add_cfg_edge(blk, b, target_block, target_blk))
            {
                free(instr_to_block);
                return false;
            }

            if (b + 1 < block_count)
            {
                woort_IRBlock* next_block = (woort_IRBlock*)woort_vector_at(
                    &f->m_blocks, b + 1);
                if (!_add_cfg_edge(blk, b, next_block, b + 1))
                {
                    free(instr_to_block);
                    return false;
                }
            }
        }
        else if (_is_return_op(last_op->m_op))
        {
            /* RET / RET_VOID: 无后继 */
        }
        else
        {
            /* 无终结符，fallthrough */
            if (b + 1 < block_count)
            {
                woort_IRBlock* next_block = (woort_IRBlock*)woort_vector_at(
                    &f->m_blocks, b + 1);
                if (!_add_cfg_edge(blk, b, next_block, b + 1))
                {
                    free(instr_to_block);
                    return false;
                }
            }
        }
    }

    free(instr_to_block);
    return true;
}

/* ===================================================================
 * Phase 2: 活跃性分析
 * =================================================================== */

static void _record_use(
    woort_Bitset* use_set,
    const woort_Bitset* def_set,
    /* OPTIONAL */ const woort_IRValue* val)
{
    if (val == NULL)
        return;
    /* CONST 源的 vreg 不参与 bitset 活跃性分析 */
    if (val->m_source == WOORT_IRVALUE_SOURCE_CONST)
        return;
    /* 如果还没被 DEF 过，则加入 USE */
    if (!woort_bitset_test(def_set, val->m_id))
        (void)woort_bitset_set(use_set, val->m_id);
}

static void _record_def(
    woort_Bitset* def_set,
    /* OPTIONAL */ woort_IRValue* val)
{
    if (val == NULL)
        return;
    (void)woort_bitset_set(def_set, val->m_id);
}

static bool _phase2_liveness_analysis(woort_IRFunction* f)
{
    const uint32_t vreg_count = f->m_next_vreg_id;
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    if (vreg_count == 0 || block_count == 0)
        return true;

    /* 为每个 block 初始化活跃性 bitset */
    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);

        if (!woort_bitset_init(&blk->m_use, vreg_count))
            return false;
        if (!woort_bitset_init(&blk->m_def, vreg_count))
            return false;
        if (!woort_bitset_init(&blk->m_live_in, vreg_count))
            return false;
        if (!woort_bitset_init(&blk->m_live_out, vreg_count))
            return false;
    }

    /* 计算每个 block 的 USE 和 DEF 集合 */
    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);

        for (uint32_t i = blk->m_begin; i < blk->m_end; ++i)
        {
            woort_IROp* op = &instrs[i];

            /* 跳过 LABEL 伪指令，不产生 USE/DEF */
            if (op->m_op == WOORT_IROP_KIND_LABEL)
                continue;

            /* 先处理读操作数 (USE) */
            _record_use(&blk->m_use, &blk->m_def, op->m_src[0]);
            _record_use(&blk->m_use, &blk->m_def, op->m_src[1]);
            _record_use(&blk->m_use, &blk->m_def, op->m_src[2]);

            /* 再处理写目标 (DEF) */
            _record_def(&blk->m_def, op->m_dst);
        }
    }

    /* 迭代数据流不动点 */
    {
        bool changed = true;
        while (changed)
        {
            changed = false;

            /* 逆序遍历 block（从最后一个到第一个） */
            for (uint32_t bi = block_count; bi > 0; --bi)
            {
                uint32_t b = bi - 1;
                woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);

                /* LIVE_OUT(B) = union of LIVE_IN(S) for all successors S */
                for (size_t si = 0; si < blk->m_successors.m_size; ++si)
                {
                    uint32_t succ_idx = *(uint32_t*)woort_vector_at(
                        &blk->m_successors, si);
                    woort_IRBlock* succ = (woort_IRBlock*)woort_vector_at(
                        &f->m_blocks, succ_idx);
                    if (_bitset_union_into(&blk->m_live_out, &succ->m_live_in))
                        changed = true;
                }

                /* LIVE_IN(B) = USE(B) | (LIVE_OUT(B) & ~DEF(B)) */
                if (_bitset_assign_live_in(
                        &blk->m_live_in,
                        &blk->m_use,
                        &blk->m_live_out,
                        &blk->m_def))
                    changed = true;
            }
        }
    }

    return true;
}

/* ===================================================================
 * Phase 2b: 常量直接使用标记
 *
 * 扫描所有 SOURCE_CONST vreg，如果某个 CONST vreg 仅被一条
 * PUSHCHK 或 RET 使用（use_count == 1），标记为 const_direct。
 * 发射层将直接使用 PUSHCCHK / RETVC 而非 LOAD + PUSHSCHK / RETVS。
 *
 * 注意：CONST vreg 不参与指令流中的 DEF（没有 LOAD_CONST 指令），
 * 它们只作为其他指令的 m_src[] 出现。
 * =================================================================== */

static bool _phase2b_const_optimization(woort_IRFunction* f)
{
    const size_t instr_count = f->m_instructions.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    /*
     * 对每个 CONST vreg 统计 use_count 并查找唯一使用者
     */
    for (woort_IRValue* cv = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
         cv != NULL;
         cv = (woort_IRValue*)woort_linklist_next(cv))
    {
        if (cv->m_source != WOORT_IRVALUE_SOURCE_CONST)
            continue;

        /* 统计该 CONST vreg 在指令流中被作为 m_src[] 引用的次数 */
        uint32_t use_count = 0;
        woort_IROp* unique_user = NULL;

        for (size_t i = 0; i < instr_count; ++i)
        {
            woort_IROp* op = &instrs[i];
            for (int s = 0; s < 3; ++s)
            {
                if (op->m_src[s] == cv)
                {
                    use_count++;
                    unique_user = op;
                    break; /* 同一指令的多个 src 引用同一 vreg 只计一次 */
                }
            }
        }

        if (use_count != 1 || unique_user == NULL)
            continue;

        /* 检查唯一使用者是否是 PUSHCHK 或 RET */
        if (unique_user->m_op != WOORT_IROP_KIND_PUSHCHK &&
            unique_user->m_op != WOORT_IROP_KIND_RET)
        {
            continue;
        }

        /* RETVC 没有扩展编码，const_index 超 U24 范围时不可用 */
        if (unique_user->m_op == WOORT_IROP_KIND_RET &&
            cv->m_const_idx > ((1u << 24) - 1))
        {
            continue;
        }

        cv->m_is_const_direct = true;
    }

    return true;
}

/* ===================================================================
 * Phase 3: 栈槽分配（线性扫描）
 * =================================================================== */

static bool _phase3_stack_allocation(
    woort_IRFunction* f,
    size_t* out_stack_space)
{
    const uint32_t vreg_count = f->m_next_vreg_id;
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    const size_t instr_count = f->m_instructions.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    if (vreg_count == 0)
    {
        *out_stack_space = 0;
        return true;
    }

    /*
     * 建立 vreg_by_id 数组：按 m_id 索引快速查找 vreg 指针
     */
    woort_IRValue** vreg_by_id = (woort_IRValue**)calloc(vreg_count, sizeof(woort_IRValue*));
    if (vreg_by_id == NULL)
        return false;

    for (woort_IRValue* v = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
         v != NULL;
         v = (woort_IRValue*)woort_linklist_next(v))
    {
        assert(v->m_id < vreg_count);
        vreg_by_id[v->m_id] = v;
    }

    /*
     * 计算每个 vreg 的活跃区间 [first_def, last_use]
     */
    uint32_t* first_point = (uint32_t*)malloc(vreg_count * sizeof(uint32_t));
    uint32_t* last_point  = (uint32_t*)malloc(vreg_count * sizeof(uint32_t));
    if (first_point == NULL || last_point == NULL)
    {
        free(first_point);
        free(last_point);
        free(vreg_by_id);
        return false;
    }

    for (uint32_t i = 0; i < vreg_count; ++i)
    {
        first_point[i] = UINT32_MAX;
        last_point[i]  = 0;
    }

    /* 扫描所有指令收集 def/use 点 */
    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* op = &instrs[i];

        /* m_dst 是定义点 */
        if (op->m_dst != NULL)
        {
            uint32_t id = op->m_dst->m_id;
            if ((uint32_t)i < first_point[id])
                first_point[id] = (uint32_t)i;
            if ((uint32_t)i > last_point[id])
                last_point[id] = (uint32_t)i;
        }

        /* m_src[] 是使用点 */
        for (int s = 0; s < 3; ++s)
        {
            if (op->m_src[s] != NULL)
            {
                uint32_t id = op->m_src[s]->m_id;
                if ((uint32_t)i < first_point[id])
                    first_point[id] = (uint32_t)i;
                if ((uint32_t)i > last_point[id])
                    last_point[id] = (uint32_t)i;
            }
        }
    }

    /* 根据活跃性信息扩展区间以覆盖跨块活跃 */
    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);

        for (uint32_t id = 0; id < vreg_count; ++id)
        {
            /* 如果 vreg 在 block 出口活跃，区间需覆盖到 block 末尾 */
            if (woort_bitset_test(&blk->m_live_out, id))
            {
                if (blk->m_end > 0 && (blk->m_end - 1) > last_point[id])
                    last_point[id] = blk->m_end - 1;
                if (first_point[id] == UINT32_MAX)
                    first_point[id] = blk->m_begin;
            }
            /* 如果 vreg 在 block 入口活跃，区间需覆盖到 block 起始 */
            if (woort_bitset_test(&blk->m_live_in, id))
            {
                if (blk->m_begin < first_point[id])
                    first_point[id] = blk->m_begin;
            }
        }
    }

    /*
     * 构建需要分配的活跃区间列表（排除参数、常量直连和未使用的 vreg）
     */
    uint32_t interval_count = 0;
    for (uint32_t id = 0; id < vreg_count; ++id)
    {
        woort_IRValue* v = vreg_by_id[id];
        if (v == NULL)
            continue;
        if (v->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT)
            continue; /* 参数已有预分配的栈偏移 SB+3+idx */
        if (v->m_is_const_direct)
            continue; /* 常量直连不需要栈槽 */
        if (first_point[id] == UINT32_MAX)
            continue; /* 从未出现 */
        interval_count++;
    }

    _woort_LiveInterval* intervals = NULL;
    if (interval_count > 0)
    {
        intervals = (_woort_LiveInterval*)malloc(
            interval_count * sizeof(_woort_LiveInterval));
        if (intervals == NULL)
        {
            free(first_point);
            free(last_point);
            free(vreg_by_id);
            return false;
        }

        uint32_t idx = 0;
        for (uint32_t id = 0; id < vreg_count; ++id)
        {
            woort_IRValue* v = vreg_by_id[id];
            if (v == NULL)
                continue;
            if (v->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT)
                continue;
            if (v->m_is_const_direct)
                continue;
            if (first_point[id] == UINT32_MAX)
                continue;

            intervals[idx].m_vreg_id = id;
            intervals[idx].m_start = first_point[id];
            intervals[idx].m_end = last_point[id];
            intervals[idx].m_assigned_slot = -1;
            idx++;
        }
        assert(idx == interval_count);
    }

    free(first_point);
    free(last_point);

    /* 按起始点排序 */
    if (interval_count > 1)
    {
        qsort(intervals, interval_count,
              sizeof(_woort_LiveInterval), _compare_intervals_by_start);
    }

    /*
     * 线性扫描分配栈槽
     */
    size_t max_slots = 0;

    /* active 列表（简单数组实现） */
    _woort_LiveInterval** active_list = NULL;
    size_t active_count = 0;

    /* 空闲栈槽栈 */
    int32_t* free_slot_stack = NULL;
    size_t free_slot_count = 0;

    if (interval_count > 0)
    {
        active_list = (_woort_LiveInterval**)malloc(
            interval_count * sizeof(_woort_LiveInterval*));
        free_slot_stack = (int32_t*)malloc(
            interval_count * sizeof(int32_t));
        if (active_list == NULL || free_slot_stack == NULL)
        {
            free(active_list);
            free(free_slot_stack);
            free(intervals);
            free(vreg_by_id);
            return false;
        }
    }

    for (uint32_t i = 0; i < interval_count; ++i)
    {
        _woort_LiveInterval* cur = &intervals[i];

        /* 过期旧区间：将 end < cur->m_start 的移除并回收栈槽 */
        {
            size_t new_active = 0;
            for (size_t a = 0; a < active_count; ++a)
            {
                if (active_list[a]->m_end < cur->m_start)
                {
                    free_slot_stack[free_slot_count++] = active_list[a]->m_assigned_slot;
                }
                else
                {
                    active_list[new_active++] = active_list[a];
                }
            }
            active_count = new_active;
        }

        /* 分配或复用栈槽 */
        if (free_slot_count > 0)
        {
            cur->m_assigned_slot = free_slot_stack[--free_slot_count];
        }
        else
        {
            cur->m_assigned_slot = (int32_t)max_slots;
            max_slots++;
        }

        /* 插入 active（保持按 end 排序） */
        {
            size_t insert_pos = active_count;
            for (size_t a = 0; a < active_count; ++a)
            {
                if (active_list[a]->m_end > cur->m_end)
                {
                    insert_pos = a;
                    break;
                }
            }
            for (size_t a = active_count; a > insert_pos; --a)
                active_list[a] = active_list[a - 1];
            active_list[insert_pos] = cur;
            active_count++;
        }
    }

    free(active_list);
    free(free_slot_stack);

    /* 将栈槽分配结果写回 vreg */
    for (uint32_t i = 0; i < interval_count; ++i)
    {
        uint32_t id = intervals[i].m_vreg_id;
        int32_t slot = intervals[i].m_assigned_slot;
        woort_IRValue* v = vreg_by_id[id];
        assert(v != NULL);
        /* slot 0 → offset 0, slot 1 → offset -1, slot 2 → offset -2, ... */
        v->m_assigned_stack_offset = -slot;
    }

    free(intervals);
    free(vreg_by_id);

    *out_stack_space = max_slots;
    return true;
}

/* ===================================================================
 * Phase 4a: Dominator tree (Cooper-Harvey-Kennedy)
 * =================================================================== */

static bool _phase4a_build_dominator_tree(woort_IRFunction* f)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    if (block_count == 0)
        return true;

    int32_t*  idom       = (int32_t*)malloc(block_count * sizeof(int32_t));
    uint32_t* rpo_order  = (uint32_t*)malloc(block_count * sizeof(uint32_t));
    uint32_t* rpo_number = (uint32_t*)malloc(block_count * sizeof(uint32_t));
    bool*     visited    = (bool*)calloc(block_count, sizeof(bool));

    if (idom == NULL || rpo_order == NULL || rpo_number == NULL || visited == NULL)
    {
        free(idom);
        free(rpo_order);
        free(rpo_number);
        free(visited);
        return false;
    }

    /* 构建 RPO：DFS 后序再逆序 */
    {
        uint32_t* stack = (uint32_t*)malloc(block_count * sizeof(uint32_t));
        uint32_t* state = (uint32_t*)calloc(block_count, sizeof(uint32_t));
        if (stack == NULL || state == NULL)
        {
            free(stack);
            free(state);
            free(idom);
            free(rpo_order);
            free(rpo_number);
            free(visited);
            return false;
        }

        uint32_t post_count = 0;
        uint32_t stack_top = 0;

        stack[stack_top++] = 0;
        visited[0] = true;

        while (stack_top > 0)
        {
            uint32_t cur = stack[stack_top - 1];
            woort_IRBlock* cur_blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, cur);

            if (state[cur] < cur_blk->m_successors.m_size)
            {
                uint32_t succ = *(uint32_t*)woort_vector_at(
                    &cur_blk->m_successors, state[cur]);
                state[cur]++;

                if (!visited[succ])
                {
                    visited[succ] = true;
                    stack[stack_top++] = succ;
                }
            }
            else
            {
                rpo_order[post_count++] = cur;
                stack_top--;
            }
        }

        free(stack);
        free(state);

        /* 逆转后序 → RPO */
        for (uint32_t i = 0; i < post_count / 2; ++i)
        {
            uint32_t tmp = rpo_order[i];
            rpo_order[i] = rpo_order[post_count - 1 - i];
            rpo_order[post_count - 1 - i] = tmp;
        }

        /* 不可达 block 追加到 RPO 末尾 */
        for (uint32_t b = 0; b < block_count; ++b)
        {
            if (!visited[b])
                rpo_order[post_count++] = b;
        }

        for (uint32_t i = 0; i < block_count; ++i)
            rpo_number[rpo_order[i]] = i;
    }

    free(visited);

    /* 初始化 idom */
    for (uint32_t b = 0; b < block_count; ++b)
        idom[b] = -1;
    idom[0] = 0; /* 入口的 idom 是自身 */

    /* Cooper-Harvey-Kennedy 迭代 */
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (uint32_t ri = 0; ri < block_count; ++ri)
            {
                uint32_t b = rpo_order[ri];
                if (b == 0)
                    continue; /* 跳过入口 */

                woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);
                int32_t new_idom = -1;

                for (size_t pi = 0; pi < blk->m_predecessors.m_size; ++pi)
                {
                    uint32_t pred = *(uint32_t*)woort_vector_at(
                        &blk->m_predecessors, pi);
                    if (idom[pred] == -1)
                        continue;

                    if (new_idom == -1)
                    {
                        new_idom = (int32_t)pred;
                    }
                    else
                    {
                        /* intersect */
                        int32_t f1 = new_idom;
                        int32_t f2 = (int32_t)pred;
                        while (f1 != f2)
                        {
                            while (rpo_number[f1] > rpo_number[f2])
                            {
                                f1 = idom[f1];
                                assert(f1 >= 0);
                            }
                            while (rpo_number[f2] > rpo_number[f1])
                            {
                                f2 = idom[f2];
                                assert(f2 >= 0);
                            }
                        }
                        new_idom = f1;
                    }
                }

                if (new_idom != -1 && idom[b] != new_idom)
                {
                    idom[b] = new_idom;
                    changed = true;
                }
            }
        }
    }

    free(rpo_number);
    free(rpo_order);

    /* 计算 dom_depth 并写入 block */
    {
        uint32_t* depth = (uint32_t*)calloc(block_count, sizeof(uint32_t));
        if (depth == NULL)
        {
            free(idom);
            return false;
        }

        /* 沿 idom 链计算每个 block 的深度 */
        for (uint32_t b = 0; b < block_count; ++b)
        {
            if (idom[b] < 0)
            {
                depth[b] = 0;
                continue;
            }
            uint32_t d = 0;
            int32_t cur = (int32_t)b;
            while (cur != idom[cur] && cur >= 0)
            {
                cur = idom[cur];
                d++;
            }
            depth[b] = d;
        }

        for (uint32_t b = 0; b < block_count; ++b)
        {
            woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);
            blk->m_idom = (b == 0) ? -1 : idom[b];
            blk->m_dom_depth = depth[b];
        }

        free(depth);
    }

    free(idom);
    return true;
}

/* ===================================================================
 * Phase 4b: 循环检测
 * =================================================================== */

static bool _phase4b_detect_loops(woort_IRFunction* f)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    if (block_count == 0)
        return true;

    /*
     * 找回边 B -> H，其中 H 支配 B。
     * 标记自然循环中的所有 block。
     */
    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);

        for (size_t si = 0; si < blk->m_successors.m_size; ++si)
        {
            uint32_t succ = *(uint32_t*)woort_vector_at(&blk->m_successors, si);

            /* 检查 succ 是否支配 b：沿 idom 链向上查找 */
            bool succ_dom_b = false;
            {
                int32_t cur = (int32_t)b;
                while (cur >= 0)
                {
                    if ((uint32_t)cur == succ)
                    {
                        succ_dom_b = true;
                        break;
                    }
                    woort_IRBlock* cur_blk = (woort_IRBlock*)woort_vector_at(
                        &f->m_blocks, (uint32_t)cur);
                    if (cur_blk->m_idom == cur || cur_blk->m_idom < 0)
                        break;
                    cur = cur_blk->m_idom;
                }
            }

            if (!succ_dom_b)
                continue;

            /* 回边 b -> succ，succ 是循环头 */
            /* 自然循环 = {succ} ∪ {从 b 沿前驱边反向可达 succ 的所有节点} */

            bool* in_loop = (bool*)calloc(block_count, sizeof(bool));
            if (in_loop == NULL)
                return false;

            in_loop[succ] = true;
            in_loop[b]    = true;

            uint32_t* worklist = (uint32_t*)malloc(block_count * sizeof(uint32_t));
            if (worklist == NULL)
            {
                free(in_loop);
                return false;
            }

            uint32_t wl_count = 0;
            if (b != succ)
                worklist[wl_count++] = b;

            while (wl_count > 0)
            {
                uint32_t n = worklist[--wl_count];
                woort_IRBlock* n_blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, n);

                for (size_t pi = 0; pi < n_blk->m_predecessors.m_size; ++pi)
                {
                    uint32_t pred = *(uint32_t*)woort_vector_at(
                        &n_blk->m_predecessors, pi);
                    if (!in_loop[pred])
                    {
                        in_loop[pred] = true;
                        worklist[wl_count++] = pred;
                    }
                }
            }

            free(worklist);

            /* 标记循环信息 */
            for (uint32_t lb = 0; lb < block_count; ++lb)
            {
                if (in_loop[lb])
                {
                    woort_IRBlock* lb_blk = (woort_IRBlock*)woort_vector_at(
                        &f->m_blocks, lb);
                    lb_blk->m_is_in_loop = true;
                    if (lb_blk->m_loop_header < 0)
                        lb_blk->m_loop_header = (int32_t)succ;
                }
            }

            free(in_loop);
        }
    }

    return true;
}

/* ===================================================================
 * Phase 4c: 常量加载放置
 * =================================================================== */

/*
 * 求两个 block 的最近公共支配者
 */
static int32_t _find_common_dominator(woort_IRFunction* f, int32_t a, int32_t b)
{
    if (a < 0) return b;
    if (b < 0) return a;

    int32_t fa = a;
    int32_t fb = b;

    while (fa != fb)
    {
        woort_IRBlock* fa_blk = (woort_IRBlock*)woort_vector_at(
            &f->m_blocks, (uint32_t)fa);
        woort_IRBlock* fb_blk = (woort_IRBlock*)woort_vector_at(
            &f->m_blocks, (uint32_t)fb);

        if (fa_blk->m_dom_depth > fb_blk->m_dom_depth)
        {
            fa = fa_blk->m_idom;
            if (fa < 0) fa = 0;
        }
        else if (fb_blk->m_dom_depth > fa_blk->m_dom_depth)
        {
            fb = fb_blk->m_idom;
            if (fb < 0) fb = 0;
        }
        else
        {
            fa = fa_blk->m_idom;
            fb = fb_blk->m_idom;
            if (fa < 0) fa = 0;
            if (fb < 0) fb = 0;
        }
    }

    return fa;
}

/*
 * 常量使用信息（临时结构，用于收集每个 CONST vreg 的所有使用 block）
 */
typedef struct _woort_ConstUseInfo
{
    woort_IRValue* m_const_vreg;
    woort_Vector /* uint32_t (block index) */ m_use_blocks;
} _woort_ConstUseInfo;

static void _cleanup_const_infos(woort_Vector* const_infos)
{
    for (size_t i = 0; i < const_infos->m_size; ++i)
    {
        _woort_ConstUseInfo* info = (_woort_ConstUseInfo*)woort_vector_at(
            const_infos, i);
        woort_vector_deinit(&info->m_use_blocks);
    }
    woort_vector_deinit(const_infos);
}

static bool _phase4c_const_load_placement(woort_IRFunction* f, size_t* out_stack_space)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    const size_t instr_count = f->m_instructions.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    if (block_count == 0 || instr_count == 0)
        return true;

    /*
     * 构建 instruction → block 映射
     */
    uint32_t* instr_to_block = (uint32_t*)malloc(instr_count * sizeof(uint32_t));
    if (instr_to_block == NULL)
        return false;

    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);
        for (uint32_t i = blk->m_begin; i < blk->m_end; ++i)
            instr_to_block[i] = b;
    }

    /*
     * 收集所有非 const_direct 的 CONST vreg，及其使用点所在的 block
     */
    woort_Vector /* _woort_ConstUseInfo */ const_infos;
    woort_vector_init(&const_infos, sizeof(_woort_ConstUseInfo));

    for (woort_IRValue* cv = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
         cv != NULL;
         cv = (woort_IRValue*)woort_linklist_next(cv))
    {
        if (cv->m_source != WOORT_IRVALUE_SOURCE_CONST)
            continue;
        if (cv->m_is_const_direct)
            continue;

        /* 创建使用信息 */
        _woort_ConstUseInfo* info;
        if (!woort_vector_emplace_back(&const_infos, 1, (void**)&info))
        {
            _cleanup_const_infos(&const_infos);
            free(instr_to_block);
            return false;
        }
        info->m_const_vreg = cv;
        woort_vector_init(&info->m_use_blocks, sizeof(uint32_t));

        /* 扫描指令流，找到所有使用该 CONST vreg 的 block */
        for (size_t i = 0; i < instr_count; ++i)
        {
            woort_IROp* op = &instrs[i];
            bool used = false;
            for (int s = 0; s < 3; ++s)
            {
                if (op->m_src[s] == cv)
                {
                    used = true;
                    break;
                }
            }
            if (!used)
                continue;

            uint32_t blk_idx = instr_to_block[i];

            /* 去重 */
            bool already_in = false;
            for (size_t ubi = 0; ubi < info->m_use_blocks.m_size; ++ubi)
            {
                if (*(uint32_t*)woort_vector_at(&info->m_use_blocks, ubi) == blk_idx)
                {
                    already_in = true;
                    break;
                }
            }
            if (!already_in)
            {
                if (!woort_vector_push_back(&info->m_use_blocks, 1, &blk_idx))
                {
                    _cleanup_const_infos(&const_infos);
                    free(instr_to_block);
                    return false;
                }
            }
        }
    }

    free(instr_to_block);

    /*
     * 为每个 CONST vreg 分配栈槽，并决定 LOAD 放置位置
     */
    size_t const_slot_base = *out_stack_space; /* 从 Phase 3 已分配的栈槽之后继续 */
    size_t next_const_slot = const_slot_base;

    for (size_t ci = 0; ci < const_infos.m_size; ++ci)
    {
        _woort_ConstUseInfo* info = (_woort_ConstUseInfo*)woort_vector_at(
            &const_infos, ci);

        if (info->m_use_blocks.m_size == 0)
            continue;

        /* 为该 CONST vreg 分配栈槽 */
        int32_t slot_offset = -(int32_t)next_const_slot;
        next_const_slot++;
        info->m_const_vreg->m_assigned_stack_offset = slot_offset;

        /* 所有使用 block 的公共支配者 */
        int32_t common_dom = (int32_t)(
            *(uint32_t*)woort_vector_at(&info->m_use_blocks, 0));
        for (size_t ubi = 1; ubi < info->m_use_blocks.m_size; ++ubi)
        {
            uint32_t ub = *(uint32_t*)woort_vector_at(&info->m_use_blocks, ubi);
            common_dom = _find_common_dominator(f, common_dom, (int32_t)ub);
        }

        /* 如果公共支配者在循环内，提升到 loop header 的 idom */
        if (common_dom >= 0 && (uint32_t)common_dom < block_count)
        {
            woort_IRBlock* dom_blk = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, (uint32_t)common_dom);
            if (dom_blk->m_is_in_loop && dom_blk->m_loop_header >= 0)
            {
                woort_IRBlock* header_blk = (woort_IRBlock*)woort_vector_at(
                    &f->m_blocks, (uint32_t)dom_blk->m_loop_header);
                if (header_blk->m_idom >= 0)
                    common_dom = header_blk->m_idom;
                else
                    common_dom = 0; /* 提升到入口 */
            }
        }

        /* 在公共支配者 block 中记录常量加载 */
        if (common_dom >= 0 && (uint32_t)common_dom < block_count)
        {
            woort_IRBlock* place_blk = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, (uint32_t)common_dom);

            _woort_ConstLoadInfo load_info;
            load_info.m_const_index = info->m_const_vreg->m_const_idx;
            load_info.m_stack_offset = slot_offset;

            if (!woort_vector_push_back(&place_blk->m_const_loads, 1, &load_info))
            {
                _cleanup_const_infos(&const_infos);
                return false;
            }
        }
    }

    *out_stack_space = next_const_slot;

    _cleanup_const_infos(&const_infos);
    return true;
}

/* ===================================================================
 * 主分析入口
 * =================================================================== */

WOORT_NODISCARD bool _woort_IRFunction_analyze_and_allocate(
    woort_IRFunction* f,
    size_t* out_stack_space)
{
    assert(f != NULL);
    assert(out_stack_space != NULL);

    *out_stack_space = 0;

    /* Phase 1: 基本块切分 + CFG 构建 */
    if (!_phase1_split_blocks_and_build_cfg(f))
        return false;

    /* Phase 2: 活跃性分析 */
    if (!_phase2_liveness_analysis(f))
        return false;

    /* Phase 2b: 常量直接使用标记（PUSHCCHK / RETVC） */
    if (!_phase2b_const_optimization(f))
        return false;

    /* Phase 3: 栈槽分配（线性扫描，跳过 CONST 源 vreg） */
    if (!_phase3_stack_allocation(f, out_stack_space))
        return false;

    /* Phase 4a: Dominator tree */
    if (!_phase4a_build_dominator_tree(f))
        return false;

    /* Phase 4b: 循环检测 */
    if (!_phase4b_detect_loops(f))
        return false;

    /* Phase 4c: 常量加载放置（为非 const_direct 的 CONST vreg 分配栈槽并放置 LOAD） */
    if (!_phase4c_const_load_placement(f, out_stack_space))
        return false;

    return true;
}
