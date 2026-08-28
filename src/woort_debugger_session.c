#include "woort.h"
#include "woort_debugger_session.h"
#include "woort_vm_debugger_api.h"
#include "woort_atomic.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_gc.h"
#include "woort_gc_closure.h"
#include "woort_gc_string.h"
#include "woort_gc_units.h"
#include "woort_hashmap.h"
#include "woort_mem.h"
#include "woort_opcode.h"
#include "woort_serialize.h"
#include "woort_util.h"
#include "woort_value.h"
#include "woort_vector.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * Debugger session implementation notes
 * =====================================
 *
 * Threading model
 * ---------------
 * The session callback runs on the trapped VM's thread, inside
 * woort_VMRuntime_Debugger_try_trap, which already serialized the VM
 * against every other debugger callback (g_debugger_execute_mx) and
 * swapped the thread-local VM out.  While a stop is active that VM is
 * completely frozen, so every query below may safely read its registers,
 * value stack and CodeEnv debug info from any host thread.  Call-stack
 * walking additionally goes through the STACK_OCCUPYING request, which
 * excludes concurrent stack readers (including the GC marking by proxy).
 *
 * Locking order is always: session m_mx -> CodeEnv registry lock
 * (woort_CodeEnv_foreach / woort_CodeEnv_find) -> CodeEnv mutex.  The
 * attach/detach lock (g_debugger_session_attach_mx) is never held
 * together with m_mx in a nesting order other than attach_mx first.
 *
 * Memory model
 * ------------
 * Session objects are allocated per attach and intentionally never freed
 * before process shutdown: after detach the (small) bookkeeping stays
 * valid so that threads still inside woort_Debugger_wait_for_break or
 * lingering callbacks cannot touch freed memory.  This trades a bounded,
 * detached-session-sized leak per attach/detach cycle for freedom from
 * use-after-free races that an explicit refcount protocol would have to
 * cover.
 */

static woort_AtomicPtr g_debugger_session_ptr;
static woort_Mutex* g_debugger_session_attach_mx;

/* ========================================================================
 * Small helpers
 * ======================================================================== */

static /* OPTIONAL */ woort_DebuggerSession* _woort_Debugger_session_load(void)
{
    return (woort_DebuggerSession*)woort_atomic_load_explicit(
        &g_debugger_session_ptr, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static void _woort_Debugger_session_store(/* OPTIONAL */ woort_DebuggerSession* session)
{
    woort_atomic_store_explicit(
        &g_debugger_session_ptr, session, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}

static void _woort_Debugger_copy_str(
    char* dst, size_t dst_capacity, /* OPTIONAL */ const char* src)
{
    if (src == NULL || dst_capacity == 0)
    {
        if (dst_capacity != 0)
            dst[0] = '\0';
        return;
    }

    const size_t len = strlen(src);
    if (len >= dst_capacity)
    {
        memcpy(dst, src, dst_capacity - 1);
        dst[dst_capacity - 1] = '\0';
    }
    else
    {
        memcpy(dst, src, len + 1);
    }
}

typedef struct _woort_Debugger_FindVmContext
{
    woort_DebuggerVmId m_serial;
    /* OPTIONAL */ woort_VMRuntime* m_vm;

} _woort_Debugger_FindVmContext;

static bool _woort_Debugger_find_vm_callback(
    woort_VMRuntime* vm, void* user_data)
{
    _woort_Debugger_FindVmContext* const ctx =
        (_woort_Debugger_FindVmContext*)user_data;

    if (vm->m_serial == ctx->m_serial)
    {
        ctx->m_vm = vm;
        return false;
    }
    return true;
}

static /* OPTIONAL */ woort_VMRuntime* _woort_Debugger_find_vm(
    woort_DebuggerVmId serial)
{
    _woort_Debugger_FindVmContext ctx;
    ctx.m_serial = serial;
    ctx.m_vm = NULL;

    woort_GC_foreach_root_vm(&_woort_Debugger_find_vm_callback, &ctx);

    return ctx.m_vm;
}

/*
 * The VM of the current stop, or NULL when `serial` does not designate it.
 * Caller holds m_mx.
 */
static /* OPTIONAL */ woort_VMRuntime* _woort_Debugger_locked_stopped_vm(
    woort_DebuggerSession* session,
    woort_DebuggerVmId serial)
{
    if (!session->m_stop_active || session->m_stop_vm == NULL)
        return NULL;

    if (session->m_stop_vm->m_serial != serial)
        return NULL;

    return session->m_stop_vm;
}

static void _woort_Debugger_clear_pending_interrupts(woort_VMRuntime* vm)
{
    (void)woort_VMRuntime_request_accept(
        vm,
        (woort_VMRuntime_CheckRequestMask)(
            WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK
            | WOORT_VMRUNTIME_CHECK_REQUEST_EXTERNAL_DEBUG_BREAK));
}

static /* OPTIONAL */ char* _woort_Debugger_serialize_dynbox(
    woort_DynBox boxed)
{
    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    /* OPTIONAL */ char* result = NULL;
    if (_woort_serialize_dynbox_to_buf_for_debug(
        boxed, &buf, &visited_set, 0, true))
    {
        result = (char*)malloc(buf.m_size + 1);
        if (result != NULL)
        {
            if (buf.m_size != 0)
                memcpy(result, buf.m_data, buf.m_size);
            result[buf.m_size] = '\0';
        }
    }

    woort_hashmap_deinit(&visited_set);
    woort_vector_deinit(&buf);
    return result;
}

/* ========================================================================
 * Breakpoint & stepping machinery
 * ========================================================================
 *
 * Trap placement is refcounted in m_breakpoints (ip -> stop count): user
 * breakpoints and the transient stepping traps share the map, and the
 * bytecode only gets unpatched when the last user of an ip goes away.
 * While a step/next/return resume is in flight, m_focusing_vms carries
 * the per-VM stepping state and _woort_Debugger_should_stop turns a raw
 * TRAP into a stop decision based on it.
 *
 * All of this runs under the session m_mx (except the attach-failure
 * cleanup, which runs under the attach lock before publication).
 */

WOORT_NODISCARD bool _woort_Debugger_break_at(
    woort_DebuggerSession* session, const woort_Bytecode* ip)
{
    woort_CodeEnv* cenv;
    if (woort_CodeEnv_find(ip, &cenv))
    {
        size_t* counter;
        switch (woort_hashmap_get_or_emplace(
            &session->m_breakpoints, &ip, (void**)&counter))
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

void _woort_Debugger_cancel_break_at(
    woort_DebuggerSession* session, const woort_Bytecode* ip)
{
    size_t* counter;
    if (woort_hashmap_find(&session->m_breakpoints, &ip, (void**)&counter))
    {
        --*counter;
        if (*counter == 0)
        {
            (void)woort_hashmap_remove(&session->m_breakpoints, &ip);

            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(ip, &cenv))
                (void)woort_CodeEnv_clear_trap(cenv, (woort_Bytecode*)ip);
        }
    }
}

typedef struct _woort_Debugger_StepContext
{
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

} _woort_Debugger_StepContext;

static void _woort_Debugger_step_context_init(
    _woort_Debugger_StepContext* vmcontext)
{
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

static bool _woort_Debugger_step_context_set_step_break(
    woort_DebuggerSession* session,
    _woort_Debugger_StepContext* vmcontext,
    const woort_Bytecode* ip)
{
    for (size_t i = 0; i < 2; ++i)
    {
        if (vmcontext->m_step_breakpoints[i] == NULL)
        {
            if (_woort_Debugger_break_at(session, ip))
            {
                vmcontext->m_step_breakpoints[i] = ip;
                return true;
            }
            break;
        }
    }
    return false;
}

static void _woort_Debugger_step_context_set_source_step(
    _woort_Debugger_StepContext* vmcontext,
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

static void _woort_Debugger_step_context_clean_step_break(
    woort_DebuggerSession* session,
    _woort_Debugger_StepContext* vmcontext)
{
    for (size_t i = 0; i < 2; ++i)
    {
        if (vmcontext->m_step_breakpoints[i] != NULL)
        {
            _woort_Debugger_cancel_break_at(
                session, vmcontext->m_step_breakpoints[i]);

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

static void _woort_Debugger_step_context_deinit(
    woort_DebuggerSession* session,
    _woort_Debugger_StepContext* vmcontext)
{
    _woort_Debugger_step_context_clean_step_break(session, vmcontext);
}

static bool _woort_Debugger_step_context_meet_step_breakdown(
    _woort_Debugger_StepContext* vmcontext, const woort_Bytecode* ip)
{
    return vmcontext->m_step_breakpoints[0] == ip
        || vmcontext->m_step_breakpoints[1] == ip;
}

static size_t _woort_Debugger_get_current_callstack_depth(
    woort_VMRuntime* vm)
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

bool _woort_Debugger_set_next_source_break(
    woort_DebuggerSession* session, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    _woort_Debugger_StepContext* vmcontext;
    if (!woort_hashmap_find(&session->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
        return false;

    const uint32_t code_offset =
        (uint32_t)(vm->m_ip - cenv->m_code_begin);
    woort_SourceLocation src_loc;

    if (woort_CodeEnv_find_srcloc_by_offset(cenv, code_offset, &src_loc))
    {
        _woort_Debugger_step_context_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_Debugger_step_context_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    vmcontext->m_is_source_next = true;
    vmcontext->m_step_target_depth =
        _woort_Debugger_get_current_callstack_depth(vm);

    return _woort_Debugger_step_context_set_step_break(session, vmcontext, ip);
}

bool _woort_Debugger_set_return_break(
    woort_DebuggerSession* session, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    _woort_Debugger_StepContext* vmcontext;
    if (!woort_hashmap_find(&session->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
        return false;

    const uint32_t code_offset =
        (uint32_t)(vm->m_ip - cenv->m_code_begin);
    woort_SourceLocation src_loc;

    if (woort_CodeEnv_find_srcloc_by_offset(cenv, code_offset, &src_loc))
    {
        _woort_Debugger_step_context_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_Debugger_step_context_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    vmcontext->m_is_source_return = true;
    vmcontext->m_step_target_depth =
        _woort_Debugger_get_current_callstack_depth(vm);

    return _woort_Debugger_step_context_set_step_break(session, vmcontext, ip);
}

bool _woort_Debugger_focus_on(
    woort_DebuggerSession* session, woort_VMRuntime* vm)
{
    _woort_Debugger_StepContext* vmcontext;
    switch (woort_hashmap_get_or_emplace(
        &session->m_focusing_vms, &vm, (void**)&vmcontext))
    {
    case WOORT_HASHMAP_RESULT_OK:
        _woort_Debugger_step_context_init(vmcontext);
        break;
    case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        break;
    case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
        /* Emm... */
        return false;
    }
    return true;
}

void _woort_Debugger_out_of_focus(
    woort_DebuggerSession* session, woort_VMRuntime* vm)
{
    _woort_Debugger_StepContext* vmcontext;
    if (woort_hashmap_find(&session->m_focusing_vms, &vm, (void**)&vmcontext))
    {
        _woort_Debugger_step_context_deinit(session, vmcontext);
        (void)woort_hashmap_remove(&session->m_focusing_vms, &vm);
    }
}

bool _woort_Debugger_set_step_break(
    woort_DebuggerSession* session, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    _woort_Debugger_StepContext* vmcontext;
    if (!woort_hashmap_find(&session->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;
    return _woort_Debugger_step_context_set_step_break(
        session, vmcontext, ip);
}

bool _woort_Debugger_set_step_source_break(
    woort_DebuggerSession* session, woort_VMRuntime* vm,
    const woort_Bytecode* ip)
{
    _woort_Debugger_StepContext* vmcontext;
    if (!woort_hashmap_find(&session->m_focusing_vms, &vm, (void**)&vmcontext))
        return false;

    woort_CodeEnv* cenv;
    if (!woort_CodeEnv_find(vm->m_ip, &cenv))
        return false;

    const uint32_t code_offset =
        (uint32_t)(vm->m_ip - cenv->m_code_begin);
    woort_SourceLocation src_loc;

    if (woort_CodeEnv_find_srcloc_by_offset(cenv, code_offset, &src_loc))
    {
        _woort_Debugger_step_context_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_Debugger_step_context_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    return _woort_Debugger_step_context_set_step_break(session, vmcontext, ip);
}

/* ====================================================================
 * Next-ip computation for the stepping machinery
 * ==================================================================== */

static int _woort_Debugger_empty_cb(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

/*
 * 根据当前指令和 VM 状态计算下一条指令的地址（用于单步执行）。
 * 考虑跳转、调用、返回等所有控制流转移情况。
 * 返回 false 表示无法确定下一条指令（如从 native 函数返回）。
 */
WOORT_NODISCARD bool _woort_Debugger_get_next_ip(
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
        *out_next_ip =
            cenv->m_data_begin[WOORT_BYTECODE(MABC26, bc)].m_script_function;
        return true;
    }
    case WOORT_OPCODE_CALLNFP:
    case WOORT_OPCODE_CALLNJIT:
    {
        (void)woort_VMRuntime_request_set(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);
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

        // Assure invoking closure is valid.
        const woort_GCClosure* const invoked_closure_instance =
            woort_mem_validate_addr_head((void*)target);

        if (invoked_closure_instance != NULL
            && invoked_closure_instance == target
            && invoked_closure_instance->m_gc_unit.m_proxy
                == &WOORT_GCCLOSURE_UNIT_PROXY)
        {
            /*
            The minimum unit of memory allocation in Woomem is 8 bytes. We need to
            verify the type of the unit here, and the type information happens to
            fall within the first eight bytes; therefore, reading the first 8 bytes
            of the unit is safe.
            */
            _Static_assert(
                offsetof(woort_GCClosure, m_gc_unit)
                + sizeof(invoked_closure_instance->m_gc_unit) <= 8,
                "woort_GCUnit is too large/far to safely verify its type.");

            if (invoked_closure_instance->m_script_function != NULL)
                *out_next_ip = invoked_closure_instance->m_script_function;
            else
                *out_next_ip = ip + 1;

            return true;
        }
        else
            /* Bad closure instance */
            return false;
    }
    case WOORT_OPCODE_RET:
    {
        if (m2 == 3)
        {
            /* Is POPRS. not ret. */
            goto label_fall_to_default;
        }

        const woort_Value* trace_sb = sb;
        while (trace_sb[1].m_ret_bp.m_way == WOORT_CALL_WAY_FROM_NATIVE)
        {
            trace_sb = trace_sb + 2 + trace_sb[1].m_ret_bp.m_bp_offset;
            if (vm->m_stack_end - trace_sb < 3)
                return false;
        }
        *out_next_ip = (const woort_Bytecode*)trace_sb[2].m_ret_addr;
        return true;
    }
    case WOORT_OPCODE_JIFINITED:
    {
        woort_AtomicInt64* const flag =
            &cenv->m_data_begin[ip[1]].m_atomic_i64;
        const int64_t flag_stat = woort_atomic_load_explicit(
            (woort_AtomicInt64*)flag,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        if (flag_stat == 2)
            *out_next_ip = cenv->m_code_begin + WOORT_BYTECODE(MABC26, bc);
        else
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
        *out_next_ip = woort_disassembly(ip, &_woort_Debugger_empty_cb);
        return true;
    }
    }
}

static bool _woort_Debugger_meet_breakpoint(
    woort_DebuggerSession* session, woort_VMRuntime* vm, bool trap_by_req,
    /* OPTIONAL */ woort_DebuggerStopReason* out_reason)
{
    const woort_Bytecode* current_ip = vm->m_ip;

    woort_DebuggerStopReason reason = trap_by_req
        ? WOORT_DEBUGGER_STOP_REASON_INTERRUPT
        : WOORT_DEBUGGER_STOP_REASON_BREAKPOINT;

    bool breakdown = false;
    if (trap_by_req || woort_hashmap_contains(
        &session->m_breakpoints, &current_ip))
    {
        if (!trap_by_req && woort_hashmap_contains(
            &session->m_debug_breakpoints, &current_ip))
        {
            breakdown = true;
        }

        /* May be step debug point? */
        _woort_Debugger_StepContext* vmcontext;
        if (woort_hashmap_find(&session->m_focusing_vms, &vm, (void**)&vmcontext))
        {
            if (trap_by_req)
            {
                /* An interrupt request always stops, even mid-stepping. */
                breakdown = true;
            }
            else if (_woort_Debugger_step_context_meet_step_breakdown(
                vmcontext, current_ip))
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
                                ? (vmcontext->m_step_source_file
                                    != src_loc.m_filepath)
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
                                if (_woort_Debugger_get_current_callstack_depth(vm)
                                    < vmcontext->m_step_target_depth)
                                {
                                    /* 调用栈深度已减少，已返回上层函数，中断 */
                                    should_break = true;
                                }
                                /* else: 仍在当前函数或更深层，继续步进 */
                            }
                            else if (vmcontext->m_is_source_next)
                            {
                                if (_woort_Debugger_get_current_callstack_depth(vm)
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
                                reason = WOORT_DEBUGGER_STOP_REASON_STEP;
                            }
                            else
                            {
                                /* 仍在同一源码位置（或 next 中尚未返回目标深度），继续步进 */
                                const char* saved_file =
                                    vmcontext->m_step_source_file;
                                const size_t saved_line =
                                    vmcontext->m_step_source_line;
                                const size_t saved_bcol =
                                    vmcontext->m_step_source_begin_column;
                                const size_t saved_eline =
                                    vmcontext->m_step_source_end_line;
                                const size_t saved_ecol =
                                    vmcontext->m_step_source_end_column;
                                const bool saved_is_next =
                                    vmcontext->m_is_source_next;
                                const bool saved_is_return =
                                    vmcontext->m_is_source_return;
                                const size_t saved_target_depth =
                                    vmcontext->m_step_target_depth;

                                _woort_Debugger_step_context_clean_step_break(
                                    session, vmcontext);

                                const woort_Bytecode* next_ip = NULL;
                                if (_woort_Debugger_get_next_ip(
                                    current_ip, cenv, vm->m_sb, vm, &next_ip))
                                {
                                    _woort_Debugger_step_context_set_source_step(
                                        vmcontext, saved_file, saved_line,
                                        saved_bcol, saved_eline, saved_ecol);
                                    vmcontext->m_is_source_next = saved_is_next;
                                    vmcontext->m_is_source_return =
                                        saved_is_return;
                                    vmcontext->m_step_target_depth =
                                        saved_target_depth;

                                    if (_woort_Debugger_step_context_set_step_break(
                                        session, vmcontext, next_ip))
                                    {
                                        /* 成功设置下一步断点，不中断 */
                                    }
                                    else
                                    {
                                        /* 设置断点失败，中断 */
                                        breakdown = true;
                                        reason =
                                            WOORT_DEBUGGER_STOP_REASON_STEP;
                                    }
                                }
                                else
                                {
                                    /* 无法确定下一条指令，中断 */
                                    breakdown = true;
                                    reason = WOORT_DEBUGGER_STOP_REASON_STEP;
                                }
                            }
                        }
                        else
                        {
                            /* 当前指令无源码信息，中断 */
                            breakdown = true;
                            reason = WOORT_DEBUGGER_STOP_REASON_STEP;
                        }
                    }
                    else
                    {
                        /* 无法定位 CodeEnv，中断 */
                        breakdown = true;
                        reason = WOORT_DEBUGGER_STOP_REASON_STEP;
                    }
                }
                else
                {
                    /* IR 级步进：直接中断 */
                    breakdown = true;
                    reason = WOORT_DEBUGGER_STOP_REASON_STEP;
                }
            }

            if (breakdown)
                _woort_Debugger_step_context_clean_step_break(
                    session, vmcontext);
        }
    }

    if (out_reason != NULL)
        *out_reason = reason;
    return breakdown;
}

/*
 * Decide whether the trap the VM just ran into should turn into a real
 * stop.  When nothing is stepping, every trap stops (user breakpoint,
 * leftover trap or interrupt request).  Otherwise the decision follows
 * the stepping rules implemented by _woort_Debugger_meet_breakpoint.
 * out_reason (optional) receives the stop classification.
 */
WOORT_NODISCARD bool _woort_Debugger_should_stop(
    woort_DebuggerSession* session,
    woort_VMRuntime* vm,
    bool trap_by_request,
    /* OPTIONAL */ woort_DebuggerStopReason* out_reason)
{
    woort_DebuggerStopReason reason = WOORT_DEBUGGER_STOP_REASON_BREAKPOINT;

    bool stop;
    if (woort_hashmap_is_empty(&session->m_focusing_vms))
    {
        /*
         * Nothing is stepping: any trap stops here.  The trap is either a
         * user breakpoint / leftover trap, or an interrupt request.
         */
        stop = true;
        reason = trap_by_request
            ? WOORT_DEBUGGER_STOP_REASON_INTERRUPT
            : WOORT_DEBUGGER_STOP_REASON_BREAKPOINT;
    }
    else
    {
        stop = _woort_Debugger_meet_breakpoint(
            session, vm, trap_by_request, &reason);
    }

    if (out_reason != NULL)
        *out_reason = reason;
    return stop;
}

static void _woort_Debugger_init_trap_state(woort_DebuggerSession* session)
{
    woort_hashmap_init(
        &session->m_breakpoints,
        sizeof(woort_Bytecode*),
        sizeof(size_t),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    woort_hashmap_init(
        &session->m_debug_breakpoints,
        sizeof(woort_Bytecode*),
        0,
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    woort_hashmap_init(
        &session->m_focusing_vms,
        sizeof(woort_VMRuntime*),
        sizeof(_woort_Debugger_StepContext),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);
}

static bool _woort_Debugger_step_context_deinit_callback(
    const void* key,
    void* value,
    void* user_data)
{
    (void)key;
    _woort_Debugger_step_context_deinit(
        (woort_DebuggerSession*)user_data,
        (_woort_Debugger_StepContext*)value);
    return true;
}

static bool _woort_Debugger_collect_break_keys_callback(
    const void* key,
    void* value,
    void* user_data)
{
    (void)value;
    return woort_vector_push_back((woort_Vector*)user_data, 1, &key);
}

/*
 * Cancel every placed trap (restoring the original bytecodes), clear the
 * stepping state of all focused VMs and release the maps.
 */
static void _woort_Debugger_deinit_trap_state(woort_DebuggerSession* session)
{
    (void)woort_hashmap_foreach(
        &session->m_focusing_vms,
        &_woort_Debugger_step_context_deinit_callback,
        session);

    woort_hashmap_deinit(&session->m_focusing_vms);

    /* Drop every placed trap so the bytecode runs unpatched afterwards.
       Keys are snapshotted first: cancel mutates the map. */
    {
        woort_Vector keys;
        woort_vector_init(&keys, sizeof(const woort_Bytecode*));

        (void)woort_hashmap_foreach(
            &session->m_breakpoints,
            &_woort_Debugger_collect_break_keys_callback,
            &keys);

        for (size_t i = 0; i < keys.m_size; ++i)
        {
            _woort_Debugger_cancel_break_at(
                session,
                *(const woort_Bytecode**)woort_vector_at(&keys, i));
        }

        woort_vector_deinit(&keys);
    }

    woort_hashmap_deinit(&session->m_breakpoints);
    woort_hashmap_deinit(&session->m_debug_breakpoints);
}

/* ========================================================================
 * Breakpoint records
 * ======================================================================== */

typedef struct _woort_Debugger_ResolveSourceCtx
{
    woort_DebuggerSession* m_session;
    woort_DebuggerBreakpointRecord* m_record;

} _woort_Debugger_ResolveSourceCtx;

static bool _woort_Debugger_resolve_source_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_Debugger_ResolveSourceCtx* const ctx =
        (_woort_Debugger_ResolveSourceCtx*)user_data;

    uint32_t offset;
    if (!woort_CodeEnv_find_offset_by_srcloc(
        cenv, ctx->m_record->m_file, ctx->m_record->m_line, &offset))
    {
        return true;
    }

    const woort_Bytecode* const ip = cenv->m_code_begin + offset;

    if (!_woort_Debugger_break_at(ctx->m_session, ip))
    {
        return true;
    }

    (void)woort_vector_push_back(&ctx->m_record->m_applied_ips, 1, &ip);

    if (!ctx->m_record->m_resolved)
    {
        ctx->m_record->m_resolved = true;

        woort_SourceLocation loc;
        ctx->m_record->m_resolved_line =
            woort_CodeEnv_find_srcloc_by_offset(cenv, offset, &loc)
            ? loc.m_begin_line
            : ctx->m_record->m_line;
    }

    return true;
}

typedef struct _woort_Debugger_ResolveFuncCtx
{
    woort_DebuggerSession* m_session;
    woort_DebuggerBreakpointRecord* m_record;

} _woort_Debugger_ResolveFuncCtx;

static bool _woort_Debugger_resolve_func_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_Debugger_ResolveFuncCtx* const ctx =
        (_woort_Debugger_ResolveFuncCtx*)user_data;

    const size_t boundary_count = cenv->m_function_boundaries.m_size;
    for (size_t i = 0; i < boundary_count; ++i)
    {
        const woort_FunctionBoundary* boundary =
            (const woort_FunctionBoundary*)woort_vector_at(
            (woort_Vector*)&cenv->m_function_boundaries, i);

        if (boundary->m_name == NULL)
            continue;
        if (strcmp(boundary->m_name, ctx->m_record->m_function_name) != 0)
            continue;

        const woort_Bytecode* const ip =
            cenv->m_code_begin + boundary->m_offset_begin;

        if (!_woort_Debugger_break_at(ctx->m_session, ip))
        {
            continue;
        }

        (void)woort_vector_push_back(&ctx->m_record->m_applied_ips, 1, &ip);

        if (!ctx->m_record->m_resolved)
        {
            ctx->m_record->m_resolved = true;

            woort_SourceLocation loc;
            ctx->m_record->m_resolved_line =
                woort_CodeEnv_find_srcloc_by_offset(
                    cenv, boundary->m_offset_begin, &loc)
                ? loc.m_begin_line
                : 0;
        }
    }

    return true;
}

/* Caller holds m_mx. */
static void _woort_Debugger_refresh_breakpoints_locked(
    woort_DebuggerSession* session)
{
    for (size_t i = 0; i < session->m_breakpoint_records.m_size; ++i)
    {
        woort_DebuggerBreakpointRecord* record =
            (woort_DebuggerBreakpointRecord*)woort_vector_at(
            &session->m_breakpoint_records, i);

        if (record->m_resolved)
            continue;

        if (record->m_is_function_bp)
        {
            _woort_Debugger_ResolveFuncCtx ctx;
            ctx.m_session = session;
            ctx.m_record = record;

            woort_CodeEnv_foreach(
                &_woort_Debugger_resolve_func_callback, &ctx);
        }
        else
        {
            _woort_Debugger_ResolveSourceCtx ctx;
            ctx.m_session = session;
            ctx.m_record = record;

            woort_CodeEnv_foreach(
                &_woort_Debugger_resolve_source_callback, &ctx);
        }
    }
}

/* Caller holds m_mx. */
static void _woort_Debugger_drop_breakpoint_record(
    woort_DebuggerSession* session,
    woort_DebuggerBreakpointRecord* record)
{
    for (size_t i = 0; i < record->m_applied_ips.m_size; ++i)
    {
        _woort_Debugger_cancel_break_at(
            session,
            *(const woort_Bytecode**)woort_vector_at(
            &record->m_applied_ips, i));
    }

    woort_vector_deinit(&record->m_applied_ips);
}

/* ========================================================================
 * Panic routing
 * ======================================================================== */

static woort_PanicHandler_Action _woort_Debugger_session_panic_handler(
    /* OPTIONAL */ woort_VMRuntime* vm,
    const char* funcname,
    const char* location,
    int line,
    int reason,
    const char* message)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();

    if (session == NULL || !session->m_attached || session->m_detached
        || vm == NULL)
    {
        /* Nothing to break into; defer to the handler we replaced. */
        if (session != NULL && session->m_prev_panic_handler != NULL)
        {
            return session->m_prev_panic_handler(
                vm, funcname, location, line, reason, message);
        }
        return WOORT_PANIC_HANDLER_ACTION_USE_DEFAULT_HANDLER;
    }

    woort_mutex_lock(session->m_mx);
    {
        session->m_has_panic = true;
        session->m_panic.m_vm = vm->m_serial;
        session->m_panic.m_reason = reason;
        session->m_panic.m_line = line;
        _woort_Debugger_copy_str(
            session->m_panic.m_function_name,
            sizeof(session->m_panic.m_function_name), funcname);
        _woort_Debugger_copy_str(
            session->m_panic.m_file, sizeof(session->m_panic.m_file),
            location);
        _woort_Debugger_copy_str(
            session->m_panic.m_message, sizeof(session->m_panic.m_message),
            message);
    }
    woort_mutex_unlock(session->m_mx);

    /* Stop this VM at its next checkpoint; cancel the abort below. */
    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);

    return WOORT_PANIC_HANDLER_ACTION_CONTINUE;
}

/* ========================================================================
 * The trap callback (VM thread)
 * ======================================================================== */

static void _woort_Debugger_session_callback(
    woort_VMRuntime* vm,
    void* context,
    bool trap_by_request)
{
    woort_DebuggerSession* const session =
        (woort_DebuggerSession*)context;

    woort_mutex_lock(session->m_mx);
    {
        if (session->m_detached || !session->m_attached)
        {
            woort_mutex_unlock(session->m_mx);
            return;
        }

        woort_DebuggerStopReason reason;
        if (!_woort_Debugger_should_stop(
            session, vm, trap_by_request, &reason))
        {
            woort_mutex_unlock(session->m_mx);
            return;
        }

        if (reason == WOORT_DEBUGGER_STOP_REASON_INTERRUPT
            && session->m_has_panic
            && session->m_panic.m_vm == vm->m_serial)
        {
            reason = WOORT_DEBUGGER_STOP_REASON_PANIC;
        }

        /* Code loaded since the last touch may resolve pending
           breakpoints now; this is where freshly loaded modules pick
           their breakpoints up. */
        _woort_Debugger_refresh_breakpoints_locked(session);

        session->m_stop_vm = vm;
        session->m_stop_reason = reason;
        session->m_stop_active = true;
        session->m_action_ready = false;

        woort_condition_variable_broadcast(session->m_cv);

        /* Park until the host resumes (or the session is detached).  The
           timed wait is a safety net so a detached session can never
           leave a VM stuck here, even if the broadcast was missed. */
        while (!session->m_action_ready && !session->m_detached)
        {
            woort_condition_variable_timed_wait(
                session->m_cv, session->m_mx, 50);
        }

        session->m_action_ready = false;

        woort_condition_variable_broadcast(session->m_cv);
    }
    woort_mutex_unlock(session->m_mx);
}

/* ========================================================================
 * Lifecycle
 * ======================================================================== */

WOORT_NODISCARD bool _woort_Debugger_session_bootup(void)
{
    return woort_mutex_create(&g_debugger_session_attach_mx);
}

void _woort_Debugger_session_shutdown(void)
{
    (void)woort_Debugger_detach();

    if (g_debugger_session_attach_mx != NULL)
    {
        woort_mutex_destroy(g_debugger_session_attach_mx);
        g_debugger_session_attach_mx = NULL;
    }
}

WOORT_NODISCARD woort_DebuggerAttachResult woort_Debugger_attach(void)
{
    woort_mutex_lock(g_debugger_session_attach_mx);
    {
        woort_DebuggerSession* const existing =
            _woort_Debugger_session_load();
        if (existing != NULL && existing->m_attached)
        {
            woort_mutex_unlock(g_debugger_session_attach_mx);
            return WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED;
        }

        woort_DebuggerSession* const session =
            (woort_DebuggerSession*)calloc(1, sizeof(woort_DebuggerSession));
        if (session == NULL)
        {
            woort_mutex_unlock(g_debugger_session_attach_mx);
            return WOORT_DEBUGGER_ATTACH_RESULT_FAILED;
        }

        if (!woort_mutex_create(&session->m_mx)
            || !woort_condition_variable_create(&session->m_cv))
        {
            if (session->m_mx != NULL)
                woort_mutex_destroy(session->m_mx);
            if (session->m_cv != NULL)
                woort_condition_variable_destroy(session->m_cv);
            free(session);
            woort_mutex_unlock(g_debugger_session_attach_mx);
            return WOORT_DEBUGGER_ATTACH_RESULT_FAILED;
        }

        _woort_Debugger_init_trap_state(session);
        woort_vector_init(
            &session->m_breakpoint_records,
            sizeof(woort_DebuggerBreakpointRecord));

        session->m_next_bp_id = 1;
        session->m_attached = true;
        session->m_detached = false;

        const woort_DebuggerAttachResult attach_result =
            woort_VMRuntime_Debugger_attach(
                &_woort_Debugger_session_callback, session, NULL);

        if (attach_result != WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS)
        {
            woort_vector_deinit(&session->m_breakpoint_records);
            _woort_Debugger_deinit_trap_state(session);
            woort_mutex_destroy(session->m_mx);
            woort_condition_variable_destroy(session->m_cv);
            free(session);
            woort_mutex_unlock(g_debugger_session_attach_mx);
            return attach_result;
        }

        /* While the session is attached it owns the panic callback; the
           previous one is restored on detach. */
        session->m_prev_panic_handler =
            woort_set_panic_callback(&_woort_Debugger_session_panic_handler);

        _woort_Debugger_session_store(session);
    }
    woort_mutex_unlock(g_debugger_session_attach_mx);

    return WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS;
}

WOORT_NODISCARD bool woort_Debugger_detach(void)
{
    if (g_debugger_session_attach_mx == NULL)
        return false;

    woort_mutex_lock(g_debugger_session_attach_mx);
    {
        woort_DebuggerSession* const session =
            _woort_Debugger_session_load();

        if (session == NULL || !session->m_attached)
        {
            woort_mutex_unlock(g_debugger_session_attach_mx);
            return false;
        }

        woort_mutex_lock(session->m_mx);
        {
            session->m_detached = true;

            if (session->m_stop_active && session->m_stop_vm != NULL)
            {
                /* Force-continue the parked VM. */
                _woort_Debugger_out_of_focus(session, session->m_stop_vm);
                _woort_Debugger_clear_pending_interrupts(session->m_stop_vm);
                session->m_has_panic = false;
                session->m_stop_active = false;
                session->m_stop_vm = NULL;
                session->m_action_ready = true;
            }

            /* Release every placed trap. */
            while (session->m_breakpoint_records.m_size != 0)
            {
                woort_DebuggerBreakpointRecord* record =
                    (woort_DebuggerBreakpointRecord*)woort_vector_at(
                    &session->m_breakpoint_records, 0);

                _woort_Debugger_drop_breakpoint_record(session, record);
                (void)woort_vector_erase_at(
                    &session->m_breakpoint_records, 0);
            }

            woort_condition_variable_broadcast(session->m_cv);
        }
        woort_mutex_unlock(session->m_mx);

        (void)woort_set_panic_callback(session->m_prev_panic_handler);
        session->m_prev_panic_handler = NULL;
        session->m_attached = false;

        woort_VMRuntime_Debugger_detach();
        _woort_Debugger_session_store(NULL);
    }
    woort_mutex_unlock(g_debugger_session_attach_mx);

    return true;
}

WOORT_NODISCARD bool woort_Debugger_is_attached(void)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    return session != NULL && session->m_attached;
}

/* ========================================================================
 * Events
 * ======================================================================== */

WOORT_NODISCARD bool woort_Debugger_wait_for_break(
    uint32_t timeout_ms,
    woort_DebuggerBreakEvent* out_event)
{
    if (out_event == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    uint32_t waited_ms = 0;
    for (;;)
    {
        bool delivered = false;
        bool aborted = false;

        woort_mutex_lock(session->m_mx);
        {
            if (session->m_stop_active && session->m_stop_vm != NULL)
            {
                out_event->m_vm = session->m_stop_vm->m_serial;
                out_event->m_reason = session->m_stop_reason;
                delivered = true;
            }
            else if (!session->m_attached || session->m_detached)
            {
                aborted = true;
            }
            else if (timeout_ms == WOORT_DEBUGGER_WAIT_INFINITE
                || waited_ms < timeout_ms)
            {
                woort_condition_variable_timed_wait(
                    session->m_cv, session->m_mx, 50);
            }
            else
            {
                aborted = true; /* timeout */
            }
        }
        woort_mutex_unlock(session->m_mx);

        if (delivered)
            return true;
        if (aborted)
            return false;

        waited_ms += 50;
    }
}

WOORT_NODISCARD bool woort_Debugger_get_current_break(
    woort_DebuggerBreakEvent* out_event)
{
    if (out_event == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool stopped = false;
    woort_mutex_lock(session->m_mx);
    {
        if (session->m_stop_active && session->m_stop_vm != NULL)
        {
            out_event->m_vm = session->m_stop_vm->m_serial;
            out_event->m_reason = session->m_stop_reason;
            stopped = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return stopped;
}

/* ========================================================================
 * Interrupts & VM listing
 * ======================================================================== */

static bool _woort_Debugger_interrupt_all_callback(
    woort_VMRuntime* vm, void* user_data)
{
    (void)user_data;
    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);
    return true;
}

void woort_Debugger_interrupt_all(void)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL || !session->m_attached)
        return;

    woort_GC_foreach_root_vm(&_woort_Debugger_interrupt_all_callback, NULL);
}

WOORT_NODISCARD bool woort_Debugger_interrupt_vm(woort_DebuggerVmId vm)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL || !session->m_attached)
        return false;

    /* Interrupting the VM that is already stopped is a no-op. */
    woort_mutex_lock(session->m_mx);
    {
        if (session->m_stop_active && session->m_stop_vm != NULL
            && session->m_stop_vm->m_serial == vm)
        {
            woort_mutex_unlock(session->m_mx);
            return false;
        }
    }
    woort_mutex_unlock(session->m_mx);

    woort_VMRuntime* const target = _woort_Debugger_find_vm(vm);
    if (target == NULL)
        return false;

    (void)woort_VMRuntime_request_set(
        target, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);
    return true;
}

typedef struct _woort_Debugger_ListVmContext
{
    size_t m_index;
    /* OPTIONAL */ woort_DebuggerVmInfo* m_out_info;
    size_t m_want_index;
    woort_DebuggerVmId m_stopped_serial;
    bool m_stopped_valid;

} _woort_Debugger_ListVmContext;

static bool _woort_Debugger_list_vm_callback(
    woort_VMRuntime* vm, void* user_data)
{
    _woort_Debugger_ListVmContext* const ctx =
        (_woort_Debugger_ListVmContext*)user_data;

    if (ctx->m_out_info != NULL && ctx->m_index == ctx->m_want_index)
    {
        ctx->m_out_info->m_id = vm->m_serial;
        ctx->m_out_info->m_is_stopped =
            ctx->m_stopped_valid && vm->m_serial == ctx->m_stopped_serial;
        ctx->m_out_info = NULL;
    }

    ++ctx->m_index;
    return true;
}

bool woort_Debugger_current_vm_id(woort_DebuggerVmId* out_vm_id)
{
    const woort_VMRuntime* const vm = woort_VMRuntime_current();
    if (vm == NULL)
        return false;

    *out_vm_id = vm->m_serial;
    return true;
}

WOORT_NODISCARD size_t woort_Debugger_get_vm_count(void)
{
    _woort_Debugger_ListVmContext ctx;
    ctx.m_index = 0;
    ctx.m_out_info = NULL;
    ctx.m_want_index = 0;
    ctx.m_stopped_serial = 0;
    ctx.m_stopped_valid = false;

    woort_GC_foreach_root_vm(&_woort_Debugger_list_vm_callback, &ctx);

    return ctx.m_index;
}

WOORT_NODISCARD bool woort_Debugger_get_vm_info(
    size_t index,
    woort_DebuggerVmInfo* out_info)
{
    if (out_info == NULL)
        return false;

    _woort_Debugger_ListVmContext ctx;
    ctx.m_index = 0;
    ctx.m_out_info = out_info;
    ctx.m_want_index = index;
    ctx.m_stopped_serial = 0;
    ctx.m_stopped_valid = false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session != NULL)
    {
        woort_mutex_lock(session->m_mx);
        {
            if (session->m_stop_active && session->m_stop_vm != NULL)
            {
                ctx.m_stopped_serial = session->m_stop_vm->m_serial;
                ctx.m_stopped_valid = true;
            }
        }
        woort_mutex_unlock(session->m_mx);
    }

    woort_GC_foreach_root_vm(&_woort_Debugger_list_vm_callback, &ctx);

    return ctx.m_out_info == NULL;
}

/* ========================================================================
 * Breakpoints (public)
 * ======================================================================== */

static /* OPTIONAL */ woort_DebuggerBreakpointRecord*
_woort_Debugger_find_breakpoint_record(
    woort_DebuggerSession* session,
    woort_DebuggerBreakpointId id)
{
    for (size_t i = 0; i < session->m_breakpoint_records.m_size; ++i)
    {
        woort_DebuggerBreakpointRecord* record =
            (woort_DebuggerBreakpointRecord*)woort_vector_at(
            &session->m_breakpoint_records, i);

        if (record->m_id == id)
            return record;
    }
    return NULL;
}

WOORT_NODISCARD bool woort_Debugger_set_source_breakpoint(
    const char* filepath,
    uint32_t line,
    /* OPTIONAL */ woort_DebuggerBreakpointId* out_id)
{
    /* line 与 srcloc 调试信息同基（首行为 0），不再做 ±1 换算。 */
    if (filepath == NULL || filepath[0] == '\0')
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL || !session->m_attached)
        return false;

    bool ok = false;
    woort_DebuggerBreakpointId id = 0;

    woort_mutex_lock(session->m_mx);
    {
        woort_DebuggerBreakpointRecord* emplaced = NULL;
        if (woort_vector_emplace_back(
            &session->m_breakpoint_records, 1, (void**)&emplaced))
        {
            memset(emplaced, 0, sizeof(*emplaced));
            emplaced->m_id = session->m_next_bp_id++;
            emplaced->m_is_function_bp = false;
            _woort_Debugger_copy_str(
                emplaced->m_file, sizeof(emplaced->m_file), filepath);
            emplaced->m_line = line;
            woort_vector_init(&emplaced->m_applied_ips,
                sizeof(const woort_Bytecode*));

            _woort_Debugger_refresh_breakpoints_locked(session);

            id = emplaced->m_id;
            ok = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    if (ok && out_id != NULL)
        *out_id = id;
    return ok;
}

WOORT_NODISCARD bool woort_Debugger_set_function_breakpoint(
    const char* function_name,
    /* OPTIONAL */ woort_DebuggerBreakpointId* out_id)
{
    if (function_name == NULL || function_name[0] == '\0')
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL || !session->m_attached)
        return false;

    bool ok = false;
    woort_DebuggerBreakpointId id = 0;

    woort_mutex_lock(session->m_mx);
    {
        woort_DebuggerBreakpointRecord* emplaced = NULL;
        if (woort_vector_emplace_back(
            &session->m_breakpoint_records, 1, (void**)&emplaced))
        {
            memset(emplaced, 0, sizeof(*emplaced));
            emplaced->m_id = session->m_next_bp_id++;
            emplaced->m_is_function_bp = true;
            _woort_Debugger_copy_str(
                emplaced->m_function_name,
                sizeof(emplaced->m_function_name), function_name);
            woort_vector_init(&emplaced->m_applied_ips,
                sizeof(const woort_Bytecode*));

            _woort_Debugger_refresh_breakpoints_locked(session);

            id = emplaced->m_id;
            ok = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    if (ok && out_id != NULL)
        *out_id = id;
    return ok;
}

WOORT_NODISCARD bool woort_Debugger_remove_breakpoint(
    woort_DebuggerBreakpointId id)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool removed = false;

    woort_mutex_lock(session->m_mx);
    {
        for (size_t i = 0; i < session->m_breakpoint_records.m_size; ++i)
        {
            woort_DebuggerBreakpointRecord* record =
                (woort_DebuggerBreakpointRecord*)woort_vector_at(
                &session->m_breakpoint_records, i);

            if (record->m_id != id)
                continue;

            _woort_Debugger_drop_breakpoint_record(session, record);
            (void)woort_vector_erase_at(&session->m_breakpoint_records, i);
            removed = true;
            break;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return removed;
}

void woort_Debugger_clear_breakpoints(void)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return;

    woort_mutex_lock(session->m_mx);
    {
        while (session->m_breakpoint_records.m_size != 0)
        {
            woort_DebuggerBreakpointRecord* record =
                (woort_DebuggerBreakpointRecord*)woort_vector_at(
                &session->m_breakpoint_records, 0);

            _woort_Debugger_drop_breakpoint_record(session, record);
            (void)woort_vector_erase_at(&session->m_breakpoint_records, 0);
        }
    }
    woort_mutex_unlock(session->m_mx);
}

void woort_Debugger_refresh_breakpoints(void)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_refresh_breakpoints_locked(session);
    }
    woort_mutex_unlock(session->m_mx);
}

WOORT_NODISCARD bool woort_Debugger_query_breakpoint(
    woort_DebuggerBreakpointId id,
    bool* out_resolved,
    /* OPTIONAL */ uint32_t* out_line)
{
    if (out_resolved == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool found = false;

    woort_mutex_lock(session->m_mx);
    {
        const woort_DebuggerBreakpointRecord* record =
            _woort_Debugger_find_breakpoint_record(session, id);

        if (record != NULL)
        {
            *out_resolved = record->m_resolved;
            if (out_line != NULL)
                *out_line = record->m_resolved
                ? record->m_resolved_line
                : (record->m_is_function_bp ? 0 : record->m_line);
            found = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return found;
}

/* ========================================================================
 * Stack & variable queries
 * ======================================================================== */

/*
 * Resolve the stopped VM and walk its callstack to `depth`.  Returns the
 * VM and fills the trace; caller holds m_mx throughout.
 */
static bool _woort_Debugger_trace_frame_locked(
    woort_DebuggerSession* session,
    woort_DebuggerVmId vm,
    size_t depth,
    /* OPTIONAL */ woort_VMRuntime** out_vm,
    /* OPTIONAL */ woort_VMRuntime_TraceCallstack* out_trace)
{
    woort_VMRuntime* const stopped_vm =
        _woort_Debugger_locked_stopped_vm(session, vm);
    if (stopped_vm == NULL)
        return false;

    woort_VMRuntime_TraceCallstack_Iter iter;
    woort_VMRuntime_trace_begin(stopped_vm, &iter);

    woort_VMRuntime_TraceCallstack trace;
    size_t walked = 0;
    while (woort_VMRuntime_trace_next(&iter, &trace))
    {
        if (walked == depth)
        {
            if (out_vm != NULL)
                *out_vm = stopped_vm;
            if (out_trace != NULL)
                *out_trace = trace;
            return true;
        }
        ++walked;
    }
    return false;
}

WOORT_NODISCARD size_t woort_Debugger_get_stack_depth(
    woort_DebuggerVmId vm)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return 0;

    size_t depth = 0;

    woort_mutex_lock(session->m_mx);
    {
        woort_VMRuntime* const stopped_vm =
            _woort_Debugger_locked_stopped_vm(session, vm);
        if (stopped_vm != NULL)
        {
            woort_VMRuntime_TraceCallstack_Iter iter;
            woort_VMRuntime_TraceCallstack trace;
            woort_VMRuntime_trace_begin(stopped_vm, &iter);
            while (woort_VMRuntime_trace_next(&iter, &trace))
                ++depth;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return depth;
}

WOORT_NODISCARD bool woort_Debugger_get_stack_frame(
    woort_DebuggerVmId vm,
    size_t depth,
    woort_DebuggerFrame* out_frame)
{
    if (out_frame == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool ok = false;

    woort_mutex_lock(session->m_mx);
    {
        woort_VMRuntime_TraceCallstack trace;
        if (_woort_Debugger_trace_frame_locked(
            session, vm, depth, NULL, &trace))
        {
            memset(out_frame, 0, sizeof(*out_frame));

            woort_CodeEnv* frame_cenv = NULL;
            out_frame->m_is_script =
                trace.m_code_addr != NULL
                && woort_CodeEnv_find(trace.m_code_addr, &frame_cenv);

            if (trace.m_function_name != NULL)
                _woort_Debugger_copy_str(
                    out_frame->m_function_name,
                    sizeof(out_frame->m_function_name),
                    trace.m_function_name);

            if (trace.m_file_or_lib_name != NULL)
                _woort_Debugger_copy_str(
                    out_frame->m_file_or_lib_name,
                    sizeof(out_frame->m_file_or_lib_name),
                    trace.m_file_or_lib_name);

            if (trace.m_has_location)
            {
                /* 行号与 srcloc 同基（首行为 0）直通；列号为 1 基。 */
                out_frame->m_has_location = true;
                out_frame->m_line = (uint32_t)trace.m_location_begin[0];
                out_frame->m_column =
                    (uint32_t)trace.m_location_begin[1];
                out_frame->m_end_line =
                    (uint32_t)trace.m_location_end[0];
                out_frame->m_end_column =
                    (uint32_t)trace.m_location_end[1];
            }

            ok = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return ok;
}

/*
 * Resolve the frame's CodeEnv, frame base pointer and the enclosing
 * function's bytecode range.  Caller holds m_mx.
 */
typedef struct _woort_Debugger_FrameContext
{
    woort_CodeEnv* m_cenv;
    const woort_Value* m_frame_sb;
    uint32_t m_func_begin;
    uint32_t m_func_end;
    bool m_valid;

} _woort_Debugger_FrameContext;

static void _woort_Debugger_resolve_frame_context_locked(
    woort_DebuggerSession* session,
    woort_DebuggerVmId vm,
    size_t depth,
    _woort_Debugger_FrameContext* out_ctx)
{
    memset(out_ctx, 0, sizeof(*out_ctx));

    woort_VMRuntime* frame_vm = NULL;
    woort_VMRuntime_TraceCallstack trace;
    if (!_woort_Debugger_trace_frame_locked(
        session, vm, depth, &frame_vm, &trace))
    {
        return;
    }

    if (trace.m_code_addr == NULL)
        return;

    woort_CodeEnv* cenv = NULL;
    if (!woort_CodeEnv_find(trace.m_code_addr, &cenv) || cenv == NULL)
        return;

    const uint32_t frame_ip_offset =
        (uint32_t)(trace.m_code_addr - cenv->m_code_begin);

    uint32_t func_begin = 0;
    uint32_t func_end = (uint32_t)(cenv->m_code_end - cenv->m_code_begin);
    for (size_t j = 0; j < cenv->m_function_boundaries.m_size; ++j)
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

    out_ctx->m_cenv = cenv;
    out_ctx->m_frame_sb =
        frame_vm->m_stack_end - trace.m_callstack_offset_of_base;
    out_ctx->m_func_begin = func_begin;
    out_ctx->m_func_end = func_end;
    out_ctx->m_valid = true;
}

WOORT_NODISCARD size_t woort_Debugger_get_local_count(
    woort_DebuggerVmId vm,
    size_t depth)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return 0;

    size_t count = 0;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid)
        {
            for (size_t i = 0;
                i < ctx.m_cenv->m_pdb.m_local_var_debug_info.m_size;
                ++i)
            {
                const woort_LocalVarDebugInfo* info =
                    (const woort_LocalVarDebugInfo*)woort_vector_at(
                    (woort_Vector*)&ctx.m_cenv->m_pdb.m_local_var_debug_info,
                    i);

                if (info->m_name == NULL)
                    continue;
                if (info->m_function_offset < ctx.m_func_begin
                    || info->m_function_offset >= ctx.m_func_end)
                    continue;

                ++count;
            }
        }
    }
    woort_mutex_unlock(session->m_mx);

    return count;
}

/*
 * Walk the n-th named local of the frame.  Caller holds m_mx.
 */
static /* OPTIONAL */ const woort_LocalVarDebugInfo*
_woort_Debugger_index_local_locked(
    const _woort_Debugger_FrameContext* ctx,
    size_t index)
{
    size_t seen = 0;
    for (size_t i = 0;
        i < ctx->m_cenv->m_pdb.m_local_var_debug_info.m_size;
        ++i)
    {
        const woort_LocalVarDebugInfo* info =
            (const woort_LocalVarDebugInfo*)woort_vector_at(
            (woort_Vector*)&ctx->m_cenv->m_pdb.m_local_var_debug_info, i);

        if (info->m_name == NULL)
            continue;
        if (info->m_function_offset < ctx->m_func_begin
            || info->m_function_offset >= ctx->m_func_end)
            continue;

        if (seen == index)
            return info;
        ++seen;
    }
    return NULL;
}

WOORT_NODISCARD bool woort_Debugger_get_local_info(
    woort_DebuggerVmId vm,
    size_t depth,
    size_t index,
    woort_DebuggerVariableInfo* out_info)
{
    if (out_info == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool ok = false;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid)
        {
            const woort_LocalVarDebugInfo* info =
                _woort_Debugger_index_local_locked(&ctx, index);

            if (info != NULL)
            {
                _woort_Debugger_copy_str(
                    out_info->m_name, sizeof(out_info->m_name),
                    info->m_name);
                ok = true;
            }
        }
    }
    woort_mutex_unlock(session->m_mx);

    return ok;
}

WOORT_NODISCARD /* OPTIONAL */ char* woort_Debugger_get_local_value(
    woort_DebuggerVmId vm,
    size_t depth,
    size_t index)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return NULL;

    /* OPTIONAL */ char* result = NULL;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid)
        {
            const woort_LocalVarDebugInfo* info =
                _woort_Debugger_index_local_locked(&ctx, index);

            if (info != NULL)
            {
                result = _woort_Debugger_serialize_dynbox(
                    ctx.m_frame_sb[info->m_stack_offset].m_dynamic);
            }
        }
    }
    woort_mutex_unlock(session->m_mx);

    return result;
}

WOORT_NODISCARD size_t woort_Debugger_get_static_count(
    woort_DebuggerVmId vm,
    size_t depth)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return 0;

    size_t count = 0;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid)
            count = ctx.m_cenv->m_pdb.m_static_var_debug_info.m_size;
    }
    woort_mutex_unlock(session->m_mx);

    return count;
}

WOORT_NODISCARD bool woort_Debugger_get_static_info(
    woort_DebuggerVmId vm,
    size_t depth,
    size_t index,
    woort_DebuggerVariableInfo* out_info)
{
    if (out_info == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool ok = false;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid
            && index < ctx.m_cenv->m_pdb.m_static_var_debug_info.m_size)
        {
            const woort_StaticVarDebugInfo* info =
                (const woort_StaticVarDebugInfo*)woort_vector_at(
                (woort_Vector*)&ctx.m_cenv->m_pdb.m_static_var_debug_info,
                index);

            if (info->m_name != NULL)
            {
                _woort_Debugger_copy_str(
                    out_info->m_name, sizeof(out_info->m_name),
                    info->m_name);
            }
            else
            {
                (void)snprintf(
                    out_info->m_name, sizeof(out_info->m_name),
                    "<static#%u>", (unsigned)info->m_static_idx);
            }
            ok = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return ok;
}

WOORT_NODISCARD /* OPTIONAL */ char* woort_Debugger_get_static_value(
    woort_DebuggerVmId vm,
    size_t depth,
    size_t index)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return NULL;

    /* OPTIONAL */ char* result = NULL;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid
            && index < ctx.m_cenv->m_pdb.m_static_var_debug_info.m_size)
        {
            const woort_StaticVarDebugInfo* info =
                (const woort_StaticVarDebugInfo*)woort_vector_at(
                (woort_Vector*)&ctx.m_cenv->m_pdb.m_static_var_debug_info,
                index);

            const size_t global_index =
                ctx.m_cenv->m_const_records.m_size
                + (size_t)info->m_static_idx;

            if (global_index < ctx.m_cenv->m_data_count)
            {
                result = _woort_Debugger_serialize_dynbox(
                    ctx.m_cenv->m_data_begin[global_index].m_dynamic);
            }
        }
    }
    woort_mutex_unlock(session->m_mx);

    return result;
}

WOORT_NODISCARD /* OPTIONAL */ char* woort_Debugger_get_variable_value_by_name(
    woort_DebuggerVmId vm,
    size_t depth,
    const char* name,
    /* OPTIONAL */ woort_DebuggerVariableKind* out_kind)
{
    if (name == NULL)
        return NULL;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return NULL;

    /* OPTIONAL */ char* result = NULL;
    woort_DebuggerVariableKind kind =
        WOORT_DEBUGGER_VARIABLE_NOT_FOUND;

    woort_mutex_lock(session->m_mx);
    {
        _woort_Debugger_FrameContext ctx;
        _woort_Debugger_resolve_frame_context_locked(
            session, vm, depth, &ctx);

        if (ctx.m_valid)
        {
            /* Locals of the selected frame first. */
            for (size_t i = 0;
                i < ctx.m_cenv->m_pdb.m_local_var_debug_info.m_size;
                ++i)
            {
                const woort_LocalVarDebugInfo* info =
                    (const woort_LocalVarDebugInfo*)woort_vector_at(
                    (woort_Vector*)&ctx.m_cenv->m_pdb.m_local_var_debug_info,
                    i);

                if (info->m_name == NULL)
                    continue;
                if (strcmp(info->m_name, name) != 0)
                    continue;
                if (info->m_function_offset < ctx.m_func_begin
                    || info->m_function_offset >= ctx.m_func_end)
                    continue;

                kind = WOORT_DEBUGGER_VARIABLE_LOCAL;
                result = _woort_Debugger_serialize_dynbox(
                    ctx.m_frame_sb[info->m_stack_offset].m_dynamic);
                break;
            }

            /* Then statics of the frame's env. */
            if (result == NULL)
            {
                for (size_t i = 0;
                    i < ctx.m_cenv->m_pdb.m_static_var_debug_info.m_size;
                    ++i)
                {
                    const woort_StaticVarDebugInfo* info =
                        (const woort_StaticVarDebugInfo*)woort_vector_at(
                        (woort_Vector*)&ctx.m_cenv
                        ->m_pdb.m_static_var_debug_info, i);

                    if (info->m_name == NULL)
                        continue;
                    if (strcmp(info->m_name, name) != 0)
                        continue;

                    const size_t global_index =
                        ctx.m_cenv->m_const_records.m_size
                        + (size_t)info->m_static_idx;

                    if (global_index >= ctx.m_cenv->m_data_count)
                        continue;

                    kind = WOORT_DEBUGGER_VARIABLE_STATIC;
                    result = _woort_Debugger_serialize_dynbox(
                        ctx.m_cenv->m_data_begin[global_index].m_dynamic);
                    break;
                }
            }
        }
    }
    woort_mutex_unlock(session->m_mx);

    if (out_kind != NULL)
        *out_kind = result != NULL
        ? kind
        : WOORT_DEBUGGER_VARIABLE_NOT_FOUND;

    return result;
}

/* ========================================================================
 * Resume / stepping
 * ======================================================================== */

static bool _woort_Debugger_resume(
    woort_DebuggerSession* session,
    woort_DebuggerResumeAction action)
{
    if (session == NULL)
        return false;

    bool ok = false;

    woort_mutex_lock(session->m_mx);
    {
        if (!session->m_attached || session->m_detached
            || !session->m_stop_active || session->m_stop_vm == NULL)
        {
            woort_mutex_unlock(session->m_mx);
            return false;
        }

        woort_VMRuntime* const vm = session->m_stop_vm;

        const bool stop_by_panic =
            session->m_stop_reason == WOORT_DEBUGGER_STOP_REASON_PANIC;

        if (stop_by_panic)
        {
            /*
             * Resuming from a panic cannot re-execute the failing
             * instruction (it would panic again), so any resume action is
             * treated as "continue", and the VM is returned to the
             * standard panic outcome: set ABORT with the panic message at
             * m_sp, ending the call with WOORT_VM_CALL_STATUS_ABORTED.
             */
            _woort_Debugger_out_of_focus(session, vm);
            ok = true;
        }
        else
        {
            switch (action)
            {
            case _WOORT_DEBUGGER_RESUME_CONTINUE:
                _woort_Debugger_out_of_focus(session, vm);
                ok = true;
                break;

            case _WOORT_DEBUGGER_RESUME_STEP_INSTRUCTION:
            case _WOORT_DEBUGGER_RESUME_STEP_IN:
            case _WOORT_DEBUGGER_RESUME_STEP_OVER:
            case _WOORT_DEBUGGER_RESUME_STEP_OUT:
            {
                /* Mirrors the WAIPO step commands: locate the env, compute
                   the next ip, focus the VM and arm the step break.  Any
                   failure leaves the VM stopped so the host can retry or
                   fall back to continue. */
                woort_CodeEnv* cenv = NULL;
                if (!woort_CodeEnv_find(vm->m_ip, &cenv))
                    break;

                const woort_Bytecode* next_ip = NULL;
                if (!_woort_Debugger_get_next_ip(
                    vm->m_ip, cenv, vm->m_sb, vm, &next_ip))
                {
                    break;
                }

                if (!_woort_Debugger_focus_on(session, vm))
                    break;

                bool armed = false;
                switch (action)
                {
                case _WOORT_DEBUGGER_RESUME_STEP_INSTRUCTION:
                    armed = _woort_Debugger_set_step_break(
                        session, vm, next_ip);
                    break;
                case _WOORT_DEBUGGER_RESUME_STEP_IN:
                    armed = _woort_Debugger_set_step_source_break(
                        session, vm, next_ip);
                    break;
                case _WOORT_DEBUGGER_RESUME_STEP_OVER:
                    armed = _woort_Debugger_set_next_source_break(
                        session, vm, next_ip);
                    break;
                case _WOORT_DEBUGGER_RESUME_STEP_OUT:
                    armed = _woort_Debugger_set_return_break(
                        session, vm, next_ip);
                    break;
                default:
                    break;
                }

                ok = armed;
                break;
            }

            default:
                break;
            }
        }

        if (!ok)
        {
            woort_mutex_unlock(session->m_mx);
            return false;
        }

        if (stop_by_panic)
        {
            /* Re-arm the abort the panic handler cancelled, following the
               ABORT request convention (panic message at m_sp), so the
               resumed VM terminates the call as it would without the
               debugger. */
            const char* const msg =
                session->m_panic.m_message[0] != '\0'
                ? session->m_panic.m_message
                : "Panicked (debugger session).";
            vm->m_sp->m_string = woort_GCString_make_string(
                msg, strlen(msg));
            (void)woort_VMRuntime_request_set(
                vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT);
        }

        /* Drop interrupt requests that would otherwise re-stop the VM
           right after it resumes, and discard the panic record. */
        _woort_Debugger_clear_pending_interrupts(vm);
        session->m_has_panic = false;

        session->m_stop_active = false;
        session->m_stop_vm = NULL;
        session->m_action_ready = true;
        woort_condition_variable_broadcast(session->m_cv);
    }
    woort_mutex_unlock(session->m_mx);

    return true;
}

WOORT_NODISCARD bool woort_Debugger_continue(void)
{
    return _woort_Debugger_resume(
        _woort_Debugger_session_load(),
        _WOORT_DEBUGGER_RESUME_CONTINUE);
}

WOORT_NODISCARD bool woort_Debugger_step_instruction(void)
{
    return _woort_Debugger_resume(
        _woort_Debugger_session_load(),
        _WOORT_DEBUGGER_RESUME_STEP_INSTRUCTION);
}

WOORT_NODISCARD bool woort_Debugger_step_in(void)
{
    return _woort_Debugger_resume(
        _woort_Debugger_session_load(),
        _WOORT_DEBUGGER_RESUME_STEP_IN);
}

WOORT_NODISCARD bool woort_Debugger_step_over(void)
{
    return _woort_Debugger_resume(
        _woort_Debugger_session_load(),
        _WOORT_DEBUGGER_RESUME_STEP_OVER);
}

WOORT_NODISCARD bool woort_Debugger_step_out(void)
{
    return _woort_Debugger_resume(
        _woort_Debugger_session_load(),
        _WOORT_DEBUGGER_RESUME_STEP_OUT);
}

static bool _woort_Debugger_terminate_all_callback(
    woort_VMRuntime* vm, void* user_data)
{
    (void)user_data;
    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE);
    return true;
}

WOORT_NODISCARD bool woort_Debugger_terminate_all(void)
{
    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL || !session->m_attached)
        return false;

    woort_GC_foreach_root_vm(&_woort_Debugger_terminate_all_callback, NULL);

    /* The stopped VM is parked in the session stop hook and cannot observe
       the TERMINATE request until it is released; resume it (flags first,
       release second, or the VM could wake before the flag is set) so it
       aborts at its next request checkpoint and the session ends now
       instead of re-presenting the same stop. Without a stopped VM the
       resume is a no-op. */
    (void)woort_Debugger_continue();

    return true;
}

/* ========================================================================
 * Panic query
 * ======================================================================== */

WOORT_NODISCARD bool woort_Debugger_get_last_panic(
    woort_DebuggerPanicInfo* out_info)
{
    if (out_info == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool has = false;

    woort_mutex_lock(session->m_mx);
    {
        if (session->m_has_panic)
        {
            *out_info = session->m_panic;
            has = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return has;
}

/* ========================================================================
 * In-tree escape hatch
 * ======================================================================== */

WOORT_NODISCARD bool _woort_Debugger_session_take_stopped_vm(
    /* OPTIONAL */ woort_VMRuntime** out_vm)
{
    if (out_vm == NULL)
        return false;

    woort_DebuggerSession* const session = _woort_Debugger_session_load();
    if (session == NULL)
        return false;

    bool ok = false;

    woort_mutex_lock(session->m_mx);
    {
        if (session->m_stop_active && session->m_stop_vm != NULL)
        {
            *out_vm = session->m_stop_vm;
            ok = true;
        }
    }
    woort_mutex_unlock(session->m_mx);

    return ok;
}
