#include "woort_waipo_debugger.h"
#include "woort_gc.h"
#include "woort_codeenv.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
        "backtrace   bt      [depth]         Print callstack backtrace (default 32).\n"
        "frame       f       <frameid>       Switch to a call frame.\n"
        "source      src     [file|range]    Display source code.\n"
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

    size_t max_depth = 32;
    if (arg_count >= 2)
    {
        const long val = strtol(args[1], NULL, 10);
        if (val > 0)
            max_depth = (size_t)val;
    }

    woort_VMRuntime_print_backtrace(vm, max_depth);

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

    woort_VMRuntime_print_backtrace(vm, 3);

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

WOORT_NODISCARD static bool _woort_WAIPO_trace_to_depth(
    woort_VMRuntime* vm,
    size_t target_depth,
    /* OPTIONAL */ woort_VMRuntime_TraceCallstack* out_trace)
{
    woort_VMRuntime_TraceCallstack_Iter trace_iter;
    woort_VMRuntime_TraceCallstack trace;

    woort_VMRuntime_trace_begin(vm, &trace_iter);

    size_t depth = 0;
    while (woort_VMRuntime_trace_next(&trace_iter, &trace))
    {
        if (depth == target_depth)
        {
            if (out_trace != NULL)
                *out_trace = trace;
            return true;
        }
        ++depth;
    }

    return false;
}

static void _woort_WAIPO_print_source_file(
    const char* filepath,
    bool has_highlight,
    size_t highlight_begin_line,
    size_t highlight_end_line,
    size_t from_line,
    size_t to_line)
{
    FILE* f = fopen(filepath, "r");
    if (f == NULL)
    {
        (void)printf("Cannot open source: '%s'.\n", filepath);
        return;
    }

    (void)printf("%s from line %zu to ", filepath, from_line + 1);
    if (to_line == SIZE_MAX)
        (void)printf("<end>:\n");
    else
        (void)printf("line %zu:\n", to_line + 1);

    char line_buf[4096];
    size_t current_line = 0;
    while (fgets(line_buf, sizeof(line_buf), f) != NULL)
    {
        if (current_line >= from_line
            && (to_line == SIZE_MAX || current_line <= to_line))
        {
            const bool is_highlight = has_highlight
                && current_line >= highlight_begin_line
                && current_line <= highlight_end_line;

            (void)printf("%c %5zu | %s",
                is_highlight ? '>' : ' ',
                current_line + 1,
                line_buf);
        }
        ++current_line;
    }

    (void)fclose(f);
    (void)printf("\n");
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_frame(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        (void)printf("Usage: frame <frameid>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const long frame_id = strtol(args[1], NULL, 10);
    if (frame_id < 0)
    {
        (void)printf("Invalid frame id.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_to_depth(vm, (size_t)frame_id, &trace))
    {
        (void)printf("No such frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    dbg->m_current_frame_depth = (size_t)frame_id;

    (void)printf("Now at: frame %zu", (size_t)frame_id);

    if (trace.m_function_name != NULL)
    {
        (void)printf("  %s", trace.m_function_name);

        if (trace.m_file_or_lib_name != NULL)
        {
            if (trace.m_has_location)
                (void)printf(" (%s:%zu:%zu)",
                    trace.m_file_or_lib_name,
                    trace.m_location_begin[0] + 1,
                    trace.m_location_begin[1] + 1);
            else
                (void)printf(" (%s)", trace.m_file_or_lib_name);
        }
    }
    else
    {
        (void)printf("  <unknown>");
    }

    (void)printf("\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_source(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_to_depth(vm, dbg->m_current_frame_depth, &trace))
    {
        (void)printf("No callstack.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (!trace.m_has_location || trace.m_file_or_lib_name == NULL)
    {
        (void)printf("No source location available for current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    size_t display_range = 5;
    const char* target_file = trace.m_file_or_lib_name;
    size_t highlight_begin = trace.m_location_begin[0];
    size_t highlight_end = trace.m_location_end[0];
    bool has_highlight = true;
    bool show_full = false;

    if (arg_count >= 2)
    {
        bool is_number = true;
        for (const char* p = args[1]; *p != '\0'; ++p)
        {
            if (*p < '0' || *p > '9')
            {
                is_number = false;
                break;
            }
        }

        if (is_number)
        {
            display_range = (size_t)strtoul(args[1], NULL, 10);
        }
        else
        {
            target_file = args[1];
            if (strcmp(args[1], trace.m_file_or_lib_name) == 0)
                has_highlight = true;
            else
            {
                has_highlight = false;
                show_full = true;
            }
        }
    }

    if (show_full)
    {
        _woort_WAIPO_print_source_file(
            target_file,
            false,
            0,
            0,
            0,
            SIZE_MAX);
    }
    else
    {
        const size_t from_line = highlight_begin >= display_range / 2
            ? highlight_begin - display_range / 2
            : 0;
        const size_t to_line = highlight_end + display_range / 2;

        _woort_WAIPO_print_source_file(
            target_file,
            has_highlight,
            highlight_begin,
            highlight_end,
            from_line,
            to_line);
    }

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static const woort_WAIPO_CommandEntry _woort_WAIPO_command_table[] = {
    { "help",      "?",    &_woort_WAIPO_cmd_help },
    { "continue",  "c",    &_woort_WAIPO_cmd_continue },
    { "quit",      NULL,   &_woort_WAIPO_cmd_quit },
    { "exit",      NULL,   &_woort_WAIPO_cmd_exit },
    { "clear",     "cls",  &_woort_WAIPO_cmd_clear },
    { "backtrace", "bt",   &_woort_WAIPO_cmd_backtrace },
    { "frame",     "f",    &_woort_WAIPO_cmd_frame },
    { "source",    "src",  &_woort_WAIPO_cmd_source },
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
    {
        woort_VMRuntime_TraceCallstack_Iter trace_iter;
        woort_VMRuntime_TraceCallstack trace;

        woort_VMRuntime_trace_begin(vm, &trace_iter);
        if (woort_VMRuntime_trace_next(&trace_iter, &trace))
        {
            (void)printf("Breakdown VM(%p) at:\n", (void*)vm);

            if (trace.m_function_name != NULL && trace.m_file_or_lib_name != NULL)
            {
                if (trace.m_has_location)
                    (void)printf("    %s (%s:%zu:%zu)",
                        trace.m_function_name,
                        trace.m_file_or_lib_name,
                        trace.m_location_begin[0] + 1,
                        trace.m_location_begin[1] + 1);
                else
                    (void)printf("    %s (%s)",
                        trace.m_function_name,
                        trace.m_file_or_lib_name);
            }
            else if (trace.m_function_name != NULL)
            {
                (void)printf("    %s",
                    trace.m_function_name);
            }
            else
            {
                (void)printf("    <unknown>");
            }

            if (trace.m_is_fuzzy)
                (void)printf(" ~");

            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(vm->m_ip, &cenv))
            {
                const uint32_t code_offset =
                    (uint32_t)(vm->m_ip - cenv->m_code_begin);
                (void)printf("\nBytecode offset = %04u", code_offset);
            }

            (void)printf("\n");
        }
    }

    if (debugger_instance->m_first_breakdown)
    {
        debugger_instance->m_first_breakdown = false;
        (void)printf("Note: You can input '?' for more informations.\n");
    }

    debugger_instance->m_last_command[0] = '\0';
    debugger_instance->m_current_frame_depth = 0;

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
