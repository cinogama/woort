#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "woomem.h"
#include "woort_value.h"
#include "woort_gc_units.h"
#include "woort_gc_string.h"
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
    *out_val = (bits & 0x8000000000000000ULL)       // 符号位
        | ((bits & 0x3FFFFFFFFFFFFFFFULL) << 1)     // 位61-0左移1位
        | WOORT_BOX_VALUE_TYPE_REAL;                // 类型标记

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

woort_DynBox woort_DynBox_box_real(woort_Real val)
{
    woort_DynBox result;
    if (!_woort_try_box_float63(val, &result.m_boxed))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_attrib(
            O, sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &_ex_box_proxy;

        ex_box->m_is_int = false;
        ex_box->m_real = val;

        result.m_boxed_ex = ex_box;
    }
    return result;
}
woort_DynBox woort_DynBox_box_int(woort_Int val)
{
    woort_DynBox result;
    if (!_woort_try_box_int62(val, &result.m_boxed))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_attrib(
            O, sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &_ex_box_proxy;

        ex_box->m_is_int = true;
        ex_box->m_int = val;

        result.m_boxed_ex = ex_box;
    }
    return result;
}
woort_DynBox woort_DynBox_box_bool(bool val)
{
    woort_DynBox result;
    result.m_boxed = _woort_box_bool(val);
    return result;
}

woort_DynBox woort_DynBox_box(woort_Value val, woort_BoxValueType type)
{
    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        return woort_DynBox_box_real(val.m_real);
    case WOORT_BOX_VALUE_TYPE_INT:
        return woort_DynBox_box_int(val.m_integer);
    case WOORT_BOX_VALUE_TYPE_BOOL:
        return woort_DynBox_box_bool(val.m_integer);
    case WOORT_BOX_VALUE_TYPE_GCUNIT:
    default:
    {
        woort_DynBox result;
        result.m_boxed_gc_unit = val.m_gcinstance;
        return result;
    }
    }
}

WOORT_NODISCARD bool woort_DynBox_check(
    woort_DynBox val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type)
{
    assert(type != WOORT_BOX_VALUE_TYPE_GCUNIT);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        if (val.m_boxed & 0b0111)
            return 0b01 & val.m_boxed;

        // May be ex value.
        _Static_assert(WOORT_BOX_VALUE_TYPE_REAL == 1,
            "WOORT_BOX_VALUE_TYPE_REAL should be 1");

        return val.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && !val.m_boxed_ex->m_is_int;
    case WOORT_BOX_VALUE_TYPE_INT:
        if (val.m_boxed & 0b0111)
            return 0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT));

        // May be ex value.
        return val.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && val.m_boxed_ex->m_is_int;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        return 0 == (0b0111 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_BOOL));
        break;
    default:
        // TODO;
        woort_panic(0, "todo");
        return false;
    }
}

WOORT_NODISCARD bool woort_DynBox_unbox(
    woort_DynBox val,
    woort_BoxValueType /* != WOORT_BOX_VALUE_TYPE_GCUNIT */ type,
    woort_Value* out_val)
{
    assert(type != WOORT_BOX_VALUE_TYPE_GCUNIT);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        if (val.m_boxed & 0b0111)
        {
            if (0b01 & val.m_boxed)
            {
                out_val->m_real = _woort_unbox_float64(val.m_boxed);
                return true;
            }
        }
        // 可能是 ex value
        else if (val.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && !val.m_boxed_ex->m_is_int)
        {
            out_val->m_real = val.m_boxed_ex->m_real;
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_INT:
        if (val.m_boxed & 0b0111)
        {
            if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            {
                out_val->m_integer = _woort_unbox_int64(val.m_boxed);
                return true;
            }
        }
        // 可能是 ex value
        else if (val.m_boxed_gc_unit->m_proxy == &_ex_box_proxy
            && val.m_boxed_ex->m_is_int)
        {
            out_val->m_integer = val.m_boxed_ex->m_int;
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        if (0 == (0b0111 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_BOOL))) {
            out_val->m_integer = _woort_unbox_bool(val.m_boxed) ? 1 : 0;
            return true;
        }
        break;
    }
    return false;
}

void woort_DynBox_unbox_no_check(
    woort_DynBox val,
    woort_Value* out_val)
{
    // Detect type from the value's tag bits and unbox accordingly
    if (val.m_boxed & 0b0111)
    {
        if (0b01 & val.m_boxed)
            // REAL (tagged double)
            out_val->m_real = _woort_unbox_float64(val.m_boxed);
        else if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            // INT
            out_val->m_integer = _woort_unbox_int64(val.m_boxed);
        else /* if (0 == (0b0111 & (val.m_boxed_bool ^ WOORT_BOX_VALUE_TYPE_BOOL))) */
            out_val->m_integer = _woort_unbox_bool(val.m_boxed) ? 1 : 0;
    }
    else
    {
        // Ex value (GC allocated)
        if (val.m_boxed_gc_unit->m_proxy != &_ex_box_proxy)
            out_val->m_gcinstance = val.m_boxed_gc_unit;
        else
        {
            if (val.m_boxed_ex->m_is_int)
                out_val->m_integer = val.m_boxed_ex->m_int;
            else
                out_val->m_real = val.m_boxed_ex->m_real;
        }
    }
}

WOORT_NODISCARD size_t _woort_hash_int(woort_Int val)
{
    size_t hash = (size_t)val;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
}

WOORT_NODISCARD size_t _woort_hash_real(woort_Real val)
{
    uint64_t real_bits;
    memcpy(&real_bits, &val, sizeof(double));
    size_t hash = (size_t)real_bits;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
}

WOORT_NODISCARD size_t woort_DynBox_hash(woort_DynBox val)
{
    if (val.m_boxed & 0b0111)
    {
        if (0b01 & val.m_boxed)
            // REAL: unbox and hash
            return _woort_hash_real(_woort_unbox_float64(val.m_boxed));
        else if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            // INT: unbox and hash
            return _woort_hash_int(_woort_unbox_int64(val.m_boxed));
        else
            // BOOL: hash 0 or 1
            return _woort_unbox_bool(val.m_boxed) ? 1 : 0;
    }
    else
    {
        // GC allocated value
        woort_GCUnit* const gc_unit = val.m_boxed_gc_unit;

        if (gc_unit->m_proxy == &g_gcstring_unit_proxy)
            // String: use string-specific hash
            return woort_GCString_hash((const woort_GCString*)gc_unit);
        else if (gc_unit->m_proxy == &_ex_box_proxy)
        {
            // Ex value: hash the internal int or real
            woort_BoxedExValue* const ex_box = val.m_boxed_ex;
            if (ex_box->m_is_int)
                return _woort_hash_int(ex_box->m_int);
            else
                return _woort_hash_real(ex_box->m_real);
        }
        else
        {
            // Other GC types: hash the pointer address
            return _woort_hash_int((woort_Int)(uintptr_t)gc_unit);
        }
    }
}

WOORT_NODISCARD bool woort_DynBox_equal(woort_DynBox a, woort_DynBox b)
{
    // Fast path: compare raw boxed values directly
    if (a.m_boxed == b.m_boxed)
        return true;

    // NOTE: 如果其中之一是 boxed，另一个是 boxed_ex/unit，或者俩都是 boxed，说明必然不相同
    if ((a.m_boxed & 0b0111) || (b.m_boxed & 0b0111))
        return false;

    // Both are GC-allocated values
    woort_GCUnit* const gc_unit_a = a.m_boxed_gc_unit;
    woort_GCUnit* const gc_unit_b = b.m_boxed_gc_unit;

    // Compare proxy pointers first for type discrimination
    if (gc_unit_a->m_proxy != gc_unit_b->m_proxy)
        return false;

    // Same proxy type
    if (gc_unit_a->m_proxy == &g_gcstring_unit_proxy)
    {
        // Extra check for string.
        return woort_GCString_compare(
            (const woort_GCString*)gc_unit_a,
            (const woort_GCString*)gc_unit_b) == 0;
    }
    else if (gc_unit_a->m_proxy == &_ex_box_proxy)
    {
        woort_BoxedExValue* const ex_a = a.m_boxed_ex;
        woort_BoxedExValue* const ex_b = b.m_boxed_ex;

        if (ex_a->m_is_int != ex_b->m_is_int)
            return false;

        return ex_a->m_is_int
            ? ex_a->m_int == ex_b->m_int
            : ex_a->m_real == ex_b->m_real;
    }

    // Other GC types: pointer equality (already checked above if m_boxed == b.m_boxed)
    return false;
}

////////////////////////////////////////////////////////////////////////
// 类型特化的比较函数：避免装箱分配
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD bool woort_DynBox_equal_int(woort_DynBox boxed_key, woort_Int int_key)
{
    // 检查内联 int62
    if (boxed_key.m_boxed & 0b0111)
    {
        if (0 == (0b011 & (boxed_key.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            return _woort_unbox_int64(boxed_key.m_boxed) == int_key;
        return false;
    }

    // 检查 ex value
    woort_GCUnit* const gc_unit = boxed_key.m_boxed_gc_unit;
    if (gc_unit->m_proxy == &_ex_box_proxy
        && boxed_key.m_boxed_ex->m_is_int)
    {
        return boxed_key.m_boxed_ex->m_int == int_key;
    }

    return false;
}

WOORT_NODISCARD bool woort_DynBox_equal_real(woort_DynBox boxed_key, woort_Real real_key)
{
    // 检查内联 float63
    if (boxed_key.m_boxed & 0b0111)
    {
        if (0b01 & boxed_key.m_boxed)
            return _woort_unbox_float64(boxed_key.m_boxed) == real_key;
        return false;
    }

    // 检查 ex value
    woort_GCUnit* const gc_unit = boxed_key.m_boxed_gc_unit;
    if (gc_unit->m_proxy == &_ex_box_proxy
        && !boxed_key.m_boxed_ex->m_is_int)
    {
        return boxed_key.m_boxed_ex->m_real == real_key;
    }

    return false;
}

WOORT_NODISCARD bool woort_DynBox_equal_bool(woort_DynBox boxed_key, bool bool_key)
{
    if (0 == (0b0111 & (boxed_key.m_boxed ^ WOORT_BOX_VALUE_TYPE_BOOL)))
        return _woort_unbox_bool(boxed_key.m_boxed) == bool_key;
    return false;
}

WOORT_NODISCARD bool woort_DynBox_equal_gcunit(woort_DynBox boxed_key, woort_GCUnit* gcunit_key)
{
    // GCUnit 类型：低 3 位必须为 0
    if (boxed_key.m_boxed & 0b0111)
        return false;

    return boxed_key.m_boxed_gc_unit == gcunit_key;
}
