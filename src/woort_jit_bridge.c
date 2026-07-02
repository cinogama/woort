#include "woort_jit_bridge.h"

#include "woort_vm.h"
#include "woort_gc.h"
#include "woort_gc_string.h"
#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc_closure.h"
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
    woort_Int idx, const woort_Value* src)
{
    woort_GCStruct* const gcstruct = woort_GCStruct_new(2);

    gcstruct->m_datas[0].m_integer = idx;
    woort_GC_init_write_barrier_value(&gcstruct->m_datas[1], *src);

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
