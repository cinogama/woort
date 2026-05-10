#include "woort.h"
#include "woort_waipo_debugger.h"
#include "woort_vm_debugger_api.h"
#include "woort_threads.h"
#include "woort_hashmap.h"
#include "woort_util.h"
#include "woort_codeenv.h"
#include "woort_gc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
Watch And Inspect Program Operation
*/

static void _woort_WAIPO_BreakpointCollection_init(woort_WAIPO_BreakpointCollection* collection)
{
    woort_hashmap_init(
        &collection->m_breakpoints,
        sizeof(woort_Bytecode*),
        sizeof(size_t),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    woort_hashmap_init(
        &collection->m_debug_breakpoints,
        sizeof(woort_Bytecode*),
        0,
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);
}

static void _woort_WAIPO_BreakpointCollection_deinit(woort_WAIPO_BreakpointCollection* collection)
{
    woort_hashmap_deinit(&collection->m_breakpoints);
    woort_hashmap_deinit(&collection->m_debug_breakpoints);
}

static bool _woort_WAIPO_BreakpointCollection_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    woort_CodeEnv* cenv;
    if (woort_CodeEnv_find(ip, &cenv))
    {
        size_t* counter;
        switch (woort_hashmap_get_or_emplace(&collection->m_breakpoints, &ip, (void**)&counter))
        {
        case WOORT_HASHMAP_RESULT_OK:
            *counter = 1;
            /*
            NOTE: 因为之前的调试器实例遗留的 Trap，woort_CodeEnv_set_trap 可能会失败。不过
                我们不在意，此处直接假装是我们设置的即可。
            */
            (void)woort_CodeEnv_set_trap(cenv, (woort_Bytecode*)ip);
            return true;
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
            ++*counter;
            return true;
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            break;
        }
    }
    return false;
}

static void _woort_WAIPO_BreakpointCollection_cancel_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    size_t* counter;
    if (woort_hashmap_find(&collection->m_breakpoints, &ip, (void**)&counter))
    {
        --*counter;
        if (*counter == 0)
        {
            (void)woort_hashmap_remove(&collection->m_breakpoints, &ip);

            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(ip, &cenv))
                (void)woort_CodeEnv_clear_trap(cenv, (woort_Bytecode*)ip);
        }
    }
}

typedef struct woort_WAIPO_VMLocalContext
{
    woort_WAIPO_BreakpointCollection* m_breakpoint_collection;

    /* 与 m_debug_breakpoints 不同，m_step_breakpoints 的断点仅限当前虚拟机关注时生效 */
    /* OPTIONAL */ const woort_Bytecode* m_step_breakpoints[2];

    /* 源码行级步进状态 */
    bool m_is_source_step;
    /* OPTIONAL */ const char* m_step_source_file;
    size_t m_step_source_line;
    size_t m_step_source_begin_column;
    size_t m_step_source_end_line;
    size_t m_step_source_end_column;

    /* "next" 步进状态 */
    bool m_is_source_next;
    size_t m_step_target_depth;

    /* "return" 步进状态 */
    bool m_is_source_return;

}woort_WAIPO_VMLocalContext;

static void _woort_WAIPO_VMLocalContext_init(
    woort_WAIPO_VMLocalContext* vmcontext, woort_WAIPO_BreakpointCollection* collection)
{
    vmcontext->m_breakpoint_collection = collection;
    vmcontext->m_step_breakpoints[0] = NULL;
    vmcontext->m_step_breakpoints[1] = NULL;
    vmcontext->m_is_source_step = false;
    vmcontext->m_step_source_file = NULL;
    vmcontext->m_step_source_line = 0;
    vmcontext->m_step_source_begin_column = 0;
    vmcontext->m_step_source_end_line = 0;
    vmcontext->m_step_source_end_column = 0;
    vmcontext->m_is_source_next = false;
    vmcontext->m_step_target_depth = 0;
    vmcontext->m_is_source_return = false;
}

static bool _woort_WAIPO_VMLocalContext_set_step_break(
    woort_WAIPO_VMLocalContext* vmcontext, const woort_Bytecode* ip)
{
    for (size_t i = 0; i < 2; ++i)
    {
        if (vmcontext->m_step_breakpoints[i] == NULL)
        {
            if (_woort_WAIPO_BreakpointCollection_break_at(
                vmcontext->m_breakpoint_collection, ip))
            {
                vmcontext->m_step_breakpoints[i] = ip;
                return true;
            }
            break;
        }
    }
    return false;
}

static void _woort_WAIPO_VMLocalContext_set_source_step(
    woort_WAIPO_VMLocalContext* vmcontext,
    /* OPTIONAL */ const char* filepath,
    size_t line,
    size_t begin_column,
    size_t end_line,
    size_t end_column)
{
    vmcontext->m_is_source_step = true;
    vmcontext->m_step_source_file = filepath;
    vmcontext->m_step_source_line = line;
    vmcontext->m_step_source_begin_column = begin_column;
    vmcontext->m_step_source_end_line = end_line;
    vmcontext->m_step_source_end_column = end_column;
}

static void _woort_WAIPO_VMLocalContext_clean_step_break(
    woort_WAIPO_VMLocalContext* vmcontext)
{
    for (size_t i = 0; i < 2; ++i)
    {
        if (vmcontext->m_step_breakpoints[i] != NULL)
        {
            _woort_WAIPO_BreakpointCollection_cancel_break_at(
                vmcontext->m_breakpoint_collection,
                vmcontext->m_step_breakpoints[i]);

            vmcontext->m_step_breakpoints[i] = NULL;
        }
    }
    vmcontext->m_is_source_step = false;
    vmcontext->m_step_source_file = NULL;
    vmcontext->m_step_source_line = 0;
    vmcontext->m_step_source_begin_column = 0;
    vmcontext->m_step_source_end_line = 0;
    vmcontext->m_step_source_end_column = 0;
    vmcontext->m_is_source_next = false;
    vmcontext->m_step_target_depth = 0;
    vmcontext->m_is_source_return = false;
}

static void _woort_WAIPO_VMLocalContext_deinit(woort_WAIPO_VMLocalContext* vmcontext)
{
    _woort_WAIPO_VMLocalContext_clean_step_break(vmcontext);
}

static bool _woort_WAIPO_VMLocalContext_meet_step_breakdown(
    woort_WAIPO_VMLocalContext* vmcontext, const woort_Bytecode* ip)
{
    return vmcontext->m_step_breakpoints[0] == ip
        || vmcontext->m_step_breakpoints[1] == ip;
}

static size_t _woort_WAIPO_get_current_callstack_depth(woort_VMRuntime* vm)
{
    woort_VMRuntime_TraceCallstack_Iter iter;
    woort_VMRuntime_TraceCallstack trace;
    woort_VMRuntime_trace_begin(vm, &iter);
    size_t depth = 0;
    while (woort_VMRuntime_trace_next(&iter, &trace))
    {
        ++depth;
    }
    return depth;
}

bool _woort_WAIPO_Debugger_set_next_source_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    if (!woort_hashmap_find(
        &debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
        return false;

    const uint32_t code_offset =
        (uint32_t)(vm->m_ip - cenv->m_code_begin);
    woort_SourceLocation src_loc;

    if (woort_CodeEnv_find_srcloc_by_offset(cenv, code_offset, &src_loc))
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    vmcontext->m_is_source_next = true;
    vmcontext->m_step_target_depth = _woort_WAIPO_get_current_callstack_depth(vm);

    return _woort_WAIPO_VMLocalContext_set_step_break(vmcontext, ip);
}

bool _woort_WAIPO_Debugger_set_return_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    if (!woort_hashmap_find(
        &debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
        return false;

    const uint32_t code_offset =
        (uint32_t)(vm->m_ip - cenv->m_code_begin);
    woort_SourceLocation src_loc;

    if (woort_CodeEnv_find_srcloc_by_offset(cenv, code_offset, &src_loc))
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    vmcontext->m_is_source_return = true;
    vmcontext->m_step_target_depth = _woort_WAIPO_get_current_callstack_depth(vm);

    return _woort_WAIPO_VMLocalContext_set_step_break(vmcontext, ip);
}

bool _woort_WAIPO_Debugger_focus_on(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    switch (woort_hashmap_get_or_emplace(
        &debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
    {
    case WOORT_HASHMAP_RESULT_OK:
        _woort_WAIPO_VMLocalContext_init(vmcontext, &debugger_instance->m_breakpoint_collection);
        break;
    case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        break;
    case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
        /* Emm... */
        return false;
    }
    return true;
}

void _woort_WAIPO_Debugger_out_of_focus(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    if (woort_hashmap_find(&debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
    {
        _woort_WAIPO_VMLocalContext_deinit(vmcontext);
        (void)woort_hashmap_remove(&debugger_instance->m_focusing_vms, &vm);
    }
}

bool _woort_WAIPO_Debugger_set_step_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    if (!woort_hashmap_find(
        &debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;
    return _woort_WAIPO_VMLocalContext_set_step_break(vmcontext, ip);
}

bool _woort_WAIPO_Debugger_set_step_source_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    if (!woort_hashmap_find(
        &debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
        return false;

    const uint32_t code_offset =
        (uint32_t)(vm->m_ip - cenv->m_code_begin);
    woort_SourceLocation src_loc;

    if (woort_CodeEnv_find_srcloc_by_offset(cenv, code_offset, &src_loc))
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    return _woort_WAIPO_VMLocalContext_set_step_break(vmcontext, ip);
}

static bool _woort_WAIPO_Debugger_is_focus_vm(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    return woort_hashmap_contains(&debugger_instance->m_focusing_vms, &vm);
}

static bool _woort_WAIPO_Debugger_meet_breakpoint(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    const woort_Bytecode* current_ip = vm->m_ip;

    bool breakdown = false;
    if (woort_hashmap_contains(
        &debugger_instance->m_breakpoint_collection.m_breakpoints,
        &current_ip))
    {
        if (woort_hashmap_contains(
            &debugger_instance->m_breakpoint_collection.m_debug_breakpoints,
            &current_ip))
        {
            breakdown = true;
        }

        /* May be step debug point? */
        woort_WAIPO_VMLocalContext* vmcontext;
        if (woort_hashmap_find(&debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
        {
            if (_woort_WAIPO_VMLocalContext_meet_step_breakdown(vmcontext, current_ip))
            {
                if (vmcontext->m_is_source_step)
                {
                    woort_CodeEnv* cenv;
                    if (woort_CodeEnv_find(current_ip, &cenv))
                    {
                        const uint32_t code_offset =
                            (uint32_t)(current_ip - cenv->m_code_begin);
                        woort_SourceLocation src_loc;

                        if (woort_CodeEnv_find_srcloc_by_offset(
                            cenv, code_offset, &src_loc))
                        {
                            const bool file_changed =
                                (vmcontext->m_step_source_file == NULL
                                    || src_loc.m_filepath == NULL)
                                ? (vmcontext->m_step_source_file != src_loc.m_filepath)
                                : (strcmp(vmcontext->m_step_source_file,
                                    src_loc.m_filepath) != 0);

                            bool loc_changed =
                                file_changed
                                || src_loc.m_begin_line
                                    != vmcontext->m_step_source_line
                                || (size_t)src_loc.m_begin_column
                                    != vmcontext->m_step_source_begin_column
                                || (size_t)src_loc.m_end_line
                                    != vmcontext->m_step_source_end_line
                                || (size_t)src_loc.m_end_column
                                    != vmcontext->m_step_source_end_column;

                            bool should_break = false;

                            if (vmcontext->m_is_source_return)
                            {
                                if (_woort_WAIPO_get_current_callstack_depth(vm)
                                    < vmcontext->m_step_target_depth)
                                {
                                    /* 调用栈深度已减少，已返回上层函数，中断 */
                                    should_break = true;
                                }
                                /* else: 仍在当前函数或更深层，继续步进 */
                            }
                            else if (vmcontext->m_is_source_next)
                            {
                                if (_woort_WAIPO_get_current_callstack_depth(vm)
                                    <= vmcontext->m_step_target_depth)
                                {
                                    if (loc_changed)
                                    {
                                        /* 已返回目标深度且源码位置变动，中断 */
                                        should_break = true;
                                    }
                                }
                                /* else: 仍在函数调用内部，无论位置是否变化都继续步进 */
                            }
                            else if (loc_changed)
                            {
                                /* 源码位置已变动，中断 */
                                should_break = true;
                            }

                            if (should_break)
                            {
                                breakdown = true;
                            }
                            else
                            {
                                /* 仍在同一源码位置（或 next 中尚未返回目标深度），继续步进 */
                                const char* saved_file = vmcontext->m_step_source_file;
                                const size_t saved_line = vmcontext->m_step_source_line;
                                const size_t saved_bcol = vmcontext->m_step_source_begin_column;
                                const size_t saved_eline = vmcontext->m_step_source_end_line;
                                const size_t saved_ecol = vmcontext->m_step_source_end_column;
                                const bool saved_is_next = vmcontext->m_is_source_next;
                                const bool saved_is_return = vmcontext->m_is_source_return;
                                const size_t saved_target_depth = vmcontext->m_step_target_depth;

                                _woort_WAIPO_VMLocalContext_clean_step_break(vmcontext);

                                const woort_Bytecode* next_ip = NULL;
                                if (_woort_WAIPO_get_next_ip(current_ip, cenv, vm->m_sb, vm, &next_ip))
                                {
                                    _woort_WAIPO_VMLocalContext_set_source_step(
                                        vmcontext, saved_file, saved_line,
                                        saved_bcol, saved_eline, saved_ecol);
                                    vmcontext->m_is_source_next = saved_is_next;
                                    vmcontext->m_is_source_return = saved_is_return;
                                    vmcontext->m_step_target_depth = saved_target_depth;

                                    if (_woort_WAIPO_VMLocalContext_set_step_break(
                                        vmcontext, next_ip))
                                    {
                                        /* 成功设置下一步断点，不中断 */
                                    }
                                    else
                                    {
                                        /* 设置断点失败，中断 */
                                        breakdown = true;
                                    }
                                }
                                else
                                {
                                    /* 无法确定下一条指令，中断 */
                                    breakdown = true;
                                }
                            }
                        }
                        else
                        {
                            /* 当前指令无源码信息，中断 */
                            breakdown = true;
                        }
                    }
                    else
                    {
                        /* 无法定位 CodeEnv，中断 */
                        breakdown = true;
                    }
                }
                else
                {
                    /* IR 级步进：直接中断 */
                    breakdown = true;
                }
            }

            if (breakdown)
                _woort_WAIPO_VMLocalContext_clean_step_break(vmcontext);
        }
    }
    return breakdown;
}

static void woort_WAIPO_Debugger_active(woort_VMRuntime* vm, void* instance, bool trap_by_request)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;

    bool trap_down = false;

    if (woort_hashmap_is_empty(&debugger_instance->m_focusing_vms)
        || _woort_WAIPO_Debugger_meet_breakpoint(debugger_instance, vm)
        || (trap_by_request && _woort_WAIPO_Debugger_is_focus_vm(debugger_instance, vm)))
    {
        woort_WAIPO_Debugger_process(debugger_instance, vm);
    }
}

static bool _woort_WAIPO_VMLocalContext_deinit_callback(
    const void* key,
    void* value,
    void* user_data)
{
    (void)key;
    (void)user_data;
    _woort_WAIPO_VMLocalContext_deinit((woort_WAIPO_VMLocalContext*)value);
    return true;
}

static void _woort_WAIPO_Debugger_close(void* instance)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;

    (void)woort_hashmap_foreach(
        &debugger_instance->m_focusing_vms,
        &_woort_WAIPO_VMLocalContext_deinit_callback,
        NULL);

    woort_hashmap_deinit(&debugger_instance->m_focusing_vms);
    _woort_WAIPO_BreakpointCollection_deinit(&debugger_instance->m_breakpoint_collection);
    
    free(debugger_instance);
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_attach(void)
{
    woort_WAIPO_Debugger* const debugger_instance =
        malloc(sizeof(woort_WAIPO_Debugger));

    if (debugger_instance == NULL)
        return false;

    woort_hashmap_init(
        &debugger_instance->m_focusing_vms,
        sizeof(woort_VMRuntime*),
        sizeof(woort_WAIPO_VMLocalContext),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    _woort_WAIPO_BreakpointCollection_init(
        &debugger_instance->m_breakpoint_collection);

    debugger_instance->m_first_breakdown = true;
    debugger_instance->m_last_command[0] = '\0';
    debugger_instance->m_current_frame_depth = 0;

    return woort_VMRuntime_Debugger_attach(
        &woort_WAIPO_Debugger_active,
        debugger_instance,
        &_woort_WAIPO_Debugger_close);
}
