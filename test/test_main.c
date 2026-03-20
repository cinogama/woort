#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_gc_string.h"

#include <stdio.h>
#include <time.h>

woort_api print_int(woort_vm vm, woort_value* args)
{
    (void)printf("%lld\n", ((woort_Value*)args)->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_string(woort_vm vm, woort_value* args)
{
    const woort_GCString* const gcstr = ((woort_Value*)args)->m_string;

    for (size_t i = 0; i < gcstr->m_length; ++i)
        putchar(gcstr->m_content[i]);

    putchar('\n');
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_current_time(woort_vm vm, woort_value* args)
{
    vm->m_sb[2].m_integer = clock();
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api bar(woort_vm vm, woort_value* args)
{
    return WOORT_VM_CALL_STATUS_NORMAL;
}

int main(int argc, char** argv) {
    woort_init();

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
        woort_OpCode_RESULT(1, -2),
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
        woort_OpCode_PUSHSCHK(0),
        woort_OpCode_CALLNFP(4),
        woort_OpCode_POPR(1),
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
    codeenv->m_data_begin[3].m_integer = 3;
    codeenv->m_data_begin[4].m_native_or_jit_function = &print_int;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin + 15);

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_destroy(vm);

    woort_shutdown();
    return 0;
}