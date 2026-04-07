#pragma once

/*
woort_value.h
*/

#include "woort.h"

#include "woort_diagnosis.h"
#include "woort_gc_units.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t woort_BoxedValue;
typedef uint64_t woort_BoxedFloat63;
typedef uint64_t woort_BoxedInt62;
typedef uint64_t woort_BoxedBool;

typedef struct woort_GCString woort_GCString;
typedef struct woort_GCMap woort_GCMap;
typedef struct woort_GCVec woort_GCVec;
typedef struct woort_GCStruct woort_GCStruct;
typedef struct woort_GCClosure woort_GCClosure;
typedef struct woort_GCHandle woort_GCHandle;

typedef union woort_Value woort_Value;

typedef woort_api(*woort_JitFunction)(
    woort_VMRuntime* vm, const woort_Value* bp);

typedef union woort_DynBox
{
    woort_BoxedValue m_boxed;
    struct woort_BoxedExValue* m_boxed_ex;
    /* OPTIONAL, NULL if NIL */ woort_GCUnit* m_boxed_gc_unit;

}woort_DynBox;

typedef enum woort_CallWay
{
    // 一个脚本中的函数调用了另一个（本地的）脚本函数
    WOORT_CALL_WAY_NEAR,

    // 调用了另一个代码环境下的脚本函数，返回时需要额外检查
    WOORT_CALL_WAY_FAR,

    // 此调用是由 native 层发起的，返回时需要中断解释器执行
    WOORT_CALL_WAY_FROM_NATIVE,

} woort_CallWay;

typedef struct woort_RetBP
{
    woort_CallWay   m_way;
    uint32_t        m_bp_offset;

} woort_RetBP;

union woort_Value
{
    woort_GCUnit*           m_gcinstance;
    woort_Int               m_integer;
    woort_Real              m_real;
    const woort_GCString*   m_string;
    woort_GCVec*            m_vec;
    woort_GCMap*            m_map;
    woort_GCStruct*         m_struct;
    const woort_GCClosure*  m_closure;
    const woort_GCHandle*   m_gchandle;

    const woort_Bytecode*   m_script_function;
    woort_NativeFunction    m_native_function;
    woort_JitFunction       m_jit_function;

    woort_DynBox            m_dynamic;

    woort_RetBP             m_ret_bp;
    const void* m_ret_addr;

};

woort_DynBox woort_DynBox_box_real(woort_Real val);
woort_DynBox woort_DynBox_box_int(woort_Int val);
woort_DynBox woort_DynBox_box_bool(bool val);
woort_DynBox woort_DynBox_box(woort_Value val, woort_BoxValueType type);

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

////////////////////////////////////////////////////////////////////////
// 内部函数：用于类型特化操作，避免装箱分配
////////////////////////////////////////////////////////////////////////

// 整数/浮点数/布尔值的哈希函数
WOORT_NODISCARD size_t _woort_hash_int(woort_Int val);
WOORT_NODISCARD size_t _woort_hash_real(woort_Real val);

// 解装箱函数
WOORT_NODISCARD double _woort_unbox_float64(woort_BoxedFloat63 val);
WOORT_NODISCARD woort_Int _woort_unbox_int64(woort_BoxedInt62 val);
WOORT_NODISCARD bool _woort_unbox_bool(uint64_t val);

// 扩展装箱类型的 proxy
extern const woort_GCUnitProxy WOORT_EX_BOX_PROXY;

// 扩展装箱类型：用于存储超出内联范围的整数或浮点数
typedef struct woort_BoxedExValue
{
    woort_GCUnit m_unit;
    bool m_is_int;
    union
    {
        woort_Real m_real;
        woort_Int m_int;
    };
} woort_BoxedExValue;

////////////////////////////////////////////////////////////////////////
// 类型特化的比较函数：避免装箱分配
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD bool woort_DynBox_equal_int(woort_DynBox boxed_key, woort_Int int_key);
WOORT_NODISCARD bool woort_DynBox_equal_real(woort_DynBox boxed_key, woort_Real real_key);
WOORT_NODISCARD bool woort_DynBox_equal_bool(woort_DynBox boxed_key, bool bool_key);
WOORT_NODISCARD bool woort_DynBox_equal_gcunit(woort_DynBox boxed_key, /* OPTIONAL */ woort_GCUnit* gcunit_key);
