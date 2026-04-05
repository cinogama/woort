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

void _woort_set_value(
    woort_Value* dst, woort_Value src)
{
    assert(dst != NULL);

    *dst = src;
}

void _woort_set_nil(
    woort_Value* dst)
{
    assert(dst != NULL);

    memset(dst, 0, sizeof(woort_Value));
}

void _woort_set_bool(
    woort_Value* dst, bool src)
{
    assert(dst != NULL);

    dst->m_integer = src ? 1 : 0;
}

void _woort_set_int(
    woort_Value* dst, woort_Int src)
{
    assert(dst != NULL);

    dst->m_integer = src;
}

void _woort_set_real(
    woort_Value* dst, woort_Real src)
{
    assert(dst != NULL);

    dst->m_real = src;
}

void _woort_set_float(
    woort_Value* dst, float src)
{
    assert(dst != NULL);

    dst->m_real = (woort_Real)src;
}

void _woort_set_string(
    woort_Value* dst, woort_U8CString src)
{
    assert(dst != NULL);
    assert(src != NULL);

    size_t len = strlen(src);
    const woort_GCString* str = woort_GCString_make_string(src, len);
    assert(str != NULL);

    dst->m_string = str;
}

void _woort_set_buffer(
    woort_Value* dst, const void* src, size_t len)
{
    assert(dst != NULL);
    assert(src != NULL);

    const woort_GCString* buf = woort_GCString_make_string((const char*)src, len);
    assert(buf != NULL);

    dst->m_string = buf;
}

void _woort_set_vec(
    woort_Value* dst, size_t cap)
{
    assert(dst != NULL);

    woort_GCVec* vec = woort_GCVec_new();
    assert(vec != NULL);

    if (cap > 0)
        woort_GCVec_resize(vec, cap);

    dst->m_vec = vec;
}

void _woort_set_map(
    woort_Value* dst, size_t reserve)
{
    assert(dst != NULL);

    woort_GCMap* map = woort_GCMap_new();
    assert(map != NULL);

    if (reserve > 0)
        woort_GCMap_reserve(map, reserve);

    dst->m_map = map;
}

void _woort_set_struct(
    woort_Value* dst, size_t reserve)
{
    assert(dst != NULL);

    woort_GCStruct* s = woort_GCStruct_new(reserve);
    assert(s != NULL);

    dst->m_struct = s;
}

void _woort_set_box_int(
    woort_Value* dst, woort_Int src)
{
    assert(dst != NULL);

    woort_DynBox boxed = woort_DynBox_box_int(src);
    dst->m_dynamic = boxed;
}

void _woort_set_box_real(
    woort_Value* dst, woort_Real src)
{
    assert(dst != NULL);

    woort_DynBox boxed = woort_DynBox_box_real(src);
    dst->m_dynamic = boxed;
}

void _woort_set_box_bool(
    woort_Value* dst, bool src)
{
    assert(dst != NULL);

    woort_DynBox boxed = woort_DynBox_box_bool(src);
    dst->m_dynamic = boxed;
}

void _woort_set_gchandle(
    woort_Value* dst,
    void* addr,
    /* OPTIONAL */ woort_Value* holding,
    woort_GCHandle_UserDestructFunction close)
{
    assert(dst != NULL);

    woort_GCHandle* handle = woort_GCHandle_new(addr, holding, close);
    assert(handle != NULL);

    dst->m_gcinstance = (woort_GCUnit*)handle;
}

void _woort_set_gcstruct(
    woort_Value* dst,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close)
{
    assert(dst != NULL);

    woort_GCHandle* handle = woort_GCHandle_new_with_marker(addr, mark, close);
    assert(handle != NULL);

    dst->m_gcinstance = (woort_GCUnit*)handle;
}

/* Public Runtime API */

/*
NOTE: _WOORT_API_STACKS 用于根据 woort_StackValue 获取栈上的时机位置
考虑到用户的接口设计，我们采用 SB+3 作为操作数原点，正数表示参数（因为
SB+3 恰好是首个参数位置。-1，-2 实际上不使用（这两个位置对应 SB+1 和
SB+2，储存函数的返回状态）。当前栈帧自 -3 开始，向负数方向延申到栈顶
方向。
*/
#define _WOORT_API_STACK(N) vm->m_sb[3 + N]

void woort_set_value(
    woort_StackValue dst, woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_value(&_WOORT_API_STACK(dst), _WOORT_API_STACK(src));
}

void woort_set_nil(
    woort_StackValue dst)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_nil(&_WOORT_API_STACK(dst));
}

void woort_set_int(
    woort_StackValue dst, woort_Int src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_int(&_WOORT_API_STACK(dst), src);
}

void woort_set_real(
    woort_StackValue dst, woort_Real src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_real(&_WOORT_API_STACK(dst), src);
}

void woort_set_float(
    woort_StackValue dst, float src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_float(&_WOORT_API_STACK(dst), src);
}

void woort_set_bool(
    woort_StackValue dst, bool src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_bool(&_WOORT_API_STACK(dst), src);
}

void woort_set_string(
    woort_StackValue dst, woort_U8CString src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_string(&_WOORT_API_STACK(dst), src);
}

void woort_set_buffer(
    woort_StackValue dst, const void* src, size_t len)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_buffer(&_WOORT_API_STACK(dst), src, len);
}

void woort_set_vec(
    woort_StackValue dst, size_t cap)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_vec(&_WOORT_API_STACK(dst), cap);
}

void woort_set_map(
    woort_StackValue dst, size_t reserve)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_map(&_WOORT_API_STACK(dst), reserve);
}

void woort_set_struct(
    woort_StackValue dst, size_t cap)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_struct(&_WOORT_API_STACK(dst), cap);
}

void woort_set_box_int(
    woort_StackValue dst, woort_Int src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_box_int(&_WOORT_API_STACK(dst), src);
}

void woort_set_box_real(
    woort_StackValue dst, woort_Real src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_box_real(&_WOORT_API_STACK(dst), src);
}

void woort_set_box_bool(
    woort_StackValue dst, bool src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_box_bool(&_WOORT_API_STACK(dst), src);
}

void woort_set_box_nil(
    woort_StackValue dst)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_box_nil(&_WOORT_API_STACK(dst));
}

void woort_set_gchandle(
    woort_StackValue dst,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_gchandle(&_WOORT_API_STACK(dst), addr, &_WOORT_API_STACK(hold), close);
}

void woort_set_gcstruct(
    woort_StackValue dst,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _woort_set_gcstruct(&_WOORT_API_STACK(dst), addr, mark, close);
}

WOORT_NODISCARD woort_Int woort_int(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return _WOORT_API_STACK(src).m_integer;
}

WOORT_NODISCARD bool woort_bool(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return _WOORT_API_STACK(src).m_integer != 0;
}

WOORT_NODISCARD woort_Real woort_real(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return _WOORT_API_STACK(src).m_real;
}

WOORT_NODISCARD float woort_float(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return (float)_WOORT_API_STACK(src).m_real;
}

WOORT_NODISCARD woort_U8CString woort_string(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return _WOORT_API_STACK(src).m_string->m_content;
}

WOORT_NODISCARD const void* woort_buffer(
    woort_StackValue src, size_t* out_len)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);
    assert(out_len != NULL);

    const woort_GCString* const str = _WOORT_API_STACK(src).m_string;
    *out_len = str->m_length;
    return str->m_content;
}

WOORT_NODISCARD void* woort_gcpointer(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return ((woort_GCHandle*)_WOORT_API_STACK(src).m_gcinstance)->m_user_handle;
}

WOORT_NODISCARD bool woort_reserve_stack(
    size_t count, woort_StackValue* out_stack)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    vm->m_sp -= count;
    if (vm->m_sp < vm->m_stack)
    {
        do
        {
            vm->m_sp += count;

            if (!_woort_VMRuntime_extern_stack(vm))
            {
                woort_panic(
                    WOORT_PANIC_STACK_OVERFLOW,
                    "Stack overflow.");

                return false;
            }

            vm->m_sp -= count;

        } while (vm->m_sp < vm->m_stack);
    }

    /* NOTE: 此处实际上是 SP - [SB+3] + 1 */
    *out_stack = (woort_StackValue)((vm->m_sp - vm->m_sb) - 2);
    return true;
}