#include "woort.h"

#include "woort_codeenv.h"
#include "woort_log.h"
#include "woort_gc.h"
#include "woort_vm.h"
#include "woort_ir_compiler.h"
#include "woort_value.h"
#include "woort_gc_string.h"
#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc_gchandle.h"
#include "woort_gc_closure.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

void woort_init(void)
{
    if (!woort_CodeEnv_bootup())
    {
        WOORT_DEBUG("Failed to bootup code env.");
        abort();
    }

    woort_GC_bootup();
}
void woort_shutdown(void)
{
    woort_GC_shutdown();

    woort_CodeEnv_shutdown();
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRCompiler* woort_IRCompiler_create(void)
{
    woort_IRCompiler* c = malloc(sizeof(woort_IRCompiler));
    if (!c)
        return NULL;
    woort_IRCompiler_init(c);
    return c;
}

void woort_IRCompiler_close(woort_IRCompiler* c)
{
    assert(c != NULL);

    woort_IRCompiler_deinit(c);
    free(c);
}

/* Runtime API */

void woort_CodeEnv_set_const_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_Value v;
    v.m_integer = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_Value v;
    v.m_real = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_string(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_U8CString val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);
    assert(val != NULL);

    size_t len = strlen(val);
    const woort_GCString* str = woort_GCString_make_string(val, len);
    assert(str != NULL);

    woort_Value v;
    v.m_string = str;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_script_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_Value v;
    v.m_script_function = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_extern_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_Value v;
    v.m_script_function = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_script_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_GCClosure* closure = woort_GCClosure_new_script_func(val);
    assert(closure != NULL);

    woort_Value v;
    v.m_closure = closure;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_extern_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_NativeFunction native_func = (woort_NativeFunction)val;
    woort_GCClosure* closure = woort_GCClosure_new_native_func(native_func);
    assert(closure != NULL);

    woort_Value v;
    v.m_closure = closure;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
}

void woort_CodeEnv_set_const_box_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_DynBox boxed = woort_DynBox_box_int(val);
    woort_GC_mixed_write_barrier_dynbox(&code_env->m_data_begin[cidx].m_dynamic, boxed);
}

void woort_CodeEnv_set_const_box_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_data_count);

    woort_DynBox boxed = woort_DynBox_box_real(val);
    woort_GC_mixed_write_barrier_dynbox(&code_env->m_data_begin[cidx].m_dynamic, boxed);
}

void _woort_set_int(
    woort_Value* dst, woort_Int src)
{
    assert(dst != NULL);

    woort_Value v;
    v.m_integer = src;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_real(
    woort_Value* dst, woort_Real src)
{
    assert(dst != NULL);

    woort_Value v;
    v.m_real = src;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_float(
    woort_Value* dst, float src)
{
    assert(dst != NULL);

    woort_Value v;
    v.m_real = (woort_Real)src;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_string(
    woort_Value* dst, woort_U8CString src)
{
    assert(dst != NULL);
    assert(src != NULL);

    size_t len = strlen(src);
    const woort_GCString* str = woort_GCString_make_string(src, len);
    assert(str != NULL);

    woort_Value v;
    v.m_string = str;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_vec(
    woort_Value* dst, size_t cap)
{
    assert(dst != NULL);

    woort_GCVec* vec = woort_GCVec_new();
    assert(vec != NULL);

    if (cap > 0)
        woort_GCVec_resize(vec, cap);

    woort_Value v;
    v.m_vec = vec;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_map(
    woort_Value* dst, size_t reserve)
{
    assert(dst != NULL);

    woort_GCMap* map = woort_GCMap_new();
    assert(map != NULL);

    if (reserve > 0)
        woort_GCMap_reserve(map, reserve);

    woort_Value v;
    v.m_map = map;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_struct(
    woort_Value* dst, size_t reserve)
{
    assert(dst != NULL);

    woort_GCStruct* s = woort_GCStruct_new(reserve);
    assert(s != NULL);

    woort_Value v;
    v.m_struct = s;

    woort_GC_mixed_write_barrier_value(dst, v);
}

void _woort_set_box_int(
    woort_Value* dst, woort_Int src)
{
    assert(dst != NULL);

    woort_DynBox boxed = woort_DynBox_box_int(src);
    woort_GC_mixed_write_barrier_dynbox(&dst->m_dynamic, boxed);
}

void _woort_set_box_real(
    woort_Value* dst, woort_Real src)
{
    assert(dst != NULL);

    woort_DynBox boxed = woort_DynBox_box_real(src);
    woort_GC_mixed_write_barrier_dynbox(&dst->m_dynamic, boxed);
}