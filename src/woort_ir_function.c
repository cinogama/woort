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
#include "woort_ir_compiler.h"
#include "woort_vector.h"
#include "woort_linklist.h"
#include "woort_bitset.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

 /* ========== 私有类型 ========== */

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
    case WOORT_IROP_KIND_JIFINITED:
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
    woort_IRBlock* to_block, uint32_t to_idx)
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

/*
 * vreg 是否需要参与线性扫描栈槽分配。
 * 排除：参数（含捕获，预分配偏移）、const_direct 常量（发射层直连，
 * 不占栈槽）、无放置块的常量、以及无任何活跃区间的 vreg。
 * 非 const_direct 的 CONST vreg 参与统一线性扫描。
 */
static bool _should_allocate_interval(
    const woort_IRValue* v,
    /* OPTIONAL */ const uint32_t* const_placement_block,
    uint32_t first_point)
{
    assert(v != NULL);

    if (v->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT)
        return false;
    if (v->m_source == WOORT_IRVALUE_SOURCE_CONST)
    {
        if (v->m_is_const_direct)
            return false;
        if (const_placement_block == NULL ||
            const_placement_block[v->m_id] == UINT32_MAX)
            return false;
    }
    return first_point != UINT32_MAX;
}

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

void woort_IRFunction_init(
    woort_IRFunction* f,
    woort_IRCompiler* c,
    uint32_t param_count,
    uint32_t captured_count)
{
    f->m_param_count = param_count;
    f->m_captured_count = captured_count;
    f->m_ircompiler = c;

    woort_linklist_init(&f->m_ir_values, sizeof(woort_IRValue));
    f->m_next_vreg_id = 0;
    woort_vector_init(&f->m_const_vreg_table, sizeof(woort_IRValue*));

    woort_linklist_init(&f->m_ir_labels, sizeof(woort_IRLabel));
    f->m_next_label_id = 0;

    woort_vector_init(&f->m_instructions, sizeof(woort_IROp));
    woort_vector_init(&f->m_blocks, sizeof(woort_IRBlock));

    f->m_name = NULL;
    f->m_code_offset = 0;
    f->m_code_length = 0;

    /* 源码位置支持 */
    woort_SourceLocationStack_init(&f->m_srcloc_stack);
    woort_vector_init(&f->m_source_locations, sizeof(woort_SourceLocation));

    /* 局部变量记录 */
    woort_vector_init(&f->m_local_var_records, sizeof(woort_LocalVarRecord));
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
    woort_vector_deinit(&f->m_const_vreg_table);
    woort_linklist_deinit(&f->m_ir_labels);
    woort_vector_deinit(&f->m_instructions);
    woort_vector_deinit(&f->m_blocks);

    /* 源码位置支持 */
    woort_SourceLocationStack_deinit(&f->m_srcloc_stack);
    woort_vector_deinit(&f->m_source_locations);

    /* 局部变量记录 */
    woort_vector_deinit(&f->m_local_var_records);
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
    loc.m_filepath = woort_IRCompiler_intern_string(f->m_ircompiler, filepath);
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

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_captured(
    woort_IRFunction* f, uint32_t captured_idx)
{
    assert(captured_idx < f->m_captured_count);

    woort_IRValue* v;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&v))
        return NULL;

    woort_IRValue_init_captured(v, f->m_next_vreg_id, captured_idx);
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

WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRFunction_fetch_const(
    woort_IRFunction* f, woort_IRConstantIndex idx)
{
    /*
     * 同一 const_index 返回同一 IRValue*。
     * 侧表按 const_idx 直接索引（O(1) 命中）；条目为 NULL 表示尚未创建。
     * 扩表时手动填 NULL（woort_vector_resize 不清零新区域）。
     */
    if (idx >= f->m_const_vreg_table.m_size)
    {
        const size_t old_size = f->m_const_vreg_table.m_size;
        if (!woort_vector_resize(&f->m_const_vreg_table, (size_t)idx + 1))
            return NULL;
        for (size_t i = old_size; i < f->m_const_vreg_table.m_size; ++i)
            *(woort_IRValue**)woort_vector_at(&f->m_const_vreg_table, i) = NULL;
    }

    woort_IRValue* const existing =
        *(woort_IRValue**)woort_vector_at(&f->m_const_vreg_table, idx);
    if (existing != NULL)
        return existing;

    /* 首次请求该 const_index，创建新的 CONST vreg */
    woort_IRValue* v;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&v))
        return NULL;

    woort_IRValue_init_const(v, f->m_next_vreg_id, idx);
    f->m_next_vreg_id++;

    *(woort_IRValue**)woort_vector_at(&f->m_const_vreg_table, idx) = v;
    return v;
}

/* ===================================================================
 * 捕获区临时槽窗口重定位（超长捕获闭包支持）
 *
 * 捕获值在调用时解包到偏移 0..-(captured_count-1)（连续，VM 行为不变）。
 * 当 captured_count > 126 时，captured_idx 126/127/128 会落在发射层固定
 * 的 a8 临时槽窗口（fact -126/-127/-128）内。处理方式：
 *
 *   1. 落在窗口内的捕获值：函数序言（发射层生成，位于所有 block 之前，
 *      后向跳转不会重新执行）把它们从窗口搬运到窗口下方的尾接区域；
 *      引用它们的 IRValue 偏移重映射到新位置。
 *   2. 窗口之下的捕获值（idx >= 129）：物理位置即 -idx（已在窗口之下，
 *      不会被临时槽触碰），但其逻辑偏移需补偿 _get_fact_offset 的
 *      -3 平移（存储 -(idx-3)，fact 后恰为 -idx）。
 *   3. 未越界的捕获（idx <= 125）保持不变。
 *
 * 尾接区域起点取临时槽窗口与捕获区两者更深者之下：
 *   captured_count <= 129 时为 -129，更大时为 -captured_count。
 * =================================================================== */

WOORT_NODISCARD uint32_t _woort_captured_relocate_count(uint32_t captured_count)
{
    if (captured_count <= 126)
        return 0;
    return (captured_count - 126 >= 3) ? 3 : (captured_count - 126);
}

WOORT_NODISCARD int32_t _woort_captured_relocate_tail_base(uint32_t captured_count)
{
    return (captured_count > 129)
        ? -(int32_t)captured_count
        : -129;
}

static void _remap_captured_window(woort_IRFunction* f)
{
    const uint32_t captured_count = f->m_captured_count;
    if (captured_count <= 126)
        return;

    const int32_t tail_base = _woort_captured_relocate_tail_base(captured_count);

    for (woort_IRValue* v = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
        v != NULL;
        v = (woort_IRValue*)woort_linklist_next(v))
    {
        if (v->m_source != WOORT_IRVALUE_SOURCE_ARGUMENT)
            continue;

        /* 参数偏移为 3+idx（正数），捕获偏移为 -idx（非正数） */
        const int32_t off = v->m_assigned_stack_offset;
        if (off > 0)
            continue;

        const int32_t idx = -off;
        assert(idx >= 0 && (uint32_t)idx < captured_count);

        if (idx <= 125)
            continue;

        if (idx <= 128)
        {
            /* 窗口内：序言搬运到尾接区域。逻辑偏移回退 +3 使
             * _get_fact_offset 平移后落在尾接槽上。 */
            const int32_t j = idx - 126;
            v->m_assigned_stack_offset = tail_base - j + 3;
        }
        else
        {
            /* 窗口之下：物理位置即 -idx，补偿 -3 平移 */
            v->m_assigned_stack_offset = -(idx - 3);
        }

        /*
         * 不变量（修改任一侧公式前先读这里）：
         * 1. 重映射后的逻辑偏移必须 <= -126 —— 补偿值只有经过
         *    _get_fact_offset 的 -3 平移才能落回预期物理槽位；
         * 2. 重映射后的逻辑偏移必须保持 > -captured_count。vreg 槽的
         *    逻辑偏移为 -slot-captured_count（slot >= 0），恒
         *    <= -captured_count，因此两者绝不相等 —— 发射层用裸相等
         *    比较 m_assigned_stack_offset 来省略冗余搬运（见
         *    woort_ir_compiler.c 的 _EMIT_BINOP_* 宏），逻辑别名会使
         *    搬运被错误省略、读到/写到错误槽位。
         * 未重映射的捕获（-idx，idx < captured_count）与参数（3+idx）
         * 天然满足同样的不别名约束。
         */
        assert(v->m_assigned_stack_offset <= -126);
        assert(v->m_assigned_stack_offset > -(int32_t)captured_count);
    }
}

/* ===================================================================
 * Phase 0: 跳转合并（Jump Chaining）
 *
 * 如果一条跳转指令的目标是一个纯重定向块（仅包含 LABEL + JMP），
 * 则将跳转目标直接指向该 JMP 的最终目标，消除中间跳转。
 * 对所有跳转类型（JMP、JCC、JCCZ、JCC_*、JIFINITED）均适用。
 * =================================================================== */

static void _phase0_jump_chaining(woort_IRFunction* f)
{
    const size_t instr_count = f->m_instructions.m_size;
    if (instr_count == 0)
        return;

    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    /*
     * 预计算 next_real[i]：位置 i 及其之后的第一条非 LABEL 指令索引
     * （不存在则为 instr_count）。逆序一趟 O(N)，
     * 替代链追踪中每步重复的前向线性扫描。
     */
    size_t* next_real = (size_t*)malloc(instr_count * sizeof(size_t));
    if (next_real == NULL)
        return; /* OOM: 跳过本优化，仅损失优化不损失正确性 */

    if (instrs[instr_count - 1].m_op == WOORT_IROP_KIND_LABEL)
        next_real[instr_count - 1] = instr_count;
    else
        next_real[instr_count - 1] = instr_count - 1;

    for (size_t i = instr_count - 1; i > 0; --i)
    {
        if (instrs[i - 1].m_op == WOORT_IROP_KIND_LABEL)
            next_real[i - 1] = next_real[i];
        else
            next_real[i - 1] = i - 1;
    }

    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* const op = &instrs[i];

        if (!_is_jump_op(op->m_op))
            continue;

        woort_IRLabel* const original_target = op->m_jump_target;

        woort_IRLabel* final_target = original_target;

        assert(final_target != NULL && final_target->m_bound);

        /* 每轮追踪恰好跟随一条 JMP，到达一个新的 label。由于 label 数目
         * 不超过 instr_count，追踪步数超过 instr_count 必然意味着进入了
         * 不含 original_target 的环（如 L_a -> L_b -> L_a）。此时终止追踪，
         * final_target 保留为环中某个合法 label，下游 Phase 1b 会消除该
         * 死代码。这样既保证可终止，又不截断任何合法（无环）跳转链。 */
        for (size_t step = 0; step < instr_count; ++step)
        {
            assert(final_target->m_bound);

            const size_t real_idx = next_real[final_target->m_bind_index];

            if (real_idx < instr_count &&
                instrs[real_idx].m_op == WOORT_IROP_KIND_JMP)
            {
                woort_IRLabel* next_target = instrs[real_idx].m_jump_target;
                assert(next_target != NULL && next_target->m_bound);

                if (next_target == final_target
                    || next_target == original_target)
                    break;

                final_target = next_target;
            }
            else
            {
                break;
            }
        }

        op->m_jump_target = final_target;
    }

    free(next_real);
}

/* ===================================================================
 * Phase 2b: 移除无意义跳转
 *
 * 如果跳转指令（JIFINITED 除外）的 fall-through 路径和跳转路径
 * 到达同一条实际指令，则该跳转无意义，替换为 EMPTY。
 * =================================================================== */

static size_t _skip_labels(woort_IROp* instrs, size_t instr_count, size_t idx)
{
    while (idx < instr_count)
    {
        switch (instrs[idx].m_op)
        {
        case WOORT_IROP_KIND_LABEL:
        case WOORT_IROP_KIND_NOP:
        case WOORT_IROP_KIND_EMPTY:
            idx++;
            break;
        default:
            return idx;
        }
    }
    return idx;
}

static bool _is_noop_jump(
    woort_IROp* instrs,
    size_t instr_count,
    size_t jump_idx,
    woort_IRLabel* target)
{
    size_t fallthrough = _skip_labels(instrs, instr_count, jump_idx + 1);
    size_t target_real = _skip_labels(instrs, instr_count, target->m_bind_index);
    return fallthrough < instr_count && fallthrough == target_real;
}

static void _phase2b_remove_noop_jumps(woort_IRFunction* f)
{
    const size_t instr_count = f->m_instructions.m_size;
    if (instr_count == 0)
        return;

    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* op = &instrs[i];

        if (!_is_jump_op(op->m_op))
            continue;

        if (op->m_op == WOORT_IROP_KIND_JIFINITED)
            continue;

        if (!_is_noop_jump(instrs, instr_count, i, op->m_jump_target))
            continue;

        op->m_op = WOORT_IROP_KIND_EMPTY;
        op->m_dst = NULL;
        op->m_src[0] = NULL;
        op->m_src[1] = NULL;
        op->m_src[2] = NULL;
    }
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

    /* instruction → block index 映射 */
    uint32_t* const instr_to_block =
        (uint32_t*)malloc(instr_count * sizeof(uint32_t));

    if (instr_to_block == NULL)
    {
        free(is_leader);
        return false;
    }

    /* 预分配 blocks vector */
    if (!woort_vector_resize(&f->m_blocks, block_count))
    {
        free(instr_to_block);
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
                    woort_IRBlock* const prev_blk =
                        (woort_IRBlock*)woort_vector_at(
                            &f->m_blocks, blk_idx - 1);
                    prev_blk->m_end = (uint32_t)i;
                }

                woort_IRBlock* const blk =
                    (woort_IRBlock*)woort_vector_at(&f->m_blocks, blk_idx);
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
            woort_IRBlock* const last_blk =
                (woort_IRBlock*)woort_vector_at(
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
            woort_IRBlock* const target_block = (woort_IRBlock*)woort_vector_at(
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
 * Phase 1b: 基于CFG的死代码消除
 *
 * 从 block 0 出发 BFS 标记所有可达块，
 * 将不可达块中的非 LABEL 指令替换为 NOP（清除 dst/src）。
 * =================================================================== */

static void _phase1b_eliminate_dead_blocks(woort_IRFunction* f)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    if (block_count <= 1)
        return;

    bool* const reachable = (bool*)calloc(block_count, sizeof(bool));
    if (reachable == NULL)
        return;

    uint32_t* const queue = (uint32_t*)malloc(block_count * sizeof(uint32_t));
    if (queue == NULL)
    {
        free(reachable);
        return;
    }

    uint32_t head = 0, tail = 0;
    reachable[0] = true;
    queue[tail++] = 0;

    while (head < tail)
    {
        uint32_t bi = queue[head++];
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, bi);
        for (size_t si = 0; si < blk->m_successors.m_size; ++si)
        {
            uint32_t succ = *(uint32_t*)woort_vector_at(&blk->m_successors, si);
            if (!reachable[succ])
            {
                reachable[succ] = true;
                queue[tail++] = succ;
            }
        }
    }

    free(queue);

    woort_IROp* const instrs = (woort_IROp*)f->m_instructions.m_data;
    for (uint32_t b = 0; b < block_count; ++b)
    {
        if (reachable[b])
            continue;

        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);
        for (uint32_t i = blk->m_begin; i < blk->m_end; ++i)
        {
            if (instrs[i].m_op != WOORT_IROP_KIND_LABEL)
            {
                instrs[i].m_op = WOORT_IROP_KIND_EMPTY;
                instrs[i].m_dst = NULL;
                instrs[i].m_src[0] = NULL;
                instrs[i].m_src[1] = NULL;
                instrs[i].m_src[2] = NULL;
            }
        }
    }

    free(reachable);
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

static bool _phase2_liveness_analysis(
    woort_IRFunction* f, 
    /* OPTIONAL */ const uint32_t* const_placement_block)
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

    /* 为 CONST vreg 在其放置 block 预设 DEF（LOAD 发生在块头部，先于任何 USE） */
    if (const_placement_block != NULL)
    {
        for (uint32_t id = 0; id < vreg_count; ++id)
        {
            if (const_placement_block[id] == UINT32_MAX)
                continue;
            woort_IRBlock* place_blk = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, const_placement_block[id]);
            (void)woort_bitset_set(&place_blk->m_def, id);
        }
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
 * Phase 3b: 常量直接使用标记
 *
 * 扫描所有 SOURCE_CONST vreg，如果某个 CONST vreg 仅被一条
 * PUSHCHK、CALL 或 RET 使用（use_count == 1），标记为 const_direct。
 * 发射层将直接使用 PUSHCCHK / CALLC / RETVC 而非 LOAD + PUSHSCHK / CALLS / RETVS。
 *
 * 注意：CONST vreg 不参与指令流中的 DEF（没有 LOAD_CONST 指令），
 * 它们只作为其他指令的 m_src[] 出现。
 * =================================================================== */

static bool _phase3b_const_optimization(woort_IRFunction* f)
{
    const uint32_t vreg_count = f->m_next_vreg_id;
    const size_t instr_count = f->m_instructions.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    if (vreg_count == 0 || instr_count == 0)
        return true;

    /*
     * 单趟扫描指令流，按 vreg id 统计每个 CONST vreg 的使用次数
     * 与唯一使用者，替代按常量遍历全部指令的 O(consts x instrs)
     * 双重循环。
     */
    uint32_t* use_count = (uint32_t*)calloc(vreg_count, sizeof(uint32_t));
    woort_IROp** unique_user = (woort_IROp**)calloc(vreg_count, sizeof(woort_IROp*));

    if (use_count == NULL || unique_user == NULL)
    {
        free(use_count);
        free(unique_user);
        /* OOM: 跳过本优化（常量走放置加载路径），不影响正确性 */
        return true;
    }

    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* const op = &instrs[i];

        for (int s = 0; s < 3; ++s)
        {
            const woort_IRValue* src = op->m_src[s];
            if (src == NULL || src->m_source != WOORT_IRVALUE_SOURCE_CONST)
                continue;

            /* 同一指令的多个 src 引用同一 vreg 只计一次 */
            bool dup_in_op = false;
            for (int t = 0; t < s; ++t)
            {
                if (op->m_src[t] == src)
                {
                    dup_in_op = true;
                    break;
                }
            }
            if (dup_in_op)
                continue;

            use_count[src->m_id]++;
            unique_user[src->m_id] = op;
        }
    }

    for (woort_IRValue* cv = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
        cv != NULL;
        cv = (woort_IRValue*)woort_linklist_next(cv))
    {
        if (cv->m_source != WOORT_IRVALUE_SOURCE_CONST)
            continue;

        if (use_count[cv->m_id] != 1)
            continue;

        woort_IROp* const user = unique_user[cv->m_id];

        /* 检查唯一使用者是否是 MOV, PUSHCHK, CALL 或 RET */
        if (user->m_op != WOORT_IROP_KIND_MOV &&
            user->m_op != WOORT_IROP_KIND_PANIC &&
            user->m_op != WOORT_IROP_KIND_PUSHCHK &&
            user->m_op != WOORT_IROP_KIND_CALL &&
            user->m_op != WOORT_IROP_KIND_RET)
        {
            continue;
        }

        /* RETVC / PANIC / CALLC 没有扩展编码，const_index 超 U24 范围时不可用 */
        if (cv->m_const_idx > ((1u << 24) - 1)
            && (user->m_op == WOORT_IROP_KIND_RET ||
                user->m_op == WOORT_IROP_KIND_PANIC ||
                user->m_op == WOORT_IROP_KIND_CALL))
        {
            continue;
        }

        cv->m_is_const_direct = true;
    }

    free(use_count);
    free(unique_user);

    return true;
}

/* ===================================================================
 * Phase 3: 栈槽分配（线性扫描）
 * =================================================================== */

static bool _phase3_stack_allocation(
    woort_IRFunction* f,
    size_t* out_stack_space,
    /* OPTIONAL */ const uint32_t* const_placement_block)
{
    const uint32_t vreg_count = f->m_next_vreg_id;
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    const size_t instr_count = f->m_instructions.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;

    if (vreg_count == 0)
    {
        /* NOTE: No need to reserve for captured, them will be expand in call. */
        *out_stack_space = 0 /*f->m_captured_count*/;
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
    uint32_t* const first_point = (uint32_t*)malloc(vreg_count * sizeof(uint32_t));
    uint32_t* const last_point = (uint32_t*)malloc(vreg_count * sizeof(uint32_t));

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
        last_point[i] = 0;
    }

    /* 扫描所有指令收集 def/use 点 */
    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* const op = &instrs[i];

        /* m_dst 是定义点（半指令粒度：DEF = 5*i+4） */
        if (op->m_dst != NULL)
        {
            const uint32_t id = op->m_dst->m_id;
            const uint32_t def_pt = (uint32_t)i * 5 + 4;

            if (def_pt < first_point[id])
                first_point[id] = def_pt;
            if (def_pt > last_point[id])
                last_point[id] = def_pt;
        }

        /* m_src[] 是使用点（半指令粒度：USE = 2*i） */
        for (uint32_t s = 0; s < 3; ++s)
        {
            if (op->m_src[s] != NULL)
            {
                const uint32_t id = op->m_src[s]->m_id;

                /* NOTE: 此处使用逆序，期待优先复用首个读操作数以便计算指令能够使用更快寻址的特化版本 */
                const uint32_t use_pt = (uint32_t)i * 5 + (3 - s);

                if (use_pt < first_point[id])
                    first_point[id] = use_pt;
                if (use_pt > last_point[id])
                    last_point[id] = use_pt;
            }
        }
    }

    /* 为 CONST vreg 设置放置块起始点作为活跃区间起点 */
    if (const_placement_block != NULL)
    {
        for (uint32_t id = 0; id < vreg_count; ++id)
        {
            if (const_placement_block[id] == UINT32_MAX)
                continue;
            woort_IRBlock* const place_blk = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, const_placement_block[id]);

            if (place_blk->m_begin * 5 < first_point[id])
                first_point[id] = place_blk->m_begin * 5;
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
                if (blk->m_end > 0)
                {
                    uint32_t block_tail = (blk->m_end - 1) * 5 + 4;
                    if (block_tail > last_point[id])
                        last_point[id] = block_tail;
                }
                if (first_point[id] == UINT32_MAX)
                    first_point[id] = blk->m_begin * 5;
            }
            /* 如果 vreg 在 block 入口活跃，区间需覆盖到 block 起始 */
            if (woort_bitset_test(&blk->m_live_in, id))
            {
                if (blk->m_begin * 5 < first_point[id])
                    first_point[id] = blk->m_begin * 5;
            }
        }
    }

    /*
     * 构建需要分配的活跃区间列表（筛选条件见 _should_allocate_interval）
     */
    uint32_t interval_count = 0;
    for (uint32_t id = 0; id < vreg_count; ++id)
    {
        if (_should_allocate_interval(
                vreg_by_id[id], const_placement_block, first_point[id]))
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
            if (!_should_allocate_interval(
                    vreg_by_id[id], const_placement_block, first_point[id]))
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
        {
            size_t new_active = 0;
            for (size_t a = 0; a < active_count; ++a)
            {
                if (active_list[a]->m_end < cur->m_start)
                    free_slot_stack[free_slot_count++] = active_list[a]->m_assigned_slot;
                else /* if (active_list[a]->m_end >= cur->m_start) */
                    active_list[new_active++] = active_list[a];
            }
            active_count = new_active;
        }

        /* 分配或复用栈槽 */
        if (free_slot_count > 0)
            cur->m_assigned_slot = free_slot_stack[--free_slot_count];
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
        const uint32_t id = intervals[i].m_vreg_id;
        const int32_t slot = intervals[i].m_assigned_slot;
        woort_IRValue* v = vreg_by_id[id];
        assert(v != NULL);
        /* slot 0 → offset -captured_count, slot 1 → offset -captured_count-1, ... */
        v->m_assigned_stack_offset = -slot - f->m_captured_count;
    }

    free(intervals);
    free(vreg_by_id);

    /*
     * NOTE: No need to reserve for captured, them will be expand in call.
     *
     * 栈槽的逻辑偏移为 -slot-captured_count，发射层会把 <= -126 的逻辑偏移
     * 再偏移 -3 以避开临时槽 -126/-127/-128（见 _get_fact_offset）。当捕获区
     * 与局部槽合计越过该边界（captured_count + max_slots > 125）时，必须
     * 额外预留 3 个槽，否则最深的栈槽会写到帧（rt_sp）之外。
     * out_stack_space 已包含该余量，发射层直接使用。
     */
    {
        size_t reserve = max_slots;

        if (max_slots > 0 && (f->m_captured_count + max_slots > 125))
            reserve += 3;

        /*
         * 捕获区越过临时槽窗口（captured_count >= 127）：函数序言要把
         * 窗口内的捕获值搬运到窗口下方的尾接区域，其最深 fact 槽为
         * tail_base - (relocate_count - 1)，对任何 captured_count >= 127
         * 都恰为 -(captured_count+2)。统一预留 3 个槽即可覆盖。
         */
        if (f->m_captured_count >= 127 && reserve < 3)
            reserve = 3;

        /*
         * 参数偏移为 3+idx。当某参数超出 S8 编码（param_count >= 126）时，
         * a8 类指令（JCC 条件、CAS、STOREPVALUE 等）会把它搬运到固定
         * 临时槽 -126..-128 —— 帧的可写底部为 -(captured_count+n)，
         * 必须预留到 -128 以下，否则临时槽在帧外（栈溢出检查被绕过）。
         */
        if (f->m_param_count >= 126)
        {
            const size_t temp_floor_need = 128 -
                (f->m_captured_count < 128 ? f->m_captured_count : 128);
            if (reserve < temp_floor_need)
                reserve = temp_floor_need;
        }

        *out_stack_space = reserve;
    }
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

    int32_t* idom = (int32_t*)malloc(block_count * sizeof(int32_t));
    uint32_t* rpo_order = (uint32_t*)malloc(block_count * sizeof(uint32_t));
    uint32_t* rpo_number = (uint32_t*)malloc(block_count * sizeof(uint32_t));
    bool* visited = (bool*)calloc(block_count, sizeof(bool));

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
            in_loop[b] = true;

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

static bool _phase4c_determine_const_load_placement(
    woort_IRFunction* f,
    uint32_t** out_const_placement_block)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    const size_t instr_count = f->m_instructions.m_size;
    woort_IROp* instrs = (woort_IROp*)f->m_instructions.m_data;
    const uint32_t vreg_count = f->m_next_vreg_id;

    *out_const_placement_block = NULL;

    if (block_count == 0 || instr_count == 0)
        return true;

    /*
     * 输出数组：const_placement_block[vreg_id] = 放置 block 索引
     * UINT32_MAX 表示不适用（非 CONST / const_direct / 无使用）
     */
    uint32_t* const_placement_block = (uint32_t*)malloc(vreg_count * sizeof(uint32_t));
    if (const_placement_block == NULL)
        return false;

    for (uint32_t i = 0; i < vreg_count; ++i)
        const_placement_block[i] = UINT32_MAX;

    /*
     * 构建 instruction → block 映射
     */
    uint32_t* instr_to_block = (uint32_t*)malloc(instr_count * sizeof(uint32_t));
    if (instr_to_block == NULL)
    {
        free(const_placement_block);
        return false;
    }

    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);
        for (uint32_t i = blk->m_begin; i < blk->m_end; ++i)
            instr_to_block[i] = b;
    }

    /*
     * 单趟扫描指令流，按 vreg id 收集每个非 const_direct 的 CONST vreg
     * 的使用 block 列表（替代按常量遍历全部指令的 O(consts x instrs)
     * 双重循环）。
     *
     * 指令按线性顺序遍历，instr_to_block 单调不减，因此每个列表内的
     * block 索引单调不减，去重只需与最后一个元素比较。
     * use_blocks_by_id 由 calloc 清零；woort_Vector 首次使用前需要
     * woort_vector_init 设置元素大小（对全零内存调用 deinit 安全）。
     */
    woort_Vector* use_blocks_by_id =
        (woort_Vector*)calloc(vreg_count, sizeof(woort_Vector));
    if (use_blocks_by_id == NULL)
    {
        free(instr_to_block);
        free(const_placement_block);
        return false;
    }

    for (size_t i = 0; i < instr_count; ++i)
    {
        woort_IROp* op = &instrs[i];

        for (int s = 0; s < 3; ++s)
        {
            const woort_IRValue* src = op->m_src[s];
            if (src == NULL || src->m_source != WOORT_IRVALUE_SOURCE_CONST)
                continue;
            if (src->m_is_const_direct)
                continue;

            woort_Vector* use_blocks = &use_blocks_by_id[src->m_id];
            if (use_blocks->m_size == 0 && use_blocks->m_element_size == 0)
                woort_vector_init(use_blocks, sizeof(uint32_t));

            const uint32_t blk_idx = instr_to_block[i];
            if (use_blocks->m_size > 0 &&
                *(uint32_t*)woort_vector_at(use_blocks, use_blocks->m_size - 1) == blk_idx)
                continue;

            if (!woort_vector_push_back(use_blocks, 1, &blk_idx))
            {
                for (uint32_t id = 0; id < vreg_count; ++id)
                    woort_vector_deinit(&use_blocks_by_id[id]);
                free(use_blocks_by_id);
                free(instr_to_block);
                free(const_placement_block);
                return false;
            }
        }
    }

    free(instr_to_block);

    /*
     * 为每个 CONST vreg 计算放置位置（不分配栈槽）
     */
    for (woort_IRValue* cv = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
        cv != NULL;
        cv = (woort_IRValue*)woort_linklist_next(cv))
    {
        if (cv->m_source != WOORT_IRVALUE_SOURCE_CONST)
            continue;
        if (cv->m_is_const_direct)
            continue;

        woort_Vector* use_blocks = &use_blocks_by_id[cv->m_id];
        if (use_blocks->m_size == 0)
            continue;

        /* 所有使用 block 的公共支配者 */
        int32_t common_dom = (int32_t)(
            *(uint32_t*)woort_vector_at(use_blocks, 0));
        for (size_t ubi = 1; ubi < use_blocks->m_size; ++ubi)
        {
            uint32_t ub = *(uint32_t*)woort_vector_at(use_blocks, ubi);
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
                    common_dom = 0;
            }
        }

        /* 记录放置 block 索引 */
        if (common_dom >= 0 && (uint32_t)common_dom < block_count)
        {
            const_placement_block[cv->m_id] = (uint32_t)common_dom;

            /* 在放置 block 中记录常量加载（栈偏移暂为占位符） */
            woort_IRBlock* place_blk = (woort_IRBlock*)woort_vector_at(
                &f->m_blocks, (uint32_t)common_dom);

            _woort_ConstLoadInfo load_info;
            load_info.m_const_index = cv->m_const_idx;
            load_info.m_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;

            if (!woort_vector_push_back(&place_blk->m_const_loads, 1, &load_info))
            {
                for (uint32_t id = 0; id < vreg_count; ++id)
                    woort_vector_deinit(&use_blocks_by_id[id]);
                free(use_blocks_by_id);
                free(const_placement_block);
                return false;
            }
        }
    }

    for (uint32_t id = 0; id < vreg_count; ++id)
        woort_vector_deinit(&use_blocks_by_id[id]);
    free(use_blocks_by_id);

    *out_const_placement_block = const_placement_block;
    return true;
}

/* ===================================================================
 * Phase 4c_part2: 更新常量加载的实际栈偏移
 * =================================================================== */

static bool _phase4c_update_const_load_offsets(woort_IRFunction* f)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;

    /*
     * 构建 const_index → m_assigned_stack_offset 映射
     */
    woort_Vector /* _woort_ConstLoadInfo */ const_map;
    woort_vector_init(&const_map, sizeof(_woort_ConstLoadInfo));

    for (woort_IRValue* cv = (woort_IRValue*)woort_linklist_iter(&f->m_ir_values);
        cv != NULL;
        cv = (woort_IRValue*)woort_linklist_next(cv))
    {
        if (cv->m_source != WOORT_IRVALUE_SOURCE_CONST)
            continue;
        if (cv->m_is_const_direct)
            continue;
        if (cv->m_assigned_stack_offset == WOORT_IRVALUE_STACK_NOT_ASSIGN)
            continue;

        _woort_ConstLoadInfo entry;
        entry.m_const_index = cv->m_const_idx;
        entry.m_stack_offset = cv->m_assigned_stack_offset;

        if (!woort_vector_push_back(&const_map, 1, &entry))
        {
            woort_vector_deinit(&const_map);
            return false;
        }
    }

    /*
     * 更新每个 block 的 m_const_loads 中的栈偏移
     */
    for (uint32_t b = 0; b < block_count; ++b)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, b);

        for (size_t li = 0; li < blk->m_const_loads.m_size; ++li)
        {
            _woort_ConstLoadInfo* info = (_woort_ConstLoadInfo*)woort_vector_at(
                &blk->m_const_loads, li);

            if (info->m_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
                continue;

            for (size_t mi = 0; mi < const_map.m_size; ++mi)
            {
                const _woort_ConstLoadInfo* map_entry =
                    (const _woort_ConstLoadInfo*)woort_vector_at(&const_map, mi);
                if (map_entry->m_const_index == info->m_const_index)
                {
                    info->m_stack_offset = map_entry->m_stack_offset;
                    break;
                }
            }
        }
    }

    woort_vector_deinit(&const_map);
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

    /* 捕获区临时槽窗口重映射（captured_count > 126 时生效） */
    _remap_captured_window(f);

    /* Phase 0: 跳转合并 */
    _phase0_jump_chaining(f);

    /* Phase 1: 基本块切分 + CFG 构建 */
    if (!_phase1_split_blocks_and_build_cfg(f))
        return false;

    /* Phase 1b: 死代码消除 */
    _phase1b_eliminate_dead_blocks(f);

    /* Phase 2b: 移除无意义跳转 */
    _phase2b_remove_noop_jumps(f);

    /* Phase 3b: 常量直接使用标记（PUSHCCHK / RETVC） */
    if (!_phase3b_const_optimization(f))
        return false;

    /* Phase 4a: Dominator tree */
    if (!_phase4a_build_dominator_tree(f))
        return false;

    /* Phase 4b: 循环检测 */
    if (!_phase4b_detect_loops(f))
        return false;

    /* Phase 4c_part1: 确定常量加载放置位置（不分配栈槽） */
    uint32_t* const_placement_block = NULL;
    if (!_phase4c_determine_const_load_placement(f, &const_placement_block))
        return false;

    /* Phase 2: 活跃性分析（CONST vreg 以放置块作为 DEF 参与分析） */
    if (!_phase2_liveness_analysis(f, const_placement_block))
    {
        free(const_placement_block);
        return false;
    }

    /* Phase 3: 栈槽分配（线性扫描，CONST vreg 统一参与复用） */
    if (!_phase3_stack_allocation(f, out_stack_space, const_placement_block))
    {
        free(const_placement_block);
        return false;
    }

    /* Phase 4c_part2: 用实际栈偏移更新常量加载记录 */
    if (!_phase4c_update_const_load_offsets(f))
    {
        free(const_placement_block);
        return false;
    }

    free(const_placement_block);
    return true;
}
