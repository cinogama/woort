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
#include "woort_utf8.h"
#include "woort_opcode_dispatcher.h"
#include "woort_codeenv.h"

#include <string.h>

const int32_t WOORT_VM_OFFSETOF_JIT_CALL_DEPTH = (int32_t)offsetof(woort_VMRuntime, m_jit_call_depth);
const int32_t WOORT_VM_OFFSETOF_IP = (int32_t)offsetof(woort_VMRuntime, m_ip);
const int32_t WOORT_VM_OFFSETOF_SP = (int32_t)offsetof(woort_VMRuntime, m_sp);
const int32_t WOORT_VM_OFFSETOF_SB = (int32_t)offsetof(woort_VMRuntime, m_sb);
const int32_t WOORT_VM_OFFSETOF_ENV = (int32_t)offsetof(woort_VMRuntime, m_env);
const int32_t WOORT_VM_OFFSETOF_STACK = (int32_t)offsetof(woort_VMRuntime, m_stack);
const int32_t WOORT_VM_OFFSETOF_STACK_END = (int32_t)offsetof(woort_VMRuntime, m_stack_end);
const int32_t WOORT_VM_OFFSETOF_CHECK_REQUEST_MASK = (int32_t)offsetof(woort_VMRuntime, m_check_request_mask);

const int32_t WOORT_GCSTRUCT_OFFSETOF_DATAS =
    (int32_t)offsetof(woort_GCStruct, m_datas);
const int32_t WOORT_GCCLOSURE_OFFSETOF_SCRIPT_FUNCTION =
    (int32_t)offsetof(woort_GCClosure, m_script_function);
const int32_t WOORT_GCCLOSURE_OFFSETOF_JIT_FUNCTION =
    (int32_t)offsetof(woort_GCClosure, m_jit_function);
const int32_t WOORT_GCCLOSURE_OFFSETOF_SIZE =
    (int32_t)offsetof(woort_GCClosure, m_size);
const int32_t WOORT_GCCLOSURE_OFFSETOF_DATAS =
    (int32_t)offsetof(woort_GCClosure, m_datas);

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

WOORT_NODISCARD woort_Value* woort_JIT_make_pvalue(woort_Value src)
{
    woort_Value* const vp =
        woort_GCUnit_alloc_delay_init(sizeof(woort_Value));

    woort_GC_init_write_barrier_value(vp, src);
    woort_GCUnit_init_delay_alloc(A, vp);

    return vp;
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

void woort_JIT_unbox_dyn_no_check(woort_DynBox val, woort_Value* out)
{
    woort_DynBox_unbox_no_check(val, out);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_int(woort_GCMap* map, woort_Int key)
{
    return woort_GCMap_get_bucket_val_by_int(map, key);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_real(woort_GCMap* map, woort_BoxedValue real_bits)
{
    woort_Real key;
    memcpy(&key, &real_bits, sizeof(woort_Real));
    return woort_GCMap_get_bucket_val_by_real(map, key);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_bool(woort_GCMap* map, woort_Int key)
{
    return woort_GCMap_get_bucket_val_by_bool(map, key != 0);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_dyn(woort_GCMap* map, woort_DynBox key)
{
    return woort_GCMap_get_bucket_val_by_dynbox(map, key);
}

WOORT_NODISCARD bool woort_JIT_ldidstring(
    const woort_GCString* str, woort_Int idx, woort_Value* out)
{
    char32_t ch;
    if (woort_u8stridx(str->m_content, str->m_length, (size_t)idx, &ch))
    {
        out->m_integer = (woort_Int)ch;
        return true;
    }
    return false;
}

void woort_JIT_store_dynbox_int(woort_DynBox* dst, woort_Int val)
{
    woort_DynBox_box_int_with_barrier(dst, val);
}

void woort_JIT_store_dynbox_real(woort_DynBox* dst, woort_BoxedValue real_bits)
{
    woort_Real val;
    memcpy(&val, &real_bits, sizeof(woort_Real));
    woort_DynBox_box_real_with_barrier(dst, val);
}

void woort_JIT_store_dynbox_bool(woort_DynBox* dst, woort_Int val)
{
    woort_DynBox_box_bool_with_barrier(dst, val != 0);
}

void woort_JIT_store_dynbox_dyn(woort_DynBox* dst, woort_DynBox val)
{
    woort_GC_mixed_write_barrier_dynbox(dst, val);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_int(woort_GCMap* map, woort_Int key)
{
    return woort_GCMap_get_or_create_bucket_val_by_int(map, key);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_real(woort_GCMap* map, woort_BoxedValue real_bits)
{
    woort_Real key;
    memcpy(&key, &real_bits, sizeof(woort_Real));
    return woort_GCMap_get_or_create_bucket_val_by_real(map, key);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_bool(woort_GCMap* map, woort_Int key)
{
    return woort_GCMap_get_or_create_bucket_val_by_bool(map, key != 0);
}

/* OPTIONAL */ woort_DynBox* woort_JIT_map_get_or_create_dyn(woort_GCMap* map, woort_DynBox key)
{
    return woort_GCMap_get_or_create_bucket_val_by_dynbox(map, key);
}

WOORT_NODISCARD bool woort_JIT_check_int_ex(woort_BoxedValue val)
{
    return val != 0
        && _woort_boxed_to_gcunit(val)->m_proxy == &WOORT_EX_BOX_PROXY
        && _woort_boxed_to_exvalue(val)->m_is_int;
}

WOORT_NODISCARD bool woort_JIT_check_real_ex(woort_BoxedValue val)
{
    return val != 0
        && _woort_boxed_to_gcunit(val)->m_proxy == &WOORT_EX_BOX_PROXY
        && !_woort_boxed_to_exvalue(val)->m_is_int;
}

WOORT_NODISCARD bool woort_JIT_check_gc(
    woort_BoxedValue val, woort_BoxValueType type)
{
    if (type == WOORT_BOX_VALUE_TYPE_NIL)
        return val == 0;

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

    return unit->m_proxy == expected;
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

void woort_JIT_pre_scan_jump_targets(
    const woort_CodeEnv* cenv,
    const woort_Bytecode* func_start,
    woort_JIT_JumpTargetCallback callback,
    void* user_data)
{
    const woort_Bytecode* const codes = cenv->m_code_begin;

    const woort_FunctionBoundary* const boundaries =
        (const woort_FunctionBoundary*)cenv->m_function_boundaries.m_data;
    const size_t boundary_count = cenv->m_function_boundaries.m_size;
    const uint32_t offset = (uint32_t)(func_start - codes);

    const woort_Bytecode* func_end = func_start;
    for (size_t i = 0; i < boundary_count; ++i)
    {
        if (boundaries[i].m_offset_begin == offset)
        {
            func_end = func_start + boundaries[i].m_code_length;
            break;
        }
    }

    const woort_Bytecode* c = func_start;
    while (c < func_end)
    {
        const woort_Bytecode bc = c[0];
        const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
        const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

        switch (op6)
        {
        case WOORT_OPCODE_JFWD:
        case WOORT_OPCODE_JBCK:
        case WOORT_OPCODE_JIFINITED:
            callback(codes +
                (woort_Opcode_CodeAbs)WOORT_BYTECODE(MABC26, bc), user_data);
            break;

        case WOORT_OPCODE_JFWDCND:
            if (m2 <= 1u)
                callback(c + (uint16_t)WOORT_BYTECODE(BC16, bc), user_data);
            else
                callback(c + (uint8_t)WOORT_BYTECODE(C8, bc), user_data);
            break;

        case WOORT_OPCODE_JBCKCND:
            if (m2 <= 1u)
                callback(c - (uint16_t)WOORT_BYTECODE(BC16, bc), user_data);
            else
                callback(c - (uint8_t)WOORT_BYTECODE(C8, bc), user_data);
            break;

        case WOORT_OPCODE_JFDCMP:
            callback(c + (uint8_t)WOORT_BYTECODE(C8, bc), user_data);
            break;

        case WOORT_OPCODE_JBCKCMP:
            callback(c - (uint8_t)WOORT_BYTECODE(C8, bc), user_data);
            break;

        default:
            break;
        }

        c = woort_OpcodeDispatcher_decode(c, NULL, NULL);
    }
}
