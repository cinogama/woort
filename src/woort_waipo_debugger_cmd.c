#include "woort_waipo_debugger.h"
#include "woort_gc.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_opcode.h"
#include "woort_atomic.h"
#include "woort_gc_closure.h"

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
        "dis                 [funcname]      Dump current VM's running bytecodes.\n"
        "                    --all           Or dump all bytecodes in current CodeEnv.\n"
        "                    [offset length] Or dump bytecodes in specified range.\n"
        "stepir      si                      Step one bytecode instruction.\n"
        "step        s                       Step one source line.\n"
        "next        n                       Step over to next source line, not entering callees.\n"
        "return      r                       Return to caller frame.\n"
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
    size_t highlight_begin_col,
    size_t highlight_end_col,
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
        for (char* p = line_buf; *p; ++p)
        {
            if (*p == '\n' || *p == '\r')
                *p = ' ';
        }

        if (current_line >= from_line
            && (to_line == SIZE_MAX || current_line <= to_line))
        {
            const bool is_highlight = has_highlight
                && current_line >= highlight_begin_line
                && current_line <= highlight_end_line;

            if (is_highlight)
            {
                const size_t line_len = strlen(line_buf);
                size_t col_start = 0;
                size_t col_end = line_len;
                if (current_line == highlight_begin_line && highlight_begin_col < line_len)
                    col_start = highlight_begin_col;
                if (current_line == highlight_end_line && highlight_end_col < line_len)
                    col_end = highlight_end_col;

                (void)printf("> %5zu | %.*s" WOORT_ANSI_INV "%.*s" WOORT_ANSI_RST "%.*s \n",
                    current_line + 1,
                    (int)col_start, line_buf,
                    (int)(col_end - col_start), line_buf + col_start,
                    (int)(line_len - col_end), line_buf + col_end);
            }
            else
                (void)printf("%c %5zu | %s\n",
                    ' ',
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
    const size_t highlight_begin = trace.m_location_begin[0];
    const size_t highlight_end = trace.m_location_end[0];
    const size_t highlight_begin_col = trace.m_location_begin[1];
    const size_t highlight_end_col = trace.m_location_end[1];
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
            highlight_begin_col,
            highlight_end_col,
            from_line,
            to_line);
    }

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static void _woort_WAIPO_dump_disassembly_range(
    const woort_CodeEnv* cenv,
    size_t begin_offset,
    size_t end_offset,
    const woort_Bytecode* current_ip)
{
    const size_t code_count = (size_t)(cenv->m_code_end - cenv->m_code_begin);
    if (begin_offset > code_count)
        begin_offset = code_count;
    if (end_offset > code_count)
        end_offset = code_count;
    if (begin_offset >= end_offset)
        return;

    const woort_Bytecode* pc = cenv->m_code_begin + begin_offset;
    const woort_Bytecode* end = cenv->m_code_begin + end_offset;
    const woort_Bytecode* next_bc = pc;

    while (pc < end)
    {
        const size_t offset = (size_t)(pc - cenv->m_code_begin);
        const bool is_current = (pc == current_ip);

        if (is_current)
            (void)printf(WOORT_ANSI_HIG "=>%04zu:\t" WOORT_ANSI_RST, offset);
        else
            (void)printf("  %04zu:\t", offset);

        if (pc == next_bc)
            next_bc = woort_disassembly(pc, (woort_Disassembly_DumpCallback)printf);
        else
            (void)printf("\n");

        ++pc;
    }

    (void)printf("\n");
}

typedef struct _woort_WAIPO_DisSearchFuncContext
{
    const char* m_funcname;
    size_t m_funcname_len;
    bool m_fullmatch;
    size_t m_match_count;
    const woort_CodeEnv* m_last_cenv;
    uint32_t m_last_offset_begin;
    uint32_t m_last_code_length;
} _woort_WAIPO_DisSearchFuncContext;

static bool _woort_WAIPO_dis_search_func_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_DisSearchFuncContext* ctx =
        (_woort_WAIPO_DisSearchFuncContext*)user_data;

    const size_t boundary_count = cenv->m_function_boundaries.m_size;
    for (size_t i = 0; i < boundary_count; ++i)
    {
        const woort_FunctionBoundary* boundary =
            (const woort_FunctionBoundary*)woort_vector_at(
                (woort_Vector*)&cenv->m_function_boundaries, i);

        if (boundary->m_name == NULL)
            continue;

        const bool match = ctx->m_fullmatch
            ? (strcmp(boundary->m_name, ctx->m_funcname) == 0)
            : (strstr(boundary->m_name, ctx->m_funcname) != NULL);

        if (match)
        {
            ++ctx->m_match_count;
            ctx->m_last_cenv = cenv;
            ctx->m_last_offset_begin = boundary->m_offset_begin;
            ctx->m_last_code_length = boundary->m_code_length;

            (void)printf("In function: " WOORT_ANSI_HIG "%s" WOORT_ANSI_RST "\n",
                boundary->m_name);

            _woort_WAIPO_dump_disassembly_range(
                cenv,
                boundary->m_offset_begin,
                boundary->m_offset_begin + boundary->m_code_length,
                NULL);
        }
    }

    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_dis(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_to_depth(vm, dbg->m_current_frame_depth, &trace))
    {
        (void)printf(WOORT_ANSI_HIR "No callstack.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const woort_Bytecode* frame_ip = trace.m_code_addr;
    if (frame_ip == NULL)
        frame_ip = vm->m_ip;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(frame_ip, &cenv))
    {
        (void)printf(WOORT_ANSI_HIR "Cannot locate CodeEnv for current IP.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const size_t current_ip_offset = (size_t)(frame_ip - cenv->m_code_begin);

    if (arg_count < 2)
    {
        const char* func_name =
            woort_CodeEnv_find_function_name_by_offset(cenv, (uint32_t)current_ip_offset);

        if (func_name != NULL)
        {
            const size_t boundary_count = cenv->m_function_boundaries.m_size;
            for (size_t i = 0; i < boundary_count; ++i)
            {
                const woort_FunctionBoundary* boundary =
                    (const woort_FunctionBoundary*)woort_vector_at(
                        (woort_Vector*)&cenv->m_function_boundaries, i);

                if (boundary->m_name != NULL
                    && strcmp(boundary->m_name, func_name) == 0)
                {
                    (void)printf("In function: " WOORT_ANSI_HIG "%s" WOORT_ANSI_RST "\n",
                        func_name);

                    _woort_WAIPO_dump_disassembly_range(
                        cenv,
                        boundary->m_offset_begin,
                        boundary->m_offset_begin + boundary->m_code_length,
                        frame_ip);

                    return WOORT_WAIPO_CMD_NEED_NEXT;
                }
            }
        }

        (void)printf("Unable to locate function, display following 100 words.\n");
        _woort_WAIPO_dump_disassembly_range(
            cenv,
            current_ip_offset,
            current_ip_offset + 100,
            frame_ip);

        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* first_arg = args[1];

    bool is_number = true;
    for (const char* p = first_arg; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            is_number = false;
            break;
        }
    }

    if (is_number)
    {
        const size_t begin_offset = (size_t)strtoul(first_arg, NULL, 10);

        if (arg_count >= 3)
        {
            const size_t length = (size_t)strtoul(args[2], NULL, 10);
            (void)printf("Display +%04zu to +%04zu.\n",
                begin_offset, begin_offset + length);

            _woort_WAIPO_dump_disassembly_range(
                cenv, begin_offset, begin_offset + length, frame_ip);
        }
        else
        {
            (void)printf(WOORT_ANSI_HIR "Missing length, command failed.\n" WOORT_ANSI_RST);
        }

        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (strcmp(first_arg, "--all") == 0)
    {
        _woort_WAIPO_dump_disassembly_range(cenv, 0, SIZE_MAX, frame_ip);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    _woort_WAIPO_DisSearchFuncContext ctx;
    ctx.m_funcname = first_arg;
    ctx.m_funcname_len = strlen(first_arg);
    ctx.m_fullmatch = false;
    ctx.m_match_count = 0;
    ctx.m_last_cenv = NULL;
    ctx.m_last_offset_begin = 0;
    ctx.m_last_code_length = 0;

    woort_CodeEnv_foreach(&_woort_WAIPO_dis_search_func_callback, &ctx);

    (void)printf("Find %zu symbol(s).\n", ctx.m_match_count);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

int _woort_WAIPO_empty_cb(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

WOORT_NODISCARD bool _woort_WAIPO_get_next_ip(
    const woort_Bytecode*   ip,
    woort_CodeEnv*          cenv,
    const woort_Value*      sb,
    woort_VMRuntime*        vm,
    /* OPTIONAL */ const woort_Bytecode**  out_next_ip)
{
    if (ip == NULL || cenv == NULL || sb == NULL || out_next_ip == NULL)
        return false;

    if (ip < cenv->m_code_begin || ip >= cenv->m_code_end)
        return false;

    const woort_Bytecode bc = woort_CodeEnv_raw_trap(cenv, ip);
    const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
    const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

    switch ((woort_Opcode)op6)
    {
    case WOORT_OPCODE_JFWD:
    case WOORT_OPCODE_JBCK:
    {
        *out_next_ip = cenv->m_code_begin + WOORT_BYTECODE(MABC26, bc);
        return true;
    }

    case WOORT_OPCODE_JFWDCND:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        switch (m2)
        {
        case 0: /* JFWDNZ */
            if (sb[a_offset].m_integer != 0)
            {
                *out_next_ip = ip + (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 1: /* JFWDZ */
            if (sb[a_offset].m_integer == 0)
            {
                *out_next_ip = ip + (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 2: /* JFWDEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer == sb[b_offset].m_integer)
            {
                *out_next_ip = ip + (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        case 3: /* JFWDNEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer != sb[b_offset].m_integer)
            {
                *out_next_ip = ip + (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_JBCKCND:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        switch (m2)
        {
        case 0: /* JBCKNZ */
            if (sb[a_offset].m_integer != 0)
            {
                *out_next_ip = ip - (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 1: /* JBCKZ */
            if (sb[a_offset].m_integer == 0)
            {
                *out_next_ip = ip - (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 2: /* JBCKEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer == sb[b_offset].m_integer)
            {
                *out_next_ip = ip - (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        case 3: /* JBCKNEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer != sb[b_offset].m_integer)
            {
                *out_next_ip = ip - (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_JFDCMP:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
        bool taken = false;
        switch (m2)
        {
        case 0: taken = (sb[a_offset].m_integer <  sb[b_offset].m_integer); break;
        case 1: taken = (sb[a_offset].m_integer >  sb[b_offset].m_integer); break;
        case 2: taken = (sb[a_offset].m_integer <= sb[b_offset].m_integer); break;
        case 3: taken = (sb[a_offset].m_integer >= sb[b_offset].m_integer); break;
        }
        if (taken)
        {
            *out_next_ip = ip + (int8_t)WOORT_BYTECODE(C8, bc);
            return true;
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_JBCKCMP:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
        bool taken = false;
        switch (m2)
        {
        case 0: taken = (sb[a_offset].m_integer <  sb[b_offset].m_integer); break;
        case 1: taken = (sb[a_offset].m_integer >  sb[b_offset].m_integer); break;
        case 2: taken = (sb[a_offset].m_integer <= sb[b_offset].m_integer); break;
        case 3: taken = (sb[a_offset].m_integer >= sb[b_offset].m_integer); break;
        }
        if (taken)
        {
            *out_next_ip = ip - (int8_t)WOORT_BYTECODE(C8, bc);
            return true;
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_CALLNWO:
    {
        *out_next_ip = cenv->m_data_begin[WOORT_BYTECODE(MABC26, bc)].m_script_function;
        return true;
    }
    case WOORT_OPCODE_CALLNFP:
    case WOORT_OPCODE_CALLNJIT:
    {
        (void)woort_VMRuntime_request_set(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK);
        *out_next_ip = ip + 1;
        return true;
    }
    case WOORT_OPCODE_CALL:
    {
        const woort_GCClosure* target;
        if (m2 == 0) /* CALLS */
        {
            target = sb[(int16_t)WOORT_BYTECODE(BC16, bc)].m_closure;
        }
        else /* m2 == 1, CALLC */
        {
            target = cenv->m_data_begin[WOORT_BYTECODE(ABC24, bc)].m_closure;
        }
        if (target->m_script_function != NULL)
        {
            *out_next_ip = target->m_script_function;
            return true;
        }
		if (target->m_native_function != NULL
            /* || target->m_jit_function != NULL */)
		{
        	(void)woort_VMRuntime_request_set(
            	vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK);
		}
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_RET:
    {
        const woort_Value* trace_sb = sb;
        while (trace_sb[1].m_ret_bp.m_way == WOORT_CALL_WAY_FROM_NATIVE)
        {
            trace_sb = vm->m_stack_end - trace_sb[1].m_ret_bp.m_bp_offset;
            if (vm->m_stack_end - trace_sb < 3)
                return false;
        }
        *out_next_ip = (const woort_Bytecode*)trace_sb[2].m_ret_addr;
        return true;
    }

    case WOORT_OPCODE_JIFINITED:
    {
        woort_AtomicInt64* flag =
            (woort_AtomicInt64*)&cenv->m_data_begin[ip[1]].m_integer;
        const int64_t flag_stat = woort_atomic_load_explicit(
            flag, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
        if (flag_stat == 2)
        {
            *out_next_ip = cenv->m_code_begin + WOORT_BYTECODE(MABC26, bc);
            return true;
        }
        *out_next_ip = ip + 2;
        return true;
    }

    case WOORT_OPCODE_TRAP:
    {
        if (m2 != 0)
            return false;
        *out_next_ip = ip + 1;
        return true;
    }

    default:
    {
        *out_next_ip = woort_disassembly(ip, &_woort_WAIPO_empty_cb);
        return true;
    }
    }
}

typedef bool (*_woort_WAIPO_SetBreakFunc)(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    const woort_Bytecode* ip);

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_step_common(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count,
    _woort_WAIPO_SetBreakFunc set_break,
    const char* message)
{
    (void)args;
    (void)arg_count;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
    {
        (void)printf(WOORT_ANSI_HIR "Cannot locate CodeEnv for current IP.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const woort_Bytecode* next_ip;
    if (!_woort_WAIPO_get_next_ip(vm->m_ip, cenv, vm->m_sb, vm, &next_ip))
    {
        (void)printf(WOORT_ANSI_HIR "Cannot determine next instruction.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (!_woort_WAIPO_Debugger_focus_on(dbg, vm))
    {
        (void)printf(WOORT_ANSI_HIR "Failed to focus on VM.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (!set_break(dbg, vm, next_ip))
    {
        (void)printf(WOORT_ANSI_HIR "Failed to set step breakpoint.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)printf(WOORT_ANSI_HIG "%s" WOORT_ANSI_RST, message);

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_stepir(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    return _woort_WAIPO_cmd_step_common(
        dbg, vm, args, arg_count,
        _woort_WAIPO_Debugger_set_step_break,
        "Stepping to next instruction...\n");
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_step(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    return _woort_WAIPO_cmd_step_common(
        dbg, vm, args, arg_count,
        _woort_WAIPO_Debugger_set_step_source_break,
        "Stepping to next source line...\n");
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_next(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    return _woort_WAIPO_cmd_step_common(
        dbg, vm, args, arg_count,
        _woort_WAIPO_Debugger_set_next_source_break,
        "Stepping over to next source line...\n");
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_return(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    return _woort_WAIPO_cmd_step_common(
        dbg, vm, args, arg_count,
        _woort_WAIPO_Debugger_set_return_break,
        "Returning to caller...\n");
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
    { "dis",       NULL,   &_woort_WAIPO_cmd_dis },
    { "stepir",    "si",   &_woort_WAIPO_cmd_stepir },
    { "step",      "s",    &_woort_WAIPO_cmd_step },
    { "next",      "n",    &_woort_WAIPO_cmd_next },
    { "return",    "r",    &_woort_WAIPO_cmd_return },
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
