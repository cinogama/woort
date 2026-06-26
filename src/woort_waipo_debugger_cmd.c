#include "woort_waipo_debugger.h"
#include "woort_gc.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_opcode.h"
#include "woort_atomic.h"
#include "woort_gc_closure.h"
#include "woort_gc_struct.h"
#include "woort_gc_gchandle.h"
#include "woort_vector.h"
#include "woort_value.h"
#include "woort_serialize.h"
#include "woort_util.h"
#include "woort_utf8.h"
#include "woort_platform.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

typedef woort_WAIPO_CommandResult(*woort_WAIPO_CommandHandler)(
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
        "break       b       [line]          Set or list breakpoints.\n"
        "                    [func]          Break at function entry.\n"
        "                    [file:line]     Break at specific file:line.\n"
        "delete      d       <num>           Delete breakpoint by number.\n"
        "frame       f       <frameid>       Switch to a call frame.\n"
        "source      src     [file|range]    Display source code.\n"
        "dis                 [funcname]      Dump current VM's running bytecodes.\n"
        "                    --all           Or dump all bytecodes in current CodeEnv.\n"
        "                    [offset length] Or dump bytecodes in specified range.\n"
        "stepir      si                      Step one bytecode instruction.\n"
        "step        s                       Step one source line.\n"
        "next        n                       Step over to next source line, not entering callees.\n"
        "return      r                       Return to caller frame.\n"
        "print       p       <varname>       Print variable value by name.\n"
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

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    { int _woort_sys_ret = system("cls"); (void)_woort_sys_ret; }
#else
    { int _woort_sys_ret = system("clear"); (void)_woort_sys_ret; }
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
        "[%zu] %p  hold=%d  codes=[%p-%p]  data=%zu  const=%zu  funcs=%zu\n",
        ctx->m_index,
        (void*)cenv,
        cenv->m_hold ? 1 : 0,
        (const void*)cenv->m_code_begin,
        (const void*)cenv->m_code_end,
        cenv->m_data_count,
        cenv->m_const_records.m_size,
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

WOORT_NODISCARD static bool _woort_WAIPO_is_numeric(const char* s)
{
    if (s == NULL || *s == '\0')
        return false;

    for (const char* p = s; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
            return false;
    }
    return true;
}

#define WOORT_WAIPO_DOT "\xe2\x97\x8f"

static void _woort_WAIPO_emit_source_line(
    const char* line_buf,
    size_t current_line,
    bool has_highlight,
    size_t highlight_begin_line,
    size_t highlight_end_line,
    size_t highlight_begin_col,
    size_t highlight_end_col,
    woort_Vector* bp_lines)
{
    const bool is_highlight = has_highlight
        && current_line >= highlight_begin_line
        && current_line <= highlight_end_line;

    bool has_bp = false;
    {
        size_t k;
        for (k = 0; k < bp_lines->m_size; ++k)
        {
            if (*(size_t*)woort_vector_at(bp_lines, k) == current_line)
            {
                has_bp = true;
                break;
            }
        }
    }

    if (is_highlight)
    {
        const size_t line_len = strlen(line_buf);
        size_t col_start = 0;
        size_t col_end = line_len;
        if (current_line == highlight_begin_line && highlight_begin_col < line_len)
            col_start = highlight_begin_col;
        if (current_line == highlight_end_line && highlight_end_col < line_len)
            col_end = highlight_end_col;

        if (has_bp)
            (void)printf(WOORT_ANSI_HIR WOORT_WAIPO_DOT WOORT_ANSI_RST "%5zu | %.*s" WOORT_ANSI_INV "%.*s" WOORT_ANSI_RST "%.*s \n",
                current_line + 1,
                (int)col_start, line_buf,
                (int)(col_end - col_start), line_buf + col_start,
                (int)(line_len - col_end), line_buf + col_end);
        else
            (void)printf("> %5zu | %.*s" WOORT_ANSI_INV "%.*s" WOORT_ANSI_RST "%.*s \n",
                current_line + 1,
                (int)col_start, line_buf,
                (int)(col_end - col_start), line_buf + col_start,
                (int)(line_len - col_end), line_buf + col_end);
    }
    else if (has_bp)
        (void)printf(WOORT_ANSI_HIR WOORT_WAIPO_DOT WOORT_ANSI_RST "%5zu | %s\n",
            current_line + 1,
            line_buf);
    else
        (void)printf("  %5zu | %s\n",
            current_line + 1,
            line_buf);
}

static void _woort_WAIPO_print_source_file(
    woort_WAIPO_Debugger* dbg,
    const char* filepath,
    bool has_highlight,
    size_t highlight_begin_line,
    size_t highlight_end_line,
    size_t highlight_begin_col,
    size_t highlight_end_col,
    size_t from_line,
    size_t to_line)
{
    /* 预收集当前文件上有用户断点的行号 */
    woort_Vector bp_lines;
    woort_vector_init(&bp_lines, sizeof(size_t));
    {
        size_t i;
        for (i = 0; i < dbg->m_breakpoint_collection.m_user_breakpoints.m_size; ++i)
        {
            const woort_WAIPO_UserBreakpoint* ub =
                (const woort_WAIPO_UserBreakpoint*)woort_vector_at(
                    &dbg->m_breakpoint_collection.m_user_breakpoints, i);

            woort_CodeEnv* cenv;
            if (!woort_CodeEnv_find(ub->m_ip, &cenv))
                continue;

            const uint32_t offset = (uint32_t)(ub->m_ip - cenv->m_code_begin);
            woort_SourceLocation loc;
            if (!woort_CodeEnv_find_srcloc_by_offset(cenv, offset, &loc))
                continue;

            if (loc.m_filepath != NULL
                && strcmp(loc.m_filepath, filepath) == 0)
            {
                const size_t line = loc.m_begin_line;
                (void)woort_vector_push_back(&bp_lines, 1, &line);
            }
        }
    }
    woort_VFile* f = NULL;
    if (!woort_vfile_open(filepath, &f) || f == NULL)
    {
        (void)printf("Cannot open source: '%s'.\n", filepath);
        woort_vector_deinit(&bp_lines);
        return;
    }

    (void)printf("%s from line %zu to ", filepath, from_line + 1);
    if (to_line == SIZE_MAX)
        (void)printf("<end>:\n");
    else
        (void)printf("line %zu:\n", to_line + 1);

    char line_buf[4096];
    size_t current_line = 0;
    size_t line_idx = 0;
    char ch;
    bool prev_was_cr = false;

    for (;;)
    {
        if (woort_vfile_read(f, &ch, 1) == 0)
            break;

        if (ch == '\r')
        {
            prev_was_cr = true;
            line_buf[line_idx] = '\0';

            if (current_line >= from_line
                && (to_line == SIZE_MAX || current_line <= to_line))
            {
                _woort_WAIPO_emit_source_line(
                    line_buf, current_line,
                    has_highlight,
                    highlight_begin_line, highlight_end_line,
                    highlight_begin_col, highlight_end_col,
                    &bp_lines);
            }

            ++current_line;
            line_idx = 0;
        }
        else if (ch == '\n')
        {
            if (prev_was_cr)
            {
                prev_was_cr = false;
                continue;
            }

            line_buf[line_idx] = '\0';

            if (current_line >= from_line
                && (to_line == SIZE_MAX || current_line <= to_line))
            {
                _woort_WAIPO_emit_source_line(
                    line_buf, current_line,
                    has_highlight,
                    highlight_begin_line, highlight_end_line,
                    highlight_begin_col, highlight_end_col,
                    &bp_lines);
            }

            ++current_line;
            line_idx = 0;
        }
        else
        {
            prev_was_cr = false;
            if (line_idx < sizeof(line_buf) - 1)
                line_buf[line_idx++] = ch;
        }
    }

    /* Handle last line if file does not end with a newline */
    if (line_idx > 0)
    {
        line_buf[line_idx] = '\0';

        if (current_line >= from_line
            && (to_line == SIZE_MAX || current_line <= to_line))
        {
            _woort_WAIPO_emit_source_line(
                line_buf, current_line,
                has_highlight,
                highlight_begin_line, highlight_end_line,
                highlight_begin_col, highlight_end_col,
                &bp_lines);
        }
    }

    woort_vfile_close(f);
    woort_vector_deinit(&bp_lines);
    (void)printf("\n");
}

#undef WOORT_WAIPO_DOT

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
        if (_woort_WAIPO_is_numeric(args[1]))
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
            dbg,
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
            dbg,
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
    const woort_Bytecode* current_ip,
    const char* sign)
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
            (void)printf(WOORT_ANSI_HIG "%s\t%04zu:\t" WOORT_ANSI_RST, sign, offset);
        else
            (void)printf("\t%04zu:\t", offset);

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
                NULL,
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

    const char* const sign = dbg->m_current_frame_depth == 0 ? "=>" : "\\>";

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
                        frame_ip,
                        sign);

                    return WOORT_WAIPO_CMD_NEED_NEXT;
                }
            }
        }

        (void)printf("Unable to locate function, display following 100 words.\n");
        _woort_WAIPO_dump_disassembly_range(
            cenv,
            current_ip_offset,
            current_ip_offset + 100,
            frame_ip,
            sign);

        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* first_arg = args[1];

    if (_woort_WAIPO_is_numeric(first_arg))
    {
        const size_t begin_offset = (size_t)strtoul(first_arg, NULL, 10);

        if (arg_count >= 3)
        {
            const size_t length = (size_t)strtoul(args[2], NULL, 10);
            (void)printf("Display +%04zu to +%04zu.\n",
                begin_offset, begin_offset + length);

            _woort_WAIPO_dump_disassembly_range(
                cenv, begin_offset,
                begin_offset + length,
                frame_ip,
                sign);
        }
        else
        {
            (void)printf(WOORT_ANSI_HIR "Missing length, command failed.\n" WOORT_ANSI_RST);
        }

        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (strcmp(first_arg, "--all") == 0)
    {
        _woort_WAIPO_dump_disassembly_range(cenv, 0, SIZE_MAX, frame_ip, sign);
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

static int _woort_WAIPO_empty_cb(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

WOORT_NODISCARD bool _woort_WAIPO_get_next_ip(
    const woort_Bytecode* ip,
    woort_CodeEnv* cenv,
    const woort_Value* sb,
    woort_VMRuntime* vm,
    /* OPTIONAL */ const woort_Bytecode** out_next_ip)
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
        case 0: taken = (sb[a_offset].m_integer < sb[b_offset].m_integer); break;
        case 1: taken = (sb[a_offset].m_integer > sb[b_offset].m_integer); break;
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
        case 0: taken = (sb[a_offset].m_integer < sb[b_offset].m_integer); break;
        case 1: taken = (sb[a_offset].m_integer > sb[b_offset].m_integer); break;
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
        if (m2 == 3)
        {
            // Is POPRS. not ret.
            goto label_fall_to_default;
        }

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
    label_fall_to_default:
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

/* ====================================================================
 * print / p command
 * ==================================================================== */

void _woort_WAIPO_print_value(woort_DynBox boxed, bool is_fuzzy)
{
    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    if (_woort_serialize_dynbox_to_buf_for_debug(
        boxed, &buf, &visited_set, 0, is_fuzzy))
    {
        (void)printf("%.*s", (int)buf.m_size, (const char*)buf.m_data);
    }

    woort_vector_deinit(&buf);
    woort_hashmap_deinit(&visited_set);
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_print(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        (void)printf("Usage: print <varname>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* const var_name = args[1];

    /*
     * 定位当前选中的调用栈帧。
     */
    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_to_depth(vm, dbg->m_current_frame_depth, &trace))
    {
        (void)printf("No callstack at current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (trace.m_code_addr == NULL)
    {
        (void)printf("No code address for current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    /*
     * 获取当前帧的 CodeEnv 和 SB 偏移。
     */
    woort_CodeEnv* cenv = NULL;
    if (!woort_CodeEnv_find(trace.m_code_addr, &cenv) || cenv == NULL)
    {
        (void)printf("Cannot locate CodeEnv for current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    woort_Value* const frame_sb =
        vm->m_stack_end - trace.m_callstack_offset_of_base;

    /*
     * 获取当前帧所在函数的字节码范围。
     */
    const uint32_t frame_ip_offset =
        (uint32_t)(trace.m_code_addr - cenv->m_code_begin);

    uint32_t func_begin = 0;
    uint32_t func_end = (uint32_t)(cenv->m_code_end - cenv->m_code_begin);
    {
        size_t j;
        for (j = 0; j < cenv->m_function_boundaries.m_size; ++j)
        {
            const woort_FunctionBoundary* boundary =
                (const woort_FunctionBoundary*)woort_vector_at(
                    (woort_Vector*)&cenv->m_function_boundaries, j);

            if (frame_ip_offset >= boundary->m_offset_begin
                && frame_ip_offset < boundary->m_offset_begin + boundary->m_code_length)
            {
                func_begin = boundary->m_offset_begin;
                func_end = boundary->m_offset_begin + boundary->m_code_length;
                break;
            }
        }
    }

    /*
     * 搜索当前 CodeEnv 中的局部变量（仅限当前函数范围内声明的）。
     */
    size_t i;
    size_t found_count = 0;
    for (i = 0; i < cenv->m_pdb.m_local_var_debug_info.m_size; ++i)
    {
        const woort_LocalVarDebugInfo* info =
            (const woort_LocalVarDebugInfo*)woort_vector_at(
                (woort_Vector*)&cenv->m_pdb.m_local_var_debug_info, i);

        if (info->m_name == NULL)
            continue;
        if (strcmp(info->m_name, var_name) != 0)
            continue;

        /*
         * 仅当变量在当前函数范围内时显示。
         */
        if (info->m_function_offset < func_begin
            || info->m_function_offset >= func_end)
            continue;

        (void)printf(
            "[local]  %s@[SB%+d] = ",
            info->m_name,
            info->m_stack_offset);

        _woort_WAIPO_print_value(frame_sb[info->m_stack_offset].m_dynamic, true);
        printf("\n");

        ++found_count;
    }

    /*
     * 搜索当前 CodeEnv 中的静态变量。
     */
    for (i = 0; i < cenv->m_pdb.m_static_var_debug_info.m_size; ++i)
    {
        const woort_StaticVarDebugInfo* info =
            (const woort_StaticVarDebugInfo*)woort_vector_at(
                (woort_Vector*)&cenv->m_pdb.m_static_var_debug_info, i);

        if (info->m_name == NULL)
            continue;
        if (strcmp(info->m_name, var_name) != 0)
            continue;

        const size_t global_index =
            cenv->m_const_records.m_size + (size_t)info->m_static_idx;

        (void)printf(
            "[static] %s@G[%zu] = ",
            info->m_name,
            global_index);

        _woort_WAIPO_print_value(cenv->m_data_begin[global_index].m_dynamic, true);
        printf("\n");

        ++found_count;
    }

    if (found_count == 0)
        (void)printf("No variable named '%s' in current frame.\n", var_name);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

/* ====================================================================
 * break / b command
 * ==================================================================== */

typedef struct _woort_WAIPO_BreakByFileLineContext
{
    const char* m_filepath;
    uint32_t m_line;
    size_t m_found_count;
    woort_WAIPO_Debugger* m_dbg;
    woort_CodeEnv* m_cenv;
    uint32_t m_offset;
} _woort_WAIPO_BreakByFileLineContext;

static bool _woort_WAIPO_break_by_file_line_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_BreakByFileLineContext* ctx =
        (_woort_WAIPO_BreakByFileLineContext*)user_data;

    uint32_t offset;
    if (woort_CodeEnv_find_offset_by_srcloc(cenv, ctx->m_filepath, ctx->m_line, &offset))
    {
        ctx->m_cenv = cenv;
        ctx->m_offset = offset;
        ++ctx->m_found_count;
    }
    return true;
}

WOORT_NODISCARD static bool _woort_WAIPO_add_user_breakpoint(
    woort_WAIPO_Debugger* dbg,
    const woort_Bytecode* ip,
    const char* desc_fmt,
    ...);

typedef struct _woort_WAIPO_BreakByFuncContext
{
    const char* m_funcname;
    size_t m_found_count;
    woort_WAIPO_Debugger* m_dbg;
} _woort_WAIPO_BreakByFuncContext;

static bool _woort_WAIPO_break_by_func_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_BreakByFuncContext* ctx =
        (_woort_WAIPO_BreakByFuncContext*)user_data;

    const size_t boundary_count = cenv->m_function_boundaries.m_size;
    size_t i;
    for (i = 0; i < boundary_count; ++i)
    {
        const woort_FunctionBoundary* boundary =
            (const woort_FunctionBoundary*)woort_vector_at(
                (woort_Vector*)&cenv->m_function_boundaries, i);

        if (boundary->m_name == NULL)
            continue;
        if (strstr(boundary->m_name, ctx->m_funcname) == NULL)
            continue;

        const woort_Bytecode* target_ip =
            cenv->m_code_begin + boundary->m_offset_begin;

        if (!_woort_WAIPO_add_user_breakpoint(
            ctx->m_dbg, target_ip, "%s", boundary->m_name))
        {
            (void)printf("Failed to set breakpoint.\n");
            return false;
        }

        (void)printf("Breakpoint %zu at %s\n",
            ctx->m_dbg->m_breakpoint_collection.m_user_breakpoints.m_size,
            boundary->m_name);

        ++ctx->m_found_count;
    }
    return true;
}

WOORT_NODISCARD static bool _woort_WAIPO_add_user_breakpoint(
    woort_WAIPO_Debugger* dbg,
    const woort_Bytecode* ip,
    const char* desc_fmt,
    ...)
{
    woort_WAIPO_UserBreakpoint ub;
    ub.m_ip = ip;

    if (desc_fmt != NULL)
    {
        va_list args;
        va_start(args, desc_fmt);
        (void)vsnprintf(ub.m_desc, sizeof(ub.m_desc), desc_fmt, args);
        va_end(args);
    }
    else
    {
        ub.m_desc[0] = '\0';
    }

    woort_WAIPO_UserBreakpoint* emplaced;
    if (!woort_vector_emplace_back(
        &dbg->m_breakpoint_collection.m_user_breakpoints, 1,
        (void**)&emplaced))
    {
        return false;
    }

    *emplaced = ub;

    if (!_woort_WAIPO_BreakpointCollection_break_at(
        &dbg->m_breakpoint_collection, ip))
    {
        (void)woort_vector_erase_at(
            &dbg->m_breakpoint_collection.m_user_breakpoints,
            dbg->m_breakpoint_collection.m_user_breakpoints.m_size - 1);
        return false;
    }

    return true;
}

WOORT_NODISCARD static woort_WAIPO_CommandResult _woort_WAIPO_break_at_file_line(
    woort_WAIPO_Debugger* dbg,
    const char* filepath,
    uint32_t line)
{
    _woort_WAIPO_BreakByFileLineContext ctx;
    ctx.m_filepath = filepath;
    ctx.m_line = line;
    ctx.m_found_count = 0;
    ctx.m_dbg = dbg;
    ctx.m_cenv = NULL;
    ctx.m_offset = 0;

    woort_CodeEnv_foreach(&_woort_WAIPO_break_by_file_line_callback, &ctx);

    if (ctx.m_found_count == 0)
    {
        (void)printf("No code at %s:%u\n", filepath, line);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const woort_Bytecode* target_ip =
        ctx.m_cenv->m_code_begin + ctx.m_offset;

    if (!_woort_WAIPO_add_user_breakpoint(
        dbg, target_ip, "%s:%u", filepath, line))
    {
        (void)printf("Failed to set breakpoint.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (ctx.m_found_count > 1)
        (void)printf("(breakpoint set at first of %zu matches)\n",
            ctx.m_found_count);

    (void)printf("Breakpoint %zu at %s:%u\n",
        dbg->m_breakpoint_collection.m_user_breakpoints.m_size,
        filepath, line);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_break(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        if (dbg->m_breakpoint_collection.m_user_breakpoints.m_size == 0)
        {
            (void)printf("No breakpoints.\n");
        }
        else
        {
            (void)printf("Num  What\n");
            size_t i;
            for (i = 0; i < dbg->m_breakpoint_collection.m_user_breakpoints.m_size; ++i)
            {
                const woort_WAIPO_UserBreakpoint* ub =
                    (const woort_WAIPO_UserBreakpoint*)woort_vector_at(
                        &dbg->m_breakpoint_collection.m_user_breakpoints, i);

                const char* desc = ub->m_desc[0] != '\0' ? ub->m_desc : "<unknown>";
                (void)printf("%-4zu %s\n", i + 1, desc);
            }
        }
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* arg = args[1];

    if (_woort_WAIPO_is_numeric(arg))
    {
        const uint32_t line = (uint32_t)strtoul(arg, NULL, 10);
        if (line == 0)
        {
            (void)printf("Invalid line number: %s\n", arg);
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        woort_VMRuntime_TraceCallstack trace;
        if (!_woort_WAIPO_trace_to_depth(vm, dbg->m_current_frame_depth, &trace))
        {
            (void)printf("No callstack. Use 'break <file>:<line>' to specify file.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        if (trace.m_code_addr == NULL || trace.m_file_or_lib_name == NULL)
        {
            (void)printf("Current frame has no source location.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        woort_CodeEnv* cenv;
        if (!woort_CodeEnv_find(trace.m_code_addr, &cenv))
        {
            (void)printf("Cannot locate CodeEnv for current frame.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        uint32_t offset;
        if (!woort_CodeEnv_find_offset_by_srcloc(
            cenv, trace.m_file_or_lib_name, line - 1, &offset))
        {
            (void)printf("Line %u not found in '%s'.\n",
                line, trace.m_file_or_lib_name);
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        const woort_Bytecode* target_ip = cenv->m_code_begin + offset;

        if (!_woort_WAIPO_add_user_breakpoint(
            dbg, target_ip, "%s:%u",
            trace.m_file_or_lib_name, line))
        {
            (void)printf("Failed to set breakpoint.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        (void)printf("Breakpoint %zu at %s:%u\n",
            dbg->m_breakpoint_collection.m_user_breakpoints.m_size,
            trace.m_file_or_lib_name, line);

        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* colon = strchr(arg, ':');
    if (colon != NULL)
    {
        const char* file_end = colon;
        const char* line_str = colon + 1;

        if (_woort_WAIPO_is_numeric(line_str))
        {
            uint32_t line = (uint32_t)strtoul(line_str, NULL, 10);
            if (line == 0)
                line = 1;

            const size_t file_len = (size_t)(file_end - arg);
            char filepath[512];
            const size_t copy_len = file_len < sizeof(filepath) - 1
                ? file_len : sizeof(filepath) - 1;
            (void)memcpy(filepath, arg, copy_len);
            filepath[copy_len] = '\0';

            return _woort_WAIPO_break_at_file_line(dbg, filepath, line);
        }
    }

    /* Check for 'break <file> <line>' two-arg format */
    if (arg_count >= 3)
    {
        if (_woort_WAIPO_is_numeric(args[2]))
        {
            uint32_t line = (uint32_t)strtoul(args[2], NULL, 10);
            if (line == 0)
                line = 1;

            return _woort_WAIPO_break_at_file_line(dbg, arg, line);
        }
    }

    /* Treat as function name */
    {
        _woort_WAIPO_BreakByFuncContext ctx;
        ctx.m_funcname = arg;
        ctx.m_found_count = 0;
        ctx.m_dbg = dbg;

        woort_CodeEnv_foreach(&_woort_WAIPO_break_by_func_callback, &ctx);

        if (ctx.m_found_count == 0)
        {
            (void)printf("Function '%s' not found.\n", arg);
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        if (ctx.m_found_count > 1)
            (void)printf("(set %zu breakpoints)\n",
                ctx.m_found_count);

        return WOORT_WAIPO_CMD_NEED_NEXT;
    }
}

/* ====================================================================
 * delete / d command
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_delete(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)vm;

    if (arg_count < 2)
    {
        (void)printf("Usage: delete <breakpoint_num>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const long num = strtol(args[1], NULL, 10);
    if (num <= 0 || (size_t)num > dbg->m_breakpoint_collection.m_user_breakpoints.m_size)
    {
        (void)printf("Invalid breakpoint number: %s\n", args[1]);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const size_t idx = (size_t)(num - 1);

    woort_WAIPO_UserBreakpoint* ub =
        (woort_WAIPO_UserBreakpoint*)woort_vector_at(
            &dbg->m_breakpoint_collection.m_user_breakpoints, idx);

    _woort_WAIPO_BreakpointCollection_cancel_break_at(
        &dbg->m_breakpoint_collection, ub->m_ip);

    (void)woort_vector_erase_at(
        &dbg->m_breakpoint_collection.m_user_breakpoints, idx);

    (void)printf("Breakpoint %zu deleted.\n", (size_t)num);

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
    { "dis",       NULL,   &_woort_WAIPO_cmd_dis },
    { "stepir",    "si",   &_woort_WAIPO_cmd_stepir },
    { "step",      "s",    &_woort_WAIPO_cmd_step },
    { "next",      "n",    &_woort_WAIPO_cmd_next },
    { "return",    "r",    &_woort_WAIPO_cmd_return },
    { "print",     "p",    &_woort_WAIPO_cmd_print },
    { "break",     "b",    &_woort_WAIPO_cmd_break },
    { "delete",    "d",    &_woort_WAIPO_cmd_delete },
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
            // Reset frame depth.
            debugger_instance->m_current_frame_depth = 0;

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

            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(vm->m_ip, &cenv))
            {
                const uint32_t code_offset =
                    (uint32_t)(vm->m_ip - cenv->m_code_begin);
                (void)printf("\nBytecode offset = %04u", code_offset);
            }

            (void)printf("\n");

            _woort_WAIPO_cmd_source(debugger_instance, vm, NULL, 0);
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

        char* tokens[128];
        size_t token_count = _woort_WAIPO_split_line(
            line_buf, tokens, 128);

        if (token_count == 0)
        {
            if (debugger_instance->m_last_command[0] == '\0')
                continue;

            (void)strncpy(line_buf, debugger_instance->m_last_command,
                sizeof(line_buf) - 1);
            line_buf[sizeof(line_buf) - 1] = '\0';

            token_count = _woort_WAIPO_split_line(
                line_buf, tokens, 128);

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
