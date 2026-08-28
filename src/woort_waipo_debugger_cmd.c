#include "woort.h"
#include "woort_debugger_session.h"
#include "woort_gc.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_opcode.h"
#include "woort_atomic.h"
#include "woort_gc_closure.h"
#include "woort_gc_struct.h"
#include "woort_gc_gchandle.h"
#include "woort_gc_units.h"
#include "woort_threads.h"
#include "woort_vector.h"
#include "woort_value.h"
#include "woort_serialize.h"
#include "woort_util.h"
#include "woort_platform.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/*
 * WAIPO (Watch And Inspect Program Operation) interactive frontend.
 *
 * Since the debugger-session rework this REPL is a plain consumer of the
 * public woort_Debugger_* API - the same interface an IDE debug adapter
 * (wooly) uses.  woort_WAIPO_Debugger_attach attaches a session and spawns
 * a dedicated REPL thread whose loop is the CLI twin of a DAP message
 * loop:
 *
 *     wait_for_break -> print stop location -> stdin command loop
 *                    -> continue/step -> back to wait_for_break
 *
 * A handful of commands are runtime-introspection helpers with no public
 * API equivalent (`list codeenv`, `dis`, `global`, the detailed `vm`
 * listing, `quit`); those reach into runtime internals through the
 * in-tree escape hatch _woort_Debugger_session_take_stopped_vm and are
 * marked below.
 */

/* ====================================================================
 * Frontend state
 * ==================================================================== */

typedef struct woort_WAIPO_FrontendBreakpoint
{
    woort_DebuggerBreakpointId m_id;
    bool m_is_function_bp;
    char m_file[WOORT_DEBUGGER_MAX_PATH];
    uint32_t m_line; /* 1-based request line (source breakpoints) */
    char m_function_name[WOORT_DEBUGGER_MAX_NAME];

} woort_WAIPO_FrontendBreakpoint;

typedef struct woort_WAIPO_Frontend
{
    woort_DebuggerVmId m_current_vm;

    size_t m_current_frame_depth;
    bool m_first_breakdown;
    /* Full command line (with args) of the last executed command, so an
     * empty input line can repeat it. Sized to hold any stdin line. */
    char m_last_command[4096];

    /* woort_WAIPO_FrontendBreakpoint; mirrors the breakpoints this
       frontend created, numbered 1..N for `break` listing and delete. */
    woort_Vector m_breakpoints;

} woort_WAIPO_Frontend;

typedef enum woort_WAIPO_CommandResult
{
    WOORT_WAIPO_CMD_NEED_NEXT,
    WOORT_WAIPO_CMD_CONTINUE,
} woort_WAIPO_CommandResult;

typedef woort_WAIPO_CommandResult(*woort_WAIPO_CommandHandler)(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count);

typedef struct woort_WAIPO_CommandEntry
{
    const char* m_name;
    const char* m_alias;
    woort_WAIPO_CommandHandler m_handler;
} woort_WAIPO_CommandEntry;

/* ====================================================================
 * Shared helpers
 * ==================================================================== */

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

/*
 * Walk the internal callstack of the (stopped) VM to a given depth.
 * In-tree escape used by `dis` / `global`, which need the raw frame ip.
 */
WOORT_NODISCARD static bool _woort_WAIPO_trace_stopped_vm_to_depth(
    size_t target_depth,
    /* OPTIONAL */ woort_VMRuntime_TraceCallstack* out_trace)
{
    woort_VMRuntime* vm = NULL;
    if (!_woort_Debugger_session_take_stopped_vm(&vm) || vm == NULL)
        return false;

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

/* ====================================================================
 * Next-ip computation for the stepping machinery (engine support)
 * ==================================================================== */

/* ====================================================================
 * help / continue / quit / exit / clear
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_help(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)frontend;
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
        "                    [func]          Break at function entry (exact name).\n"
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
        "global      g       <index>         Print const/static slot value by global index.\n"
        "vm                  [id]            List all VM(s), or switch to VM by id.\n"
        "\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_continue(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)args;
    (void)arg_count;

    if (!woort_Debugger_continue())
    {
        (void)printf(WOORT_ANSI_HIR "No VM is stopped.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)frontend;
    (void)printf("Continue running...\n");

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_quit(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)frontend;
    (void)args;
    (void)arg_count;

    (void)woort_Debugger_terminate_all();

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_exit(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)frontend;
    (void)args;
    (void)arg_count;

    _Exit(0);

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_clear(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)frontend;
    (void)args;
    (void)arg_count;

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    { int _woort_sys_ret = system("cls"); (void)_woort_sys_ret; }
#else
    { int _woort_sys_ret = system("clear"); (void)_woort_sys_ret; }
#endif

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

/* ====================================================================
 * list codeenv / list vm   (runtime introspection, in-tree internals)
 * ==================================================================== */

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
    woort_WAIPO_Frontend* frontend)
{
    (void)frontend;

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
    woort_DebuggerVmId m_stopped_vm;
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
        "%c[%" PRIu64 "] VMRuntime(%p)  ip=%p  sp=%p  stack=[%p-%p]  usage=%.1f%%\n",
        vm->m_serial == ctx->m_stopped_vm ? '*' : ' ',
        (uint64_t)vm->m_serial,
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
    woort_WAIPO_Frontend* frontend)
{
    _woort_WAIPO_ListVMContext ctx;
    ctx.m_index = 0;
    ctx.m_stopped_vm = frontend->m_current_vm;

    woort_GC_foreach_root_vm(&_woort_WAIPO_list_vm_callback, &ctx);

    if (ctx.m_index == 0)
        (void)printf("No VM.\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_list(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        (void)printf("Usage: list <codeenv|vm>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (strcmp(args[1], "codeenv") == 0)
        return _woort_WAIPO_list_codeenv(frontend);

    if (strcmp(args[1], "vm") == 0)
        return _woort_WAIPO_list_vm(frontend);

    (void)printf("Unknown list target: '%s'. Available: codeenv, vm\n", args[1]);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

/* ====================================================================
 * backtrace / frame
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_backtrace(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    size_t max_depth = 32;
    if (arg_count >= 2)
    {
        const long val = strtol(args[1], NULL, 10);
        if (val > 0)
            max_depth = (size_t)val;
    }

    const size_t depth =
        woort_Debugger_get_stack_depth(frontend->m_current_vm);

    (void)printf("Backtrace:\n");

    size_t printed = 0;
    for (size_t i = 0; i < depth; ++i)
    {
        if (printed >= max_depth)
        {
            (void)printf("    ...\n");
            break;
        }

        woort_DebuggerFrame frame;
        if (!woort_Debugger_get_stack_frame(
            frontend->m_current_vm, i, &frame))
        {
            break;
        }

        const char* const func =
            frame.m_function_name[0] != '\0'
            ? frame.m_function_name : NULL;
        const char* const file =
            frame.m_file_or_lib_name[0] != '\0'
            ? frame.m_file_or_lib_name : NULL;

        if (func != NULL && file != NULL)
        {
            if (frame.m_has_location)
                (void)printf("    at %s (%s:%u:%u)\n",
                    func, file, frame.m_line + 1, frame.m_column + 1);
            else
                (void)printf("    at %s (%s)\n", func, file);
        }
        else if (func != NULL)
        {
            (void)printf("    at %s\n", func);
        }
        else if (file != NULL)
        {
            if (frame.m_has_location)
                (void)printf("    at <unknown> (%s:%u:%u)\n",
                    file, frame.m_line + 1, frame.m_column + 1);
            else
                (void)printf("    at <unknown> (%s)\n", file);
        }
        else
        {
            (void)printf("    at <unknown>\n");
        }

        ++printed;
    }

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_frame(
    woort_WAIPO_Frontend* frontend,
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

    woort_DebuggerFrame frame;
    if (!woort_Debugger_get_stack_frame(
        frontend->m_current_vm, (size_t)frame_id, &frame))
    {
        (void)printf("No such frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    frontend->m_current_frame_depth = (size_t)frame_id;

    (void)printf("Now at: frame %zu", (size_t)frame_id);

    if (frame.m_function_name[0] != '\0')
    {
        (void)printf("  %s", frame.m_function_name);

        if (frame.m_file_or_lib_name[0] != '\0')
        {
            if (frame.m_has_location)
                (void)printf(" (%s:%u:%u)",
                    frame.m_file_or_lib_name,
                    frame.m_line + 1, frame.m_column + 1);
            else
                (void)printf(" (%s)", frame.m_file_or_lib_name);
        }
    }
    else
    {
        (void)printf("  <unknown>");
    }

    (void)printf("\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

/* ====================================================================
 * source
 * ==================================================================== */

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
    woort_WAIPO_Frontend* frontend,
    const char* filepath,
    bool has_highlight,
    size_t highlight_begin_line,
    size_t highlight_end_line,
    size_t highlight_begin_col,
    size_t highlight_end_col,
    size_t from_line,
    size_t to_line)
{
    /* Collect the (resolved) lines this frontend's breakpoints sit on
       for the requested file. */
    woort_Vector bp_lines;
    woort_vector_init(&bp_lines, sizeof(size_t));
    {
        for (size_t i = 0; i < frontend->m_breakpoints.m_size; ++i)
        {
            const woort_WAIPO_FrontendBreakpoint* fb =
                (const woort_WAIPO_FrontendBreakpoint*)woort_vector_at(
                &frontend->m_breakpoints, i);

            if (fb->m_is_function_bp)
                continue;
            if (strcmp(fb->m_file, filepath) != 0)
                continue;

            /* Query reports the resolved line, or the requested line for a
               still-pending source breakpoint; either way it is 1-based,
               while the emit path compares 0-based lines.  Copy through a
               real size_t so push_back does not read past a uint32_t. */
            bool resolved = false;
            uint32_t marked_line = 0;
            if (woort_Debugger_query_breakpoint(
                fb->m_id, &resolved, &marked_line)
                && marked_line != 0)
            {
                const size_t marked_line0 = (size_t)marked_line - 1;
                (void)woort_vector_push_back(&bp_lines, 1, &marked_line0);
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

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_source(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    woort_DebuggerFrame frame;
    if (!woort_Debugger_get_stack_frame(
        frontend->m_current_vm, frontend->m_current_frame_depth, &frame))
    {
        (void)printf("No callstack.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (!frame.m_has_location || frame.m_file_or_lib_name[0] == '\0')
    {
        (void)printf("No source location available for current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    size_t display_range = 5;
    const char* target_file = frame.m_file_or_lib_name;
    /* The public API shares the srcloc basis (first line = 0),
       same as the source printer. */
    const size_t highlight_begin = frame.m_line;
    const size_t highlight_end = frame.m_end_line;
    const size_t highlight_begin_col = frame.m_column;
    const size_t highlight_end_col = frame.m_end_column;
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
            if (strcmp(args[1], frame.m_file_or_lib_name) == 0)
            {
                has_highlight = true;
            }
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
            frontend,
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
            frontend,
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

/* ====================================================================
 * dis   (runtime introspection, in-tree internals)
 * ==================================================================== */

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
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_stopped_vm_to_depth(
        frontend->m_current_frame_depth, &trace))
    {
        (void)printf(WOORT_ANSI_HIR "No callstack.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* const sign = frontend->m_current_frame_depth == 0 ? "=>" : "\\>";

    const woort_Bytecode* frame_ip = trace.m_code_addr;
    if (frame_ip == NULL)
    {
        woort_VMRuntime* vm = NULL;
        if (_woort_Debugger_session_take_stopped_vm(&vm) && vm != NULL)
            frame_ip = vm->m_ip;
    }

    if (frame_ip == NULL)
    {
        (void)printf(WOORT_ANSI_HIR "No code address for current frame.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

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

/* ====================================================================
 * stepir / step / next / return
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_stepir(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)args;
    (void)arg_count;

    if (!woort_Debugger_step_instruction())
    {
        (void)printf(WOORT_ANSI_HIR "Cannot determine next instruction.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)frontend;
    (void)printf("Stepping to next instruction...\n");

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_step(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)args;
    (void)arg_count;

    if (!woort_Debugger_step_in())
    {
        (void)printf(WOORT_ANSI_HIR "Failed to arm source step.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)frontend;
    (void)printf("Stepping to next source line...\n");

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_next(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)args;
    (void)arg_count;

    if (!woort_Debugger_step_over())
    {
        (void)printf(WOORT_ANSI_HIR "Failed to arm source step-over.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)frontend;
    (void)printf("Stepping over to next source line...\n");

    return WOORT_WAIPO_CMD_CONTINUE;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_return(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    (void)args;
    (void)arg_count;

    if (!woort_Debugger_step_out())
    {
        (void)printf(WOORT_ANSI_HIR "Failed to arm step-out.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)frontend;
    (void)printf("Returning to caller...\n");

    return WOORT_WAIPO_CMD_CONTINUE;
}

/* ====================================================================
 * print / global
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_print(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        (void)printf("Usage: print <varname>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* const var_name = args[1];

    woort_DebuggerVariableKind kind = WOORT_DEBUGGER_VARIABLE_NOT_FOUND;
    char* const value = woort_Debugger_get_variable_value_by_name(
        frontend->m_current_vm, frontend->m_current_frame_depth,
        var_name, &kind);

    if (value == NULL)
    {
        (void)printf("No variable named '%s' in current frame.\n", var_name);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    switch (kind)
    {
    case WOORT_DEBUGGER_VARIABLE_LOCAL:
        (void)printf("[local]  %s = %s\n", var_name, value);
        break;
    case WOORT_DEBUGGER_VARIABLE_STATIC:
        (void)printf("[static] %s = %s\n", var_name, value);
        break;
    default:
        (void)printf("%s = %s\n", var_name, value);
        break;
    }

    woort_free(value);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_global(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2 || !_woort_WAIPO_is_numeric(args[1]))
    {
        (void)printf("Usage: global <index>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const long index = strtol(args[1], NULL, 10);
    if (index < 0)
    {
        (void)printf("Invalid index: %s\n", args[1]);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_stopped_vm_to_depth(
        frontend->m_current_frame_depth, &trace))
    {
        (void)printf("No callstack at current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (trace.m_code_addr == NULL)
    {
        (void)printf("No code address for current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    woort_CodeEnv* cenv = NULL;
    if (!woort_CodeEnv_find(trace.m_code_addr, &cenv) || cenv == NULL)
    {
        (void)printf("Cannot locate CodeEnv for current frame.\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const size_t global_index = (size_t)index;
    if (global_index >= cenv->m_data_count)
    {
        (void)printf("Index %zu out of range [0, %zu).\n",
            global_index, cenv->m_data_count);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    /*
     * m_data_begin 前 m_const_records.m_size 个槽位为常量区，其余为静态区。
     */
    const size_t const_count = cenv->m_const_records.m_size;

    if (global_index < const_count)
    {
        const woort_ConstRecord* record =
            (const woort_ConstRecord*)woort_vector_at(
            (woort_Vector*)&cenv->m_const_records, global_index);

        if (record->m_func_name != NULL)
            (void)printf("[const]  %s@G[%zu] = ",
                record->m_func_name, global_index);
        else
            (void)printf("[const]  G[%zu] = ", global_index);
    }
    else
    {
        /* Resolve the static variable name from debug info when available. */
        const size_t static_idx = global_index - const_count;
        const char* var_name = NULL;

        for (size_t i = 0;
            i < cenv->m_pdb.m_static_var_debug_info.m_size;
            ++i)
        {
            const woort_StaticVarDebugInfo* info =
                (const woort_StaticVarDebugInfo*)woort_vector_at(
                (woort_Vector*)&cenv->m_pdb.m_static_var_debug_info, i);

            if ((size_t)info->m_static_idx == static_idx)
            {
                var_name = info->m_name;
                break;
            }
        }

        if (var_name != NULL)
            (void)printf("[static] %s@G[%zu] = ",
                var_name, global_index);
        else
            (void)printf("[static] G[%zu] = ", global_index);
    }

    _woort_serialize_dynbox_print_for_debug(cenv->m_data_begin[global_index].m_dynamic, true);
    printf("\n");

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

/* ====================================================================
 * break / delete
 * ==================================================================== */

static bool _woort_WAIPO_add_frontend_breakpoint(
    woort_WAIPO_Frontend* frontend,
    const woort_WAIPO_FrontendBreakpoint* record)
{
    woort_WAIPO_FrontendBreakpoint* emplaced = NULL;
    if (!woort_vector_emplace_back(
        &frontend->m_breakpoints, 1, (void**)&emplaced))
    {
        return false;
    }

    *emplaced = *record;
    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_break(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    /* Bare `break`: list this frontend's breakpoints with resolution state. */
    if (arg_count < 2)
    {
        if (frontend->m_breakpoints.m_size == 0)
        {
            (void)printf("No breakpoints.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        (void)printf("Num  What\n");
        for (size_t i = 0; i < frontend->m_breakpoints.m_size; ++i)
        {
            const woort_WAIPO_FrontendBreakpoint* fb =
                (const woort_WAIPO_FrontendBreakpoint*)woort_vector_at(
                &frontend->m_breakpoints, i);

            bool resolved = false;
            uint32_t resolved_line = 0;
            (void)woort_Debugger_query_breakpoint(
                fb->m_id, &resolved, &resolved_line);

            if (fb->m_is_function_bp)
            {
                (void)printf("%-4zu func %s%s\n",
                    i + 1,
                    fb->m_function_name,
                    resolved ? "" : "  [pending]");
            }
            else if (resolved && resolved_line + 1 != fb->m_line)
            {
                (void)printf("%-4zu %s:%u -> line %u\n",
                    i + 1, fb->m_file, fb->m_line, resolved_line + 1);
            }
            else
            {
                (void)printf("%-4zu %s:%u%s\n",
                    i + 1, fb->m_file, fb->m_line,
                    resolved ? "" : "  [pending]");
            }
        }
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const char* arg = args[1];

    /* break <line>: use the selected frame's file. */
    if (_woort_WAIPO_is_numeric(arg))
    {
        const uint32_t line = (uint32_t)strtoul(arg, NULL, 10);
        if (line == 0)
        {
            (void)printf("Invalid line number: %s\n", arg);
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        woort_DebuggerFrame frame;
        if (!woort_Debugger_get_stack_frame(
            frontend->m_current_vm, frontend->m_current_frame_depth,
            &frame)
            || frame.m_file_or_lib_name[0] == '\0')
        {
            (void)printf(
                "Current frame has no source location. "
                "Use 'break <file>:<line>' to specify file.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        woort_WAIPO_FrontendBreakpoint record;
        memset(&record, 0, sizeof(record));
        record.m_is_function_bp = false;
        record.m_line = line;
        (void)snprintf(record.m_file, sizeof(record.m_file), "%s",
            frame.m_file_or_lib_name);

        if (!woort_Debugger_set_source_breakpoint(
            record.m_file, line - 1, &record.m_id)
            || !_woort_WAIPO_add_frontend_breakpoint(frontend, &record))
        {
            (void)printf("Failed to set breakpoint.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        bool resolved = false;
        (void)woort_Debugger_query_breakpoint(record.m_id, &resolved, NULL);

        (void)printf("Breakpoint %zu at %s:%u%s\n",
            frontend->m_breakpoints.m_size,
            record.m_file, line,
            resolved ? "" : "  [pending]");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    /* break <file>:<line> */
    const char* colon = strchr(arg, ':');
    if (colon != NULL)
    {
        const char* line_str = colon + 1;

        if (_woort_WAIPO_is_numeric(line_str))
        {
            uint32_t line = (uint32_t)strtoul(line_str, NULL, 10);
            if (line == 0)
                line = 1;

            char filepath[WOORT_DEBUGGER_MAX_PATH];
            const size_t file_len = (size_t)(colon - arg);
            const size_t copy_len = file_len < sizeof(filepath) - 1
                ? file_len : sizeof(filepath) - 1;
            (void)memcpy(filepath, arg, copy_len);
            filepath[copy_len] = '\0';

            woort_WAIPO_FrontendBreakpoint record;
            memset(&record, 0, sizeof(record));
            record.m_is_function_bp = false;
            record.m_line = line;
            (void)snprintf(record.m_file, sizeof(record.m_file), "%s",
                filepath);

            if (!woort_Debugger_set_source_breakpoint(
                filepath, line - 1, &record.m_id)
                || !_woort_WAIPO_add_frontend_breakpoint(frontend, &record))
            {
                (void)printf("Failed to set breakpoint.\n");
                return WOORT_WAIPO_CMD_NEED_NEXT;
            }

            bool resolved = false;
            (void)woort_Debugger_query_breakpoint(record.m_id, &resolved, NULL);

            (void)printf("Breakpoint %zu at %s:%u%s\n",
                frontend->m_breakpoints.m_size,
                filepath, line,
                resolved ? "" : "  [pending]");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }
    }

    /* break <file> <line> */
    if (arg_count >= 3 && _woort_WAIPO_is_numeric(args[2]))
    {
        uint32_t line = (uint32_t)strtoul(args[2], NULL, 10);
        if (line == 0)
            line = 1;

        woort_WAIPO_FrontendBreakpoint record;
        memset(&record, 0, sizeof(record));
        record.m_is_function_bp = false;
        record.m_line = line;
        (void)snprintf(record.m_file, sizeof(record.m_file), "%s", arg);

        if (!woort_Debugger_set_source_breakpoint(
            arg, line - 1, &record.m_id)
            || !_woort_WAIPO_add_frontend_breakpoint(frontend, &record))
        {
            (void)printf("Failed to set breakpoint.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        bool resolved = false;
        (void)woort_Debugger_query_breakpoint(record.m_id, &resolved, NULL);

        (void)printf("Breakpoint %zu at %s:%u%s\n",
            frontend->m_breakpoints.m_size,
            arg, line,
            resolved ? "" : "  [pending]");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    /* break <funcname>: exact match against function boundaries. */
    {
        woort_WAIPO_FrontendBreakpoint record;
        memset(&record, 0, sizeof(record));
        record.m_is_function_bp = true;
        (void)snprintf(record.m_function_name,
            sizeof(record.m_function_name), "%s", arg);

        if (!woort_Debugger_set_function_breakpoint(
            arg, &record.m_id)
            || !_woort_WAIPO_add_frontend_breakpoint(frontend, &record))
        {
            (void)printf("Failed to set breakpoint.\n");
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        bool resolved = false;
        (void)woort_Debugger_query_breakpoint(record.m_id, &resolved, NULL);

        if (!resolved)
        {
            (void)printf("Function '%s' not found (breakpoint pending).\n",
                arg);
            return WOORT_WAIPO_CMD_NEED_NEXT;
        }

        (void)printf("Breakpoint %zu at function %s\n",
            frontend->m_breakpoints.m_size, arg);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_delete(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        (void)printf("Usage: delete <breakpoint_num>\n");
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const long num = strtol(args[1], NULL, 10);
    if (num <= 0 || (size_t)num > frontend->m_breakpoints.m_size)
    {
        (void)printf("Invalid breakpoint number: %s\n", args[1]);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const size_t idx = (size_t)(num - 1);

    const woort_WAIPO_FrontendBreakpoint* fb =
        (const woort_WAIPO_FrontendBreakpoint*)woort_vector_at(
        &frontend->m_breakpoints, idx);

    (void)woort_Debugger_remove_breakpoint(fb->m_id);
    (void)woort_vector_erase_at(&frontend->m_breakpoints, idx);

    (void)printf("Breakpoint %zu deleted.\n", (size_t)num);

    return WOORT_WAIPO_CMD_NEED_NEXT;
}

/* ====================================================================
 * vm
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_vm(
    woort_WAIPO_Frontend* frontend,
    char** args,
    size_t arg_count)
{
    /* 'vm' with no argument lists all VMs; ids are stable serials. */
    if (arg_count < 2)
        return _woort_WAIPO_list_vm(frontend);

    if (!_woort_WAIPO_is_numeric(args[1]))
    {
        (void)printf("Invalid VM id: %s\n", args[1]);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    const woort_DebuggerVmId id =
        (woort_DebuggerVmId)strtoull(args[1], NULL, 10);

    if (id == frontend->m_current_vm)
    {
        (void)printf("Already debugging VM %" PRIu64 ".\n", id);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    if (!woort_Debugger_interrupt_vm(id))
    {
        (void)printf("No such VM: %" PRIu64 "\n", id);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    (void)printf("Switching to VM %" PRIu64 "...\n", id);

    /*
     * Release the current VM and let it resume; the target VM holds a
     * pending DEBUG_BREAK request and traps into the debugger on its own.
     */
    if (!woort_Debugger_continue())
    {
        (void)printf(WOORT_ANSI_HIR "Failed to resume current VM.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_NEED_NEXT;
    }

    return WOORT_WAIPO_CMD_CONTINUE;
}

/* ====================================================================
 * Command table / prompt loop
 * ==================================================================== */

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
    { "global",    "g",    &_woort_WAIPO_cmd_global },
    { "break",     "b",    &_woort_WAIPO_cmd_break },
    { "delete",    "d",    &_woort_WAIPO_cmd_delete },
    { "vm",        NULL,   &_woort_WAIPO_cmd_vm },
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

static void _woort_WAIPO_print_stop_header(
    woort_WAIPO_Frontend* frontend,
    const woort_DebuggerBreakEvent* event)
{
    woort_DebuggerFrame frame;
    const bool has_frame = woort_Debugger_get_stack_frame(
        event->m_vm, 0, &frame);

    (void)printf("\nBreakdown VM#%" PRIu64 " by ", (uint64_t)event->m_vm);
    switch (event->m_reason)
    {
    case WOORT_DEBUGGER_STOP_REASON_BREAKPOINT:
        (void)printf("breakpoint");
        break;
    case WOORT_DEBUGGER_STOP_REASON_STEP:
        (void)printf("step");
        break;
    case WOORT_DEBUGGER_STOP_REASON_INTERRUPT:
        (void)printf("interrupt");
        break;
    case WOORT_DEBUGGER_STOP_REASON_PANIC:
    {
        (void)printf("panic");
        woort_DebuggerPanicInfo panic;
        if (woort_Debugger_get_last_panic(&panic))
        {
            (void)printf(": %s", panic.m_message);
        }
        break;
    }
    default:
        (void)printf("unknown cause");
        break;
    }
    (void)printf(", at:\n");

    if (has_frame)
    {
        if (frame.m_function_name[0] != '\0'
            && frame.m_file_or_lib_name[0] != '\0')
        {
            if (frame.m_has_location)
                (void)printf("    %s (%s:%u:%u)",
                    frame.m_function_name,
                    frame.m_file_or_lib_name,
                    frame.m_line + 1,
                    frame.m_column);
            else
                (void)printf("    %s (%s)",
                    frame.m_function_name,
                    frame.m_file_or_lib_name);
        }
        else if (frame.m_function_name[0] != '\0')
        {
            (void)printf("    %s", frame.m_function_name);
        }
        else
        {
            (void)printf("    <unknown>");
        }
    }
    else
    {
        (void)printf("    <unknown>");
    }

    /* In-tree escape: raw bytecode offset of the stop. */
    woort_VMRuntime* vm = NULL;
    if (_woort_Debugger_session_take_stopped_vm(&vm) && vm != NULL)
    {
        woort_CodeEnv* cenv = NULL;
        if (woort_CodeEnv_find(vm->m_ip, &cenv))
        {
            const uint32_t code_offset =
                (uint32_t)(vm->m_ip - cenv->m_code_begin);
            (void)printf("\nBytecode offset = %04u", code_offset);
        }
    }

    (void)printf("\n");

    (void)_woort_WAIPO_cmd_source(frontend, NULL, 0);

    if (frontend->m_first_breakdown)
    {
        frontend->m_first_breakdown = false;
        (void)printf("Note: You can input '?' for more informations.\n");
    }
}

/*
 * One prompt session for the current stop.  Returns when a resume-class
 * command was issued (the outer loop then waits for the next stop) or on
 * stdin EOF (in which case the VM is resumed like the pre-session WAIPO
 * REPL did).
 */
static void _woort_WAIPO_prompt_loop(woort_WAIPO_Frontend* frontend)
{
    for (;;)
    {
        (void)printf("> ");
        (void)fflush(stdout);

        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), stdin) == NULL)
        {
            /* EOF: resume like the old REPL's break-out path. */
            (void)woort_Debugger_continue();
            break;
        }

        char* tokens[128];
        size_t token_count = _woort_WAIPO_split_line(
            line_buf, tokens, 128);

        if (token_count == 0)
        {
            if (frontend->m_last_command[0] == '\0')
                continue;

            (void)strncpy(line_buf, frontend->m_last_command,
                sizeof(line_buf) - 1);
            line_buf[sizeof(line_buf) - 1] = '\0';

            token_count = _woort_WAIPO_split_line(
                line_buf, tokens, 128);

            if (token_count == 0)
                continue;
        }

        /* Remember the whole command line, not just the command name. */
        size_t last_pos = 0;
        for (size_t i = 0; i < token_count; ++i)
        {
            if (i > 0
                && last_pos + 1 < sizeof(frontend->m_last_command))
            {
                frontend->m_last_command[last_pos++] = ' ';
            }

            for (const char* q = tokens[i]; *q != '\0'; ++q)
            {
                if (last_pos + 1 >= sizeof(frontend->m_last_command))
                    break;
                frontend->m_last_command[last_pos++] = *q;
            }
        }
        frontend->m_last_command[last_pos] = '\0';

        woort_WAIPO_CommandHandler handler =
            _woort_WAIPO_find_command(tokens[0]);

        if (handler == NULL)
        {
            (void)printf(
                "Unknown debug command, please input 'help' for more informations.\n");
            continue;
        }

        const woort_WAIPO_CommandResult result =
            handler(frontend, tokens, token_count);

        if (result == WOORT_WAIPO_CMD_CONTINUE)
            break;
    }
}

static void _woort_WAIPO_frontend_thread_job(void* user_data)
{
    woort_WAIPO_Frontend* const frontend =
        (woort_WAIPO_Frontend*)user_data;

    for (;;)
    {
        woort_DebuggerBreakEvent event;
        if (!woort_Debugger_wait_for_break(
            WOORT_DEBUGGER_WAIT_INFINITE, &event))
        {
            /* Session detached; this thread owns the frontend, release it. */
            break;
        }

        frontend->m_current_vm = event.m_vm;
        frontend->m_current_frame_depth = 0;

        _woort_WAIPO_print_stop_header(frontend, &event);
        _woort_WAIPO_prompt_loop(frontend);
    }

    woort_vector_deinit(&frontend->m_breakpoints);
    free(frontend);
}

WOORT_NODISCARD woort_DebuggerAttachResult woort_WAIPO_Debugger_attach(void)
{
    const woort_DebuggerAttachResult session_result =
        woort_Debugger_attach();

    if (session_result != WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS)
        return session_result;

    woort_WAIPO_Frontend* const frontend =
        (woort_WAIPO_Frontend*)calloc(1, sizeof(woort_WAIPO_Frontend));

    if (frontend == NULL)
    {
        (void)woort_Debugger_detach();
        return WOORT_DEBUGGER_ATTACH_RESULT_FAILED;
    }

    frontend->m_current_vm = 0;
    frontend->m_current_frame_depth = 0;
    frontend->m_first_breakdown = true;
    frontend->m_last_command[0] = '\0';
    woort_vector_init(&frontend->m_breakpoints,
        sizeof(woort_WAIPO_FrontendBreakpoint));

    woort_Thread* /* never joined, freed by the OS at exit */ thread = NULL;
    if (!woort_thread_start(
        &_woort_WAIPO_frontend_thread_job, frontend, &thread))
    {
        woort_vector_deinit(&frontend->m_breakpoints);
        free(frontend);
        (void)woort_Debugger_detach();
        return WOORT_DEBUGGER_ATTACH_RESULT_FAILED;
    }

    return WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS;
}
