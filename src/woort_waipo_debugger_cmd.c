#include "woort_waipo_debugger.h"
#include "woort_gc.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_gc_struct.h"
#include "woort_gc_gchandle.h"
#include "woort_vector.h"
#include "woort_value.h"
#include "woort_serialize.h"
#include "woort_util.h"
#include "woort_platform.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

typedef enum woort_WAIPO_CommandResult
{
    WOORT_WAIPO_CMD_REINPUT,
    WOORT_WAIPO_CMD_CONTINUE,
    WOORT_WAIPO_CMD_STEPIR,
    WOORT_WAIPO_CMD_STEPIN,
    WOORT_WAIPO_CMD_STEPOVER,
    WOORT_WAIPO_CMD_STEPOUT,

} woort_WAIPO_CommandResult;

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
        "global      g       <index>         Print const/static slot value by global index.\n"
        "vm                  [id]            List all VM(s), or switch to VM by id.\n"
        "\n");

    return WOORT_WAIPO_CMD_REINPUT;
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
    return WOORT_WAIPO_CMD_CONTINUE;
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

    woort_VMRuntime_Debugger_terminate_all_vm();
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

    return WOORT_WAIPO_CMD_REINPUT;
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

    return WOORT_WAIPO_CMD_REINPUT;
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

    return WOORT_WAIPO_CMD_REINPUT;
}

typedef struct _woort_WAIPO_ListVMContext
{
    size_t m_index;
    /* OPTIONAL */ const woort_VMRuntime* m_current;
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
        "%c[%zu] VMRuntime(%p)  ip=%p  sp=%p  stack=[%p-%p]  usage=%.1f%%\n",
        vm == ctx->m_current ? '*' : ' ',
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

    _woort_WAIPO_ListVMContext ctx;
    ctx.m_index = 0;
    ctx.m_current = vm;

    woort_GC_foreach_root_vm(&_woort_WAIPO_list_vm_callback, &ctx);

    if (ctx.m_index == 0)
        (void)printf("No VM.\n");

    return WOORT_WAIPO_CMD_REINPUT;
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
        return WOORT_WAIPO_CMD_REINPUT;
    }

    if (strcmp(args[1], "codeenv") == 0)
        return _woort_WAIPO_list_codeenv(dbg, vm);

    if (strcmp(args[1], "vm") == 0)
        return _woort_WAIPO_list_vm(dbg, vm);

    (void)printf("Unknown list target: '%s'. Available: codeenv, vm\n", args[1]);

    return WOORT_WAIPO_CMD_REINPUT;
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

/*
 * 显示一行源码前检查该行关联的字节码是否已陷入断点（真实 TRAP 状态）：
 * 使用与 break 命令设断相同的正向映射（行 -> 该行全部关联指令地址，
 * 含无覆盖行回退最近有条目行），任一地址被调试断点（m_debug_breakpoints
 * 表）持有即视为该行有断点；刻意只查调试断点，用户断点与步进断点
 * （m_breakpoints 表）不计入。
 */
typedef struct _woort_WAIPO_LineTrapCheckContext
{
    woort_WAIPO_BreakpointCollection* m_collection;
    const char* m_filepath;
    uint32_t m_line;
    woort_CodeEnv* m_cenv;
    bool m_trapped;
} _woort_WAIPO_LineTrapCheckContext;

static bool _woort_WAIPO_line_trap_check_offset_callback(
    uint32_t bytecode_offset, void* user_data)
{
    _woort_WAIPO_LineTrapCheckContext* ctx =
        (_woort_WAIPO_LineTrapCheckContext*)user_data;

    const woort_Bytecode* ip = ctx->m_cenv->m_code_begin + bytecode_offset;

    if (_woort_WAIPO_BreakpointCollection_contains_debug_break_at(
            ctx->m_collection, ip))
    {
        ctx->m_trapped = true;
        return false; /* 已命中，终止迭代 */
    }

    return true;
}

static bool _woort_WAIPO_line_trap_check_cenv_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_LineTrapCheckContext* ctx =
        (_woort_WAIPO_LineTrapCheckContext*)user_data;

    ctx->m_cenv = cenv;

    (void)woort_CodeEnv_foreach_offset_by_srcloc(
        cenv, ctx->m_filepath, ctx->m_line,
        &_woort_WAIPO_line_trap_check_offset_callback, ctx);

    return !ctx->m_trapped; /* 已命中即不再遍历其余 CodeEnv */
}

static bool _woort_WAIPO_source_line_is_trapped(
    woort_WAIPO_Debugger* dbg,
    const char* filepath,
    size_t line)
{
    _woort_WAIPO_LineTrapCheckContext ctx;
    ctx.m_collection = &dbg->m_breakpoint_collection;
    ctx.m_filepath = filepath;
    ctx.m_line = (uint32_t)line;
    ctx.m_cenv = NULL;
    ctx.m_trapped = false;

    woort_CodeEnv_foreach(&_woort_WAIPO_line_trap_check_cenv_callback, &ctx);

    return ctx.m_trapped;
}

static void _woort_WAIPO_emit_source_line(
    const char* line_buf,
    size_t current_line,
    bool has_highlight,
    size_t highlight_begin_line,
    size_t highlight_end_line,
    size_t highlight_begin_col,
    size_t highlight_end_col,
    bool has_bp)
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
    woort_VFile* f = NULL;
    if (!woort_vfile_open(filepath, &f) || f == NULL)
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
                const bool has_bp = _woort_WAIPO_source_line_is_trapped(
                    dbg, filepath, current_line);

                _woort_WAIPO_emit_source_line(
                    line_buf, current_line,
                    has_highlight,
                    highlight_begin_line, highlight_end_line,
                    highlight_begin_col, highlight_end_col,
                    has_bp);
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
                const bool has_bp = _woort_WAIPO_source_line_is_trapped(
                    dbg, filepath, current_line);

                _woort_WAIPO_emit_source_line(
                    line_buf, current_line,
                    has_highlight,
                    highlight_begin_line, highlight_end_line,
                    highlight_begin_col, highlight_end_col,
                    has_bp);
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
            const bool has_bp = _woort_WAIPO_source_line_is_trapped(
                dbg, filepath, current_line);

            _woort_WAIPO_emit_source_line(
                line_buf, current_line,
                has_highlight,
                highlight_begin_line, highlight_end_line,
                highlight_begin_col, highlight_end_col,
                has_bp);
        }
    }

    woort_vfile_close(f);
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
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const long frame_id = strtol(args[1], NULL, 10);
    if (frame_id < 0)
    {
        (void)printf("Invalid frame id.\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

    woort_VMRuntime_TraceCallstack trace;
    if (!woort_WAIPO_Debugger_do_switch_trace_frame(
        dbg, (woort_WAIPO_Debugger_FrameId)frame_id, &trace))
    {
        (void)printf("No such frame.\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

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

    return WOORT_WAIPO_CMD_REINPUT;
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
        return WOORT_WAIPO_CMD_REINPUT;
    }

    if (!trace.m_has_location || trace.m_file_or_lib_name == NULL)
    {
        (void)printf("No source location available for current frame.\n");
        return WOORT_WAIPO_CMD_REINPUT;
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

    return WOORT_WAIPO_CMD_REINPUT;
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
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const char* const sign = dbg->m_current_frame_depth == 0 ? "=>" : "\\>";

    const woort_Bytecode* frame_ip = trace.m_code_addr;
    if (frame_ip == NULL)
        frame_ip = vm->m_ip;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(frame_ip, &cenv))
    {
        (void)printf(WOORT_ANSI_HIR "Cannot locate CodeEnv for current IP.\n" WOORT_ANSI_RST);
        return WOORT_WAIPO_CMD_REINPUT;
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

                    return WOORT_WAIPO_CMD_REINPUT;
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

        return WOORT_WAIPO_CMD_REINPUT;
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

        return WOORT_WAIPO_CMD_REINPUT;
    }

    if (strcmp(first_arg, "--all") == 0)
    {
        _woort_WAIPO_dump_disassembly_range(cenv, 0, SIZE_MAX, frame_ip, sign);
        return WOORT_WAIPO_CMD_REINPUT;
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

    return WOORT_WAIPO_CMD_REINPUT;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_stepir(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)printf("Stepping to next instruction...\n");
    return WOORT_WAIPO_CMD_STEPIR;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_step(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)printf("Stepping to next source line...\n");
    return WOORT_WAIPO_CMD_STEPIN;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_next(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)printf("Stepping over to next source line...\n");
    return WOORT_WAIPO_CMD_STEPOVER;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_return(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)printf("Returning to caller...\n");
    return WOORT_WAIPO_CMD_STEPOUT;
}

/* ====================================================================
 * print / p command
 * ==================================================================== */

void _woort_WAIPO_print_value(woort_DynBox boxed, bool is_fuzzy)
{
    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    if (_woort_serialize_dynbox_to_buf_for_debug(
        boxed, &buf, &visited_set, 0, is_fuzzy))
    {
        (void)printf("%.*s", (int)buf.m_size, (const char*)buf.m_data);
    }

    woort_vector_deinit(&buf);
    woort_hashmap_deinit(&visited_set);
}

typedef struct _woort_WAIPO_PrintVarContext
{
    size_t m_found_count;

} _woort_WAIPO_PrintVarContext;

static bool _woort_WAIPO_print_var_callback(
    const woort_WAIPO_Debugger_VariableInfo* info,
    void* userdata)
{
    _woort_WAIPO_PrintVarContext* const ctx = userdata;

    if (info->m_is_local)
    {
        (void)printf(
            "[local]  %s@[SB%+d] = ",
            info->m_name,
            info->m_location.m_stack_frame_bp_offset);
    }
    else
    {
        (void)printf(
            "[static] %s@G[%u] = ",
            info->m_name,
            info->m_location.m_static_constant_index);
    }

    _woort_WAIPO_print_value(info->m_value->m_dynamic, true);
    printf("\n");

    ++ctx->m_found_count;
    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_print(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    (void)vm;

    if (arg_count < 2)
    {
        (void)printf("Usage: print <varname>\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const char* const var_name = args[1];

    /*
     * 按名检索当前帧可见的局部变量与当前 CodeEnv 的静态变量。
     */
    _woort_WAIPO_PrintVarContext ctx;
    ctx.m_found_count = 0;

    (void)woort_WAIPO_Debugger_do_get_variable_by_name(
        dbg, var_name, &_woort_WAIPO_print_var_callback, &ctx);

    if (ctx.m_found_count == 0)
        (void)printf("No variable named '%s' in current frame.\n", var_name);

    return WOORT_WAIPO_CMD_REINPUT;
}

/* ====================================================================
 * global / g command
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_global(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2 || !_woort_WAIPO_is_numeric(args[1]))
    {
        (void)printf("Usage: global <index>\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const long index = strtol(args[1], NULL, 10);
    if (index < 0)
    {
        (void)printf("Invalid index: %s\n", args[1]);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    /*
     * 定位当前选中的调用栈帧。
     */
    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_WAIPO_trace_to_depth(vm, dbg->m_current_frame_depth, &trace))
    {
        (void)printf("No callstack at current frame.\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

    if (trace.m_code_addr == NULL)
    {
        (void)printf("No code address for current frame.\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

    woort_CodeEnv* cenv = NULL;
    if (!woort_CodeEnv_find(trace.m_code_addr, &cenv) || cenv == NULL)
    {
        (void)printf("Cannot locate CodeEnv for current frame.\n");
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const size_t global_index = (size_t)index;
    if (global_index >= cenv->m_data_count)
    {
        (void)printf("Index %zu out of range [0, %zu).\n",
            global_index, cenv->m_data_count);
        return WOORT_WAIPO_CMD_REINPUT;
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
            i < cenv->m_pdb.m_static_var_debug_info.m_size; ++i)
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

    _woort_WAIPO_print_value(cenv->m_data_begin[global_index].m_dynamic, true);
    printf("\n");

    return WOORT_WAIPO_CMD_REINPUT;
}

/* ====================================================================
 * break / b command
 * ==================================================================== */

/*
 * 落一个源码行断点并打印结果，break 的三种源码行形式
 * （行号 / 文件:行号 / 文件 行号）共用此收尾。
 */
static woort_WAIPO_CommandResult _woort_WAIPO_break_at_source(
    woort_WAIPO_Debugger* dbg,
    const char* file,
    uint32_t line)
{
    if (line == 0)
        line = 1;

    /* 命令行号 1 起始，接口行号 0 起始 */
    woort_WAIPO_Debugger_BreakpointId bp_id = 0;
    if (!woort_WAIPO_Debugger_set_source_breakpoint(dbg, file, line - 1, &bp_id))
    {
        (void)printf("No code at %s:%u\n", file, line);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    (void)printf("Breakpoint %" PRIu64 " at %s:%u\n", bp_id, file, line);
    return WOORT_WAIPO_CMD_REINPUT;
}

typedef struct _woort_WAIPO_ListBreakpointContext
{
    size_t m_count;

} _woort_WAIPO_ListBreakpointContext;

static bool _woort_WAIPO_list_breakpoint_callback(
    const woort_WAIPO_Debugger_BreakpointInfo* info,
    void* userdata)
{
    _woort_WAIPO_ListBreakpointContext* const ctx = userdata;

    /* 表头随首条输出打印，空表由命令体提示 */
    if (ctx->m_count == 0)
        (void)printf("Num  What\n");
    ++ctx->m_count;

    if (info->m_filename == NULL)
        (void)printf("%-4" PRIu64 " <unknown>\n", info->m_id);
    else if (info->m_line != SIZE_MAX)
        (void)printf("%-4" PRIu64 " %s:%zu\n",
            info->m_id, info->m_filename, info->m_line + 1);
    else
        (void)printf("%-4" PRIu64 " %s\n", info->m_id, info->m_filename);

    return true;
}

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_break(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    if (arg_count < 2)
    {
        /*
         * 枚举当前全部用户断点。
         */
        _woort_WAIPO_ListBreakpointContext ctx;
        ctx.m_count = 0;

        (void)woort_WAIPO_Debugger_query_breakpoints(
            dbg, &_woort_WAIPO_list_breakpoint_callback, &ctx);

        if (ctx.m_count == 0)
            (void)printf("No breakpoints.\n");

        return WOORT_WAIPO_CMD_REINPUT;
    }

    const char* arg = args[1];

    if (_woort_WAIPO_is_numeric(arg))
    {
        const uint32_t line = (uint32_t)strtoul(arg, NULL, 10);
        if (line == 0)
        {
            (void)printf("Invalid line number: %s\n", arg);
            return WOORT_WAIPO_CMD_REINPUT;
        }

        woort_VMRuntime_TraceCallstack trace;
        if (!_woort_WAIPO_trace_to_depth(vm, dbg->m_current_frame_depth, &trace))
        {
            (void)printf("No callstack. Use 'break <file>:<line>' to specify file.\n");
            return WOORT_WAIPO_CMD_REINPUT;
        }

        if (trace.m_code_addr == NULL || trace.m_file_or_lib_name == NULL)
        {
            (void)printf("Current frame has no source location.\n");
            return WOORT_WAIPO_CMD_REINPUT;
        }

        return _woort_WAIPO_break_at_source(dbg, trace.m_file_or_lib_name, line);
    }

    /* 'break <file>:<line>' 形式 */
    const char* colon = strchr(arg, ':');
    if (colon != NULL && _woort_WAIPO_is_numeric(colon + 1))
    {
        const size_t file_len = (size_t)(colon - arg);
        char filepath[512];
        const size_t copy_len = file_len < sizeof(filepath) - 1
            ? file_len : sizeof(filepath) - 1;
        (void)memcpy(filepath, arg, copy_len);
        filepath[copy_len] = '\0';

        return _woort_WAIPO_break_at_source(
            dbg, filepath, (uint32_t)strtoul(colon + 1, NULL, 10));
    }

    /* Check for 'break <file> <line>' two-arg format */
    if (arg_count >= 3 && _woort_WAIPO_is_numeric(args[2]))
    {
        return _woort_WAIPO_break_at_source(
            dbg, arg, (uint32_t)strtoul(args[2], NULL, 10));
    }

    /* Treat as function name */
    {
        woort_WAIPO_Debugger_BreakpointId bp_id = 0;
        if (!woort_WAIPO_Debugger_set_function_breakpoint(dbg, arg, &bp_id))
        {
            (void)printf("Function '%s' not found.\n", arg);
            return WOORT_WAIPO_CMD_REINPUT;
        }

        (void)printf("Breakpoint %" PRIu64 " at %s\n", bp_id, arg);
        return WOORT_WAIPO_CMD_REINPUT;
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
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const long num = strtol(args[1], NULL, 10);
    if (num <= 0)
    {
        (void)printf("Invalid breakpoint number: %s\n", args[1]);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const woort_WAIPO_Debugger_BreakpointId target_id =
        (woort_WAIPO_Debugger_BreakpointId)num;

    if (!woort_WAIPO_Debugger_delete_breakpoint(dbg, target_id))
    {
        (void)printf("No breakpoint number %" PRIu64 ".\n", target_id);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    (void)printf("Breakpoint %" PRIu64 " deleted.\n", target_id);

    return WOORT_WAIPO_CMD_REINPUT;
}

typedef struct _woort_WAIPO_SwitchVMContext
{
    long m_id;
    /* OPTIONAL */ woort_VMRuntime* m_target;

} _woort_WAIPO_SwitchVMContext;

static bool _woort_WAIPO_count_vm_to_break(
    woort_VMRuntime* vm, void* user_data)
{
    _woort_WAIPO_SwitchVMContext* const ctx =
        (_woort_WAIPO_SwitchVMContext*)user_data;

    if (ctx->m_id < 0)
        return false;

    if (ctx->m_id-- == 0)
    {
        ctx->m_target = vm;

        /* Let the target vm breakdown at its next request checkpoint. */
        (void)woort_VMRuntime_request_set(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);

        return false;
    }
    return true;
}

/* ====================================================================
 * vm command
 * ==================================================================== */

static woort_WAIPO_CommandResult _woort_WAIPO_cmd_vm(
    woort_WAIPO_Debugger* dbg,
    woort_VMRuntime* vm,
    char** args,
    size_t arg_count)
{
    /* 'vm' with no argument lists all VMs (ids come from this listing). */
    if (arg_count < 2)
        return _woort_WAIPO_list_vm(dbg, vm);

    if (!_woort_WAIPO_is_numeric(args[1]))
    {
        (void)printf("Invalid VM id: %s\n", args[1]);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    const long id = strtol(args[1], NULL, 10);

    _woort_WAIPO_SwitchVMContext ctx = {
        .m_id = id,
        .m_target = NULL,
    };

    woort_GC_foreach_root_vm(&_woort_WAIPO_count_vm_to_break, &ctx);

    if (ctx.m_target == NULL)
    {
        (void)printf("No such VM: %ld\n", id);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    if (ctx.m_target == vm)
    {
        (void)printf("Already debugging VM %ld.\n", id);
        return WOORT_WAIPO_CMD_REINPUT;
    }

    (void)printf("Switching to VM %ld...\n", id);

    /*
     * Release the current VM and let it resume; the target VM holds a
     * pending DEBUG_BREAK request and traps into the debugger on its own.
     */
    return WOORT_WAIPO_CMD_CONTINUE;
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

WOORT_NODISCARD woort_WAIPO_TrapEndBehavior woort_WAIPO_Debugger_process_cmdline(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    {
        woort_VMRuntime_TraceCallstack_Iter trace_iter;
        woort_VMRuntime_TraceCallstack trace;

        woort_VMRuntime_trace_begin(vm, &trace_iter);
        if (woort_VMRuntime_trace_next(&trace_iter, &trace))
        {
            /* Reset frame depth. */
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

    /* Keep m_last_command across breakdowns: an empty input line repeats
     * the last command even after step/next re-entered the debugger. */
    debugger_instance->m_current_frame_depth = 0;

    for (;;)
    {
        (void)printf("> ");
        (void)fflush(stdout);

        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), stdin) == NULL)
        {
            /* Failed to input. treat as continue. */
            return WOORT_WAIPO_TRAP_CONTINUE;
        }

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

        /* Remember the whole command line, not just the command name. */
        size_t last_pos = 0;
        for (size_t i = 0; i < token_count; ++i)
        {
            if (i > 0
                && last_pos + 1 < sizeof(debugger_instance->m_last_command))
            {
                debugger_instance->m_last_command[last_pos++] = ' ';
            }

            for (const char* q = tokens[i]; *q != '\0'; ++q)
            {
                if (last_pos + 1 >= sizeof(debugger_instance->m_last_command))
                    break;
                debugger_instance->m_last_command[last_pos++] = *q;
            }
        }
        debugger_instance->m_last_command[last_pos] = '\0';

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

        switch (result)
        {
        case WOORT_WAIPO_CMD_REINPUT:
            continue;
        case WOORT_WAIPO_CMD_CONTINUE:
            return WOORT_WAIPO_TRAP_CONTINUE;
        case WOORT_WAIPO_CMD_STEPIR:
            return WOORT_WAIPO_TRAP_STEPIR;
        case WOORT_WAIPO_CMD_STEPIN:
            return WOORT_WAIPO_TRAP_STEPIN;
        case WOORT_WAIPO_CMD_STEPOVER:
            return WOORT_WAIPO_TRAP_STEPOVER;
        case WOORT_WAIPO_CMD_STEPOUT:
            return WOORT_WAIPO_TRAP_STEPOUT;
        }

        /* Never been here. */
        abort();
    }

    /* Never been here. */
    abort();
}
