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

    woort_CodeEnv* codeenv;

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
        // 8:       JBCONDNE +7 if [SB-0] != [SB-1]
        woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JCONDGC, 3, 0, -1, 1),

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

        // 14:      PUSHSCHK [SB-3]
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 1, -3),
        // 15:      CALL G[3]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 3),
        // 16:      POPR 1
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, 1),

        // 17:      PUSHSCHK [SB-4]
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 1, -4),
        // 18:      CALL G[3]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 3),
        // 19:      POPR 1
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, 1),

        // 20:      RET
        woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0),
    };

    (void)woort_CodeEnv_create(
        bcs,
        sizeof(bcs) / sizeof(woort_Bytecode),
        6,
        &codeenv);

    codeenv->m_data_begin[0].m_integer = 0;
    codeenv->m_data_begin[1].m_integer = 3000000000;
    codeenv->m_data_begin[2].m_integer = 1;
    codeenv->m_data_begin[3].m_native_or_jit_function = &print_int;
    codeenv->m_data_begin[4].m_native_or_jit_function = &print_current_time;
    codeenv->m_data_begin[5].m_native_or_jit_function = &bar;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

    woort_VMRuntime_destroy(vm);
    woort_CodeEnv_drop(codeenv);

    woort_shutdown();
    return 0;
}