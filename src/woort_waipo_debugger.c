#include "woort.h"
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

typedef struct woort_WAIPO_BreakpointCollection
{
    woort_HashMap /* woort_Bytecode*, size_t */ m_breakpoints;

    /* m_debug_breakpoints 是所有无条件的断点，无论 VM 是否处于 m_focusing_vms */
    /* 命中断点都会使得 VM Trap. */
    woort_HashMap /* woort_Bytecode*, None */ m_debug_breakpoints;

}woort_WAIPO_BreakpointCollection;

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

typedef struct woort_WAIPO_Debugger
{
    woort_WAIPO_BreakpointCollection m_breakpoint_collection;
    /* OPTIONAL */ woort_HashMap /* woort_VMRuntime*, woort_WAIPO_VMLocalContext */
        m_focusing_vms;

    bool m_first_breakdown;
    char m_last_command[256];

}woort_WAIPO_Debugger;

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

static void _woort_WAIPO_Debugger_out_of_focus(
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

/* ========== CLI 命令系统 ========== */

typedef enum woort_WAIPO_CommandResult
{
    WOORT_WAIPO_CMD_NEED_NEXT,
    WOORT_WAIPO_CMD_CONTINUE,
} woort_WAIPO_CommandResult;

typedef woort_WAIPO_CommandResult (*woort_WAIPO_CommandHandler)(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count);

typedef struct woort_WAIPO_CommandEntry
{
    const char* m_name;
    const char* m_alias;
    woort_WAIPO_CommandHandler m_handler;
} woort_WAIPO_CommandEntry;

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_help(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)dbg;
    (void)vm;
    (void)args;
    (void)arg_count;

    (void)printf(
        "WAIPO Debugger command list:\n"
        "\n"
        "COMMAND     ALIAS   ARGUMENT        DESCRIBE\n"
        "----------------------------------------------------------------------\n"
        "continue    c                       Continue to run.\n"
        "exit                                Invoke _Exit(0) to shutdown.\n"
        "help        ?                       Get help informations.\n"
        "list        l       codeenv         List all CodeEnv(s).\n"
        "                    vm              List all VM(s).\n"
        "quit                                Stop all vm to exit.\n"
        "clear       cls                     Clean the screen.\n"
        "\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_continue(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)dbg;
    (void)vm;
    (void)args;
    (void)arg_count;

    (void)printf("Continue running...\n");

    _woort_WAIPO_Debugger_out_of_focus(dbg, vm);

    return WOORT_WAIPO_CMD_CONTINUE;
}

static bool _woort_WAIPO_quit_terminate_vm_callback(
    woort_VMRuntime* vm, void* user_data)
{
    (void)user_data;
    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE);
    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_quit(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)vm;
    (void)args;
    (void)arg_count;

    woort_GC_foreach_root_vm(
        &_woort_WAIPO_quit_terminate_vm_callback, NULL);

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_exit(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)dbg;
    (void)vm;
    (void)args;
    (void)arg_count;

    _Exit(0);

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_clear(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)dbg;
    (void)vm;
    (void)args;
    (void)arg_count;

#if defined(_WIN32) || defined(_WIN64)
    (void)system("cls");
#else
    (void)system("clear");
#endif

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

typedef struct _woort_WAIPO_ListCodeEnvContext
{
    size_t m_index;
} _woort_WAIPO_ListCodeEnvContext;

static bool _woort_WAIPO_list_codeenv_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_ListCodeEnvContext* ctx =
        (_woort_WAIPO_ListCodeEnvContext*)user_data;

    (void)printf(
        "[%zu] %p  hold=%d  codes=[%p-%p]  data=%zu  funcs=%zu\n",
        ctx->m_index,
        (void*)cenv,
        cenv->m_hold ? 1 : 0,
        (const void*)cenv->m_code_begin,
        (const void*)cenv->m_code_end,
        cenv->m_data_count,
        cenv->m_function_boundaries.m_size);

    ++ctx->m_index;
    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_list_codeenv(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm)
{
    (void)dbg;
    (void)vm;

    _woort_WAIPO_ListCodeEnvContext ctx;
    ctx.m_index = 0;

    woort_CodeEnv_foreach(&_woort_WAIPO_list_codeenv_callback, &ctx);

    if (ctx.m_index == 0)
        (void)printf("No CodeEnv.\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

typedef struct _woort_WAIPO_ListVMContext
{
    size_t m_index;
} _woort_WAIPO_ListVMContext;

static bool _woort_WAIPO_list_vm_callback(
    woort_VMRuntime* vm, void* user_data)
{
    _woort_WAIPO_ListVMContext* ctx =
        (_woort_WAIPO_ListVMContext*)user_data;

    size_t stack_total = (size_t)(vm->m_stack_end - vm->m_stack);
    size_t stack_used = (size_t)(vm->m_sp - vm->m_stack);

    double usage = stack_total > 0
        ? (double)stack_used / (double)stack_total * 100.0
        : 0.0;

    (void)printf(
        "[%zu] %p  ip=%p  sp=%p  stack=[%p-%p]  usage=%.1f%%  env=%p\n",
        ctx->m_index,
        (void*)vm,
        (const void*)vm->m_ip,
        (void*)vm->m_sp,
        (void*)vm->m_stack,
        (void*)vm->m_stack_end,
        usage,
        (void*)vm->m_env);

    ++ctx->m_index;
    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_list_vm(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm)
{
    (void)dbg;
    (void)vm;

    _woort_WAIPO_ListVMContext ctx;
    ctx.m_index = 0;

    woort_GC_foreach_root_vm(&_woort_WAIPO_list_vm_callback, &ctx);

    if (ctx.m_index == 0)
        (void)printf("No VM.\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_list(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        (void)printf("Usage: list <codeenv|vm>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (strcmp(args[1], "codeenv") == 0)
        return _woort_WAIPO_list_codeenv(dbg, vm);

    if (strcmp(args[1], "vm") == 0)
        return _woort_WAIPO_list_vm(dbg, vm);

    (void)printf("Unknown list target: '%s'. Available: codeenv, vm\n", args[1]);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static const woort_WAIPO_CommandEntry _woort_WAIPO_command_table[] = {
    { "help",      "?",    &_woort_WAIPO_cmd_help },
    { "continue",  "c",    &_woort_WAIPO_cmd_continue },
    { "quit",      NULL,   &_woort_WAIPO_cmd_quit },
    { "exit",      NULL,   &_woort_WAIPO_cmd_exit },
    { "clear",     "cls",  &_woort_WAIPO_cmd_clear },
    { "list",      "l",    &_woort_WAIPO_cmd_list },
};

static const size_t _woort_WAIPO_command_table_size =
    sizeof(_woort_WAIPO_command_table) / sizeof(_woort_WAIPO_command_table[0]);

/* OPTIONAL */ static woort_WAIPO_CommandHandler _woort_WAIPO_find_command(
    const char* name)
{
    for (size_t i = 0; i < _woort_WAIPO_command_table_size; ++i)
    {
        if (strcmp(name, _woort_WAIPO_command_table[i].m_name) == 0)
            return _woort_WAIPO_command_table[i].m_handler;

        if (_woort_WAIPO_command_table[i].m_alias != NULL
            && strcmp(name, _woort_WAIPO_command_table[i].m_alias) == 0)
            return _woort_WAIPO_command_table[i].m_handler;
    }
    return NULL;
}

static size_t _woort_WAIPO_split_line(
    char* line, char** tokens, size_t max_tokens)
{
    size_t count = 0;
    char* p = line;

    while (*p != '\0' && count < max_tokens)
    {
        while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;

        if (*p == '\0')
            break;

        tokens[count++] = p;

        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            ++p;

        if (*p != '\0')
            *p++ = '\0';
    }

    return count;
}

static void woort_WAIPO_Debugger_process(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    if (debugger_instance->m_first_breakdown)
    {
        debugger_instance->m_first_breakdown = false;
        (void)printf("Note: You can input '?' for more informations.\n");
    }

    debugger_instance->m_last_command[0] = '\0';

    for (;;)
    {
        (void)printf("> ");
        (void)fflush(stdout);

        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), stdin) == NULL)
            break;

        char* tokens[64];
        size_t token_count = _woort_WAIPO_split_line(
            line_buf, tokens, 64);

        if (token_count == 0)
        {
            if (debugger_instance->m_last_command[0] == '\0')
                continue;

            (void)strncpy(line_buf, debugger_instance->m_last_command,
                sizeof(line_buf) - 1);
            line_buf[sizeof(line_buf) - 1] = '\0';

            token_count = _woort_WAIPO_split_line(
                line_buf, tokens, 64);

            if (token_count == 0)
                continue;
        }

        (void)strncpy(debugger_instance->m_last_command, tokens[0],
            sizeof(debugger_instance->m_last_command) - 1);
        debugger_instance->m_last_command[
            sizeof(debugger_instance->m_last_command) - 1] = '\0';

        woort_WAIPO_CommandHandler handler =
            _woort_WAIPO_find_command(tokens[0]);

        if (handler == NULL)
        {
            (void)printf(
                "Unknown debug command, please input 'help' for more informations.\n");
            continue;
        }

        woort_WAIPO_CommandResult result =
            handler(debugger_instance, vm, tokens, token_count);

        if (result == WOORT_WAIPO_CMD_CONTINUE)
            break;
    }
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

    return woort_VMRuntime_Debugger_attach(
        &woort_WAIPO_Debugger_active,
        debugger_instance,
        &_woort_WAIPO_Debugger_close);
}
