#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_disassembly.h"

#include <stdio.h>
#include <time.h>

woort_api print_int(woort_VMRuntime* vm)
{
    (void)vm;
    printf("%lld\n", (long long)woort_int(0));
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_string(woort_VMRuntime* vm)
{
    (void)vm;
    printf("%s\n", woort_string(0));
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_current_time(woort_VMRuntime* vm)
{
    (void)vm;
    woort_set_int(-1, (woort_Int)clock());
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api bar(woort_VMRuntime* vm)
{
    (void)vm;
    return WOORT_VM_CALL_STATUS_NORMAL;
}

void dump_Code(woort_CodeEnv* cenv)
{
    const woort_Bytecode* pc = cenv->m_code_begin;

    printf("\n");

    while (pc < cenv->m_code_end)
        pc = woort_Disassembly(pc);

    printf("\n");

    fflush(stdout);
}

int main(int argc, char** argv) {
    woort_init();

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_fib;
    woort_IRFunction* f_main;

    woort_IRConstantIndex c_f_fib = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_35 = woort_IRCompiler_add_constant(irc);
    {
        (void)woort_IRCompiler_add_function(irc, 1, &f_fib);
        {
            const woort_IRValue* n = woort_IRFunction_get_argument(f_fib, 0);
            const woort_IRValue* cv_1 = woort_IRFunction_load_const(f_fib, c_1);
            const woort_IRValue* cv_2 = woort_IRFunction_load_const(f_fib, c_2);

            woort_IRLabel* lb = woort_IRFunction_new_label(f_fib);

            (void)woort_IR_jcc_ge(f_fib, n, cv_2, lb);
            {
                (void)woort_IR_ret(f_fib, cv_1);
            }

            (void)woort_IR_bind(f_fib, lb);
            woort_IRValue* n0 = woort_IRFunction_new_vreg(f_fib);
            (void)woort_IR_SUBI(f_fib, n0, n, cv_1);
            (void)woort_IR_PUSHCHK(f_fib, n0);
            woort_IRValue* r0 = woort_IRFunction_new_vreg(f_fib);
            (void)woort_IR_CALLNWO(f_fib, c_f_fib, 1, r0);

            woort_IRValue* n1 = woort_IRFunction_new_vreg(f_fib);
            (void)woort_IR_SUBI(f_fib, n1, n, cv_2);
            (void)woort_IR_PUSHCHK(f_fib, n1);
            woort_IRValue* r1 = woort_IRFunction_new_vreg(f_fib);
            (void)woort_IR_CALLNWO(f_fib, c_f_fib, 1, r1);

            woort_IRValue* r = woort_IRFunction_new_vreg(f_fib);
            (void)woort_IR_ADDI(f_fib, r, r0, r1);

            (void)woort_IR_ret(f_fib, r);
        }

        (void)woort_IRCompiler_add_function(irc, 0, &f_main);
        {
            (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c_35));

            woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_CALLNWO(f_main, c_f_fib, 1, v);
            (void)woort_IR_ret(f_main, v);
        }
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* fib_addr;
    (void)woort_CodeEnv_query_function(cenv, f_fib, &fib_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_function(cenv, c_f_fib, fib_addr);
    woort_CodeEnv_set_const_int(cenv, c_1, 1);
    woort_CodeEnv_set_const_int(cenv, c_2, 2);
    woort_CodeEnv_set_const_int(cenv, c_35, 35);
    woort_CodeEnv_unlock(cenv);

    dump_Code(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    const clock_t b0 = clock();
    (void)woort_VMRuntime_invoke(vm, main_addr);
    const clock_t e0 = clock();

    printf("%lld\n", (long long)vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    /*
     * Raw bytecode fib benchmark
     *
     * Function: fib(n: int)=> int
     * PUSHRCHK    3                           ; Reserving stack.
     * LOAD        [SB - 0] = G[0]             ; Load 2
     * LOAD        [SB - 1] = G[1]             ; Load 1
     * JFWDEG      +2 IF [SB + 3] >= [SB - 0]  ; Jump if arg0 >= 2
     * RETVS       [SB - 1]                    ; Return 1
     * SUBI        [SB - 0] = [SB + 3] - [SB - 1]
     * SUBI        [SB - 2] = [SB - 0] - [SB - 1]
     * PUSHSCHK    [SB - 0]
     * CALLNWO     G[2]                        ; Call fib(n - 1)
     * RESULT      [SB - 0], POP 1
     * PUSHCHK     [SB - 2]
     * CALLNWO     G[2]                        ; Call fib(n - 2)
     * RESULT      [SB - 2], POP 1
     * CADDI       [SB - 2] += [SB - 0]
     * RETVS       [SB - 2]
     *
     * Function: main()=> void
     * PUSHRCHK    1                           ; Reserving stack.
     * PUSHCCHK    G[3]                        ; Push 35
     * CALLNWO     G[2]                        ; Call fib(35)
     * RESULT      [SB - 0], POP 1
     * RET
     */
    const woort_Bytecode bcs[] =
    {
        woort_OpCode_PUSHRCHK(3),
        woort_OpCode_LOAD(0, 0),
        woort_OpCode_LOAD(1, -1),
        woort_OpCode_JFWDEG(3, 0, 2),
        woort_OpCode_RETVS(-1),
        woort_OpCode_SUBI(3, -1, 0),
        woort_OpCode_SUBI(0, -1, -2),
        woort_OpCode_PUSHSCHK(0),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, 0),
        woort_OpCode_PUSHSCHK(-2),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, -2),
        woort_OpCode_CADDI(0, -2),
        woort_OpCode_RETVS(-2),

        woort_OpCode_PUSHRCHK(1),
        woort_OpCode_PUSHCCHK(3),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, 0),
        woort_OpCode_RET(),
    };

    woort_CodeEnv* codeenv;
    (void)woort_CodeEnv_create(
        bcs,
        sizeof(bcs) / sizeof(woort_Bytecode),
        4,
        &codeenv);

    woort_CodeEnv_lock(codeenv);
    woort_CodeEnv_set_const_int(codeenv, 0, 2);
    woort_CodeEnv_set_const_int(codeenv, 1, 1);
    woort_CodeEnv_set_const_script_function(codeenv, 2, codeenv->m_code_begin);
    woort_CodeEnv_set_const_int(codeenv, 3, 35);
    woort_CodeEnv_unlock(codeenv);

    (void)woort_VMRuntime_create(&vm);

    const clock_t b1 = clock();
    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin + 15);
    const clock_t e1 = clock();

    printf("%d, %d", (int)(e0 - b0), (int)(e1 - b1));

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_destroy(vm);

    woort_shutdown();
    return 0;
}
