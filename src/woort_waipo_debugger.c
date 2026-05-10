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
            (void)woort_CodeEnv_set_trap(cenv, ip);
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

}woort_WAIPO_VMLocalContext;

static void _woort_WAIPO_VMLocalContext_init(
    woort_WAIPO_VMLocalContext* vmcontext, woort_WAIPO_BreakpointCollection* collection)
{
    vmcontext->m_breakpoint_collection = collection;
    vmcontext->m_step_breakpoints[0] = NULL;
    vmcontext->m_step_breakpoints[1] = NULL;
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

static bool _woort_WAIPO_Debugger_focus_on(
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
                breakdown = true;

            if (breakdown)
                _woort_WAIPO_VMLocalContext_clean_step_break(vmcontext);
        }
    }
    return breakdown;
}

static void woort_WAIPO_Debugger_active(woort_VMRuntime* vm, void* instance)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;

    bool trap_down = false;

    if (woort_hashmap_is_empty(&debugger_instance->m_focusing_vms)
        || _woort_WAIPO_Debugger_meet_breakpoint(debugger_instance, vm))
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
