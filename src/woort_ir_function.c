#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_bitset.h"
#include "woort_vector.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

size_t _woort_IRFunction_constant_hash(const void* key)
{
    return (size_t)(*(const woort_IRConstantIndex*)key);
}

bool _woort_IRFunction_constant_equal(const void* key1, const void* key2)
{
    return *(const woort_IRConstantIndex*)key1 == *(const woort_IRConstantIndex*)key2;
}

void woort_IRFunction_init(woort_IRFunction* ir_function, uint32_t param_count)
{
    ir_function->m_param_count = param_count;
    ir_function->m_entry_block = NULL;

    woort_linklist_init(&ir_function->m_ir_values, sizeof(woort_IRValue));
    woort_linklist_init(&ir_function->m_ir_blocks, sizeof(woort_IRBlock));
    woort_hashmap_init(
        &ir_function->m_ir_constant_values,
        sizeof(woort_IRConstantIndex),
        sizeof(woort_IRValue*),
        _woort_IRFunction_constant_hash,
        _woort_IRFunction_constant_equal);
}

void woort_IRFunction_deinit(woort_IRFunction* ir_function)
{
    for (woort_IRBlock* block = woort_linklist_iter(&ir_function->m_ir_blocks);
        block != NULL;
        block = woort_linklist_next(block))
    {
        woort_IRBlock_deinit(block);
    }

    woort_linklist_deinit(&ir_function->m_ir_values);
    woort_linklist_deinit(&ir_function->m_ir_blocks);
    woort_hashmap_deinit(&ir_function->m_ir_constant_values);
}

/* OPTIONAL */ woort_IRBlock* woort_IRFunction_entry_block(woort_IRFunction* f)
{
    if (f->m_entry_block == NULL)
        f->m_entry_block = woort_IRFuntion_add_block(f);

    return f->m_entry_block;
}

/* OPTIONAL */ woort_IRBlock* woort_IRFuntion_add_block(woort_IRFunction* f)
{
    woort_IRBlock* block;
    if (!woort_linklist_emplace_back(&f->m_ir_blocks, (void**)&block))
        return NULL;

    woort_IRBlock_init(block, f);

    if (f->m_entry_block == NULL)
        f->m_entry_block = block;

    return block;
}

/* OPTIONAL */ woort_IRValue* _woort_IRFunction_new_value(woort_IRFunction* f)
{
    woort_IRValue* value;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&value))
        return NULL;

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFuntion_load_constant(
    woort_IRFunction* f, woort_IRConstantIndex c)
{
    woort_IRValue* existing_value;
    if (woort_hashmap_find(&f->m_ir_constant_values, &c, (void**)&existing_value))
        return existing_value;

    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_constant(value, c);

    if (woort_hashmap_insert(&f->m_ir_constant_values, &c, &value) != WOORT_HASHMAP_RESULT_OK)
        return NULL;

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f, uint32_t param_idx)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_argument(value, param_idx);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_operate_result(
    woort_IRFunction* f, woort_IROp* modify_op)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_operate(value, modify_op);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_phi_value(woort_IRFunction* f)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_phi(value);

    return value;
}

/* ==========================================================================
 *  Stack Slot Assignment
 *
 *  为每个需要栈槽的 SSA value 分配栈偏移 (m_assigned_stack_offset)，
 *  并决定常量值的最佳加载位置 (m_loading_constants)。
 *
 *  算法概要:
 *    1. 收集需要分配的 value，建立双向 index 映射
 *    2. Phi coalescing —— union-find 合并 phi result 与 incoming value
 *    3. 标准迭代数据流活跃性分析 (USE/DEF/LIVE_IN/LIVE_OUT)
 *    4. 基于活跃区间的线性扫描栈槽分配（支持 slot 复用）
 *    5. 支配树 + 循环检测
 *    6. 常量加载放置：公共支配者 + 循环外提升
 * ========================================================================== */

/* ========== 内部数据结构 ========== */

/*
 * value 指针 <-> dense index 双向映射
 */
typedef struct {
    woort_HashMap m_value_to_index;
    woort_IRValue** m_index_to_value;
    size_t m_count;
} _woort_ValueIndexMap;

/*
 * 每个 value 的活跃性信息
 */
typedef struct {
    size_t m_first_def;         /* 首次定义的全局指令编号 */
    size_t m_last_use;          /* 最后使用的全局指令编号 */
    /* OPTIONAL */ woort_IRBlock* m_first_use_block; /* 首次被使用的 block */
    woort_Bitset m_use_blocks;  /* 被使用的所有 block 的 bitset */
} _woort_LivenessInfo;

/*
 * 每个 block 的活跃性数据流集合
 */
typedef struct {
    woort_Bitset m_use;
    woort_Bitset m_def;
    woort_Bitset m_live_in;
    woort_Bitset m_live_out;
} _woort_BlockLiveness;

/*
 * Union-Find (用于 phi coalescing)
 */
typedef struct {
    size_t* m_parent;
    size_t m_count;
} _woort_DisjointSet;

/*
 * 已分配的 slot 条目（仅 representative 级别）
 */
typedef struct {
    int32_t m_slot;    /* 分配的 slot 编号 */
    size_t m_def;      /* 合并后的 first_def */
    size_t m_last_use; /* 合并后的 last_use */
} _woort_AllocatedSlot;

/*
 * 支配者分析中间数据
 */
typedef struct {
    woort_Bitset* m_dom_sets;
    size_t m_block_count;
} _woort_DominatorInfo;

/* ========== 工具函数 ========== */

static size_t _pointer_hash(const void* key)
{
    return (size_t)(*(const void* const*)key);
}

static bool _pointer_equal(const void* key1, const void* key2)
{
    return *(const void* const*)key1 == *(const void* const*)key2;
}

/*
 * 判断一个 IRValue 是否需要分配栈槽：
 *   - 已经有预分配偏移（如 argument）的跳过
 *   - 常量如果没有被标记为 m_constant_need_stack_slot 则跳过
 */
static bool _needs_stack_slot(const woort_IRValue* v)
{
    if (v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
        return false;

    if (v->m_source == WOORT_IRVALUE_SOURCE_CONSTANT && !v->m_constant_need_stack_slot)
        return false;

    return true;
}

/* ========== Union-Find ========== */

static bool _disjoint_set_init(_woort_DisjointSet* ds, size_t count)
{
    ds->m_parent = (size_t*)malloc(count * sizeof(size_t));
    if (ds->m_parent == NULL)
        return false;

    ds->m_count = count;
    for (size_t i = 0; i < count; i++)
        ds->m_parent[i] = i;

    return true;
}

static void _disjoint_set_deinit(_woort_DisjointSet* ds)
{
    free(ds->m_parent);
    ds->m_parent = NULL;
}

static size_t _disjoint_set_find(_woort_DisjointSet* ds, size_t x)
{
    /* 路径压缩 */
    if (ds->m_parent[x] != x)
        ds->m_parent[x] = _disjoint_set_find(ds, ds->m_parent[x]);
    return ds->m_parent[x];
}

static void _disjoint_set_union(_woort_DisjointSet* ds, size_t a, size_t b)
{
    size_t root_a = _disjoint_set_find(ds, a);
    size_t root_b = _disjoint_set_find(ds, b);
    if (root_a != root_b)
        ds->m_parent[root_a] = root_b;
}

/* ========== Value Index Map ========== */

static bool _value_index_map_init(_woort_ValueIndexMap* map, size_t capacity)
{
    woort_hashmap_init(
        &map->m_value_to_index,
        sizeof(woort_IRValue*),
        sizeof(size_t),
        _pointer_hash,
        _pointer_equal);

    map->m_index_to_value = (woort_IRValue**)malloc(capacity * sizeof(woort_IRValue*));
    if (map->m_index_to_value == NULL)
    {
        return false;
    }

    map->m_count = 0;
    return true;
}

static void _value_index_map_deinit(_woort_ValueIndexMap* map)
{
    woort_hashmap_deinit(&map->m_value_to_index);

    if (map->m_index_to_value != NULL)
        free(map->m_index_to_value);

    map->m_count = 0;
}

static bool _value_index_map_insert(_woort_ValueIndexMap* map, woort_IRValue* v, size_t index)
{
    if (woort_hashmap_insert(&map->m_value_to_index, &v, &index) != WOORT_HASHMAP_RESULT_OK)
        return false;

    map->m_index_to_value[index] = v;
    map->m_count++;
    return true;
}

static bool _value_index_map_get(_woort_ValueIndexMap* map, woort_IRValue* v, size_t* out_index)
{
    void* val_ptr = NULL;
    if (!woort_hashmap_find(&map->m_value_to_index, &v, &val_ptr))
        return false;
    *out_index = *(size_t*)val_ptr;
    return true;
}

/* ========== Block Liveness ========== */

static bool _block_liveness_init(_woort_BlockLiveness* bl, size_t value_count)
{
    bool init_result = woort_bitset_init(&bl->m_use, value_count);
    init_result = woort_bitset_init(&bl->m_def, value_count) && init_result;
    init_result = woort_bitset_init(&bl->m_live_in, value_count) && init_result;
    init_result = woort_bitset_init(&bl->m_live_out, value_count) && init_result;

    if (!init_result)
    {
        woort_bitset_deinit(&bl->m_use);
        woort_bitset_deinit(&bl->m_def);
        woort_bitset_deinit(&bl->m_live_in);
        woort_bitset_deinit(&bl->m_live_out);
    }

    return init_result;
}

static void _block_liveness_deinit(_woort_BlockLiveness* bl)
{
    woort_bitset_deinit(&bl->m_use);
    woort_bitset_deinit(&bl->m_def);
    woort_bitset_deinit(&bl->m_live_in);
    woort_bitset_deinit(&bl->m_live_out);
}

/* ========== CFG 后继提取 ========== */

static size_t _get_successors(
    const woort_IRBlock* B,
    /* OPTIONAL */ woort_IRBlock* successors[2])
{
    size_t count = 0;

    switch (B->m_cond_type)
    {
    case WOORT_IRBLOCK_ENDWAY_BR:
        if (successors != NULL)
            successors[0] = B->m_br_next_block;
        count = 1;
        break;

    case WOORT_IRBLOCK_ENDWAY_BR_COND:
        if (successors != NULL)
        {
            successors[0] = B->m_br_next_block_cond_true;
            successors[1] = B->m_br_next_block_cond_false;
        }
        count = 2;
        break;

    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
        if (successors != NULL)
        {
            successors[0] = B->m_br_next_block_compare_true;
            successors[1] = B->m_br_next_block_compare_false;
        }
        count = 2;
        break;

    case WOORT_IRBLOCK_ENDWAY_RET:
    default:
        count = 0;
        break;
    }

    return count;
}

/* ========== Block Index Map ========== */

static bool _block_index_map_init(
    woort_HashMap* map,
    woort_IRFunction* f,
    woort_IRBlock*** out_index_to_block,
    size_t block_count)
{
    woort_hashmap_init(
        map,
        sizeof(woort_IRBlock*),
        sizeof(size_t),
        _pointer_hash,
        _pointer_equal);

    woort_IRBlock** idx_to_blk = (woort_IRBlock**)malloc(block_count * sizeof(woort_IRBlock*));
    if (idx_to_blk == NULL)
    {
        woort_hashmap_deinit(map);
        return false;
    }

    size_t idx = 0;
    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        if (woort_hashmap_insert(map, &B, &idx) != WOORT_HASHMAP_RESULT_OK)
        {
            free(idx_to_blk);
            woort_hashmap_deinit(map);
            return false;
        }
        idx_to_blk[idx] = B;
        idx++;
    }

    *out_index_to_block = idx_to_blk;
    return true;
}

static bool _block_index_map_get(woort_HashMap* map, woort_IRBlock* B, size_t* out_index)
{
    void* val_ptr = NULL;
    if (!woort_hashmap_find(map, &B, &val_ptr))
        return false;
    *out_index = *(size_t*)val_ptr;
    return true;
}

/* ========== Bitset 辅助操作 ========== */

static void _bitset_union(woort_Bitset* dst, const woort_Bitset* src)
{
    size_t wc = dst->m_word_count < src->m_word_count ? dst->m_word_count : src->m_word_count;
    for (size_t i = 0; i < wc; i++)
        dst->m_data[i] |= src->m_data[i];
}

static void _bitset_intersect(woort_Bitset* dst, const woort_Bitset* src)
{
    size_t wc = dst->m_word_count < src->m_word_count ? dst->m_word_count : src->m_word_count;
    for (size_t i = 0; i < wc; i++)
        dst->m_data[i] &= src->m_data[i];
    for (size_t i = wc; i < dst->m_word_count; i++)
        dst->m_data[i] = 0;
}

static void _bitset_copy(woort_Bitset* dst, const woort_Bitset* src)
{
    size_t wc = dst->m_word_count < src->m_word_count ? dst->m_word_count : src->m_word_count;
    for (size_t i = 0; i < wc; i++)
        dst->m_data[i] = src->m_data[i];
    for (size_t i = wc; i < dst->m_word_count; i++)
        dst->m_data[i] = 0;
}

static bool _bitset_equal(const woort_Bitset* a, const woort_Bitset* b)
{
    size_t wc = a->m_word_count < b->m_word_count ? a->m_word_count : b->m_word_count;
    for (size_t i = 0; i < wc; i++)
    {
        if (a->m_data[i] != b->m_data[i])
            return false;
    }
    for (size_t i = wc; i < a->m_word_count; i++)
    {
        if (a->m_data[i] != 0)
            return false;
    }
    for (size_t i = wc; i < b->m_word_count; i++)
    {
        if (b->m_data[i] != 0)
            return false;
    }
    return true;
}

/* ========== Terminator 操作数的使用收集 ========== */

static void _analyze_terminator_uses(
    woort_IRBlock* B,
    _woort_BlockLiveness* bl,
    _woort_ValueIndexMap* idx_map,
    _woort_LivenessInfo* liveness,
    size_t block_idx,
    size_t global_inst_idx)
{
    woort_IRValue* values_to_check[3] = { NULL, NULL, NULL };
    size_t check_count = 0;

    switch (B->m_cond_type)
    {
    case WOORT_IRBLOCK_ENDWAY_BR_COND:
        values_to_check[0] = B->m_br_cond_value;
        check_count = 1;
        break;

    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
        values_to_check[0] = B->m_br_compare_values[0];
        values_to_check[1] = B->m_br_compare_values[1];
        check_count = 2;
        break;

    case WOORT_IRBLOCK_ENDWAY_RET:
        values_to_check[0] = B->m_ret_value_may_null;
        check_count = 1;
        break;

    default:
        check_count = 0;
        break;
    }

    for (size_t i = 0; i < check_count; i++)
    {
        if (values_to_check[i] != NULL)
        {
            size_t v_idx;
            if (_value_index_map_get(idx_map, values_to_check[i], &v_idx))
            {
                if (!woort_bitset_test(&bl->m_def, v_idx))
                    woort_bitset_set(&bl->m_use, v_idx);
                liveness[v_idx].m_last_use = global_inst_idx;
                woort_bitset_set(&liveness[v_idx].m_use_blocks, block_idx);
                if (liveness[v_idx].m_first_use_block == NULL)
                    liveness[v_idx].m_first_use_block = B;
            }
        }
    }
}

/*
 * 收集 phi incoming value 在前驱 block 中的 use。
 * 在 SSA 中，phi 的 incoming value 在语义上是在前驱 block 的末尾被"使用"的。
 */
static void _analyze_phi_incoming_uses(
    woort_IRBlock* B,
    _woort_BlockLiveness* bl,
    _woort_ValueIndexMap* idx_map,
    _woort_LivenessInfo* liveness,
    woort_HashMap* block_index_map,
    size_t block_idx,
    size_t global_inst_idx)
{
    woort_IRBlock* successors[2];
    size_t succ_count = _get_successors(B, successors);

    for (size_t s = 0; s < succ_count; s++)
    {
        for (woort_IRPhi* phi = woort_linklist_iter(&successors[s]->m_phis);
             phi != NULL;
             phi = woort_linklist_next(phi))
        {
            for (woort_IRPhi_ReentryRecord* rec = woort_linklist_iter(&phi->m_records);
                 rec != NULL;
                 rec = woort_linklist_next(rec))
            {
                if (rec->m_from_block == B)
                {
                    size_t v_idx;
                    if (_value_index_map_get(idx_map, rec->m_value, &v_idx))
                    {
                        if (!woort_bitset_test(&bl->m_def, v_idx))
                            woort_bitset_set(&bl->m_use, v_idx);
                        if (liveness[v_idx].m_last_use < global_inst_idx)
                            liveness[v_idx].m_last_use = global_inst_idx;
                        woort_bitset_set(&liveness[v_idx].m_use_blocks, block_idx);
                        if (liveness[v_idx].m_first_use_block == NULL)
                            liveness[v_idx].m_first_use_block = B;
                    }
                }
            }
        }
    }
}

/* ========== Slot 复用 ========== */

/*
 * 在已分配的 slot 列表中查找一个可复用的 slot。
 * 复用条件：两者的活跃区间不重叠 (last_use_b < def_a || last_use_a < def_b)
 */
static int32_t _find_reusable_slot(
    _woort_AllocatedSlot* allocated,
    size_t allocated_count,
    size_t def_a,
    size_t last_use_a)
{
    for (size_t i = 0; i < allocated_count; i++)
    {
        size_t def_b = allocated[i].m_def;
        size_t last_use_b = allocated[i].m_last_use;

        if (last_use_b < def_a || last_use_a < def_b)
            return allocated[i].m_slot;
    }
    return -1;
}

/* ========== 支配者分析 ========== */

static bool _dominator_info_init(_woort_DominatorInfo* info, size_t block_count)
{
    info->m_block_count = block_count;
    info->m_dom_sets = (woort_Bitset*)malloc(block_count * sizeof(woort_Bitset));
    if (info->m_dom_sets == NULL)
        return false;

    for (size_t i = 0; i < block_count; i++)
    {
        if (!woort_bitset_init(&info->m_dom_sets[i], block_count))
        {
            for (size_t j = 0; j < i; j++)
                woort_bitset_deinit(&info->m_dom_sets[j]);
            free(info->m_dom_sets);
            info->m_dom_sets = NULL;
            return false;
        }
    }
    return true;
}

static void _dominator_info_deinit(_woort_DominatorInfo* info)
{
    for (size_t i = 0; i < info->m_block_count; i++)
        woort_bitset_deinit(&info->m_dom_sets[i]);
    free(info->m_dom_sets);
    info->m_dom_sets = NULL;
}

/*
 * 迭代数据流计算支配者集合 (Cooper-Harvey-Kennedy 风格)
 *
 *   Dom(entry) = {entry}
 *   Dom(B) = {B} ∪ (∩ Dom(pred) for all pred of B)
 */
static bool _compute_dominators(
    woort_IRFunction* f,
    _woort_DominatorInfo* dom_info,
    woort_HashMap* block_index_map,
    size_t block_count)
{
    woort_IRBlock* entry = f->m_entry_block;
    if (entry == NULL)
        return false;

    size_t entry_idx;
    if (!_block_index_map_get(block_index_map, entry, &entry_idx))
        return false;

    /* 初始化: 所有 block 的 dom 集合为全集 */
    for (size_t i = 0; i < block_count; i++)
    {
        for (size_t j = 0; j < block_count; j++)
            woort_bitset_set(&dom_info->m_dom_sets[i], j);
    }

    /* entry 的 dom 集合仅包含自己 */
    woort_bitset_clear(&dom_info->m_dom_sets[entry_idx]);
    woort_bitset_set(&dom_info->m_dom_sets[entry_idx], entry_idx);

    bool changed = true;
    while (changed)
    {
        changed = false;

        for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
             B != NULL;
             B = woort_linklist_next(B))
        {
            size_t B_idx;
            _block_index_map_get(block_index_map, B, &B_idx);

            if (B_idx == entry_idx)
                continue;

            woort_Bitset new_dom;
            if (!woort_bitset_init(&new_dom, block_count))
                return false;

            bool first_pred = true;
            for (size_t i = 0; i < B->m_prev_blocks.m_size; i++)
            {
                woort_IRBlock* pred = *(woort_IRBlock**)woort_vector_at(&B->m_prev_blocks, i);
                size_t pred_idx;
                _block_index_map_get(block_index_map, pred, &pred_idx);

                if (first_pred)
                {
                    _bitset_copy(&new_dom, &dom_info->m_dom_sets[pred_idx]);
                    first_pred = false;
                }
                else
                {
                    _bitset_intersect(&new_dom, &dom_info->m_dom_sets[pred_idx]);
                }
            }

            woort_bitset_set(&new_dom, B_idx);

            if (!_bitset_equal(&new_dom, &dom_info->m_dom_sets[B_idx]))
            {
                _bitset_copy(&dom_info->m_dom_sets[B_idx], &new_dom);
                changed = true;
            }

            woort_bitset_deinit(&new_dom);
        }
    }

    return true;
}

#ifdef _MSC_VER
#include <intrin.h>
static size_t _bitset_popcount(const woort_Bitset* bs)
{
    size_t count = 0;
    for (size_t i = 0; i < bs->m_word_count; i++)
        count += (size_t)__popcnt64(bs->m_data[i]);
    return count;
}
#else
static size_t _bitset_popcount(const woort_Bitset* bs)
{
    size_t count = 0;
    for (size_t i = 0; i < bs->m_word_count; i++)
        count += (size_t)__builtin_popcountll(bs->m_data[i]);
    return count;
}
#endif

/*
 * 从支配者 bitset 集合中提取 idom 并构建支配树。
 *
 * idom(B) 定义为 B 的严格支配者中，自身支配者集合最大（popcount 最大）
 * 的那个。即：idom 是离 B 最"近"的严格支配者。
 *
 * 构建完 idom 后，用 BFS 从 entry 出发计算 m_dom_depth。
 */
static bool _build_dominator_tree(
    woort_IRFunction* f,
    _woort_DominatorInfo* dom_info,
    woort_HashMap* block_index_map,
    woort_IRBlock** index_to_block,
    size_t block_count)
{
    woort_IRBlock* entry = f->m_entry_block;
    size_t entry_idx;
    _block_index_map_get(block_index_map, entry, &entry_idx);

    /* Phase 1: 提取每个 block 的 idom */
    for (size_t B_idx = 0; B_idx < block_count; B_idx++)
    {
        woort_IRBlock* B = index_to_block[B_idx];

        if (B_idx == entry_idx)
        {
            B->m_idom = NULL;
            continue;
        }

        /*
         * 在 B 的支配者集合中找到 popcount 最大的严格支配者。
         * popcount 最大意味着它自身被最多的 block 支配，即它在支配树中
         * 最"深" —— 也就是离 B 最近的支配者。
         */
        woort_IRBlock* idom = NULL;
        size_t max_popcount = 0;

        for (size_t i = 0; i < block_count; i++)
        {
            if (i == B_idx)
                continue;

            /* i 支配 B */
            if (!woort_bitset_test(&dom_info->m_dom_sets[B_idx], i))
                continue;

            /* 严格支配: i 不等于 B（已排除），且 B 不支配 i */
            if (woort_bitset_test(&dom_info->m_dom_sets[i], B_idx))
                continue;

            size_t pc = _bitset_popcount(&dom_info->m_dom_sets[i]);
            if (pc > max_popcount)
            {
                max_popcount = pc;
                idom = index_to_block[i];
            }
        }

        B->m_idom = idom;
        if (idom != NULL)
        {
            if (!woort_vector_push_back(&idom->m_dom_children, 1, &B))
                return false;
        }
    }

    /* Phase 2: BFS 计算 m_dom_depth */
    entry->m_dom_depth = 0;

    woort_Vector bfs_queue;
    woort_vector_init(&bfs_queue, sizeof(woort_IRBlock*));
    if (!woort_vector_push_back(&bfs_queue, 1, &entry))
    {
        woort_vector_deinit(&bfs_queue);
        return false;
    }

    size_t front = 0;
    while (front < bfs_queue.m_size)
    {
        woort_IRBlock* cur = *(woort_IRBlock**)woort_vector_at(&bfs_queue, front);
        front++;

        for (size_t i = 0; i < cur->m_dom_children.m_size; i++)
        {
            woort_IRBlock* child = *(woort_IRBlock**)woort_vector_at(&cur->m_dom_children, i);
            child->m_dom_depth = cur->m_dom_depth + 1;
            if (!woort_vector_push_back(&bfs_queue, 1, &child))
            {
                woort_vector_deinit(&bfs_queue);
                return false;
            }
        }
    }

    woort_vector_deinit(&bfs_queue);
    return true;
}

/* ========== 循环检测 ========== */

static bool _mark_loop_blocks(
    woort_IRBlock* header,
    woort_IRBlock* back_edge_src)
{
    header->m_is_in_loop = true;
    header->m_loop_header = header;

    if (back_edge_src == header)
        return true;

    woort_Vector worklist;
    woort_vector_init(&worklist, sizeof(woort_IRBlock*));

    back_edge_src->m_is_in_loop = true;
    back_edge_src->m_loop_header = header;
    if (!woort_vector_push_back(&worklist, 1, &back_edge_src))
    {
        woort_vector_deinit(&worklist);
        return false;
    }

    while (worklist.m_size > 0)
    {
        woort_IRBlock* current = *(woort_IRBlock**)woort_vector_at(&worklist, worklist.m_size - 1);
        woort_vector_erase_at(&worklist, worklist.m_size - 1);

        for (size_t i = 0; i < current->m_prev_blocks.m_size; i++)
        {
            woort_IRBlock* pred = *(woort_IRBlock**)woort_vector_at(&current->m_prev_blocks, i);

            if (!pred->m_is_in_loop)
            {
                pred->m_is_in_loop = true;
                pred->m_loop_header = header;
                if (!woort_vector_push_back(&worklist, 1, &pred))
                {
                    woort_vector_deinit(&worklist);
                    return false;
                }
            }
        }
    }

    woort_vector_deinit(&worklist);
    return true;
}

/*
 * 检测 back-edge (B -> S 且 S 支配 B) 来识别自然循环。
 */
static bool _detect_loops(
    woort_IRFunction* f,
    _woort_DominatorInfo* dom_info,
    woort_HashMap* block_index_map)
{
    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        woort_IRBlock* successors[2];
        size_t succ_count = _get_successors(B, successors);

        for (size_t i = 0; i < succ_count; i++)
        {
            woort_IRBlock* S = successors[i];

            size_t B_idx, S_idx;
            _block_index_map_get(block_index_map, B, &B_idx);
            _block_index_map_get(block_index_map, S, &S_idx);

            /* S 支配 B => B->S 是 back-edge */
            if (woort_bitset_test(&dom_info->m_dom_sets[B_idx], S_idx))
            {
                if (!_mark_loop_blocks(S, B))
                    return false;
            }
        }
    }

    return true;
}

/* ========== 常量加载放置 ========== */

static /* OPTIONAL */ woort_IRBlock* _find_common_dominator(
    /* OPTIONAL */ woort_IRBlock* a,
    /* OPTIONAL */ woort_IRBlock* b)
{
    if (a == NULL)
        return b;
    if (b == NULL)
        return a;

    while (a != b)
    {
        if (a->m_dom_depth > b->m_dom_depth)
            a = a->m_idom;
        else if (b->m_dom_depth > a->m_dom_depth)
            b = b->m_idom;
        else
        {
            a = a->m_idom;
            b = b->m_idom;
        }
    }

    return a;
}

/*
 * 为一个常量值找到最佳的加载 block：
 *  1. 计算所有 use-block 的公共支配者
 *  2. 如果公共支配者在循环内，提升到循环外（loop header 的 idom）
 */
static /* OPTIONAL */ woort_IRBlock* _find_best_loading_block(
    woort_IRFunction* f,
    _woort_LivenessInfo* liveness,
    woort_IRBlock** index_to_block,
    size_t block_count)
{
    /* 计算所有 use-block 的公共支配者 */
    woort_IRBlock* common_dom = NULL;

    for (size_t i = 0; i < block_count; i++)
    {
        if (woort_bitset_test(&liveness->m_use_blocks, i))
        {
            common_dom = _find_common_dominator(common_dom, index_to_block[i]);
        }
    }

    if (common_dom == NULL)
        return f->m_entry_block;

    /* 循环外提升: 如果公共支配者在循环内，向上走到循环外 */
    while (common_dom->m_is_in_loop && common_dom->m_loop_header != NULL)
    {
        woort_IRBlock* header = common_dom->m_loop_header;
        if (header->m_idom != NULL)
        {
            common_dom = header->m_idom;
        }
        else
        {
            common_dom = f->m_entry_block;
            break;
        }
    }

    return common_dom;
}

/* ==========================================================================
 *  主函数: woort_IRFunction_stack_slot_assign
 *
 *  成功返回 true，*out_stack_space 为分配的栈槽总数（可能为 0）。
 *  发生 OOM 时返回 false。
 * ========================================================================== */

WOORT_NODISCARD bool _woort_IRFunction_stack_slot_assign(
    woort_IRFunction* f, size_t* out_stack_space)
{
    /* =====================================================================
     *  Phase 1: 收集需要分配的 value，建立映射
     * ===================================================================== */

    size_t value_count = 0;
    for (woort_IRValue* v = woort_linklist_iter(&f->m_ir_values);
         v != NULL;
         v = woort_linklist_next(v))
    {
        if (_needs_stack_slot(v))
            value_count++;
    }

    if (value_count == 0)
    {
        *out_stack_space = 0;
        return true;
    }

    _woort_ValueIndexMap idx_map;
    if (!_value_index_map_init(&idx_map, value_count))
    {
        _value_index_map_deinit(&idx_map);
        return false;
    }

    do
    {
        size_t idx = 0;
        for (woort_IRValue* v = woort_linklist_iter(&f->m_ir_values);
             v != NULL;
             v = woort_linklist_next(v))
        {
            if (_needs_stack_slot(v))
            {
                if (!_value_index_map_insert(&idx_map, v, idx))
                {
                    _value_index_map_deinit(&idx_map);
                    return false;
                }
                idx++;
            }
        }
    } while (0);

    /* 统计 block 数量 */
    size_t block_count = 0;
    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        block_count++;
    }

    if (block_count == 0)
    {
        _value_index_map_deinit(&idx_map);
        *out_stack_space = 0;
        return true;
    }

    /* 建立 block index 映射（含反向查找数组） */
    woort_HashMap block_index_map;
    woort_IRBlock** index_to_block = NULL;
    if (!_block_index_map_init(&block_index_map, f, &index_to_block, block_count))
    {
        _value_index_map_deinit(&idx_map);
        return false;
    }

    /* =====================================================================
     *  Phase 2: Phi Coalescing
     *
     *  使用 union-find 将每个 phi 的 result 与其所有 incoming value 合并。
     *  在 SSA 中, phi 的 incoming 来自不同前驱, 不会同时活跃,
     *  因此直接合并是安全的。
     * ===================================================================== */

    _woort_DisjointSet ds;
    if (!_disjoint_set_init(&ds, value_count))
    {
        free(index_to_block);
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);
        return false;
    }

    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        for (woort_IRPhi* phi = woort_linklist_iter(&B->m_phis);
             phi != NULL;
             phi = woort_linklist_next(phi))
        {
            size_t phi_idx;
            if (!_value_index_map_get(&idx_map, phi->m_phi_value, &phi_idx))
                continue;

            for (woort_IRPhi_ReentryRecord* rec = woort_linklist_iter(&phi->m_records);
                 rec != NULL;
                 rec = woort_linklist_next(rec))
            {
                size_t rec_idx;
                if (_value_index_map_get(&idx_map, rec->m_value, &rec_idx))
                    _disjoint_set_union(&ds, phi_idx, rec_idx);
            }
        }
    }

    /* =====================================================================
     *  Phase 3: 活跃性分析
     * ===================================================================== */

    /* 分配 block liveness 数组 */
    _woort_BlockLiveness* block_liveness =
        (_woort_BlockLiveness*)malloc(block_count * sizeof(_woort_BlockLiveness));
    if (block_liveness == NULL)
        goto fail_after_ds;

    /* 分配 per-value liveness 数组 */
    _woort_LivenessInfo* liveness =
        (_woort_LivenessInfo*)malloc(value_count * sizeof(_woort_LivenessInfo));
    if (liveness == NULL)
    {
        free(block_liveness);
        goto fail_after_ds;
    }

    /* 初始化 per-value liveness */
    {
        size_t init_count = 0;
        bool ok = true;
        for (size_t i = 0; i < value_count; i++)
        {
            liveness[i].m_first_def = SIZE_MAX;
            liveness[i].m_last_use = 0;
            liveness[i].m_first_use_block = NULL;
            if (!woort_bitset_init(&liveness[i].m_use_blocks, block_count))
            {
                ok = false;
                break;
            }
            init_count++;
        }
        if (!ok)
        {
            for (size_t j = 0; j < init_count; j++)
                woort_bitset_deinit(&liveness[j].m_use_blocks);
            free(liveness);
            free(block_liveness);
            goto fail_after_ds;
        }
    }

    /* 初始化 block liveness */
    {
        size_t init_count = 0;
        bool ok = true;
        for (size_t i = 0; i < block_count; i++)
        {
            if (!_block_liveness_init(&block_liveness[i], value_count))
            {
                ok = false;
                break;
            }
            init_count++;
        }
        if (!ok)
        {
            for (size_t j = 0; j < init_count; j++)
                _block_liveness_deinit(&block_liveness[j]);
            /* liveness 完全初始化了 (init_count == value_count for liveness) */
            for (size_t j = 0; j < value_count; j++)
                woort_bitset_deinit(&liveness[j].m_use_blocks);
            free(liveness);
            free(block_liveness);
            goto fail_after_ds;
        }
    }

    /* ----- 3a: 计算 local USE/DEF 集合 + per-value liveness info ----- */
    {
        size_t global_inst_idx = 0;

        for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
             B != NULL;
             B = woort_linklist_next(B))
        {
            size_t block_idx;
            _block_index_map_get(&block_index_map, B, &block_idx);
            _woort_BlockLiveness* bl = &block_liveness[block_idx];

            /* Phi 定义: phi_value 被定义在 block 入口 */
            for (woort_IRPhi* phi = woort_linklist_iter(&B->m_phis);
                 phi != NULL;
                 phi = woort_linklist_next(phi))
            {
                size_t phi_idx;
                if (_value_index_map_get(&idx_map, phi->m_phi_value, &phi_idx))
                {
                    woort_bitset_set(&bl->m_def, phi_idx);
                    if (liveness[phi_idx].m_first_def > global_inst_idx)
                        liveness[phi_idx].m_first_def = global_inst_idx;
                }
            }

            /* 遍历每条指令: 先读后写 (woort 保证写入在读取之后) */
            for (woort_IROp* op = woort_linklist_iter(&B->m_operates);
                 op != NULL;
                 op = woort_linklist_next(op))
            {
                global_inst_idx++;

                /* 读操作数 */
                for (int i = 0; i < 3; i++)
                {
                    if (op->m_r[i] != NULL)
                    {
                        size_t r_idx;
                        if (_value_index_map_get(&idx_map, (woort_IRValue*)op->m_r[i], &r_idx))
                        {
                            if (!woort_bitset_test(&bl->m_def, r_idx))
                                woort_bitset_set(&bl->m_use, r_idx);
                            liveness[r_idx].m_last_use = global_inst_idx;
                            woort_bitset_set(&liveness[r_idx].m_use_blocks, block_idx);
                            if (liveness[r_idx].m_first_use_block == NULL)
                                liveness[r_idx].m_first_use_block = B;
                        }
                    }
                }

                /* 写操作数 */
                if (op->m_w != NULL)
                {
                    size_t w_idx;
                    if (_value_index_map_get(&idx_map, (woort_IRValue*)op->m_w, &w_idx))
                    {
                        woort_bitset_set(&bl->m_def, w_idx);
                        if (liveness[w_idx].m_first_def > global_inst_idx)
                            liveness[w_idx].m_first_def = global_inst_idx;
                    }
                }
            }

            /* Terminator 使用 */
            global_inst_idx++;
            _analyze_terminator_uses(B, bl, &idx_map, liveness, block_idx, global_inst_idx);

            /* Phi incoming value 的使用 (在前驱 block 末尾) */
            _analyze_phi_incoming_uses(B, bl, &idx_map, liveness,
                                       &block_index_map, block_idx, global_inst_idx);
        }
    }

    /* ----- 3b: 迭代数据流计算 LIVE_IN / LIVE_OUT (固定点) ----- */
    /*
     *  LIVE_OUT(B) = ∪ LIVE_IN(S)  for each successor S
     *              ∪ { phi incoming values in successors coming from B }
     *  LIVE_IN(B)  = USE(B) ∪ (LIVE_OUT(B) - DEF(B))
     */
    {
        bool changed = true;
        bool error = false;

        while (changed && !error)
        {
            changed = false;

            for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
                 B != NULL && !error;
                 B = woort_linklist_next(B))
            {
                size_t block_idx;
                _block_index_map_get(&block_index_map, B, &block_idx);
                _woort_BlockLiveness* bl = &block_liveness[block_idx];

                woort_Bitset new_live_out;
                if (!woort_bitset_init(&new_live_out, value_count))
                {
                    error = true;
                    break;
                }

                /* ∪ LIVE_IN(S) */
                woort_IRBlock* successors[2];
                size_t succ_count = _get_successors(B, successors);

                for (size_t i = 0; i < succ_count; i++)
                {
                    size_t succ_idx;
                    if (_block_index_map_get(&block_index_map, successors[i], &succ_idx))
                    {
                        _bitset_union(&new_live_out, &block_liveness[succ_idx].m_live_in);
                    }
                }

                /* ∪ phi incoming values from B */
                for (size_t i = 0; i < succ_count; i++)
                {
                    for (woort_IRPhi* phi = woort_linklist_iter(&successors[i]->m_phis);
                         phi != NULL;
                         phi = woort_linklist_next(phi))
                    {
                        for (woort_IRPhi_ReentryRecord* rec = woort_linklist_iter(&phi->m_records);
                             rec != NULL;
                             rec = woort_linklist_next(rec))
                        {
                            if (rec->m_from_block == B)
                            {
                                size_t rec_idx;
                                if (_value_index_map_get(&idx_map, rec->m_value, &rec_idx))
                                    woort_bitset_set(&new_live_out, rec_idx);
                            }
                        }
                    }
                }

                /* LIVE_IN = USE ∪ (LIVE_OUT - DEF) */
                woort_Bitset new_live_in;
                if (!woort_bitset_init(&new_live_in, value_count))
                {
                    woort_bitset_deinit(&new_live_out);
                    error = true;
                    break;
                }

                _bitset_copy(&new_live_in, &bl->m_use);
                for (size_t i = 0; i < new_live_out.m_word_count && i < new_live_in.m_word_count; i++)
                {
                    new_live_in.m_data[i] |= (new_live_out.m_data[i] & ~bl->m_def.m_data[i]);
                }

                if (!_bitset_equal(&new_live_in, &bl->m_live_in) ||
                    !_bitset_equal(&new_live_out, &bl->m_live_out))
                {
                    changed = true;
                    _bitset_copy(&bl->m_live_in, &new_live_in);
                    _bitset_copy(&bl->m_live_out, &new_live_out);
                }

                woort_bitset_deinit(&new_live_in);
                woort_bitset_deinit(&new_live_out);
            }
        }

        if (error)
            goto fail_after_liveness;
    }

    /* 修正未定义的 first_def (常量等可能只被使用、无显式定义) */
    for (size_t i = 0; i < value_count; i++)
    {
        if (liveness[i].m_first_def == SIZE_MAX)
            liveness[i].m_first_def = 0;
    }

    /* =====================================================================
     *  Phase 4: 栈槽分配 (基于活跃区间的线性扫描)
     * ===================================================================== */
    {
        /*
         * rep_slot[i]:     representative i 被分配的 slot (-1 表示尚未分配)
         * rep_def[i]:      representative i 的合并 first_def
         * rep_last_use[i]: representative i 的合并 last_use
         */
        int32_t* rep_slot = (int32_t*)malloc(value_count * sizeof(int32_t));
        size_t* rep_def = (size_t*)malloc(value_count * sizeof(size_t));
        size_t* rep_last_use = (size_t*)malloc(value_count * sizeof(size_t));

        if (rep_slot == NULL || rep_def == NULL || rep_last_use == NULL)
        {
            free(rep_slot);
            free(rep_def);
            free(rep_last_use);
            goto fail_after_liveness;
        }

        for (size_t i = 0; i < value_count; i++)
        {
            rep_slot[i] = -1;
            rep_def[i] = SIZE_MAX;
            rep_last_use[i] = 0;
        }

        /* 聚合: 将每个 value 的活跃区间合并到其 representative */
        for (size_t i = 0; i < value_count; i++)
        {
            size_t rep = _disjoint_set_find(&ds, i);

            if (rep_def[rep] > liveness[i].m_first_def)
                rep_def[rep] = liveness[i].m_first_def;
            if (rep_last_use[rep] < liveness[i].m_last_use)
                rep_last_use[rep] = liveness[i].m_last_use;
        }

        /*
         * 已分配 slot 的列表 (仅 representative 级别)
         * 用于查找可复用的 slot
         */
        _woort_AllocatedSlot* allocated =
            (_woort_AllocatedSlot*)malloc(value_count * sizeof(_woort_AllocatedSlot));
        size_t allocated_count = 0;
        int32_t next_slot = 0;

        if (allocated == NULL)
        {
            free(rep_slot);
            free(rep_def);
            free(rep_last_use);
            goto fail_after_liveness;
        }

        /* 为每个 value 分配 slot */
        for (size_t i = 0; i < value_count; i++)
        {
            size_t rep = _disjoint_set_find(&ds, i);

            if (rep_slot[rep] != -1)
            {
                /* representative 已有 slot，直接赋值 */
                idx_map.m_index_to_value[i]->m_assigned_stack_offset = rep_slot[rep];
                continue;
            }

            /* 尝试复用已分配的 slot */
            int32_t slot = _find_reusable_slot(
                allocated, allocated_count,
                rep_def[rep], rep_last_use[rep]);

            if (slot == -1)
            {
                /* 分配新 slot */
                slot = next_slot;
                next_slot--;
            }
            else
            {
                /* 复用: 更新该 slot 的活跃区间为并集 */
                for (size_t j = 0; j < allocated_count; j++)
                {
                    if (allocated[j].m_slot == slot)
                    {
                        if (allocated[j].m_def > rep_def[rep])
                            allocated[j].m_def = rep_def[rep];
                        if (allocated[j].m_last_use < rep_last_use[rep])
                            allocated[j].m_last_use = rep_last_use[rep];
                        break;
                    }
                }
            }

            rep_slot[rep] = slot;
            idx_map.m_index_to_value[i]->m_assigned_stack_offset = slot;

            /* 如果是新分配的 slot，加入已分配列表 */
            if (slot == next_slot + 1)
            {
                /* 刚刚 next_slot-- 了，所以 next_slot + 1 == 原来的 next_slot == slot */
                allocated[allocated_count].m_slot = slot;
                allocated[allocated_count].m_def = rep_def[rep];
                allocated[allocated_count].m_last_use = rep_last_use[rep];
                allocated_count++;
            }
        }

        size_t slot_count = (size_t)(-next_slot);

        free(allocated);
        free(rep_slot);
        free(rep_def);
        free(rep_last_use);

        /* =====================================================================
         *  Phase 5: 支配树 + 循环检测
         * ===================================================================== */

        _woort_DominatorInfo dom_info;
        if (_dominator_info_init(&dom_info, block_count))
        {
            if (_compute_dominators(f, &dom_info, &block_index_map, block_count))
            {
                if (!_build_dominator_tree(f, &dom_info, &block_index_map,
                                           index_to_block, block_count))
                {
                    _dominator_info_deinit(&dom_info);
                    goto fail_after_liveness;
                }

                if (!_detect_loops(f, &dom_info, &block_index_map))
                {
                    _dominator_info_deinit(&dom_info);
                    goto fail_after_liveness;
                }

                /* =============================================================
                 *  Phase 6: 常量加载放置
                 *
                 *  对每个需要栈槽的常量，找到最佳加载 block:
                 *    - 计算所有 use-block 的公共支配者
                 *    - 如果在循环内，提升到循环外
                 *    - 存入 block 的 m_loading_constants
                 * ============================================================= */

                for (size_t i = 0; i < value_count; i++)
                {
                    woort_IRValue* v = idx_map.m_index_to_value[i];
                    if (v->m_source != WOORT_IRVALUE_SOURCE_CONSTANT ||
                        !v->m_constant_need_stack_slot)
                        continue;

                    woort_IRBlock* loading_block = _find_best_loading_block(
                        f, &liveness[i], index_to_block, block_count);

                    if (loading_block != NULL)
                    {
                        if (!woort_vector_push_back(&loading_block->m_loading_constants, 1, &v))
                        {
                            _dominator_info_deinit(&dom_info);
                            goto fail_after_liveness;
                        }
                    }
                }
            }
            _dominator_info_deinit(&dom_info);
        }

        /* =====================================================================
         *  Phase 7: 清理
         * ===================================================================== */

        for (size_t i = 0; i < value_count; i++)
            woort_bitset_deinit(&liveness[i].m_use_blocks);
        free(liveness);
        for (size_t i = 0; i < block_count; i++)
            _block_liveness_deinit(&block_liveness[i]);
        free(block_liveness);
        _disjoint_set_deinit(&ds);
        free(index_to_block);
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);

        *out_stack_space = slot_count;
        return true;
    }

    /* 错误处理: 统一 cleanup 路径 */

fail_after_liveness:
    for (size_t i = 0; i < value_count; i++)
        woort_bitset_deinit(&liveness[i].m_use_blocks);
    free(liveness);
    for (size_t i = 0; i < block_count; i++)
        _block_liveness_deinit(&block_liveness[i]);
    free(block_liveness);

fail_after_ds:
    _disjoint_set_deinit(&ds);
    free(index_to_block);
    woort_hashmap_deinit(&block_index_map);
    _value_index_map_deinit(&idx_map);
    return false;
}
