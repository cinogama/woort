#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"

#include <stdio.h>
#include <time.h>

woort_api print_int(woort_vm vm, woort_value* args)
{
    (void)printf("%lld\n", ((woort_Value*)args)->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

woort_api print_string(woort_vm vm, woort_value* args)
{
    woort_GCString* const gcstr = ((woort_Value*)args)->m_string;

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
        // 0:       PUSHRCHK 5
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 0, 5),
        // 1:       LOAD G[0], [SB-0]
        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 0, 0),
        // 2:       LOAD G[1], [SB-1]
        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 1, -1),
        // 3:       LOAD G[2], [SB-2]
        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 2, -2),

        // 4:       CALL G[4]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 4),
        // 5:       RESULT POP 0 [SB-3]
        woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_RESULT, 0, -3),

        // 6:       JMPF +8
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JMP, 8),
        // 7:       CADDI [SB-2], [SB-0]
        woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 0, -2, 0),
        // 8:       JBCONDLT +7 if [SB-0] < [SB-1]
        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JCMPGC, 0, 0, -1, 1),

        // 9:       CALL G[4]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 4),
        // 10:      RESULT POP 0 [SB-4]
        woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_RESULT, 0, -4),

        // 11:      PUSHSCHK [SB-0]
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 1, 0),
        // 12:      CALL G[3]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 3),
        // 13:      POPR 1
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, 1),

        // 14:      SUBI [SB-4], [SB-3], [SB-4]
        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 1, -4, -3, -4),
        // 15:      PUSHSCHK [SB-4]
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 1, -4),
        // 16:      CALL G[3]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 3),
        // 17:      POPR 1
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, 1),

        // 18:      LOAD G[7], [SB-4]
        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 7, -4),
        // 19:      LOAD G[8], [SB-3]
        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 8, -3),
        // 20:      ADDS [SB-4], [SB-3], [SB-4]
        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 0, -4, -3, -4),
        // 21:      PUSHSCHK [SB-4]
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 1, -4),
        // 22:      CALL G[6]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 6),
        // 23:      POPR 1
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, 1),
        // 24:      JMPF +18
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JMPGC, 18),

        // 25:      RET
        woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0),
    };

    woort_CodeEnv* codeenv;
    (void)woort_CodeEnv_create(
        bcs,
        sizeof(bcs) / sizeof(woort_Bytecode),
        9,
        &codeenv);

    codeenv->m_data_begin[0].m_integer = 0;
    codeenv->m_data_begin[1].m_integer = 300000000;
    codeenv->m_data_begin[2].m_integer = 1;
    codeenv->m_data_begin[3].m_native_or_jit_function = &print_int;
    codeenv->m_data_begin[4].m_native_or_jit_function = &print_current_time;
    codeenv->m_data_begin[5].m_native_or_jit_function = &bar;
    codeenv->m_data_begin[6].m_native_or_jit_function = &print_string;
    codeenv->m_data_begin[7].m_string = woort_GCString_make_string("hello", 5);
    codeenv->m_data_begin[8].m_string = woort_GCString_make_string("world", 5);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

    woort_VMRuntime_destroy(vm);
    woort_CodeEnv_drop(codeenv);

    woort_shutdown();
    return 0;
}