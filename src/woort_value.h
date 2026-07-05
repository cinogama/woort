#pragma once

/*
woort_value.h
*/

#include "woort_value_types.h"

#include "woort_gc_units.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static inline woort_BoxedValue _woort_gcunit_to_boxed(
    /* OPTIONAL */ woort_GCUnit* ptr)
{
    return (woort_BoxedValue)(uintptr_t)ptr;
}

static inline /* OPTIONAL */ woort_GCUnit* _woort_boxed_to_gcunit(
    woort_BoxedValue val)
{
    return (woort_GCUnit*)(uintptr_t)val;
}

static inline struct woort_BoxedExValue* _woort_boxed_to_exvalue(
    woort_BoxedValue val)
{
    return (struct woort_BoxedExValue*)(uintptr_t)val;
}

woort_DynBox woort_DynBox_box_real(woort_Real val);
woort_DynBox woort_DynBox_box_int(woort_Int val);
woort_DynBox woort_DynBox_box_bool(bool val);
woort_DynBox woort_DynBox_box(woort_Value val, woort_BoxValueType type);

woort_DynBox woort_DynBox_box_real_for_env_constant(woort_CodeEnv* cenv, woort_Real val);
woort_DynBox woort_DynBox_box_int_for_env_constant(woort_CodeEnv* cenv, woort_Int val);

////////////////////////////////////////////////////////////////////////
// 带混合写屏障的装箱函数：用于向 GC 管理的内存中写入装箱值
////////////////////////////////////////////////////////////////////////

void woort_DynBox_box_real_with_barrier(woort_DynBox* dst, woort_Real val);
void woort_DynBox_box_int_with_barrier(woort_DynBox* dst, woort_Int val);
void woort_DynBox_box_bool_with_barrier(woort_DynBox* dst, bool val);
void woort_DynBox_box_with_barrier(woort_DynBox* dst, woort_Value val, woort_BoxValueType type);

WOORT_NODISCARD bool woort_DynBox_check(
    woort_DynBox val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type);

WOORT_NODISCARD bool woort_DynBox_unbox(
    woort_DynBox val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type,
    woort_Value* out_val);

void woort_DynBox_unbox_no_check(
    woort_DynBox val,
    woort_Value* out_val);

WOORT_NODISCARD woort_BoxValueType
woort_DynBox_unbox_no_check_and_get_type(
    woort_DynBox val,
    woort_Value* out_val);

WOORT_NODISCARD size_t woort_DynBox_hash(woort_DynBox val);
WOORT_NODISCARD bool woort_DynBox_equal(woort_DynBox a, woort_DynBox b);

WOORT_NODISCARD woort_DynBox _woort_DynBox_make_dup_boxed(woort_DynBox v_m_s_to_dup);

////////////////////////////////////////////////////////////////////////
// 内部函数：用于类型特化操作，避免装箱分配
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD size_t _woort_hash_int(woort_Int val);
WOORT_NODISCARD size_t _woort_hash_real(woort_Real val);

// 解装箱函数
WOORT_NODISCARD double _woort_unbox_float64(woort_BoxedFloat63 val);
WOORT_NODISCARD woort_Int _woort_unbox_int64(woort_BoxedInt62 val);
WOORT_NODISCARD bool _woort_unbox_bool(uint64_t val);

// 扩展装箱类型的 proxy
extern const woort_GCUnitProxy WOORT_EX_BOX_PROXY;

////////////////////////////////////////////////////////////////////////
// 类型特化的比较函数：避免装箱分配
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD bool woort_DynBox_equal_int(woort_DynBox boxed_key, woort_Int int_key);
WOORT_NODISCARD bool woort_DynBox_equal_real(woort_DynBox boxed_key, woort_Real real_key);
WOORT_NODISCARD bool woort_DynBox_equal_bool(woort_DynBox boxed_key, bool bool_key);
WOORT_NODISCARD bool woort_DynBox_equal_gcunit(woort_DynBox boxed_key, /* OPTIONAL */ woort_GCUnit* gcunit_key);
WOORT_NODISCARD bool woort_DynBox_equal_string(woort_DynBox boxed_key, const char* str, size_t len);

WOORT_NODISCARD bool woort_DynBox_debug_check_is_valid(woort_DynBox may_not_a_valid_box);
