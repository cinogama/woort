/*
 * test_debugger_session.c
 *
 * End-to-end exercise of the public debugger session API
 * (woort_Debugger_*): breakpoints, stop events, stack/variable queries,
 * stepping, interrupts, panic routing and detach, driven from the main
 * thread while a worker thread runs the program - the same shape a DAP
 * adapter would use.
 */

#include "woort.h"
#include "woort_threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            ++g_failures;                                               \
            fprintf(stderr, "CHECK FAILED %s:%d: %s\n",                 \
                __FILE__, __LINE__, #cond);                             \
        }                                                               \
    } while (0)

#define WAIT_TIMEOUT_MS 5000

/* ====================================================================
 * Worker that runs an entry closure on its own VM/thread.
 * ==================================================================== */

typedef struct RunContext
{
    woort_CodeEnv* m_cenv;
    woort_IRConstantIndex m_entry_cidx;
    woort_VmCallStatus m_status;
    volatile bool m_running;   /* set once the VM is created and invoking */

} RunContext;

static void wait_until_running(const RunContext* ctx)
{
    for (int i = 0; i < 1000 && !ctx->m_running; ++i)
        woort_thread_sleep_ms(5);
    CHECK(ctx->m_running);
}

static bool value_starts_with(const char* value, const char* prefix)
{
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

static void run_worker(void* user_data)
{
    RunContext* const ctx = (RunContext*)user_data;

    woort_VMRuntime* vm = NULL;
    if (!woort_VMRuntime_create(&vm))
    {
        ctx->m_status = WOORT_VM_CALL_STATUS_ABORTED;
        return;
    }

    (void)woort_VMRuntime_swap(vm);
    {
        woort_StackValue sv;
        (void)woort_push_reserve(2, &sv);
        woort_load_const(sv, ctx->m_cenv, ctx->m_entry_cidx);
        ctx->m_running = true;
        ctx->m_status = woort_invoke(sv + 1, sv);
        ctx->m_running = false;
        woort_pop(2);
    }
    (void)woort_VMRuntime_swap(NULL);

    woort_VMRuntime_destroy(vm);
}

static void start_program_on_worker(
    RunContext* ctx, woort_CodeEnv* cenv,
    woort_IRConstantIndex entry_cidx,
    woort_Thread** out_thread)
{
    ctx->m_cenv = cenv;
    ctx->m_entry_cidx = entry_cidx;
    ctx->m_status = WOORT_VM_CALL_STATUS_NORMAL;
    ctx->m_running = false;

    CHECK(woort_thread_start(&run_worker, ctx, out_thread));
    wait_until_running(ctx);
}

/* ====================================================================
 * Program 1: add(a, b) called from main, with debug info.
 *
 *   10:  sum = a + b        (add)
 *   11:  return sum         (add)
 *   20:  push 10, 20        (main)
 *   21:  r = add(10, 20)    (main)
 *   22:  g_counter = r      (main)
 *   23:  return r           (main)
 * ==================================================================== */

typedef struct ArithProgram
{
    woort_CodeEnv* m_cenv;
    woort_IRConstantIndex m_entry_cidx;

} ArithProgram;

static bool compile_arith_program(ArithProgram* out)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();
    const char* path = woort_IRCompiler_intern_string(irc, "dbgtest.wo");

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRStaticIndex s_counter = woort_IRCompiler_add_static(irc);

    woort_IRCompiler_record_static_var(irc, "g_counter", s_counter);

    woort_IRFunction* f_add = NULL;
    CHECK(woort_IRCompiler_add_function(irc, 2, 0, &f_add));
    woort_IRFunction_set_name(f_add, "add");
    {
        woort_IRValue* a = woort_IRFunction_get_argument(f_add, 0);
        woort_IRValue* b = woort_IRFunction_get_argument(f_add, 1);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f_add);
        CHECK(a != NULL && b != NULL && sum != NULL);

        woort_IRFunction_record_local_var(f_add, "a", a);
        woort_IRFunction_record_local_var(f_add, "b", b);
        woort_IRFunction_record_local_var(f_add, "sum", sum);

        CHECK(woort_IRFunction_push_srcloc(f_add, path, 10, 1, 10, 20));
        CHECK(woort_IR_ADDI(f_add, sum, a, b));
        woort_IRFunction_pop_srcloc(f_add);

        CHECK(woort_IRFunction_push_srcloc(f_add, path, 11, 1, 11, 10));
        CHECK(woort_IR_ret(f_add, sum));
        woort_IRFunction_pop_srcloc(f_add);
    }

    woort_IRFunction* f_main = NULL;
    CHECK(woort_IRCompiler_add_function(irc, 0, 0, &f_main));
    woort_IRFunction_set_name(f_main, "main");
    {
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f_main, c10);
        const woort_IRValue* v20 = woort_IRFunction_fetch_const(f_main, c20);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(v10 != NULL && v20 != NULL && r != NULL);

        CHECK(woort_IRFunction_push_srcloc(f_main, path, 20, 1, 20, 20));
        /* The calling convention expects arguments pushed in reverse:
           argN-1 first, so that arg0 ends up nearest the frame base. */
        CHECK(woort_IR_PUSHCHK(f_main, v20));
        CHECK(woort_IR_PUSHCHK(f_main, v10));
        woort_IRFunction_pop_srcloc(f_main);

        CHECK(woort_IRFunction_push_srcloc(f_main, path, 21, 1, 21, 20));
        CHECK(woort_IR_CALLNWO(f_main, cfn, 2, r));
        woort_IRFunction_pop_srcloc(f_main);

        CHECK(woort_IRFunction_push_srcloc(f_main, path, 22, 1, 22, 20));
        CHECK(woort_IR_STORE(f_main, s_counter, r));
        woort_IRFunction_pop_srcloc(f_main);

        CHECK(woort_IRFunction_push_srcloc(f_main, path, 23, 1, 23, 10));
        CHECK(woort_IR_ret(f_main, r));
        woort_IRFunction_pop_srcloc(f_main);
    }

    woort_CodeEnv* cenv = NULL;
    if (!woort_IRCompiler_finish(irc, &cenv))
    {
        woort_IRCompiler_close(irc);
        return false;
    }

    const woort_Bytecode* add_addr = NULL;
    const woort_Bytecode* main_addr = NULL;
    (void)woort_CodeEnv_query_function(cenv, f_add, &add_addr);
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c20, 20);
    woort_CodeEnv_set_const_script_function(cenv, cfn, add_addr);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, main_addr);
    woort_CodeEnv_unlock(cenv);

    woort_IRCompiler_close(irc);

    out->m_cenv = cenv;
    out->m_entry_cidx = c_entry;
    return true;
}

/* ====================================================================
 * Program 2: a panic (division by zero) at line 5.
 * ==================================================================== */

typedef struct PanicProgram
{
    woort_CodeEnv* m_cenv;
    woort_IRConstantIndex m_entry_cidx;

} PanicProgram;

static bool compile_panic_program(PanicProgram* out)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();
    const char* path = woort_IRCompiler_intern_string(irc, "panic.wo");

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f = NULL;
    CHECK(woort_IRCompiler_add_function(irc, 0, 0, &f));
    woort_IRFunction_set_name(f, "boom");
    {
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        CHECK(v0 != NULL);

        CHECK(woort_IRFunction_push_srcloc(f, path, 5, 1, 5, 10));
        CHECK(woort_IR_CHKDIVIRZ(f, v0));
        woort_IRFunction_pop_srcloc(f);

        CHECK(woort_IR_ret_void(f));
    }

    woort_CodeEnv* cenv = NULL;
    if (!woort_IRCompiler_finish(irc, &cenv))
    {
        woort_IRCompiler_close(irc);
        return false;
    }

    const woort_Bytecode* f_addr = NULL;
    (void)woort_CodeEnv_query_function(cenv, f, &f_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, f_addr);
    woort_CodeEnv_unlock(cenv);

    woort_IRCompiler_close(irc);

    out->m_cenv = cenv;
    out->m_entry_cidx = c_entry;
    return true;
}

/* ====================================================================
 * Program 3: a long bounded loop (interruptible, then finishes).
 *
 *   3:  while (i < 2000000000) { i = i + 1; }
 * ==================================================================== */

typedef struct SpinProgram
{
    woort_CodeEnv* m_cenv;
    woort_IRConstantIndex m_entry_cidx;

} SpinProgram;

static bool compile_spin_program(SpinProgram* out)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();
    const char* path = woort_IRCompiler_intern_string(irc, "spin.wo");

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_limit = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f = NULL;
    CHECK(woort_IRCompiler_add_function(irc, 0, 0, &f));
    woort_IRFunction_set_name(f, "spin");
    {
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v_limit = woort_IRFunction_fetch_const(f, c_limit);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRLabel* top = woort_IRFunction_new_label(f);
        woort_IRLabel* end = woort_IRFunction_new_label(f);
        CHECK(v0 != NULL && v1 != NULL && v_limit != NULL && i != NULL);
        CHECK(top != NULL && end != NULL);

        CHECK(woort_IR_MOV(f, i, v0));

        CHECK(woort_IR_bind(f, top));
        CHECK(woort_IRFunction_push_srcloc(f, path, 3, 1, 3, 30));
        CHECK(woort_IR_jcc_ge(f, i, v_limit, end));
        CHECK(woort_IR_ADDI(f, i, i, v1));
        CHECK(woort_IR_jmp(f, top));
        woort_IRFunction_pop_srcloc(f);

        CHECK(woort_IR_bind(f, end));
        CHECK(woort_IR_ret_void(f));
    }

    woort_CodeEnv* cenv = NULL;
    if (!woort_IRCompiler_finish(irc, &cenv))
    {
        woort_IRCompiler_close(irc);
        return false;
    }

    const woort_Bytecode* f_addr = NULL;
    (void)woort_CodeEnv_query_function(cenv, f, &f_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c_limit, 2000000000LL);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, f_addr);
    woort_CodeEnv_unlock(cenv);

    woort_IRCompiler_close(irc);

    out->m_cenv = cenv;
    out->m_entry_cidx = c_entry;
    return true;
}

/* ====================================================================
 * Tests
 * ==================================================================== */

static void test_source_breakpoint_stop_and_inspect(void)
{
    ArithProgram prog;
    CHECK(compile_arith_program(&prog));

    woort_DebuggerBreakpointId bp = 0;
    CHECK(woort_Debugger_set_source_breakpoint("dbgtest.wo", 11, &bp));
    CHECK(bp != 0);

    bool resolved = false;
    uint32_t resolved_line = 0;
    CHECK(woort_Debugger_query_breakpoint(bp, &resolved, &resolved_line));
    CHECK(resolved);
    CHECK(resolved_line == 11);

    RunContext run;
    woort_Thread* worker = NULL;
    start_program_on_worker(
        &run, prog.m_cenv, prog.m_entry_cidx, &worker);

    woort_DebuggerBreakEvent event;
    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &event));
    CHECK(event.m_reason == WOORT_DEBUGGER_STOP_REASON_BREAKPOINT);

    /* Level-triggered: the same event is reported again. */
    woort_DebuggerBreakEvent again;
    CHECK(woort_Debugger_get_current_break(&again));
    CHECK(again.m_vm == event.m_vm);

    /* VM listing knows the stopped VM. */
    CHECK(woort_Debugger_get_vm_count() == 1);
    woort_DebuggerVmInfo vi;
    CHECK(woort_Debugger_get_vm_info(0, &vi));
    CHECK(vi.m_id == event.m_vm);
    CHECK(vi.m_is_stopped);

    /* Frames: add over main. */
    const size_t depth = woort_Debugger_get_stack_depth(event.m_vm);
    CHECK(depth >= 2);

    woort_DebuggerFrame frame0;
    CHECK(woort_Debugger_get_stack_frame(event.m_vm, 0, &frame0));
    CHECK(frame0.m_is_script);
    CHECK(frame0.m_has_location);
    CHECK(strcmp(frame0.m_function_name, "add") == 0);
    CHECK(strcmp(frame0.m_file_or_lib_name, "dbgtest.wo") == 0);
    CHECK(frame0.m_line == 11);

    woort_DebuggerFrame frame1;
    CHECK(woort_Debugger_get_stack_frame(event.m_vm, 1, &frame1));
    CHECK(strcmp(frame1.m_function_name, "main") == 0);

    CHECK(!woort_Debugger_get_stack_frame(event.m_vm, depth + 8, &frame1));

    /* Locals of frame 0: a, b, sum (sum computed by now). */
    const size_t locals = woort_Debugger_get_local_count(event.m_vm, 0);
    CHECK(locals == 3);

    woort_DebuggerVariableInfo info;
    CHECK(woort_Debugger_get_local_info(event.m_vm, 0, 0, &info));

    woort_DebuggerVariableKind kind = WOORT_DEBUGGER_VARIABLE_NOT_FOUND;
    char* value = woort_Debugger_get_variable_value_by_name(
        event.m_vm, 0, "a", &kind);
    CHECK(value != NULL);
    CHECK(kind == WOORT_DEBUGGER_VARIABLE_LOCAL);
    fprintf(stderr, "[dbg] a = %s\n", value);
    CHECK(value_starts_with(value, "10"));
    woort_free(value);

    value = woort_Debugger_get_variable_value_by_name(
        event.m_vm, 0, "b", &kind);
    CHECK(value != NULL);
    fprintf(stderr, "[dbg] b = %s\n", value);
    CHECK(value_starts_with(value, "20"));
    woort_free(value);

    value = woort_Debugger_get_variable_value_by_name(
        event.m_vm, 0, "sum", &kind);
    CHECK(value != NULL);
    CHECK(value_starts_with(value, "30"));
    woort_free(value);

    value = woort_Debugger_get_variable_value_by_name(
        event.m_vm, 0, "no_such_var", &kind);
    CHECK(value == NULL);
    CHECK(kind == WOORT_DEBUGGER_VARIABLE_NOT_FOUND);

    /* Statics of the frame's env. */
    const size_t statics = woort_Debugger_get_static_count(event.m_vm, 0);
    CHECK(statics >= 1);

    CHECK(woort_Debugger_get_static_info(event.m_vm, 0, 0, &info));
    CHECK(strcmp(info.m_name, "g_counter") == 0);

    value = woort_Debugger_get_static_value(event.m_vm, 0, 0);
    CHECK(value != NULL);
    woort_free(value);

    /* Step over from `return sum` lands the caller in main. */
    CHECK(woort_Debugger_step_over());

    woort_DebuggerBreakEvent step_event;
    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &step_event));
    CHECK(step_event.m_reason == WOORT_DEBUGGER_STOP_REASON_STEP);
    CHECK(woort_Debugger_get_stack_depth(step_event.m_vm) == 1);

    CHECK(woort_Debugger_get_stack_frame(step_event.m_vm, 0, &frame0));
    CHECK(strcmp(frame0.m_function_name, "main") == 0);

    CHECK(woort_Debugger_remove_breakpoint(bp));
    CHECK(!woort_Debugger_remove_breakpoint(bp));

    CHECK(woort_Debugger_continue());
    woort_thread_join(worker);

    CHECK(run.m_status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_CodeEnv_drop(prog.m_cenv);
}

static void test_function_breakpoint(void)
{
    ArithProgram prog;
    CHECK(compile_arith_program(&prog));

    woort_DebuggerBreakpointId bp = 0;
    CHECK(woort_Debugger_set_function_breakpoint("add", &bp));

    bool resolved = false;
    CHECK(woort_Debugger_query_breakpoint(bp, &resolved, NULL));
    CHECK(resolved);

    RunContext run;
    woort_Thread* worker = NULL;
    start_program_on_worker(
        &run, prog.m_cenv, prog.m_entry_cidx, &worker);

    woort_DebuggerBreakEvent event;
    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &event));
    CHECK(event.m_reason == WOORT_DEBUGGER_STOP_REASON_BREAKPOINT);

    woort_DebuggerFrame frame;
    CHECK(woort_Debugger_get_stack_frame(event.m_vm, 0, &frame));
    CHECK(strcmp(frame.m_function_name, "add") == 0);
    CHECK(frame.m_line == 10); /* function entry */

    CHECK(woort_Debugger_remove_breakpoint(bp));
    CHECK(woort_Debugger_continue());
    woort_thread_join(worker);

    CHECK(run.m_status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_CodeEnv_drop(prog.m_cenv);
}

static void test_panic_routing(void)
{
    PanicProgram prog;
    CHECK(compile_panic_program(&prog));

    RunContext run;
    woort_Thread* worker = NULL;
    start_program_on_worker(
        &run, prog.m_cenv, prog.m_entry_cidx, &worker);

    woort_DebuggerBreakEvent event;
    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &event));
    CHECK(event.m_reason == WOORT_DEBUGGER_STOP_REASON_PANIC);

    woort_DebuggerPanicInfo panic;
    CHECK(woort_Debugger_get_last_panic(&panic));
    CHECK(panic.m_vm == event.m_vm);
    CHECK(panic.m_reason == (int)WOORT_PANIC_INTEGER_DIV_FAIL);
    CHECK(strstr(panic.m_message, "Divisor") != NULL);

    woort_DebuggerFrame frame;
    CHECK(woort_Debugger_get_stack_frame(event.m_vm, 0, &frame));
    CHECK(strcmp(frame.m_function_name, "boom") == 0);

    CHECK(woort_Debugger_continue());
    woort_thread_join(worker);

    CHECK(run.m_status == WOORT_VM_CALL_STATUS_ABORTED);

    woort_CodeEnv_drop(prog.m_cenv);
}

static void test_interrupts(void)
{
    SpinProgram prog;
    CHECK(compile_spin_program(&prog));

    RunContext run;
    woort_Thread* worker = NULL;
    start_program_on_worker(
        &run, prog.m_cenv, prog.m_entry_cidx, &worker);

    woort_DebuggerVmId vm_id = 0;
    {
        CHECK(woort_Debugger_get_vm_count() == 1);
        woort_DebuggerVmInfo vi;
        CHECK(woort_Debugger_get_vm_info(0, &vi));
        CHECK(!vi.m_is_stopped);
        vm_id = vi.m_id;
    }

    /* interrupt_all */
    woort_Debugger_interrupt_all();

    woort_DebuggerBreakEvent event;
    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &event));
    CHECK(event.m_reason == WOORT_DEBUGGER_STOP_REASON_INTERRUPT);
    CHECK(event.m_vm == vm_id);

    woort_DebuggerFrame frame;
    CHECK(woort_Debugger_get_stack_frame(event.m_vm, 0, &frame));
    CHECK(strcmp(frame.m_function_name, "spin") == 0);

    /* Interrupting the stopped VM is a no-op. */
    CHECK(!woort_Debugger_interrupt_vm(vm_id));

    CHECK(woort_Debugger_continue());
    woort_thread_sleep_ms(100);

    /* interrupt_vm by id */
    CHECK(woort_Debugger_interrupt_vm(vm_id));
    CHECK(!woort_Debugger_interrupt_vm(vm_id + 12345));

    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &event));
    CHECK(event.m_reason == WOORT_DEBUGGER_STOP_REASON_INTERRUPT);

    CHECK(woort_Debugger_continue());
    woort_thread_join(worker);

    CHECK(run.m_status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_CodeEnv_drop(prog.m_cenv);
}

static void test_terminate_all_while_running(void)
{
    SpinProgram prog;
    CHECK(compile_spin_program(&prog));

    RunContext run;
    woort_Thread* worker = NULL;
    start_program_on_worker(
        &run, prog.m_cenv, prog.m_entry_cidx, &worker);

    /* Terminate a VM that is freely running: no stop is presented, the
       worker observes the request at its next checkpoint and aborts. */
    CHECK(woort_Debugger_terminate_all());
    woort_thread_join(worker);

    CHECK(run.m_status != WOORT_VM_CALL_STATUS_NORMAL);

    woort_CodeEnv_drop(prog.m_cenv);
}

static void test_terminate_all_while_stopped(void)
{
    SpinProgram prog;
    CHECK(compile_spin_program(&prog));

    RunContext run;
    woort_Thread* worker = NULL;
    start_program_on_worker(
        &run, prog.m_cenv, prog.m_entry_cidx, &worker);

    /* Stop first: the parked VM must be released by terminate_all instead
       of re-presenting the same stop. */
    woort_Debugger_interrupt_all();

    woort_DebuggerBreakEvent event;
    CHECK(woort_Debugger_wait_for_break(WAIT_TIMEOUT_MS, &event));
    CHECK(event.m_reason == WOORT_DEBUGGER_STOP_REASON_INTERRUPT);

    CHECK(woort_Debugger_terminate_all());
    woort_thread_join(worker);

    CHECK(run.m_status != WOORT_VM_CALL_STATUS_NORMAL);
    CHECK(!woort_Debugger_get_current_break(&event));

    woort_CodeEnv_drop(prog.m_cenv);
}

int main(void)
{
    char* argv_dbg[] = {
        "test_debugger_session",
        "--woort-enable-ctrlc-debug", "0",
    };
    woort_init(3, argv_dbg);

    CHECK(woort_Debugger_attach()
        == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS);
    CHECK(woort_Debugger_attach()
        == WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED);
    CHECK(woort_Debugger_is_attached());

    /* current_vm_id reports whether a VM is current on the calling
       thread (this thread runs none) and only writes the id then. Ids
       are valid from 0, so no sentinel is used. */
    woort_DebuggerVmId probe_id = 12345;
    CHECK(!woort_Debugger_current_vm_id(&probe_id));
    CHECK(probe_id == 12345);

    test_source_breakpoint_stop_and_inspect();
    test_function_breakpoint();
    test_panic_routing();
    test_interrupts();
    test_terminate_all_while_running();
    test_terminate_all_while_stopped();

    CHECK(woort_Debugger_detach());
    CHECK(!woort_Debugger_is_attached());
    CHECK(!woort_Debugger_detach());

    woort_DebuggerBreakEvent event;
    CHECK(!woort_Debugger_wait_for_break(0, &event));

    woort_shutdown(NULL, NULL);

    if (g_failures == 0)
    {
        printf("test_debugger_session: all checks passed.\n");
        return 0;
    }

    printf("test_debugger_session: %d check(s) failed.\n", g_failures);
    return 1;
}
