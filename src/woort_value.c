#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#include "woomem.h"
#include "woort_value.h"
#include "woort_gc_units.h"
#include "woort_diagnosis.h"

/*
Boxed value:    | ............................... | 3 type bits |
Boxed GCUnit:   | ...Address pointer high 61 bits...| 0 | 0 | 0 |
Boxed Float63:  | ............... Float63 ................. | 1 |
Boxed Int62:    | ................ Int62 .............. | 1 | 0 |
Boxed Bool:     | ................ Bool61 ..........| 1 | 0 | 0 |
*/

const woort_GCUnitProxy _ex_box_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

const int64_t BOXED_INT62_MAX = (1LL << 61) - 1;
const int64_t BOXED_INT62_MIN = 1LL << 61;

WOORT_NODISCARD bool _woort_try_box_float63(double val, woort_BoxedFloat63* out_val)
{
    // 使用 union 进行二进制重解释
    const union { double d; uint64_t u; } conv = { .d = val };
    const uint64_t bits = conv.u;

    // 检查指数最高位(位62)和次高位(位61)是否不同
    const bool exp_highest = (bits >> 62) & 1;
    const bool exp_second_highest = (bits >> 61) & 1;

    if (exp_highest == exp_second_highest) {
        return false;
    }

    // 压缩：去掉位62，将位61-0左移1位，保留符号位，设置类型标记
    *out_val = (bits & 0x8000000000000000ULL)         // 符号位
        | ((bits & 0x3FFFFFFFFFFFFFFFULL) << 1)  // 位61-0左移1位
        | WOORT_BOX_VALUE_TYPE_REAL;             // 类型标记

    return true;
}
WOORT_NODISCARD bool _woort_try_box_int62(woort_Int val, woort_BoxedInt62* out_val)
{
    if (val >= BOXED_INT62_MIN && val <= BOXED_INT62_MAX)
    {
        *out_val = (woort_BoxedInt62)(
            (val << 2) | WOORT_BOX_VALUE_TYPE_INT);

        return true;
    }
    return false;
}
WOORT_NODISCARD woort_BoxedBool _woort_box_bool(bool val)
{
    return  (woort_BoxedBool)(
        (uint64_t)val << 3) | WOORT_BOX_VALUE_TYPE_BOOL;
}

WOORT_NODISCARD double _woort_unbox_float64(woort_BoxedFloat63 val)
{
    // 提取符号位（位63）
    const uint64_t sign_bit = val & 0x8000000000000000ULL;

    // 提取压缩后的指数次高位 E9（现在是位62）
    const uint64_t exp_second_highest = (val >> 62) & 1;

    // 恢复指数最高位 E10 = !E9
    const uint64_t exp_highest = exp_second_highest ^ 1;

    // 解压：去掉类型标记，恢复 E10
    const uint64_t result = sign_bit                        // 符号位（位63）
        | (exp_highest << 62)                   // 恢复的 E10（位62）
        | ((val >> 1) & 0x3FFFFFFFFFFFFFFFULL); // 数据部分（位61-0）

    // 使用 union 进行二进制重解释
    const union { double d; uint64_t u; } conv = { .u = result };
    return conv.d;
}
WOORT_NODISCARD woort_Int _woort_unbox_int64(woort_BoxedInt62 val)
{
    return ((woort_Int)val) >> 2;
}
WOORT_NODISCARD bool _woort_unbox_bool(uint64_t val)
{
    return (val >> 3) != 0;
}

////////////////////////////////////////////////////////////////////////

typedef struct woort_BoxedExValue
{
    woort_GCUnit m_unit;
    bool m_is_int;
    union
    {
        woort_Real m_real;
        woort_Int m_int;
    };
}woort_BoxedExValue;

void woort_box_real(woort_Real val, woort_DynBox* out_box_val)
{
    if (!_woort_try_box_float63(val, &out_box_val->m_boxed_real))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_attrib(
            O, sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &_ex_box_proxy;

        ex_box->m_is_int = false;
        ex_box->m_real = val;

        out_box_val->m_boxed_ex = ex_box;
    }
}
void woort_box_int(woort_Int val, woort_DynBox* out_box_val)
{
    if (!_woort_try_box_int62(val, &out_box_val->m_boxed_int))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_attrib(
            O, sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &_ex_box_proxy;

        ex_box->m_is_int = true;
        ex_box->m_int = val;

        out_box_val->m_boxed_ex = ex_box;
    }
}
void woort_box_bool(bool val, woort_DynBox* out_box_val)
{
    out_box_val->m_boxed_bool = _woort_box_bool(val);
}

void woort_Value_box(
    woort_Value val, woort_BoxValueType type, woort_DynBox* out_val)
{
    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        woort_box_real(val.m_real, out_val);
        break;
    case WOORT_BOX_VALUE_TYPE_INT:
        woort_box_int(val.m_real, out_val);
        break;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        woort_box_bool(val.m_integer, out_val);
        break;
    case WOORT_BOX_VALUE_TYPE_GCUNIT:
    default:
        out_val->m_boxed_gc_unit = val.m_gcinstance;
        break;
    }
}

WOORT_NODISCARD bool woort_Value_box_check(
    woort_DynBox val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type)
{
    assert(type != WOORT_BOX_VALUE_TYPE_GCUNIT);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        if (val.m_boxed_real & 0b0111)
            return 0b01 & val.m_boxed_real;

        // May be ex value.
        _Static_assert(WOORT_BOX_VALUE_TYPE_REAL == 1,
            "WOORT_BOX_VALUE_TYPE_REAL should be 1");

        return val.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && !val.m_boxed_ex->m_is_int;
    case WOORT_BOX_VALUE_TYPE_INT:
        if (val.m_boxed_int & 0b0111)
            return 0 == (0b011 & (val.m_boxed_int ^ WOORT_BOX_VALUE_TYPE_INT));

        // May be ex value.
        return val.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && val.m_boxed_ex->m_is_int;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        return 0 == (0b0111 & (val.m_boxed_int ^ WOORT_BOX_VALUE_TYPE_BOOL));
        break;
    default:
        // TODO;
        woort_panic(0, "todo");
        return false;
    }
}

WOORT_NODISCARD bool woort_Value_unbox(
    woort_Value val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type,
    woort_Value* out_val)
{
    assert(type != WOORT_BOX_VALUE_TYPE_GCUNIT);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        if (val.m_dynamic.m_boxed_real & 0b0111)
        {
            if (0b01 & val.m_dynamic.m_boxed_real)
            {
                out_val->m_real = _woort_unbox_float64(val.m_dynamic.m_boxed_real);
                return true;
            }
        }
        // 可能是 ex value
        else if (val.m_dynamic.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && !val.m_dynamic.m_boxed_ex->m_is_int)
        {
            out_val->m_real = val.m_dynamic.m_boxed_ex->m_real;
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_INT:
        if (val.m_dynamic.m_boxed_int & 0b0111)
        {
            if (0 == (0b011 & (val.m_dynamic.m_boxed_int ^ WOORT_BOX_VALUE_TYPE_INT)))
            {
                out_val->m_integer = _woort_unbox_int64(val.m_dynamic.m_boxed_int);
                return true;
            }
        }
        // 可能是 ex value
        else if (val.m_dynamic.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && val.m_dynamic.m_boxed_ex->m_is_int)
        {
            out_val->m_integer = val.m_dynamic.m_boxed_ex->m_int;
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        if (0 == (0b0111 & (val.m_dynamic.m_boxed_bool ^ WOORT_BOX_VALUE_TYPE_BOOL))) {
            out_val->m_integer = _woort_unbox_bool(val.m_dynamic.m_boxed_bool) ? 1 : 0;
            return true;
        }
        break;
    default:
        woort_panic(0, "todo");
        break;
    }
    return false;
}
