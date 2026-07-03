#include "woort_jit_bridge.h"

#include "woort_vm.h"
#include "woort_gc.h"
#include "woort_gc_string.h"
#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc_closure.h"
#include "woort_gc_gchandle.h"
#include "woort_serialize.h"
#include "woort_opcode_dispatcher.h"

#include <string.h>

const int32_t WOORT_VM_OFFSETOF_JIT_CALL_DEPTH = (int32_t)offsetof(woort_VMRuntime, m_jit_call_depth);
const int32_t WOORT_VM_OFFSETOF_IP = (int32_t)offsetof(woort_VMRuntime, m_ip);
const int32_t WOORT_VM_OFFSETOF_SP = (int32_t)offsetof(woort_VMRuntime, m_sp);
const int32_t WOORT_VM_OFFSETOF_SB = (int32_t)offsetof(woort_VMRuntime, m_sb);
const int32_t WOORT_VM_OFFSETOF_ENV = (int32_t)offsetof(woort_VMRuntime, m_env);
const int32_t WOORT_VM_OFFSETOF_STACK = (int32_t)offsetof(woort_VMRuntime, m_stack);
const int32_t WOORT_VM_OFFSETOF_STACK_END = (int32_t)offsetof(woort_VMRuntime, m_stack_end);
const int32_t WOORT_VM_OFFSETOF_CHECK_REQUEST_MASK = (int32_t)offsetof(woort_VMRuntime, m_check_request_mask);

const void* const WOORT_JIT_GC_MARKING_STATE_FLAG_ADDR =
    (const void*)&woomem_gc_marking_state_flag;

void woort_JIT_GC_mixed_write_barrier_value(
    woort_Value* modified_value, woort_Value src_value)
{
    woort_GC_mixed_write_barrier_value(modified_value, src_value);
}

WOORT_NODISCARD woort_GCVec* woort_JIT_make_vec(
    woort_Value* sp, size_t count)
{
    woort_GCVec* const gcvec = woort_GCVec_new();

    _woort_GCVec_extern(gcvec, count);

    for (size_t i = 1; i <= count; ++i)
        woort_GC_init_write_barrier_dynbox(
            &gcvec->m_datas[count - i], sp[i].m_dynamic);

    return gcvec;
}

WOORT_NODISCARD woort_GCMap* woort_JIT_make_map(
    woort_Value* sp, size_t count)
{
    woort_GCMap* const gcmap = woort_GCMap_new();

    woort_GCMap_reserve(gcmap, count);

    for (size_t i = 0; i < count; ++i)
    {
        woort_DynBox val = sp[1 + i * 2].m_dynamic;
        woort_DynBox key = sp[2 + i * 2].m_dynamic;
        woort_GCMap_set_or_insert(gcmap, key, val);
    }

    return gcmap;
}

WOORT_NODISCARD woort_GCStruct* woort_JIT_make_struct(
    woort_Value* sp, size_t count)
{
    woort_GCStruct* const gcstruct = woort_GCStruct_new(count);

    for (size_t i = 1; i <= count; ++i)
        woort_GC_init_write_barrier_value(
            &gcstruct->m_datas[count - i], sp[i]);

    return gcstruct;
}

WOORT_NODISCARD woort_GCStruct* woort_JIT_make_union(
    woort_Int idx, woort_Value src)
{
    woort_GCStruct* const gcstruct = woort_GCStruct_new(2);

    gcstruct->m_datas[0].m_integer = idx;
    woort_GC_init_write_barrier_value(&gcstruct->m_datas[1], src);

    return gcstruct;
}

WOORT_NODISCARD woort_GCClosure* woort_JIT_make_closure(
    const woort_GCClosure* tmpl, woort_Value* sp, size_t count)
{
    woort_GCClosure* const gcclosure = woort_GCClosure_new(tmpl, count);

    for (size_t i = 0; i < count; ++i)
        gcclosure->m_datas[i] = sp[1 + i];

    return gcclosure;
}

WOORT_NODISCARD woort_BoxedValue woort_JIT_box_int_ex(woort_Int val)
{
    woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_delay_init(
        sizeof(woort_BoxedExValue));

    ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

    ex_box->m_is_int = true;
    ex_box->m_int = val;

    woort_GCUnit_init_delay_alloc(O, ex_box);

    return _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
}

WOORT_NODISCARD woort_BoxedValue woort_JIT_box_real_ex(woort_Real val)
{
    woort_BoxedExValue* const ex_box = woort_GCUnit_alloc_delay_init(
        sizeof(woort_BoxedExValue));

    ex_box->m_unit.m_proxy = &WOORT_EX_BOX_PROXY;

    ex_box->m_is_int = false;
    ex_box->m_real = val;

    woort_GCUnit_init_delay_alloc(O, ex_box);

    return _woort_gcunit_to_boxed((woort_GCUnit*)ex_box);
}

WOORT_NODISCARD bool woort_JIT_unbox_int_ex(woort_BoxedValue val, woort_Int* out)
{
    if (val != 0
        && _woort_boxed_to_gcunit(val)->m_proxy == &WOORT_EX_BOX_PROXY
        && _woort_boxed_to_exvalue(val)->m_is_int)
    {
        *out = _woort_boxed_to_exvalue(val)->m_int;
        return true;
    }
    return false;
}

WOORT_NODISCARD bool woort_JIT_unbox_real_ex(woort_BoxedValue val, woort_Real* out)
{
    if (val != 0
        && _woort_boxed_to_gcunit(val)->m_proxy == &WOORT_EX_BOX_PROXY
        && !_woort_boxed_to_exvalue(val)->m_is_int)
    {
        *out = _woort_boxed_to_exvalue(val)->m_real;
        return true;
    }
    return false;
}

WOORT_NODISCARD bool woort_JIT_unbox_gc(
    woort_BoxedValue val, woort_BoxValueType type, woort_Value* out)
{
    if (type == WOORT_BOX_VALUE_TYPE_NIL)
    {
        if (val == 0)
        {
            out->m_gcinstance = NULL;
            return true;
        }
        return false;
    }

    if ((val & 0b0111) || val == 0)
        return false;

    const woort_GCUnit* const unit = _woort_boxed_to_gcunit(val);
    const woort_GCUnitProxy* const expected =
        type == WOORT_BOX_VALUE_TYPE_STRING   ? &WOORT_GCSTRING_UNIT_PROXY   :
        type == WOORT_BOX_VALUE_TYPE_VEC      ? &WOORT_GCVEC_UNIT_PROXY      :
        type == WOORT_BOX_VALUE_TYPE_MAP      ? &WOORT_GCMAP_UNIT_PROXY      :
        type == WOORT_BOX_VALUE_TYPE_STRUCT   ? &WOORT_GCSTRUCT_UNIT_PROXY   :
        type == WOORT_BOX_VALUE_TYPE_GCHANDLE ? &WOORT_GCHANDLE_UNIT_PROXY  :
                                                &WOORT_GCCLOSURE_UNIT_PROXY;

    if (unit->m_proxy == expected)
    {
        out->m_gcinstance = (woort_GCUnit*)unit;
        return true;
    }
    return false;
}

WOORT_NODISCARD woort_Int woort_JIT_GCString_to_bool(const woort_GCString* str)
{
    return (woort_Int)(0 == strcmp("true", str->m_content));
}

WOORT_NODISCARD const woort_GCString* woort_JIT_GCString_from_bool(woort_Int value)
{
    return value
        ? woort_GCString_make_string("true", 4)
        : woort_GCString_make_string("false", 5);
}

WOORT_NODISCARD woort_Int woort_JIT_serialize_vec(woort_Value* dst, woort_GCVec* src)
{
    woort_Value tmp;
    tmp.m_vec = src;
    return (woort_Int)_woort_serialize_vec_impl(dst, &tmp, WOORT_SERIALIZE_FLAG_NONE);
}

WOORT_NODISCARD woort_Int woort_JIT_serialize_map(woort_Value* dst, woort_GCMap* src)
{
    woort_Value tmp;
    tmp.m_map = src;
    return (woort_Int)_woort_serialize_map_impl(dst, &tmp, WOORT_SERIALIZE_FLAG_NONE);
}

WOORT_NODISCARD const woort_Bytecode* woort_JIT_CodeEnv_codes(const woort_CodeEnv* cenv)
{
    return cenv->m_code_begin;
}

WOORT_NODISCARD size_t woort_JIT_CodeEnv_constant_count(const woort_CodeEnv* cenv)
{
    return cenv->m_const_records.m_size;
}

WOORT_NODISCARD const woort_Value* woort_JIT_CodeEnv_static_data(const woort_CodeEnv* cenv)
{
    return cenv->m_data_begin;
}

WOORT_NODISCARD const woort_Bytecode* woort_JIT_next_bytecode(const woort_Bytecode* bc)
{
    return woort_OpcodeDispatcher_decode(bc, NULL, NULL);
}
