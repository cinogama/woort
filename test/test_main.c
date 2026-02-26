#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"

#include <stdio.h>

woort_api foo(woort_vm vm, woort_value* args)
{
    printf("Helloworld");

    return WOORT_VM_CALL_STATUS_NORMAL;
}

int main(int argc, char** argv) {
    woort_init();

    woort_CodeEnv* codeenv;

    const woort_Bytecode bcs[] =
    {
        // CALLFP G[0]
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 0),
        // RET
        woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0),
    };

    (void)woort_CodeEnv_create(
        bcs,
        sizeof(bcs) / sizeof(woort_Bytecode),
        1,
        &codeenv);

    codeenv->m_data_begin[0].m_native_or_jit_function = &foo;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

    woort_VMRuntime_destroy(vm);
    woort_CodeEnv_drop(codeenv);

    woort_shutdown();
    return 0;
}