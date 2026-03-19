#pragma once

/*
 * woort_ir_builder.h
 */

#include "woort_ir_module.h"
#include "woort_ir_local.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct woort_IRBuilder
{
    woort_IRFunction*       m_function;
    woort_IRBlock*          m_insert_block;

} woort_IRBuilder;

WOORT_NODISCARD bool woort_IRBuilder_create(
    woort_IRFunction* func,
    woort_IRBuilder** out_builder);

void woort_IRBuilder_destroy(woort_IRBuilder* builder);

WOORT_NODISCARD bool woort_IRBuilder_create_block(
    woort_IRBuilder* builder,
    woort_IRBlock** out_block);

void woort_IRBuilder_position_at_end(woort_IRBuilder* builder, woort_IRBlock* block);

woort_IRBlock* woort_IRBuilder_get_insert_block(woort_IRBuilder* builder);

void woort_IRBlock_seal(woort_IRBlock* block);

WOORT_NODISCARD bool woort_IRBuilder_create_local(
    woort_IRBuilder* builder,
    woort_IRLocal** out_local);

void woort_IRBuilder_set_local(
    woort_IRBuilder* builder,
    woort_IRLocal* local,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_get_local(
    woort_IRBuilder* builder,
    woort_IRLocal* local,
    woort_IRValue** out_value);

void woort_IRBuilder_ret_void(woort_IRBuilder* builder);

void woort_IRBuilder_ret(woort_IRBuilder* builder, woort_IRValue* value);

void woort_IRBuilder_br(woort_IRBuilder* builder, woort_IRBlock* dest);

WOORT_NODISCARD bool woort_IRBuilder_cond_br(
    woort_IRBuilder* builder,
    woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

WOORT_NODISCARD bool woort_IRBuilder_add_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_sub_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mul_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_div_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mod_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_neg_i(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_add_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_sub_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mul_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_div_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mod_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_neg_r(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_lt_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_le_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_gt_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ge_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_eq_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ne_i(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_lt_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_le_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_gt_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ge_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_eq_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ne_r(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_add_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_lt_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_le_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_gt_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ge_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_eq_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ne_s(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_and(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_or(
    woort_IRBuilder* builder,
    woort_IRValue* lhs,
    woort_IRValue* rhs,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_not(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_const_int(
    woort_IRBuilder* builder,
    int64_t value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_const_real(
    woort_IRBuilder* builder,
    double value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_const_bool(
    woort_IRBuilder* builder,
    bool value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_const_str(
    woort_IRBuilder* builder,
    const char* str,
    size_t len,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_const_null(
    woort_IRBuilder* builder,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_const_func(
    woort_IRBuilder* builder,
    uint32_t func_id,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_param(
    woort_IRBuilder* builder,
    uint32_t index,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_cast_i_to_r(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_cast_r_to_i(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_box_dyn(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_unbox_dyn(
    woort_IRBuilder* builder,
    woort_IRValue* value,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mkvec(
    woort_IRBuilder* builder,
    woort_IRValue** elems,
    uint32_t count,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mkmap(
    woort_IRBuilder* builder,
    woort_IRValue** kvs,
    uint32_t count,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_mkstruct(
    woort_IRBuilder* builder,
    woort_IRValue** fields,
    uint32_t count,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldvec(
    woort_IRBuilder* builder,
    woort_IRValue* vec,
    woort_IRValue* index,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldstr(
    woort_IRBuilder* builder,
    woort_IRValue* str,
    woort_IRValue* index,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldstruct(
    woort_IRBuilder* builder,
    woort_IRValue* st,
    uint32_t field_index,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldmap_i(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldmap_r(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldmap_b(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_ldmap_x(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue** out_result);

WOORT_NODISCARD bool woort_IRBuilder_stvec_i(
    woort_IRBuilder* builder,
    woort_IRValue* vec,
    woort_IRValue* index,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stvec_r(
    woort_IRBuilder* builder,
    woort_IRValue* vec,
    woort_IRValue* index,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stvec_b(
    woort_IRBuilder* builder,
    woort_IRValue* vec,
    woort_IRValue* index,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stvec_x(
    woort_IRBuilder* builder,
    woort_IRValue* vec,
    woort_IRValue* index,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_i(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_r(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_b(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_x(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_i(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_r(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_b(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_x(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_i(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_r(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_b(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_x(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_i(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_r(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_b(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_x(
    woort_IRBuilder* builder,
    woort_IRValue* map,
    woort_IRValue* key,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_ststruct(
    woort_IRBuilder* builder,
    woort_IRValue* st,
    uint32_t field_index,
    woort_IRValue* value);

WOORT_NODISCARD bool woort_IRBuilder_call(
    woort_IRBuilder* builder,
    woort_IRValue* func,
    woort_IRValue** args,
    uint32_t arg_count,
    /* OPTIONAL */ woort_IRValue** out_result);