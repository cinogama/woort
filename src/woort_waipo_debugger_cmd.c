#include "woort_waipo_debugger.h"
#include "woort_gc.h"
#include "woort_codeenv.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
        "backtrace   bt                      Print callstack backtrace.\n"
        "\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_continue(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
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
    (void)dbg;
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

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_backtrace(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)dbg;
    (void)args;
    (void)arg_count;

    woort_VMRuntime_TraceCallstack_Iter trace_iter;
    woort_VMRuntime_TraceCallstack trace;

    woort_VMRuntime_trace_begin(vm, &trace_iter);
    (void)printf("Backtrace:\n");
    while (woort_VMRuntime_trace_next(&trace_iter, &trace))
        woort_VMRuntime_log_trace(&trace);

    return WOORT_WAIPO_CMD_NEED_NEXT;
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

    const size_t stack_total = (size_t)(vm->m_stack_end - vm->m_stack);
    const size_t stack_used = (size_t)(vm->m_stack_end - vm->m_sp);

    const double usage = stack_total > 0
        ? (double)stack_used / (double)stack_total * 100.0
        : 0.0;

    (void)printf(
        "[%zu] VMRuntime(%p)  ip=%p  sp=%p  stack=[%p-%p]  usage=%.1f%%\n",
        ctx->m_index,
        (void*)vm,
        (const void*)vm->m_ip,
        (void*)vm->m_sp,
        (void*)vm->m_stack,
        (void*)vm->m_stack_end,
        usage);

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
    { "backtrace", "bt",   &_woort_WAIPO_cmd_backtrace },
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

void woort_WAIPO_Debugger_process(
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
