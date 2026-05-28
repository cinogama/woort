#include "woort.h"

#include "woort_builtin.h"
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
#include "woort_disassembly.h"
#include "woort_env.h"
#include "woort_path.h"
#include "woort_vfs.h"
#include "woort_dylib.h"
#include "woort_serialize.h"
#include "woort_util.h"
#include "woort_utf8.h"
#include "woort_vm_debugger_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

static bool woort_ctrl_c_callback_hooked = true;

#undef woort_init
void woort_init(int argc, char** argv)
{
    _woort_env_bootup();
    _woort_path_bootup();
    _woort_vfs_bootup();

    if (!woort_CodeEnv_bootup())
    {
        WOORT_DEBUG("Failed to bootup code env manager.");
        abort();
    }

    /*
        Parse runtime-level command-line arguments.
    */
    size_t max_reserved_memory = 1024;
    for (int command_idx = 0; command_idx + 1 < argc; command_idx++)
    {
        const char* current_arg = argv[command_idx];
        size_t arg_len = strlen(current_arg);
        if (arg_len >= 7 && strncmp(current_arg, "--woort", 7) == 0)
        {
            const char* setting = current_arg + 2;
            if (strcmp(setting, "woort-enable-ctrlc-debug") == 0)
                woort_ctrl_c_callback_hooked = (bool)atoi(argv[++command_idx]);
            else if (strcmp(setting, "woort-gc-max-reserved-memory") == 0)
                max_reserved_memory = (size_t)atoi(argv[++command_idx]);
            else
                woort_log("WOORT: Unknown command line option named: `%s`.\n", current_arg);
        }
    }

    if (woort_ctrl_c_callback_hooked)
        woort_ctrlc_setup();

    if (!woort_GC_bootup(max_reserved_memory * 1024 * 1024))
    {
        WOORT_DEBUG("Failed to bootup gc.");
        abort();
    }

    if (!_woort_dylib_bootup())
    {
        WOORT_DEBUG("Failed to bootup dylib manager.");
        abort();
    }

    if (!_woort_builtin_bootup(argc, argv))
    {
        WOORT_DEBUG("Failed to bootup builtin functions.");
        abort();
    }

    if (!woort_VMRuntime_Debugger_bootup())
    {
        WOORT_DEBUG("Failed to bootup debugger support.");
        abort();
    }
}
void woort_shutdown(void)
{
    if (woort_ctrl_c_callback_hooked)
        woort_ctrlc_teardown();

    woort_VMRuntime_Debugger_shutdown();

    woort_GC_shutdown();
    _woort_builtin_shutdown();
    _woort_dylib_shutdown();
    woort_CodeEnv_shutdown();
    _woort_path_shutdown();
    _woort_vfs_shutdown();
    _woort_env_shutdown();
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
    assert((size_t)cidx < code_env->m_constant_count);

    woort_Value v;
    v.m_integer = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_INT, NULL, NULL);
}

void woort_CodeEnv_set_const_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_Value v;
    v.m_real = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_REAL, NULL, NULL);
}

void woort_CodeEnv_set_const_buffer(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const void* buf,
    size_t buflen)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);
    assert(buf != NULL);

    const woort_GCString* str =
        woort_GCString_make_string_for_env_constant(code_env, buf, buflen);

    assert(str != NULL);

    woort_Value v;
    v.m_string = str;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_STRING, NULL, NULL);
}

void woort_CodeEnv_set_const_script_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_Value v;
    v.m_script_function = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_SCRIPT_FUNC, NULL, NULL);
}

void woort_CodeEnv_set_const_extern_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_NativeFunction val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_Value v;
    v.m_native_function = val;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_EXTERN_FUNC, NULL, NULL);
}

void woort_CodeEnv_set_const_script_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_GCClosure* const closure =
        woort_GCClosure_new_script_func_for_env_constant(
            code_env, val);

    assert(closure != NULL);

    woort_Value v;
    v.m_closure = closure;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_SCRIPT_CLOSURE, NULL, NULL);
}

void woort_CodeEnv_set_const_extern_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_NativeFunction val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_GCClosure* const closure =
        woort_GCClosure_new_native_func_for_env_constant(
            code_env, val);

    assert(closure != NULL);

    woort_Value v;
    v.m_closure = closure;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_EXTERN_CLOSURE, NULL, NULL);
}

void woort_CodeEnv_set_const_box_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_DynBox boxed = woort_DynBox_box_int_for_env_constant(code_env, val);
    woort_GC_init_write_barrier_dynbox(&code_env->m_data_begin[cidx].m_dynamic, boxed);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_BOX_INT, NULL, NULL);
}

void woort_CodeEnv_set_const_box_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_DynBox boxed = woort_DynBox_box_real_for_env_constant(code_env, val);
    woort_GC_init_write_barrier_dynbox(&code_env->m_data_begin[cidx].m_dynamic, boxed);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_BOX_REAL, NULL, NULL);
}

void woort_CodeEnv_set_const_box_bool(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    bool val)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    woort_DynBox boxed = woort_DynBox_box_bool(val);
    woort_GC_init_write_barrier_dynbox(&code_env->m_data_begin[cidx].m_dynamic, boxed);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_BOX_BOOL, NULL, NULL);
}

void woort_CodeEnv_set_const_struct(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    /* OPTIONAL if member_count == 0 */ const woort_IRConstantIndex* members,
    size_t member_count)
{
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);
    assert(member_count == 0 || members != NULL);

    woort_GCStruct* const s = 
        woort_GCStruct_new_for_env_constant(code_env, member_count);

    assert(s != NULL);

    for (size_t i = 0; i < member_count; ++i) {
        assert((size_t)members[i] < code_env->m_data_count);
        woort_GC_init_write_barrier_value(
            &s->m_datas[i], code_env->m_data_begin[members[i]]);
    }

    woort_Value v;
    v.m_struct = s;

    woort_GC_mixed_write_barrier_value(&code_env->m_data_begin[cidx], v);
    (void)woort_CodeEnv_set_const_record(code_env, cidx,
        WOORT_CONST_TYPE_STRUCT, NULL, NULL);
}

WOORT_NODISCARD woort_GCStruct* _woort_set_union(
    woort_Value* dst, woort_Int id)
{
    assert(dst != NULL);
    woort_GCStruct* const s = woort_GCStruct_new(2);

    s->m_datas[0].m_integer = id;
    /* s->m_datas[1] = ... */

    return dst->m_struct = s;
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

/* Write */

void woort_load_const(
    woort_StackValue dst, const woort_CodeEnv* code_env, woort_IRConstantIndex cidx)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);
    assert(code_env != NULL);
    assert((size_t)cidx < code_env->m_constant_count);

    _WOORT_API_STACK(dst) = code_env->m_data_begin[cidx];
}

WOORT_NODISCARD bool woort_load_extern_const(
    woort_StackValue dst, const woort_CodeEnv* code_env, const char* name)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);
    assert(code_env != NULL);
    assert(name != NULL);

    woort_IRConstantIndex cidx;
    if (!woort_CodeEnv_find_extern_constant(code_env, name, &cidx))
        return false;

    assert((size_t)cidx < code_env->m_constant_count);
    _WOORT_API_STACK(dst) = code_env->m_data_begin[cidx];
    return true;
}

void woort_set_value(
    woort_StackValue dst, woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _WOORT_API_STACK(dst) = _WOORT_API_STACK(src);
}

void woort_set_nil(
    woort_StackValue dst)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _WOORT_API_STACK(dst).m_integer = 0;
}

void woort_set_int(
    woort_StackValue dst, woort_Int src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _WOORT_API_STACK(dst).m_integer = src;
}

void woort_set_real(
    woort_StackValue dst, woort_Real src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _WOORT_API_STACK(dst).m_real = src;
}

void woort_set_float(
    woort_StackValue dst, float src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _WOORT_API_STACK(dst).m_real = (woort_Real)src;
}

void woort_set_bool(
    woort_StackValue dst, bool src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    _WOORT_API_STACK(dst).m_integer = src ? 1 : 0;
}

void woort_set_string(
    woort_StackValue dst, woort_U8CString src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const size_t len = strlen(src);
    const woort_GCString* const str = woort_GCString_make_string(src, len);
    assert(str != NULL);

    _WOORT_API_STACK(dst).m_string = str;
}

void woort_set_string_fmt(
    woort_StackValue dst, woort_U8CString fmt, ...)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    va_list args;
    va_start(args, fmt);
    const woort_GCString* const str = woort_GCString_make_format_va(fmt, args);
    va_end(args);
    assert(str != NULL);

    _WOORT_API_STACK(dst).m_string = str;
}

void woort_set_buffer(
    woort_StackValue dst, const void* src, size_t len)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_GCString* const buf = woort_GCString_make_string((const char*)src, len);
    assert(buf != NULL);

    _WOORT_API_STACK(dst).m_string = buf;
}

void woort_set_vec(
    woort_StackValue dst)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = woort_GCVec_new();
    assert(vec != NULL);

    _WOORT_API_STACK(dst).m_vec = vec;
}

void woort_set_map(
    woort_StackValue dst)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const map = woort_GCMap_new();
    assert(map != NULL);

    _WOORT_API_STACK(dst).m_map = map;
}

void woort_set_struct(
    woort_StackValue dst, size_t cap)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = woort_GCStruct_new(cap);
    assert(s != NULL);

    _WOORT_API_STACK(dst).m_struct = s;
}

void woort_set_box_int(
    woort_StackValue dst, woort_Int src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_DynBox const boxed = woort_DynBox_box_int(src);
    _WOORT_API_STACK(dst).m_dynamic = boxed;
}

void woort_set_box_real(
    woort_StackValue dst, woort_Real src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_DynBox const boxed = woort_DynBox_box_real(src);
    _WOORT_API_STACK(dst).m_dynamic = boxed;
}

void woort_set_box_bool(
    woort_StackValue dst, bool src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_DynBox const boxed = woort_DynBox_box_bool(src);
    _WOORT_API_STACK(dst).m_dynamic = boxed;
}

void woort_set_gchandle(
    woort_StackValue dst,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_GCHandle* const handle = woort_GCHandle_new(
        addr,
        hold != WOORT_IGNORE ? &_WOORT_API_STACK(hold) : NULL,
        close,
        dylib);
    assert(handle != NULL);

    _WOORT_API_STACK(dst).m_gchandle = handle;
}

void woort_set_gcstruct(
    woort_StackValue dst,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_GCHandle* const handle = woort_GCHandle_new_with_marker(addr, mark, close, dylib);
    assert(handle != NULL);

    _WOORT_API_STACK(dst).m_gchandle = handle;
}

void woort_set_union_without_value(
    woort_StackValue dst, woort_Int id)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    s->m_datas[1].m_integer = 0;
}
void woort_set_union_value(
    woort_StackValue dst, woort_Int id, woort_StackValue val)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    // NOTE: We cannot use `_woort_set_union` here because dst and val may refer 
    // to the same stack location; we need to handle it manually to ensure the order 
    // of assignment.
    woort_GCStruct* const s = woort_GCStruct_new(2);
    s->m_datas[0].m_integer = id;
    woort_GC_init_write_barrier_value(&s->m_datas[1], _WOORT_API_STACK(val));

    _WOORT_API_STACK(dst).m_struct = s;
}

void woort_set_union_nil(
    woort_StackValue dst, woort_Int id)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    memset(&s->m_datas[1], 0, sizeof(woort_Value));
}

void woort_set_union_int(
    woort_StackValue dst, woort_Int id, woort_Int src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    s->m_datas[1].m_integer = src;
}

void woort_set_union_real(
    woort_StackValue dst, woort_Int id, woort_Real src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    s->m_datas[1].m_real = src;
}

void woort_set_union_float(
    woort_StackValue dst, woort_Int id, float src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    s->m_datas[1].m_real = (woort_Real)src;
}

void woort_set_union_bool(
    woort_StackValue dst, woort_Int id, bool src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    s->m_datas[1].m_integer = src ? 1 : 0;
}

void woort_set_union_string(
    woort_StackValue dst, woort_Int id, woort_U8CString src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    const size_t len = strlen(src);
    const woort_GCString* const str = woort_GCString_make_string(src, len);
    assert(str != NULL);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_string, (void*)str);
}

void woort_set_union_string_fmt(
    woort_StackValue dst, woort_Int id, woort_U8CString fmt, ...)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    va_list args;
    va_start(args, fmt);
    const woort_GCString* const str = woort_GCString_make_format_va(fmt, args);
    va_end(args);
    assert(str != NULL);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_string, (void*)str);
}

void woort_set_union_buffer(
    woort_StackValue dst, woort_Int id, const void* src, size_t len)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    const woort_GCString* const buf = woort_GCString_make_string((const char*)src, len);
    assert(buf != NULL);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_string, (void*)buf);
}

void woort_set_union_vec(
    woort_StackValue dst, woort_Int id, size_t cap)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    woort_GCVec* const vec = woort_GCVec_new();
    assert(vec != NULL);
    if (cap > 0)
        woort_GCVec_resize_without_init(vec, cap);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_vec, vec);
}

void woort_set_union_map(
    woort_StackValue dst, woort_Int id, size_t reserve)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    woort_GCMap* const map = woort_GCMap_new();
    assert(map != NULL);
    if (reserve > 0)
        woort_GCMap_reserve(map, reserve);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_map, map);
}

void woort_set_union_struct(
    woort_StackValue dst, woort_Int id, size_t cap)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    woort_GCStruct* const inner = woort_GCStruct_new(cap);
    assert(inner != NULL);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_struct, inner);
}

void woort_set_union_gchandle(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    const woort_GCHandle* const handle =
        woort_GCHandle_new(
            addr,
            hold != WOORT_IGNORE ? &_WOORT_API_STACK(hold) : NULL,
            close,
            dylib);
    assert(handle != NULL);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_gchandle, (void*)handle);
}

void woort_set_union_gcstruct(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    const woort_GCHandle* const handle =
        woort_GCHandle_new_with_marker(addr, mark, close, dylib);
    assert(handle != NULL);

    woort_GC_init_write_barrier_gcunit(
        (void**)&s->m_datas[1].m_gchandle, (void*)handle);
}

void woort_set_union_box_int(
    woort_StackValue dst, woort_Int id, woort_Int src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    woort_DynBox const boxed = woort_DynBox_box_int(src);
    woort_GC_init_write_barrier_dynbox(&s->m_datas[1].m_dynamic, boxed);
}

void woort_set_union_box_real(
    woort_StackValue dst, woort_Int id, woort_Real src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    woort_DynBox const boxed = woort_DynBox_box_real(src);
    woort_GC_init_write_barrier_dynbox(&s->m_datas[1].m_dynamic, boxed);
}

void woort_set_union_box_bool(
    woort_StackValue dst, woort_Int id, bool src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _woort_set_union(&_WOORT_API_STACK(dst), id);
    woort_DynBox const boxed = woort_DynBox_box_bool(src);
    woort_GC_init_write_barrier_dynbox(&s->m_datas[1].m_dynamic, boxed);
}

/* Read */

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

    void* const r = _WOORT_API_STACK(src).m_gchandle->m_user_handle;
    if (r == NULL)
        woort_panic(WOORT_PANIC_ALREADY_CLOSED, "This gchandle already closed.");
    return r;
}

WOORT_NODISCARD bool woort_push_reserve(
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
                return false;

            vm->m_sp -= count;

        } while (vm->m_sp < vm->m_stack);
    }

    /* NOTE: 此处实际上是 SP - [SB+3] + 1 */
    *out_stack = (woort_StackValue)((vm->m_sp - vm->m_sb) - 2);
    return true;
}

void woort_pop(size_t count)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    vm->m_sp += count;
}

WOORT_NODISCARD woort_Value* woort_internal_value(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return &vm->m_sb[3 + src];
}

void woort_import_value(
    woort_StackValue dst,
    woort_VMRuntime* src_vm,
    woort_StackValue src_in_vm)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);
    assert(src_vm != NULL);

    woort_GC_mixed_write_barrier_value(
        &_WOORT_API_STACK(dst),
        src_vm->m_sb[3 + src_in_vm]);
}

WOORT_NODISCARD bool _woort_pre_invoke(woort_VMRuntime* vm, const woort_GCClosure* target)
{
    while (vm->m_sp - 2 - target->m_size < vm->m_stack)
    {
        if (!_woort_VMRuntime_extern_stack(vm))
        {
            woort_panic(
                WOORT_PANIC_STACK_OVERFLOW,
                "Stack overflow.");

            return false;
        }
    }

    vm->m_sp -= 2;
    // Set call way and bp offset.
    vm->m_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_FROM_NATIVE;
    vm->m_sp[1].m_ret_bp.m_bp_offset =
        (uint32_t)(vm->m_stack_end - vm->m_sb);

    // Set ret addr (Only for trace).
    vm->m_sp[2].m_ret_addr = vm->m_ip /* trace from current. */;

    vm->m_sb = vm->m_sp;

    // Expand arguments from `target`
    if (target->m_size != 0)
    {
        vm->m_sp -= target->m_size;
        memcpy(
            vm->m_sp + 1,
            target->m_datas,
            sizeof(woort_Value) * target->m_size);
    }
    return true;
}

WOORT_NODISCARD woort_VmCallStatus woort_resume(
    woort_StackValue dst)
{
    /*
    NOTE: VM 已经保存到之前的状态，直接继续派发执行即可
    */
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;

    switch (_woort_VMRuntime_dispatch(vm))
    {
    case WOORT_VM_CALL_STATUS_NORMAL:
        // Fetch return value.
        if (dst != WOORT_IGNORE)
            _WOORT_API_STACK(dst) = *vm->m_sp;
        return WOORT_VM_CALL_STATUS_NORMAL;
    case WOORT_VM_CALL_STATUS_YIELD:
        /*
        NOTE: 此处实现存在瑕疵，考虑：
                1）Target function 是 Native function，且尝试 Yield.
            此时返回 YIELD 会导致后续执行 step 失败，但是考虑到这种情况
            几乎不会发生，暂时先搁置。
        */
        return WOORT_VM_CALL_STATUS_YIELD;
    case WOORT_VM_CALL_STATUS_ABORTED:
        return WOORT_VM_CALL_STATUS_ABORTED;
    default:
        // Unexpected status, should not happend!
        abort();
    }
}

WOORT_NODISCARD woort_VmCallStatus woort_spawn(
    woort_StackValue dst, woort_StackValue f)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    const woort_GCClosure* const target = _WOORT_API_STACK(f).m_closure;

    if (!_woort_pre_invoke(vm, target))
        return WOORT_VM_CALL_STATUS_ABORTED;

    if (target->m_script_function != NULL)
    {
        vm->m_ip = target->m_script_function;
        if (target->m_jit_function != NULL)
        {
            if (target->m_jit_function(vm, vm->m_sb) == WOORT_VM_CALL_STATUS_NORMAL)
            {
                if (dst != WOORT_IGNORE)
                    _WOORT_API_STACK(dst) = *vm->m_sp;
                return WOORT_VM_CALL_STATUS_NORMAL;
            }
            /* else, WOORT_VM_CALL_STATUS_RESYNC, need vm to rehandle. */
        }
        else if (vm->m_env == NULL
            || target->m_script_function < vm->m_env->m_code_begin
            || target->m_script_function >= vm->m_env->m_code_end)
        {
            // Target out of env, refetch env.
            woort_CodeEnv* env;
            if (!woort_CodeEnv_find(target->m_script_function, &env))
            {
                woort_panic(
                    WOORT_PANIC_CODE_ENV_NOT_FOUND,
                    "Cannot find code environment from `%p`.", vm->m_ip);
                return WOORT_VM_CALL_STATUS_ABORTED;
            }
            // Ok, apply new env.
            vm->m_env = env;
        }
    }
    else
    {
        const woort_Bytecode* const origin_ip = vm->m_ip;
        vm->m_ip = (const woort_Bytecode*)target->m_script_function;

        const woort_VmCallStatus r = target->m_native_function();
        /*
        NOTE: Restore preinvoke status.
            仅 Native function call 需要在此处手动恢复调用状态，其他的调用
            会由 RET/RETV 指令自动恢复调用栈

        TODO: 考虑 Native function API 在 return 时处理返回？
        */
        vm->m_ip = origin_ip;
        vm->m_sp = vm->m_sb + 2;

        assert(vm->m_sp[-1].m_ret_bp.m_way == WOORT_CALL_WAY_FROM_NATIVE);
        vm->m_sb = vm->m_stack_end - vm->m_sp[-1].m_ret_bp.m_bp_offset;

        switch (r)
        {
        case WOORT_VM_CALL_STATUS_NORMAL:
            // Fetch return value.
            if (dst != WOORT_IGNORE)
                _WOORT_API_STACK(dst) = *vm->m_sp;
            return WOORT_VM_CALL_STATUS_NORMAL;
        case WOORT_VM_CALL_STATUS_RESYNC:
            /*
            NOTE: Native function 仅当以下情况发生时会返回 WOORT_VM_CALL_STATUS_RESYNC：
                1) 发生 Abort
                2）发生 Yield
                无论上述何种情况，直接回落到 SIM 执行处理即可
            */
            break;
        default:
            // Unexpected status, should not happend!
            abort();
        }
    }

    return woort_resume(dst);
}

WOORT_NODISCARD woort_VmCallStatus woort_bootup_codeenv(
    woort_StackValue dst, woort_CodeEnv* cenv)
{
    woort_StackValue v;
    if (!woort_push_reserve(1, &v))
    {
        woort_panic(WOORT_PANIC_STACK_OVERFLOW, "Stack overflow.");
        return WOORT_VM_CALL_STATUS_ABORTED;
    }

    if (!woort_load_extern_const(v, cenv, WOORT_DEFAULT_ENTRY))
    {
        woort_panic(WOORT_PANIC_STACK_OVERFLOW, "Cannot find entry: `" WOORT_DEFAULT_ENTRY "`.");
        return WOORT_VM_CALL_STATUS_ABORTED;
    }

    const woort_VmCallStatus result = woort_invoke(dst, v);
    
    if (result == WOORT_VM_CALL_STATUS_NORMAL)
        woort_pop(1);

    return result;
}

WOORT_NODISCARD woort_VmCallStatus woort_invoke(
    woort_StackValue dst, woort_StackValue f)
{
    const woort_VmCallStatus r = woort_spawn(dst, f);
    if (r == WOORT_VM_CALL_STATUS_YIELD)
    {
        woort_panic(
            WOORT_PANIC_BAD_VM_REQUEST,
            "Cannot yield during `woort_invoke`.");

        return WOORT_VM_CALL_STATUS_ABORTED;
    }
    return r;
}

WOORT_NODISCARD woort_Int woort_unbox_int(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_Value out_val;
    if (!woort_DynBox_unbox(
        _WOORT_API_STACK(src).m_dynamic,
        WOORT_BOX_VALUE_TYPE_INT,
        &out_val))
    {
        woort_panic(WOORT_PANIC_BAD_TYPE, "Expected boxed int.");
    }

    return out_val.m_integer;
}

WOORT_NODISCARD woort_Real woort_unbox_real(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_Value out_val;
    if (!woort_DynBox_unbox(
        _WOORT_API_STACK(src).m_dynamic,
        WOORT_BOX_VALUE_TYPE_REAL,
        &out_val))
    {
        woort_panic(WOORT_PANIC_BAD_TYPE, "Expected boxed real.");
    }

    return out_val.m_real;
}

WOORT_NODISCARD bool woort_unbox_bool(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_Value out_val;
    if (!woort_DynBox_unbox(
        _WOORT_API_STACK(src).m_dynamic,
        WOORT_BOX_VALUE_TYPE_BOOL,
        &out_val))
    {
        woort_panic(WOORT_PANIC_BAD_TYPE, "Expected boxed bool.");
    }

    return out_val.m_integer != 0;
}

WOORT_NODISCARD woort_BoxValueType woort_unbox_type(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_DynBox val = _WOORT_API_STACK(src).m_dynamic;

    if (val.m_boxed & 0b0111)
    {
        if (0b01 & val.m_boxed)
            return WOORT_BOX_VALUE_TYPE_REAL;

        if (0 == (0b011 & (val.m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
            return WOORT_BOX_VALUE_TYPE_INT;

        return WOORT_BOX_VALUE_TYPE_BOOL;
    }

    if (val.m_boxed == 0)
        return WOORT_BOX_VALUE_TYPE_NIL;

    const woort_GCUnitProxy* const proxy = _woort_boxed_to_gcunit(val.m_boxed)->m_proxy;

    if (proxy == &WOORT_EX_BOX_PROXY)
    {
        return _woort_boxed_to_exvalue(val.m_boxed)->m_is_int
            ? WOORT_BOX_VALUE_TYPE_INT
            : WOORT_BOX_VALUE_TYPE_REAL;
    }

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

    woort_panic(WOORT_PANIC_BAD_TYPE, "Unknown boxed type.");
    return WOORT_BOX_VALUE_TYPE_NIL;
}

WOORT_NODISCARD woort_BoxValueType woort_unbox(
    woort_StackValue dst,
    woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    return woort_DynBox_unbox_no_check_and_get_type(
        _WOORT_API_STACK(src).m_dynamic,
        &_WOORT_API_STACK(dst));
}

WOORT_NODISCARD woort_Int woort_union_get(
    woort_StackValue dst, woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _WOORT_API_STACK(src).m_struct;
    assert(s != NULL);

    _WOORT_API_STACK(dst) = s->m_datas[1];

    return s->m_datas[0].m_integer;
}

/* ========== Struct ========== */

WOORT_NODISCARD size_t woort_struct_len(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _WOORT_API_STACK(src).m_struct;
    assert(s != NULL);

    return s->m_size;
}

void woort_struct_get(
    woort_StackValue dst,
    woort_StackValue src,
    size_t index)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _WOORT_API_STACK(src).m_struct;
    assert(s != NULL);
    assert(index < s->m_size);

    woort_GC_mixed_write_barrier_value(&_WOORT_API_STACK(dst), s->m_datas[index]);
}

void woort_struct_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue val)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCStruct* const s = _WOORT_API_STACK(src).m_struct;
    assert(s != NULL);
    assert(index < s->m_size);

    woort_GC_mixed_write_barrier_value(&s->m_datas[index], _WOORT_API_STACK(val));
}

/* ========== Vector ========== */

WOORT_NODISCARD size_t woort_vec_len(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    return vec->m_length;
}

void woort_vec_resize(woort_StackValue src, size_t new_size)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    const size_t origin_size = vec->m_length;
    woort_GCVec_resize_without_init(vec, new_size);
    if (new_size > origin_size)
        memset(vec->m_datas + origin_size, 0, (new_size - origin_size) * sizeof(woort_DynBox));
}

void woort_vec_resize_with(
    woort_StackValue src,
    size_t new_size,
    woort_StackValue init_val)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    woort_GCVec_resize_with(vec, new_size, _WOORT_API_STACK(init_val).m_dynamic);
}

WOORT_NODISCARD bool woort_vec_shrink(
    woort_StackValue src,
    size_t new_size)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    return woort_GCVec_shrink(vec, new_size);
}

WOORT_NODISCARD bool woort_vec_get(
    woort_StackValue dst_boxed,
    woort_StackValue src,
    size_t index)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    woort_DynBox boxval;
    if (!woort_GCVec_get(vec, index, &boxval))
        return false;

    _WOORT_API_STACK(dst_boxed).m_dynamic = boxval;
    return true;
}

WOORT_NODISCARD bool woort_vec_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue boxed_elem)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    return woort_GCVec_set(vec, index, _WOORT_API_STACK(boxed_elem).m_dynamic);
}

void woort_vec_push(
    woort_StackValue src,
    woort_StackValue boxed_elem)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    woort_GCVec_push_back(vec, _WOORT_API_STACK(boxed_elem).m_dynamic);
}

WOORT_NODISCARD bool woort_vec_pop(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    return woort_GCVec_pop_back(vec);
}

WOORT_NODISCARD bool woort_vec_insert(
    woort_StackValue src,
    size_t index,
    woort_StackValue boxed_elem)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    return woort_GCVec_insert(vec, index, _WOORT_API_STACK(boxed_elem).m_dynamic);
}

WOORT_NODISCARD bool woort_vec_erase(
    woort_StackValue src,
    size_t index)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    return woort_GCVec_erase(vec, index);
}

void woort_vec_clear(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec = _WOORT_API_STACK(src).m_vec;
    assert(vec != NULL);

    woort_GCVec_clear(vec);
}

void woort_vec_copy(
    woort_StackValue dst,
    woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const dst_vec = _WOORT_API_STACK(dst).m_vec;
    const woort_GCVec* const src_vec = _WOORT_API_STACK(src).m_vec;
    assert(dst_vec != NULL);
    assert(src_vec != NULL);

    woort_GCVec_copy(dst_vec, src_vec);
}

void woort_vec_swap(
    woort_StackValue a,
    woort_StackValue b)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCVec* const vec_a = _WOORT_API_STACK(a).m_vec;
    woort_GCVec* const vec_b = _WOORT_API_STACK(b).m_vec;
    assert(vec_a != NULL);
    assert(vec_b != NULL);

    woort_GCVec_swap(vec_a, vec_b);
}

/* ========== Mapping ========== */

/* --- Mapping Capacity --- */

WOORT_NODISCARD size_t woort_map_len(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return gcmap->m_size;
}

void woort_map_reserve(
    woort_StackValue src,
    size_t reserve)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_GCMap_reserve(gcmap, reserve);
}

/* --- Mapping Lookup --- */

WOORT_NODISCARD bool woort_map_get(
    woort_StackValue dst,
    woort_StackValue src,
    woort_StackValue key_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox out_val;
    if (!woort_GCMap_get(gcmap, _WOORT_API_STACK(key_boxed).m_dynamic, &out_val))
        return false;

    _WOORT_API_STACK(dst).m_dynamic = out_val;
    return true;
}

WOORT_NODISCARD bool woort_map_get_by_int(
    woort_StackValue dst,
    woort_StackValue src,
    woort_Int key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox* const val = woort_GCMap_get_bucket_val_by_int(gcmap, key);
    if (val == NULL)
        return false;

    _WOORT_API_STACK(dst).m_dynamic = *val;
    return true;
}

WOORT_NODISCARD bool woort_map_get_by_real(
    woort_StackValue dst,
    woort_StackValue src,
    woort_Real key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox* const val = woort_GCMap_get_bucket_val_by_real(gcmap, key);
    if (val == NULL)
        return false;

    _WOORT_API_STACK(dst).m_dynamic = *val;
    return true;
}

WOORT_NODISCARD bool woort_map_get_by_bool(
    woort_StackValue dst,
    woort_StackValue src,
    bool key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox* const val = woort_GCMap_get_bucket_val_by_bool(gcmap, key);
    if (val == NULL)
        return false;

    _WOORT_API_STACK(dst).m_dynamic = *val;
    return true;
}

WOORT_NODISCARD bool woort_map_get_by_string(
    woort_StackValue dst,
    woort_StackValue src,
    woort_U8CString key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    const size_t len = strlen(key);
    woort_DynBox* const val = woort_GCMap_get_bucket_val_by_string(gcmap, key, len);
    if (val == NULL)
        return false;

    _WOORT_API_STACK(dst).m_dynamic = *val;
    return true;
}

/* --- Mapping Insert / Update --- */

WOORT_NODISCARD bool woort_map_set(
    woort_StackValue src,
    woort_StackValue key_boxed,
    woort_StackValue val_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox key = _WOORT_API_STACK(key_boxed).m_dynamic;
    woort_DynBox val = _WOORT_API_STACK(val_boxed).m_dynamic;

    const size_t old_size = gcmap->m_size;
    woort_GCMap_set_or_insert(gcmap, key, val);
    return gcmap->m_size > old_size;
}

WOORT_NODISCARD bool woort_map_set_by_int(
    woort_StackValue src,
    woort_Int key,
    woort_StackValue val_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox val = _WOORT_API_STACK(val_boxed).m_dynamic;

    const size_t old_size = gcmap->m_size;
    woort_DynBox* const slot = woort_GCMap_get_or_create_bucket_val_by_int(gcmap, key);
    woort_GC_mixed_write_barrier_dynbox(slot, val);
    return gcmap->m_size > old_size;
}

WOORT_NODISCARD bool woort_map_set_by_real(
    woort_StackValue src,
    woort_Real key,
    woort_StackValue val_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox val = _WOORT_API_STACK(val_boxed).m_dynamic;

    const size_t old_size = gcmap->m_size;
    woort_DynBox* const slot = woort_GCMap_get_or_create_bucket_val_by_real(gcmap, key);
    woort_GC_mixed_write_barrier_dynbox(slot, val);
    return gcmap->m_size > old_size;
}

WOORT_NODISCARD bool woort_map_set_by_bool(
    woort_StackValue src,
    bool key,
    woort_StackValue val_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox val = _WOORT_API_STACK(val_boxed).m_dynamic;

    const size_t old_size = gcmap->m_size;
    woort_DynBox* const slot = woort_GCMap_get_or_create_bucket_val_by_bool(gcmap, key);
    woort_GC_mixed_write_barrier_dynbox(slot, val);
    return gcmap->m_size > old_size;
}

WOORT_NODISCARD bool woort_map_set_by_string(
    woort_StackValue src,
    woort_U8CString key,
    woort_StackValue val_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    const size_t len = strlen(key);
    woort_DynBox val = _WOORT_API_STACK(val_boxed).m_dynamic;

    const size_t old_size = gcmap->m_size;
    woort_DynBox* const slot = woort_GCMap_get_or_create_bucket_val_by_string(gcmap, key, len);
    woort_GC_mixed_write_barrier_dynbox(slot, val);
    return gcmap->m_size > old_size;
}

/* --- Mapping Erase --- */

void woort_map_clear(woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_GCMap_clear(gcmap);
}

void woort_map_copy(
    woort_StackValue dst,
    woort_StackValue src)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const dst_map = _WOORT_API_STACK(dst).m_map;
    const woort_GCMap* const src_map = _WOORT_API_STACK(src).m_map;
    assert(dst_map != NULL);
    assert(src_map != NULL);

    woort_GCMap_copy(dst_map, src_map);
}

void woort_map_swap(
    woort_StackValue a,
    woort_StackValue b)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const map_a = _WOORT_API_STACK(a).m_map;
    woort_GCMap* const map_b = _WOORT_API_STACK(b).m_map;
    assert(map_a != NULL);
    assert(map_b != NULL);

    woort_GCMap_swap(map_a, map_b);
}

WOORT_NODISCARD bool woort_map_erase(
    woort_StackValue src,
    woort_StackValue key_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return woort_GCMap_erase(gcmap, _WOORT_API_STACK(key_boxed).m_dynamic);
}

WOORT_NODISCARD bool woort_map_erase_by_int(
    woort_StackValue src,
    woort_Int key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox boxed_key = woort_DynBox_box_int(key);
    return woort_GCMap_erase(gcmap, boxed_key);
}

WOORT_NODISCARD bool woort_map_erase_by_real(
    woort_StackValue src,
    woort_Real key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox boxed_key = woort_DynBox_box_real(key);
    return woort_GCMap_erase(gcmap, boxed_key);
}

WOORT_NODISCARD bool woort_map_erase_by_bool(
    woort_StackValue src,
    bool key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    woort_DynBox boxed_key = woort_DynBox_box_bool(key);
    return woort_GCMap_erase(gcmap, boxed_key);
}

WOORT_NODISCARD bool woort_map_erase_by_string(
    woort_StackValue src,
    woort_U8CString key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    const size_t len = strlen(key);
    const woort_GCString* const str = woort_GCString_make_string(key, len);
    assert(str != NULL);

    woort_DynBox boxed_key;
    boxed_key.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)str);

    return woort_GCMap_erase(gcmap, boxed_key);
}

/* --- Mapping Contains --- */

WOORT_NODISCARD bool woort_map_contains(
    woort_StackValue src,
    woort_StackValue key_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return woort_GCMap_get(gcmap, _WOORT_API_STACK(key_boxed).m_dynamic, NULL);
}

WOORT_NODISCARD bool woort_map_contains_int(
    woort_StackValue src,
    woort_Int key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return woort_GCMap_get_bucket_val_by_int(gcmap, key) != NULL;
}

WOORT_NODISCARD bool woort_map_contains_real(
    woort_StackValue src,
    woort_Real key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return woort_GCMap_get_bucket_val_by_real(gcmap, key) != NULL;
}

WOORT_NODISCARD bool woort_map_contains_bool(
    woort_StackValue src,
    bool key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return woort_GCMap_get_bucket_val_by_bool(gcmap, key) != NULL;
}

WOORT_NODISCARD bool woort_map_contains_string(
    woort_StackValue src,
    woort_U8CString key)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    const size_t len = strlen(key);
    return woort_GCMap_get_bucket_val_by_string(gcmap, key, len) != NULL;
}

/* --- Mapping Iteration --- */

WOORT_NODISCARD bool woort_map_iter(
    woort_StackValue src,
    size_t index,
    woort_StackValue out_key_boxed,
    woort_StackValue out_val_boxed)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    woort_GCMap* const gcmap = _WOORT_API_STACK(src).m_map;
    assert(gcmap != NULL);

    return woort_GCMap_get_key_value_by_index(
        gcmap,
        index,
        out_key_boxed != WOORT_IGNORE ? &_WOORT_API_STACK(out_key_boxed).m_dynamic : NULL,
        out_val_boxed != WOORT_IGNORE ? &_WOORT_API_STACK(out_val_boxed).m_dynamic : NULL);
}

void woort_CodeEnv_dumps(
    const woort_CodeEnv* env)
{
    woort_dump_codes(env, printf);
}

WOORT_NODISCARD /* OPTIONAL */ woort_VMRuntime* woort_vm_create(void)
{
    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
        return NULL;
    return vm;
}

/* ========== Serialize / Deserialize ========== */

WOORT_NODISCARD /* OPTIONAL */ char* woort_serialize_dynbox(
    woort_StackValue src, uint32_t flags)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_DynBox src_box = _WOORT_API_STACK(src).m_dynamic;

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash, 
        woort_util_ptr_equal);

    if (!_woort_serialize_dynbox_to_buf(
        src_box, 
        &buf,
        &visited_set,
        0, 
        flags))
    {
        woort_hashmap_deinit(&visited_set);
        woort_vector_deinit(&buf);
        return NULL;
    }

    woort_hashmap_deinit(&visited_set);

    if (!woort_vector_push_back(&buf, 1, ""))
    {
        woort_vector_deinit(&buf);
        return NULL;
    }

    size_t size;
    /* OPTIONAL */ char* result = (char*)woort_vector_move_out(&buf, &size);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ char* woort_serialize_map(
    woort_StackValue src, uint32_t flags)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_Value src_val = _WOORT_API_STACK(src);
    assert(src_val.m_map != NULL);

    woort_DynBox box;
    memset(&box, 0, sizeof(box));
    box.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)src_val.m_map);

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash, 
        woort_util_ptr_equal);

    if (!_woort_serialize_dynbox_to_buf(
        box, 
        &buf,
        &visited_set,
        0, 
        flags))
    {
        woort_hashmap_deinit(&visited_set);
        woort_vector_deinit(&buf);
        return NULL;
    }

    woort_hashmap_deinit(&visited_set);

    if (!woort_vector_push_back(&buf, 1, ""))
    {
        woort_vector_deinit(&buf);
        return NULL;
    }

    size_t size;
    /* OPTIONAL */ char* result = (char*)woort_vector_move_out(&buf, &size);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ char* woort_serialize_vec(
    woort_StackValue src, uint32_t flags)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const woort_Value src_val = _WOORT_API_STACK(src);
    assert(src_val.m_vec != NULL);

    woort_DynBox box;
    memset(&box, 0, sizeof(box));
    box.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)src_val.m_vec);

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*), 
        0,
        woort_util_ptr_hash, 
        woort_util_ptr_equal);

    if (!_woort_serialize_dynbox_to_buf(
        box, 
        &buf,
        &visited_set,
        0,
        flags))
    {
        woort_hashmap_deinit(&visited_set);
        woort_vector_deinit(&buf);
        return NULL;
    }

    woort_hashmap_deinit(&visited_set);

    if (!woort_vector_push_back(&buf, 1, ""))
    {
        woort_vector_deinit(&buf);
        return NULL;
    }

    size_t size;
    /* OPTIONAL */ char* result = (char*)woort_vector_move_out(&buf, &size);
    return result;
}

WOORT_NODISCARD bool woort_deserialize_dynbox(
    woort_StackValue dst, const char* str)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const char* p = str;

    if (!_woort_deserialize_dynbox_from_str(&p, &_WOORT_API_STACK(dst).m_dynamic))
        return false;

    return true;
}

WOORT_NODISCARD bool woort_deserialize_map(
    woort_StackValue dst, const char* str)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const char* p = str;

    p = _woort_deserialize_skip_whitespace(p);

    if (*p != '{')
        return false;

    if (!_woort_deserialize_map_impl(&p, &_WOORT_API_STACK(dst).m_dynamic))
        return false;

    p = _woort_deserialize_skip_whitespace(p);
    if (*p != '\0')
        return false;

    return true;
}

WOORT_NODISCARD bool woort_deserialize_vec(
    woort_StackValue dst, const char* str)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    const char* p = str;
    p = _woort_deserialize_skip_whitespace(p);

    if (*p != '[')
        return false;

    if (!_woort_deserialize_vec_impl(&p, &_WOORT_API_STACK(dst).m_dynamic))
        return false;

    p = _woort_deserialize_skip_whitespace(p);
    if (*p != '\0')
        return false;

    return true;
}

/* ========== String / Unicode Conversion API ========== */

WOORT_NODISCARD bool woort_str_get_char(
    const char* str, size_t index, char32_t* out_ch)
{
    return woort_strn_get_char(str, strlen(str), index, out_ch);
}

WOORT_NODISCARD bool woort_strn_get_char(
    const char* str, size_t size, size_t index, char32_t* out_ch)
{
    return woort_u8stridx(str, size, index, out_ch);
}

WOORT_NODISCARD size_t woort_str_to_wstr(const char* str, /* OPTIONAL */ wchar_t* outbuf, size_t buflen)
{
    return woort_strn_to_wstr(str, strlen(str), outbuf, buflen);
}

WOORT_NODISCARD size_t woort_strn_to_wstr(const char* str, size_t size, /* OPTIONAL */ wchar_t* outbuf, size_t buflen)
{
#if defined(_WIN32)
    return woort_strn_to_u16str(str, size, (char16_t*)outbuf, buflen);
#else
    return woort_strn_to_u32str(str, size, (char32_t*)outbuf, buflen);
#endif
}

WOORT_NODISCARD size_t woort_wstr_to_str(const wchar_t* str, /* OPTIONAL */ char* outbuf, size_t buflen)
{
    return woort_wstrn_to_str(str, wcslen(str), outbuf, buflen);
}

WOORT_NODISCARD size_t woort_wstrn_to_str(const wchar_t* str, size_t size, /* OPTIONAL */ char* outbuf, size_t buflen)
{
#if defined(_WIN32)
    return woort_u16strn_to_str((const char16_t*)str, size, outbuf, buflen);
#else
    return woort_u32strn_to_str((const char32_t*)str, size, outbuf, buflen);
#endif
}

WOORT_NODISCARD size_t woort_str_to_u16str(const char* str, /* OPTIONAL */ char16_t* outbuf, size_t buflen)
{
    return woort_strn_to_u16str(str, strlen(str), outbuf, buflen);
}

WOORT_NODISCARD size_t woort_strn_to_u16str(const char* str, size_t size, /* OPTIONAL */ char16_t* outbuf, size_t buflen)
{
    size_t count = 0;
    const char* p = str;
    size_t remaining = size;

    while (remaining != 0)
    {
        char16_t u16buf[2];
        size_t u16len = 0;
        const size_t u8forward = woort_u8combineu16(p, remaining, u16buf, &u16len);

        if (buflen != 0 && count + u16len <= buflen)
        {
            for (size_t i = 0; i < u16len; ++i)
                outbuf[count + i] = u16buf[i];
        }

        count += u16len;
        p += u8forward;
        remaining -= u8forward;
    }

    if (buflen != 0 && count < buflen)
        outbuf[count] = 0;

    return count;
}

WOORT_NODISCARD size_t woort_u16str_to_str(const char16_t* str, /* OPTIONAL */ char* outbuf, size_t buflen)
{
    return woort_u16strn_to_str(str, woort_u16strcount(str), outbuf, buflen);
}

WOORT_NODISCARD size_t woort_u16strn_to_str(const char16_t* str, size_t size, /* OPTIONAL */ char* outbuf, size_t buflen)
{
    size_t count = 0;
    const char16_t* p = str;
    size_t remaining = size;

    while (remaining != 0)
    {
        char u8buf[WOORT_UTF8MAXLEN];
        size_t u8len = 0;
        const size_t u16forward = woort_u16exractu8(p, remaining, u8buf, &u8len);

        if (buflen != 0 && count + u8len <= buflen)
        {
            for (size_t i = 0; i < u8len; ++i)
                outbuf[count + i] = u8buf[i];
        }

        count += u8len;
        p += u16forward;
        remaining -= u16forward;
    }

    if (buflen != 0 && count < buflen)
        outbuf[count] = 0;

    return count;
}

WOORT_NODISCARD size_t woort_str_to_u32str(const char* str, /* OPTIONAL */ char32_t* outbuf, size_t buflen)
{
    return woort_strn_to_u32str(str, strlen(str), outbuf, buflen);
}

WOORT_NODISCARD size_t woort_strn_to_u32str(const char* str, size_t size, /* OPTIONAL */ char32_t* outbuf, size_t buflen)
{
    size_t count = 0;
    const char* p = str;
    size_t remaining = size;

    while (remaining != 0)
    {
        char32_t c32;
        const size_t u8forward = woort_u8combineu32(p, remaining, &c32);

        if (buflen != 0 && count < buflen)
            outbuf[count] = c32;

        ++count;
        p += u8forward;
        remaining -= u8forward;
    }

    if (buflen != 0 && count < buflen)
        outbuf[count] = 0;

    return count;
}

WOORT_NODISCARD size_t woort_u32str_to_str(
    const char32_t* str, /* OPTIONAL */ char* outbuf, size_t buflen)
{
    return woort_u32strn_to_str(str, woort_u32strcount(str), outbuf, buflen);
}

WOORT_NODISCARD size_t woort_u32strn_to_str(
    const char32_t* str, size_t size, /* OPTIONAL */ char* outbuf, size_t buflen)
{
    size_t count = 0;
    const char32_t* p = str;
    size_t remaining = size;

    while (remaining != 0)
    {
        char u8buf[WOORT_UTF8MAXLEN];
        size_t u8len = 0;
        woort_u32exractu8(*p, u8buf, &u8len);

        if (buflen != 0 && count + u8len <= buflen)
        {
            for (size_t i = 0; i < u8len; ++i)
                outbuf[count + i] = u8buf[i];
        }

        count += u8len;
        ++p;
        --remaining;
    }

    if (buflen != 0 && count < buflen)
        outbuf[count] = 0;

    return count;
}

WOORT_NODISCARD woort_VmCallStatus woort_ret_panic(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    const bool vm_aborted =
        woort_vpanic(WOORT_PANIC_ABORTED, fmt, args);

    va_end(args);

    // NOTE: 考虑到外部函数以 RESYNC 返回虚拟机时，虚拟机实现会将
    // 栈拉平到调用发生前，为了确保异常信息能被正确记录到拉平之后
    // 的 m_sp，此处做一次额外的转移.
    if (vm_aborted)
    {
        woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
        assert(vm != NULL);

        _WOORT_API_STACK(WOORT_RETURN_SLOT).m_string =
            vm->m_sp->m_string;
    }

    return WOORT_VM_CALL_STATUS_RESYNC;
}

WOORT_NODISCARD woort_VmCallStatus woort_ret_yield(void)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    assert(vm != NULL);

    (void)woort_VMRuntime_request_set(
        vm,
        WOORT_VMRUNTIME_CHECK_REQUEST_YIELD);

    return WOORT_VM_CALL_STATUS_RESYNC;
}
