#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_gc_string.h"
#include "woort_opcode_builder.h"
#include "woort_ir_compiler.h"
#include "woort_disassembly.h"

#include <stdio.h>
#include <time.h>

woort_api print_int(woort_VMRuntime* vm)
{
    (void)printf("%lld\n", ((woort_Value*)args)->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_string(woort_VMRuntime* vm)
{
    const woort_GCString* const gcstr = ((woort_Value*)args)->m_string;

    for (size_t i = 0; i < gcstr->m_length; ++i)
        putchar(gcstr->m_content[i]);

    putchar('\n');
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_current_time(woort_VMRuntime* vm, woort_value* args)
{
    vm->m_sb[2].m_integer = clock();
    return WOORT_VM_CALL_STATUS_NORMAL;
} 

woort_api bar(woort_VMRuntime* vm, woort_value* args)
{
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

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRFunction* f_fib;
    woort_IRFunction* f_main;

    woort_IRConstantIndex c_f_fib = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_2 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_40 = woort_IRCompiler_add_constant(&irc);
    {
        (void)woort_IRCompiler_add_function(&irc, 1, &f_fib);
        {
            woort_IRValue* n = woort_IRFunction_get_argument(f_fib, 0);
            woort_IRValue* cv_1 = woort_IRFunction_load_const(f_fib, c_1);
            woort_IRValue* cv_2 = woort_IRFunction_load_const(f_fib, c_2);

            woort_IRLabel* lb = woort_IRFunction_new_label(f_fib);

            (void)woort_IR_jcc_ge(f_fib, n, cv_2, lb);
            {
                (void)woort_IR_ret(f_fib, cv_1);
            }

            (void)woort_IR_bind(f_fib, lb);
            woort_IRValue* n0 = woort_IRFunction_new_vreg(f_fib);
            woort_IR_SUBI(f_fib, n0, n, cv_1);
            woort_IR_PUSHCHK(f_fib, n0);
            woort_IRValue* r0 = woort_IRFunction_new_vreg(f_fib);
            woort_IR_CALLNWO(f_fib, c_f_fib, 1, r0);

            woort_IRValue* n1 = woort_IRFunction_new_vreg(f_fib);
            woort_IR_SUBI(f_fib, n1, n, cv_2);
            woort_IR_PUSHCHK(f_fib, n1);
            woort_IRValue* r1 = woort_IRFunction_new_vreg(f_fib);
            woort_IR_CALLNWO(f_fib, c_f_fib, 1, r1);

            woort_IRValue* r = woort_IRFunction_new_vreg(f_fib);
            woort_IR_ADDI(f_fib, r, r0, r1);

            (void)woort_IR_ret(f_fib, r);
        }

        (void)woort_IRCompiler_add_function(&irc, 0, &f_main);
        {
            (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c_40));

            woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_CALLNWO(f_main, c_f_fib, 1, v);
            (void)woort_IR_ret(f_main, v);
        }
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(&irc, &cenv);

    cenv->m_data_begin[c_f_fib].m_script_function = cenv->m_code_begin + f_fib->m_code_offset;
    cenv->m_data_begin[c_1].m_integer = 1;
    cenv->m_data_begin[c_2].m_integer = 2;
    cenv->m_data_begin[c_40].m_integer = 40;

    dump_Code(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    const clock_t b0 = clock();
    (void)woort_VMRuntime_invoke(vm, cenv->m_code_begin + f_main->m_code_offset);
    const clock_t e0 = clock();

    printf("%lld\n", vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);

    const woort_Bytecode bcs[] =
    {
        // Function: fib(n: int)=> int
        /*
        PUSHRCHK    3                           ; Reserving stack.
        LOAD        [SB - 0] = G[0]             ; Load 2
        LOAD        [SB - 1] = G[1]             ; Load 1
        JFWDEG      +2 IF [SB + 3] >= [SB - 0]  ; Jump if arg0 >= 2
        RETVS       [SB - 1]                    ; Return 1
        SUBI        [SB - 0] = [SB + 3] - [SB - 1]
        SUBI        [SB - 2] = [SB - 0] - [SB - 1]
        PUSHSCHK    [SB - 0]
        CALLNWO     G[2]                        ; Call fib(n - 1)
        RESULT      [SB - 0], POP 1
        PUSHCHK     [SB - 2]
        CALLNWO     G[2]                        ; Call fib(n - 2)
        RESULT      [SB - 2], POP 1
        CADDI       [SB - 2] += [SB - 0]
        RETVS       [SB - 2]
        */
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

        // Function: main()=> void
/*
        PUSHRCHK    1                           ; Reserving stack.
        PUSHCCHK    G[3]                        ; Push 5
        CALLNWO     G[2]                        ; Call fib(5)
        RESULT      [SB - 0], POP 1
        PUSHSCHK    [SB - 0]
        CALLNFP     G[4]                        ; Call print_int(fib(5))
        POPR        1
        RET
*/
        woort_OpCode_PUSHRCHK(1),
        woort_OpCode_PUSHCCHK(3),
        woort_OpCode_CALLNWO(2),
        woort_OpCode_RESULT(1, 0),
        //woort_OpCode_PUSHSCHK(0),
        //woort_OpCode_CALLNFP(4),
        //woort_OpCode_POPR(1),
        woort_OpCode_RET(),
    };

    woort_CodeEnv* codeenv;
    (void)woort_CodeEnv_create(
        bcs,
        sizeof(bcs) / sizeof(woort_Bytecode),
        5,
        &codeenv);

    codeenv->m_data_begin[0].m_integer = 2;
    codeenv->m_data_begin[1].m_integer = 1;
    codeenv->m_data_begin[2].m_script_function = codeenv->m_code_begin + 0;
    codeenv->m_data_begin[3].m_integer = 40;
    codeenv->m_data_begin[4].m_native_or_jit_function = &print_int;

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