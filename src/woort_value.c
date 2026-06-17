#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "woomem.h"
#include "woort_value.h"
#include "woort_gc_units.h"
#include "woort_gc_string.h"
#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc_gchandle.h"
#include "woort_gc_closure.h"
#include "woort_diagnosis.h"
#include "woort_gc.h"
#include "woort_util.h"
#include "woort_platform.h"

/*
Boxed value:    | ............................... | 3 type bits |
Boxed GCUnit:   | ...Address pointer high 61 bits...| 0 | 0 | 0 |
Boxed Float63:  | ............... Float63 ................. | 1 |
Boxed Int62:    | ................ Int62 .............. | 1 | 0 |
Boxed Bool:     | ................ Bool61 ..........| 1 | 0 | 0 |
*/

const woort_GCUnitProxy WOORT_EX_BOX_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

static const int64_t WOORT_BOXED_INT62_MAX = (1LL << 61) - 1;
static const int64_t WOORT_BOXED_INT62_MIN = -(1LL << 61);

WOORT_NODISCARD static bool _woort_try_box_float63(double val, woort_BoxedFloat63* out_val)
{
    /* 使用 union 进行二进制重解释 */
    const union { double d; uint64_t u; } conv = { .d = val };
    const uint64_t bits = conv.u;

    /* 检查指数最高位(位62)和次高位(位61)是否不同 */
    const bool exp_highest = (bits >> 62) & 1;
    const bool exp_second_highest = (bits >> 61) & 1;

    if (exp_highest == exp_second_highest) {
        return false;
    }

    /* 压缩：去掉位62，将位61-0左移1位，保留符号位，设置类型标记 */
    *out_val = (bits & 0x8000000000000000ULL)       /* 符号位 */
        | ((bits & 0x3FFFFFFFFFFFFFFFULL) << 1)     /* 位61-0左移1位 */
        | WOORT_BOX_VALUE_TYPE_REAL;                /* 类型标记 */

    return true;
}
WOORT_NODISCARD static bool _woort_try_box_int62(woort_Int val, woort_BoxedInt62* out_val)
{
    if (val >= WOORT_BOXED_INT62_MIN && val <= WOORT_BOXED_INT62_MAX)
    {
        *out_val = (woort_BoxedInt62)(
            (val << 2) | WOORT_BOX_VALUE_TYPE_INT);

        return true;
    }
    return false;
}
WOORT_NODISCARD static woort_BoxedBool _woort_box_bool(bool val)
{
    return  (woort_BoxedBool)(
        (uint64_t)val << 3) | WOORT_BOX_VALUE_TYPE_BOOL;
}

WOORT_NODISCARD double _woort_unbox_float64(woort_BoxedFloat63 val)
{
    /* 提取符号位（位63） */
    const uint64_t sign_bit = val & 0x8000000000000000ULL;

    /* 提取压缩后的指数次高位 E9（现在是位62） */
    const uint64_t exp_second_highest = (val >> 62) & 1;

    /* 恢复指数最高位 E10 = !E9 */
    const uint64_t exp_highest = exp_second_highest ^ 1;

    /* 解压：去掉类型标记，恢复 E10 */
    const uint64_t result = sign_bit                        /* 符号位（位63） */
        | (exp_highest << 62)                   /* 恢复的 E10（位62） */
        | ((val >> 1) & 0x3FFFFFFFFFFFFFFFULL); /* 数据部分（位61-0） */

    /* 使用 union 进行二进制重解释 */
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
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_delay_init(
            sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

        ex_box->m_is_int = false;
        ex_box->m_real = val;

        woort_GCUnit_init_delay_alloc(O, ex_box);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
    }
    return result;
}
woort_DynBox woort_DynBox_box_int(woort_Int val)
{
    woort_DynBox result;
    if (!_woort_try_box_int62(val, &result.m_boxed))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_delay_init(
            sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

        ex_box->m_is_int = true;
        ex_box->m_int = val;

        woort_GCUnit_init_delay_alloc(O, ex_box);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
    }
    return result;
}
woort_DynBox woort_DynBox_box_bool(bool val)
{
    woort_DynBox result;
    result.m_boxed = _woort_box_bool(val);
    return result;
}

woort_DynBox woort_DynBox_box_real_for_env_constant(woort_CodeEnv* cenv, woort_Real val)
{
    woort_DynBox result;
    if (!_woort_try_box_float63(val, &result.m_boxed))
    {
        woort_BoxedExValue* ex_box;

        do
        {
            ex_box = woomem_allocate_begin(
                sizeof(woort_BoxedExValue));

            if (ex_box != NULL)
                break;

            woort_CodeEnv_unlock(cenv);
            {
                _woort_GCUnit_alloc_failed();
            }
            woort_CodeEnv_lock(cenv);

        } while (true);

        ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

        ex_box->m_is_int = false;
        ex_box->m_real = val;

        woort_GCUnit_init_delay_alloc(O, ex_box);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
    }
    return result;
}

woort_DynBox woort_DynBox_box_int_for_env_constant(woort_CodeEnv* cenv, woort_Int val)
{
    woort_DynBox result;
    if (!_woort_try_box_int62(val, &result.m_boxed))
    {
        woort_BoxedExValue* ex_box;

        do
        {
            ex_box = woomem_allocate_begin(
                sizeof(woort_BoxedExValue));

            if (ex_box != NULL)
                break;

            woort_CodeEnv_unlock(cenv);
            {
                _woort_GCUnit_alloc_failed();
            }
            woort_CodeEnv_lock(cenv);

        } while (true);

        ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

        ex_box->m_is_int = true;
        ex_box->m_int = val;

        woort_GCUnit_init_delay_alloc(O, ex_box);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
    }
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
    case WOORT_BOX_VALUE_TYPE_NIL:
    case WOORT_BOX_VALUE_TYPE_STRING:
    case WOORT_BOX_VALUE_TYPE_VEC:
    case WOORT_BOX_VALUE_TYPE_MAP:
    case WOORT_BOX_VALUE_TYPE_STRUCT:
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
    {
        woort_DynBox result;
        result.m_boxed = _woort_gcunit_to_boxed(val.m_gcinstance);
        return result;
    }
    default:
        // Should not been here.
        abort();
    }
}

////////////////////////////////////////////////////////////////////////
/* 带混合写屏障的装箱函数 */
////////////////////////////////////////////////////////////////////////

void woort_DynBox_box_real_with_barrier(woort_DynBox* dst, woort_Real val)
{
    woort_DynBox result;
    if (!_woort_try_box_float63(val, &result.m_boxed))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_delay_init(
            sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

        ex_box->m_is_int = false;
        ex_box->m_real = val;

        woort_GCUnit_init_delay_alloc(O, ex_box);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
    }
    woort_GC_mixed_write_barrier_dynbox(dst, result);
}

void woort_DynBox_box_int_with_barrier(woort_DynBox* dst, woort_Int val)
{
    woort_DynBox result;
    if (!_woort_try_box_int62(val, &result.m_boxed))
    {
        woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_delay_init(
            sizeof(woort_BoxedExValue));

        ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

        ex_box->m_is_int = true;
        ex_box->m_int = val;

        woort_GCUnit_init_delay_alloc(O, ex_box);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
    }
    woort_GC_mixed_write_barrier_dynbox(dst, result);
}

void woort_DynBox_box_bool_with_barrier(woort_DynBox* dst, bool val)
{
    woort_DynBox result;
    result.m_boxed = _woort_box_bool(val);
    woort_GC_mixed_write_barrier_dynbox(dst, result);
}

void woort_DynBox_box_with_barrier(woort_DynBox* dst, woort_Value val, woort_BoxValueType type)
{
    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_REAL:
        woort_DynBox_box_real_with_barrier(dst, val.m_real);
        break;
    case WOORT_BOX_VALUE_TYPE_INT:
        woort_DynBox_box_int_with_barrier(dst, val.m_integer);
        break;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        woort_DynBox_box_bool_with_barrier(dst, val.m_integer);
        break;
    case WOORT_BOX_VALUE_TYPE_GCUNIT:
    case WOORT_BOX_VALUE_TYPE_NIL:
    case WOORT_BOX_VALUE_TYPE_STRING:
    case WOORT_BOX_VALUE_TYPE_VEC:
    case WOORT_BOX_VALUE_TYPE_MAP:
    case WOORT_BOX_VALUE_TYPE_STRUCT:
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
    {
        woort_DynBox result;
        result.m_boxed = _woort_gcunit_to_boxed(val.m_gcinstance);
        woort_GC_mixed_write_barrier_dynbox(dst, result);
        break;
    }
    default:
        // Should not been here.
        abort();
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

        /* May be ex value. */
        _Static_assert(WOORT_BOX_VALUE_TYPE_REAL == 1,
            "WOORT_BOX_VALUE_TYPE_REAL should be 1");

        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_EX_BOX_PROXY
            && !_woort_boxed_to_exvalue(val.m_boxed)->m_is_int;
    case WOORT_BOX_VALUE_TYPE_INT:
        if (val.m_boxed & 0b0111)
            return 0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT));

        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_EX_BOX_PROXY
            && _woort_boxed_to_exvalue(val.m_boxed)->m_is_int;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        return 0 == (0b0111 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_BOOL));
        break;
    case WOORT_BOX_VALUE_TYPE_STRING:
        if (val.m_boxed & 0b0111)
            return false;
        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCSTRING_UNIT_PROXY;
    case WOORT_BOX_VALUE_TYPE_VEC:
        if (val.m_boxed & 0b0111)
            return false;
        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCVEC_UNIT_PROXY;
    case WOORT_BOX_VALUE_TYPE_MAP:
        if (val.m_boxed & 0b0111)
            return false;
        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCMAP_UNIT_PROXY;
    case WOORT_BOX_VALUE_TYPE_STRUCT:
        if (val.m_boxed & 0b0111)
            return false;
        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCSTRUCT_UNIT_PROXY;
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
        if (val.m_boxed & 0b0111)
            return false;
        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCHANDLE_UNIT_PROXY;
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
        if (val.m_boxed & 0b0111)
            return false;
        return val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCCLOSURE_UNIT_PROXY;
    case WOORT_BOX_VALUE_TYPE_NIL:
        return val.m_boxed == 0;
    default:
        // Should not been here.
        abort();
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
        else if (val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_EX_BOX_PROXY
            && !_woort_boxed_to_exvalue(val.m_boxed)->m_is_int)
        {
            out_val->m_real = _woort_boxed_to_exvalue(val.m_boxed)->m_real;
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
        else if (val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_EX_BOX_PROXY
            && _woort_boxed_to_exvalue(val.m_boxed)->m_is_int)
        {
            out_val->m_integer = _woort_boxed_to_exvalue(val.m_boxed)->m_int;
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_BOOL:
        if (0 == (0b0111 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_BOOL))) {
            out_val->m_integer = _woort_unbox_bool(val.m_boxed) ? 1 : 0;
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_STRING:
        if (!(val.m_boxed & 0b0111)
            && val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCSTRING_UNIT_PROXY)
        {
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_VEC:
        if (!(val.m_boxed & 0b0111)
            && val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCVEC_UNIT_PROXY)
        {
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_MAP:
        if (!(val.m_boxed & 0b0111)
            && val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCMAP_UNIT_PROXY)
        {
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_STRUCT:
        if (!(val.m_boxed & 0b0111)
            && val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCSTRUCT_UNIT_PROXY)
        {
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
        if (!(val.m_boxed & 0b0111)
            && val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCHANDLE_UNIT_PROXY)
        {
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
        if (!(val.m_boxed & 0b0111)
            && val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_GCCLOSURE_UNIT_PROXY)
        {
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
            return true;
        }
        break;
    case WOORT_BOX_VALUE_TYPE_NIL:
        if (val.m_boxed == 0)
        {
            out_val->m_gcinstance = NULL;
            return true;
        }
        break;
    default:
        // Should not been here.
        abort();
    }
    return false;
}

void woort_DynBox_unbox_no_check(
    woort_DynBox val,
    woort_Value* out_val)
{
    /* Detect type from the value's tag bits and unbox accordingly */
    if (val.m_boxed & 0b0111)
    {
        if (0b01 & val.m_boxed)
            /* REAL (tagged double) */
            out_val->m_real = _woort_unbox_float64(val.m_boxed);
        else if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            /* INT */
            out_val->m_integer = _woort_unbox_int64(val.m_boxed);
        else /* if (0 == (0b0111 & (val.m_boxed_bool ^ WOORT_BOX_VALUE_TYPE_BOOL))) */
            out_val->m_integer = _woort_unbox_bool(val.m_boxed) ? 1 : 0;
    }
    else
    {
        if (val.m_boxed != 0
            && _woort_boxed_to_gcunit(val.m_boxed)->m_proxy == &WOORT_EX_BOX_PROXY)
        {
            if (_woort_boxed_to_exvalue(val.m_boxed)->m_is_int)
                out_val->m_integer = _woort_boxed_to_exvalue(val.m_boxed)->m_int;
            else
                out_val->m_real = _woort_boxed_to_exvalue(val.m_boxed)->m_real;
        }
        else
            out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);
    }
}

WOORT_NODISCARD woort_BoxValueType
woort_DynBox_unbox_no_check_and_get_type(
    woort_DynBox val,
    woort_Value* out_val)
{
    if (val.m_boxed & 0b0111)
    {
        if (0b01 & val.m_boxed)
        {
            out_val->m_real = _woort_unbox_float64(val.m_boxed);
            return WOORT_BOX_VALUE_TYPE_REAL;
        }

        if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
        {
            out_val->m_integer = _woort_unbox_int64(val.m_boxed);
            return WOORT_BOX_VALUE_TYPE_INT;
        }

        out_val->m_integer = _woort_unbox_bool(val.m_boxed) ? 1 : 0;
        return WOORT_BOX_VALUE_TYPE_BOOL;
    }

    if (val.m_boxed == 0)
    {
        out_val->m_gcinstance = NULL;
        return WOORT_BOX_VALUE_TYPE_NIL;
    }

    const woort_GCUnitProxy* const proxy = _woort_boxed_to_gcunit(val.m_boxed)->m_proxy;

    if (proxy == &WOORT_EX_BOX_PROXY)
    {
        if (_woort_boxed_to_exvalue(val.m_boxed)->m_is_int)
        {
            out_val->m_integer = _woort_boxed_to_exvalue(val.m_boxed)->m_int;
            return WOORT_BOX_VALUE_TYPE_INT;
        }
        out_val->m_real = _woort_boxed_to_exvalue(val.m_boxed)->m_real;
        return WOORT_BOX_VALUE_TYPE_REAL;
    }

    out_val->m_gcinstance = _woort_boxed_to_gcunit(val.m_boxed);

    if (proxy == &WOORT_GCSTRING_UNIT_PROXY)
        return WOORT_BOX_VALUE_TYPE_STRING;

    if (proxy == &WOORT_GCVEC_UNIT_PROXY)
        return WOORT_BOX_VALUE_TYPE_VEC;

    if (proxy == &WOORT_GCMAP_UNIT_PROXY)
        return WOORT_BOX_VALUE_TYPE_MAP;

    if (proxy == &WOORT_GCSTRUCT_UNIT_PROXY)
        return WOORT_BOX_VALUE_TYPE_STRUCT;

    if (proxy == &WOORT_GCHANDLE_UNIT_PROXY)
        return WOORT_BOX_VALUE_TYPE_GCHANDLE;

    if (proxy == &WOORT_GCCLOSURE_UNIT_PROXY)
        return WOORT_BOX_VALUE_TYPE_CLOSURE;

    // Should not been here.
    abort();
}

WOORT_NODISCARD size_t _woort_hash_int(woort_Int val)
{
#ifdef WOORT_PLATFORM_64
    /* Murmur3 64-bit finalizer */
    uint64_t hash = (uint64_t)val;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return (size_t)hash;
#else
    /* Fold 64-bit int to 32 bits, then apply Murmur3 32-bit finalizer */
    uint32_t hash = (uint32_t)((uint64_t)val ^ ((uint64_t)val >> 32));
    hash ^= hash >> 16;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35U;
    hash ^= hash >> 16;
    return (size_t)hash;
#endif
}

WOORT_NODISCARD size_t _woort_hash_real(woort_Real val)
{
    uint64_t real_bits;
    memcpy(&real_bits, &val, sizeof(double));
#ifdef WOORT_ARCH_64
    /* Murmur3 64-bit finalizer */
    size_t hash = (size_t)real_bits;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
#else
    /* Fold 64-bit double bits to 32 bits, then apply Murmur3 32-bit finalizer */
    uint32_t hash = (uint32_t)(real_bits ^ (real_bits >> 32));
    hash ^= hash >> 16;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35U;
    hash ^= hash >> 16;
    return (size_t)hash;
#endif
}

WOORT_NODISCARD size_t woort_DynBox_hash(woort_DynBox val)
{
    if (val.m_boxed & 0b0111)
    {
        if (0b01 & val.m_boxed)
            /* REAL: unbox and hash */
            return _woort_hash_real(_woort_unbox_float64(val.m_boxed));
        else if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            /* INT: unbox and hash */
            return _woort_hash_int(_woort_unbox_int64(val.m_boxed));
        else
            /* BOOL: hash 0 or 1 */
            return _woort_unbox_bool(val.m_boxed) ? 1 : 0;
    }
    else
    {
        /* GC allocated value */
        woort_GCUnit* const gc_unit = _woort_boxed_to_gcunit(val.m_boxed);

        if (gc_unit == NULL)
            return 0;

        if (gc_unit->m_proxy == &WOORT_GCSTRING_UNIT_PROXY)
            /* String: use string-specific hash */
            return woort_GCString_hash((const woort_GCString*)gc_unit);
        else if (gc_unit->m_proxy == &WOORT_EX_BOX_PROXY)
        {
            /* Ex value: hash the internal int or real */
            woort_BoxedExValue* const ex_box = _woort_boxed_to_exvalue(val.m_boxed);
            if (ex_box->m_is_int)
                return _woort_hash_int(ex_box->m_int);
            else
                return _woort_hash_real(ex_box->m_real);
        }
        else
        {
            /* Other GC types: hash the pointer address */
            return woort_util_ptr_hash(gc_unit);
        }
    }
}

WOORT_NODISCARD bool woort_DynBox_equal(woort_DynBox a, woort_DynBox b)
{
    /* Fast path: compare raw boxed values directly */
    if (a.m_boxed == b.m_boxed)
        return true;

    /* NOTE: 如果其中之一是 boxed，另一个是 boxed_ex/unit，或者俩都是 boxed，说明必然不相同 */
    if ((a.m_boxed & 0b0111) || (b.m_boxed & 0b0111))
        return false;

    /* Both are GC-allocated values */
    woort_GCUnit* const gc_unit_a = _woort_boxed_to_gcunit(a.m_boxed);
    woort_GCUnit* const gc_unit_b = _woort_boxed_to_gcunit(b.m_boxed);

    if (gc_unit_a == gc_unit_b)
        return true;

    if (gc_unit_a == NULL || gc_unit_b == NULL)
        return false;

    /* Compare proxy pointers first for type discrimination */
    if (gc_unit_a->m_proxy != gc_unit_b->m_proxy)
        return false;

    /* Same proxy type */
    if (gc_unit_a->m_proxy == &WOORT_GCSTRING_UNIT_PROXY)
    {
        /* Extra check for string. */
        return woort_GCString_compare(
            (const woort_GCString*)gc_unit_a,
            (const woort_GCString*)gc_unit_b) == 0;
    }
    else if (gc_unit_a->m_proxy == &WOORT_EX_BOX_PROXY)
    {
        woort_BoxedExValue* const ex_a = _woort_boxed_to_exvalue(a.m_boxed);
        woort_BoxedExValue* const ex_b = _woort_boxed_to_exvalue(b.m_boxed);

        if (ex_a->m_is_int != ex_b->m_is_int)
            return false;

        return ex_a->m_is_int
            ? ex_a->m_int == ex_b->m_int
            : ex_a->m_real == ex_b->m_real;
    }

    /* Other GC types: pointer equality (already checked above if m_boxed == b.m_boxed) */
    return false;
}

WOORT_NODISCARD woort_DynBox _woort_DynBox_make_dup_boxed(woort_DynBox v_m_s_to_dup)
{
    woort_Value _unboxed;
    woort_DynBox result;
    switch (woort_DynBox_unbox_no_check_and_get_type(v_m_s_to_dup, &_unboxed))
    {
    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        woort_GCVec* const dst = woort_GCVec_new();
        const woort_GCVec* const src = _unboxed.m_vec;

        woort_GCVec_copy(dst, src);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)dst);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        woort_GCMap* const dst = woort_GCMap_new();
        const woort_GCMap* const src = _unboxed.m_map;

        woort_GCMap_copy(dst, src);

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)dst);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_STRUCT:
    {
        const woort_GCStruct* const src = _unboxed.m_struct;
        woort_GCStruct* const dst = woort_GCStruct_new(src->m_size);

        for (size_t i = 0; i < src->m_size; i++)
        {
            woort_GC_init_write_barrier_value(
                &dst->m_datas[i], src->m_datas[i]);
        }

        result.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)dst);
        break;
    }
    default:
        result.m_boxed = v_m_s_to_dup.m_boxed;
        break;
    }
    return result;
}

////////////////////////////////////////////////////////////////////////
/* 类型特化的比较函数：避免装箱分配 */
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD bool woort_DynBox_equal_int(woort_DynBox boxed_key, woort_Int int_key)
{
    /* 检查内联 int62 */
    if (boxed_key.m_boxed & 0b0111)
    {
        if (0 == (0b011 & (boxed_key.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            return _woort_unbox_int64(boxed_key.m_boxed) == int_key;
        return false;
    }

    /* 检查 ex value */
    woort_GCUnit* const gc_unit = _woort_boxed_to_gcunit(boxed_key.m_boxed);

    if (gc_unit != NULL
        && gc_unit->m_proxy == &WOORT_EX_BOX_PROXY
        && _woort_boxed_to_exvalue(boxed_key.m_boxed)->m_is_int)
    {
        return _woort_boxed_to_exvalue(boxed_key.m_boxed)->m_int == int_key;
    }

    return false;
}

WOORT_NODISCARD bool woort_DynBox_equal_real(woort_DynBox boxed_key, woort_Real real_key)
{
    /* 检查内联 float63 */
    if (boxed_key.m_boxed & 0b0111)
    {
        if (0b01 & boxed_key.m_boxed)
            return _woort_unbox_float64(boxed_key.m_boxed) == real_key;
        return false;
    }

    /* 检查 ex value */
    woort_GCUnit* const gc_unit = _woort_boxed_to_gcunit(boxed_key.m_boxed);

    if (gc_unit != NULL
        && gc_unit->m_proxy == &WOORT_EX_BOX_PROXY
        && !_woort_boxed_to_exvalue(boxed_key.m_boxed)->m_is_int)
    {
        return _woort_boxed_to_exvalue(boxed_key.m_boxed)->m_real == real_key;
    }

    return false;
}

WOORT_NODISCARD bool woort_DynBox_equal_bool(woort_DynBox boxed_key, bool bool_key)
{
    if (0 == (0b0111 & (boxed_key.m_boxed ^ WOORT_BOX_VALUE_TYPE_BOOL)))
        return _woort_unbox_bool(boxed_key.m_boxed) == bool_key;
    return false;
}

WOORT_NODISCARD bool woort_DynBox_equal_gcunit(woort_DynBox boxed_key, /* OPTIONAL */ woort_GCUnit* gcunit_key)
{
    if (boxed_key.m_boxed & 0b0111)
        return false;

    return _woort_boxed_to_gcunit(boxed_key.m_boxed) == gcunit_key;
}

WOORT_NODISCARD bool woort_DynBox_equal_string(woort_DynBox boxed_key, const char* str, size_t len)
{
    if (boxed_key.m_boxed & 0b0111)
        return false;

    woort_GCUnit* const gc_unit = _woort_boxed_to_gcunit(boxed_key.m_boxed);
    if (gc_unit == NULL || gc_unit->m_proxy != &WOORT_GCSTRING_UNIT_PROXY)
        return false;

    const woort_GCString* const gc_str = (const woort_GCString*)gc_unit;
    if (gc_str->m_length != len)
        return false;

    return memcmp(gc_str->m_content, str, len) == 0;
}

WOORT_NODISCARD bool woort_DynBox_debug_check_is_valid(
    woort_DynBox may_not_a_valid_box)
{
    if (0 == (may_not_a_valid_box.m_boxed & 0b0111))
    {
        // Check pointer is valid?
        woort_GCUnit* const p = _woort_boxed_to_gcunit(may_not_a_valid_box.m_boxed);
        if (p != NULL
            && (woomem_validate_addr(p) != p
                || (p->m_proxy != &WOORT_EX_BOX_PROXY
                    && p->m_proxy != &WOORT_GCSTRING_UNIT_PROXY
                    && p->m_proxy != &WOORT_GCVEC_UNIT_PROXY
                    && p->m_proxy != &WOORT_GCMAP_UNIT_PROXY
                    && p->m_proxy != &WOORT_GCSTRUCT_UNIT_PROXY
                    && p->m_proxy != &WOORT_GCHANDLE_UNIT_PROXY
                    && p->m_proxy != &WOORT_GCCLOSURE_UNIT_PROXY)))
        {
            return false;
        }
    }
    return true;
}
