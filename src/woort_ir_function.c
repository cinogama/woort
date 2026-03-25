#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_bitset.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

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

/* ========== Stack Slot Assignment Internal Structures ========== */

typedef struct {
    woort_HashMap m_value_to_index;
    woort_IRValue** m_index_to_value;
    size_t m_count;
} _woort_ValueIndexMap;

typedef struct {
    size_t m_first_def;
    size_t m_last_use;
} _woort_LivenessInfo;

typedef struct {
    woort_Bitset m_use;
    woort_Bitset m_def;
    woort_Bitset m_live_in;
    woort_Bitset m_live_out;
} _woort_BlockLiveness;

typedef struct {
    size_t* m_parent;
    size_t m_count;
} _woort_DisjointSet;

typedef struct {
    int32_t* m_rep_slot;
    size_t* m_rep_def;
    size_t* m_rep_last_use;
    int32_t m_next_slot;
    size_t m_value_count;
} _woort_SlotAssignResult;

/* ========== Helper Functions ========== */

static size_t _pointer_hash(const void* key)
{
    return (size_t)(*(const void* const*)key);
}

static bool _pointer_equal(const void* key1, const void* key2)
{
    return *(const void* const*)key1 == *(const void* const*)key2;
}

static bool _needs_stack_slot(const woort_IRValue* v)
{
    if (v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
        return false;

    if (v->m_source == WOORT_IRVALUE_SOURCE_CONSTANT && !v->m_constant_need_stack_slot)
        return false;

    return true;
}

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
        woort_hashmap_deinit(&map->m_value_to_index);
        return false;
    }

    map->m_count = 0;
    return true;
}

static void _value_index_map_deinit(_woort_ValueIndexMap* map)
{
    woort_hashmap_deinit(&map->m_value_to_index);
    free(map->m_index_to_value);
    map->m_index_to_value = NULL;
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

static bool _value_index_map_get(const _woort_ValueIndexMap* map, woort_IRValue* v, size_t* out_index)
{
    return woort_hashmap_find(&map->m_value_to_index, &v, (void**)out_index);
}

static bool _block_liveness_init(_woort_BlockLiveness* bl, size_t value_count)
{
    if (!woort_bitset_init(&bl->m_use, value_count))
        return false;
    if (!woort_bitset_init(&bl->m_def, value_count))
    {
        woort_bitset_deinit(&bl->m_use);
        return false;
    }
    if (!woort_bitset_init(&bl->m_live_in, value_count))
    {
        woort_bitset_deinit(&bl->m_use);
        woort_bitset_deinit(&bl->m_def);
        return false;
    }
    if (!woort_bitset_init(&bl->m_live_out, value_count))
    {
        woort_bitset_deinit(&bl->m_use);
        woort_bitset_deinit(&bl->m_def);
        woort_bitset_deinit(&bl->m_live_in);
        return false;
    }
    return true;
}

static void _block_liveness_deinit(_woort_BlockLiveness* bl)
{
    woort_bitset_deinit(&bl->m_use);
    woort_bitset_deinit(&bl->m_def);
    woort_bitset_deinit(&bl->m_live_in);
    woort_bitset_deinit(&bl->m_live_out);
}

static size_t _get_successors(const woort_IRBlock* B, /* OPTIONAL */ woort_IRBlock* successors[2])
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
        count = 0;
        break;

    default:
        count = 0;
        break;
    }

    return count;
}

static bool _block_index_map_init(woort_HashMap* map, woort_IRFunction* f)
{
    woort_hashmap_init(
        map,
        sizeof(woort_IRBlock*),
        sizeof(size_t),
        _pointer_hash,
        _pointer_equal);

    size_t idx = 0;
    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        if (woort_hashmap_insert(map, &B, &idx) != WOORT_HASHMAP_RESULT_OK)
        {
            woort_hashmap_deinit(map);
            return false;
        }
        idx++;
    }

    return true;
}

static bool _block_index_map_get(const woort_HashMap* map, woort_IRBlock* B, size_t* out_index)
{
    return woort_hashmap_find(map, &B, (void**)out_index);
}

static void _bitset_union(woort_Bitset* dst, const woort_Bitset* src)
{
    size_t word_count = dst->m_word_count < src->m_word_count ? dst->m_word_count : src->m_word_count;
    for (size_t i = 0; i < word_count; i++)
        dst->m_data[i] |= src->m_data[i];
}

static void _bitset_copy(woort_Bitset* dst, const woort_Bitset* src)
{
    size_t word_count = dst->m_word_count < src->m_word_count ? dst->m_word_count : src->m_word_count;
    for (size_t i = 0; i < word_count; i++)
        dst->m_data[i] = src->m_data[i];
    for (size_t i = word_count; i < dst->m_word_count; i++)
        dst->m_data[i] = 0;
}

static bool _bitset_equal(const woort_Bitset* a, const woort_Bitset* b)
{
    size_t word_count = a->m_word_count < b->m_word_count ? a->m_word_count : b->m_word_count;
    for (size_t i = 0; i < word_count; i++)
    {
        if (a->m_data[i] != b->m_data[i])
            return false;
    }
    for (size_t i = word_count; i < a->m_word_count; i++)
    {
        if (a->m_data[i] != 0)
            return false;
    }
    for (size_t i = word_count; i < b->m_word_count; i++)
    {
        if (b->m_data[i] != 0)
            return false;
    }
    return true;
}

static void _analyze_terminator_uses(
    woort_IRBlock* B,
    _woort_BlockLiveness* bl,
    _woort_ValueIndexMap* idx_map,
    _woort_LivenessInfo* liveness,
    size_t* global_inst_idx)
{
    woort_IRValue* values_to_check[3] = { NULL, NULL, NULL };
    size_t value_count = 0;

    switch (B->m_cond_type)
    {
    case WOORT_IRBLOCK_ENDWAY_BR_COND:
        values_to_check[0] = B->m_br_cond_value;
        value_count = 1;
        break;

    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
        values_to_check[0] = B->m_br_compare_values[0];
        values_to_check[1] = B->m_br_compare_values[1];
        value_count = 2;
        break;

    case WOORT_IRBLOCK_ENDWAY_RET:
        values_to_check[0] = B->m_ret_value_may_null;
        value_count = 1;
        break;

    default:
        value_count = 0;
        break;
    }

    for (size_t i = 0; i < value_count; i++)
    {
        if (values_to_check[i] != NULL)
        {
            size_t v_idx;
            if (_value_index_map_get(idx_map, values_to_check[i], &v_idx))
            {
                if (!woort_bitset_test(&bl->m_def, v_idx))
                    woort_bitset_set(&bl->m_use, v_idx);
                liveness[v_idx].m_last_use = *global_inst_idx;
            }
        }
    }
}

static int32_t _find_reusable_slot(
    _woort_SlotAssignResult* result,
    size_t rep)
{
    for (size_t i = 0; i < result->m_value_count; i++)
    {
        if (result->m_rep_slot[i] != -1 && result->m_rep_slot[i] <= 0)
        {
            size_t def_a = result->m_rep_def[rep];
            size_t last_use_a = result->m_rep_last_use[rep];
            size_t def_b = result->m_rep_def[i];
            size_t last_use_b = result->m_rep_last_use[i];

            if (last_use_b < def_a || last_use_a < def_b)
                return result->m_rep_slot[i];
        }
    }
    return -1;
}

/* ========== Main Implementation ========== */

WOORT_NODISCARD size_t woort_IRFunction_stack_slot_assign(woort_IRFunction* f)
{
    size_t value_count = 0;
    for (woort_IRValue* v = woort_linklist_iter(&f->m_ir_values);
         v != NULL;
         v = woort_linklist_next(v))
    {
        if (_needs_stack_slot(v))
            value_count++;
    }

    if (value_count == 0)
        return 0;

    _woort_ValueIndexMap idx_map;
    if (!_value_index_map_init(&idx_map, value_count))
        return 0;

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
                return 0;
            }
            idx++;
        }
    }

    size_t block_count = 0;
    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        block_count++;
    }

    woort_HashMap block_index_map;
    if (!_block_index_map_init(&block_index_map, f))
    {
        _value_index_map_deinit(&idx_map);
        return 0;
    }

    _woort_DisjointSet ds;
    if (!_disjoint_set_init(&ds, value_count))
    {
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);
        return 0;
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

    _woort_BlockLiveness* block_liveness = (_woort_BlockLiveness*)malloc(block_count * sizeof(_woort_BlockLiveness));
    if (block_liveness == NULL)
    {
        _disjoint_set_deinit(&ds);
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);
        return 0;
    }

    _woort_LivenessInfo* liveness = (_woort_LivenessInfo*)malloc(value_count * sizeof(_woort_LivenessInfo));
    if (liveness == NULL)
    {
        free(block_liveness);
        _disjoint_set_deinit(&ds);
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);
        return 0;
    }

    for (size_t i = 0; i < value_count; i++)
    {
        liveness[i].m_first_def = SIZE_MAX;
        liveness[i].m_last_use = 0;
    }

    bool init_success = true;
    for (size_t i = 0; i < block_count; i++)
    {
        if (!_block_liveness_init(&block_liveness[i], value_count))
        {
            for (size_t j = 0; j < i; j++)
                _block_liveness_deinit(&block_liveness[j]);
            init_success = false;
            break;
        }
    }

    if (!init_success)
    {
        free(liveness);
        free(block_liveness);
        _disjoint_set_deinit(&ds);
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);
        return 0;
    }

    size_t global_inst_idx = 0;
    for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
         B != NULL;
         B = woort_linklist_next(B))
    {
        size_t block_idx;
        _block_index_map_get(&block_index_map, B, &block_idx);
        _woort_BlockLiveness* bl = &block_liveness[block_idx];

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

        for (woort_IROp* op = woort_linklist_iter(&B->m_operates);
             op != NULL;
             op = woort_linklist_next(op))
        {
            global_inst_idx++;

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
                    }
                }
            }

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

        _analyze_terminator_uses(B, bl, &idx_map, liveness, &global_inst_idx);
    }

    bool changed = true;
    while (changed)
    {
        changed = false;

        for (woort_IRBlock* B = woort_linklist_iter(&f->m_ir_blocks);
             B != NULL;
             B = woort_linklist_next(B))
        {
            size_t block_idx;
            _block_index_map_get(&block_index_map, B, &block_idx);
            _woort_BlockLiveness* bl = &block_liveness[block_idx];

            woort_Bitset new_live_out;
            if (!woort_bitset_init(&new_live_out, value_count))
                continue;

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

            woort_Bitset new_live_in;
            if (!woort_bitset_init(&new_live_in, value_count))
            {
                woort_bitset_deinit(&new_live_out);
                continue;
            }

            _bitset_copy(&new_live_in, &bl->m_use);

            for (size_t i = 0; i < value_count; i++)
            {
                if (woort_bitset_test(&new_live_out, i) && !woort_bitset_test(&bl->m_def, i))
                    woort_bitset_set(&new_live_in, i);
            }

            if (!_bitset_equal(&new_live_in, &bl->m_live_in) || !_bitset_equal(&new_live_out, &bl->m_live_out))
            {
                changed = true;
                _bitset_copy(&bl->m_live_in, &new_live_in);
                _bitset_copy(&bl->m_live_out, &new_live_out);
            }

            woort_bitset_deinit(&new_live_in);
            woort_bitset_deinit(&new_live_out);
        }
    }

    for (size_t i = 0; i < value_count; i++)
    {
        if (liveness[i].m_first_def == SIZE_MAX)
            liveness[i].m_first_def = 0;
    }

    _woort_SlotAssignResult result;
    result.m_rep_slot = (int32_t*)malloc(value_count * sizeof(int32_t));
    result.m_rep_def = (size_t*)malloc(value_count * sizeof(size_t));
    result.m_rep_last_use = (size_t*)malloc(value_count * sizeof(size_t));
    result.m_next_slot = 0;
    result.m_value_count = value_count;

    if (result.m_rep_slot == NULL || result.m_rep_def == NULL || result.m_rep_last_use == NULL)
    {
        free(result.m_rep_slot);
        free(result.m_rep_def);
        free(result.m_rep_last_use);
        free(liveness);
        for (size_t i = 0; i < block_count; i++)
            _block_liveness_deinit(&block_liveness[i]);
        free(block_liveness);
        _disjoint_set_deinit(&ds);
        woort_hashmap_deinit(&block_index_map);
        _value_index_map_deinit(&idx_map);
        return 0;
    }

    for (size_t i = 0; i < value_count; i++)
    {
        result.m_rep_slot[i] = -1;
        result.m_rep_def[i] = SIZE_MAX;
        result.m_rep_last_use[i] = 0;
    }

    for (size_t i = 0; i < value_count; i++)
    {
        size_t rep = _disjoint_set_find(&ds, i);

        if (result.m_rep_def[rep] > liveness[i].m_first_def)
            result.m_rep_def[rep] = liveness[i].m_first_def;

        if (result.m_rep_last_use[rep] < liveness[i].m_last_use)
            result.m_rep_last_use[rep] = liveness[i].m_last_use;
    }

    for (size_t i = 0; i < value_count; i++)
    {
        size_t rep = _disjoint_set_find(&ds, i);

        if (result.m_rep_slot[rep] != -1)
        {
            idx_map.m_index_to_value[i]->m_assigned_stack_offset = result.m_rep_slot[rep];
            continue;
        }

        int32_t slot = _find_reusable_slot(&result, rep);

        if (slot == -1)
        {
            slot = result.m_next_slot;
            result.m_next_slot--;
        }

        result.m_rep_slot[rep] = slot;
        idx_map.m_index_to_value[i]->m_assigned_stack_offset = slot;
    }

    size_t slot_count = (size_t)(-result.m_next_slot);

    free(result.m_rep_slot);
    free(result.m_rep_def);
    free(result.m_rep_last_use);
    free(liveness);
    for (size_t i = 0; i < block_count; i++)
        _block_liveness_deinit(&block_liveness[i]);
    free(block_liveness);
    _disjoint_set_deinit(&ds);
    woort_hashmap_deinit(&block_index_map);
    _value_index_map_deinit(&idx_map);

    return slot_count;
}
