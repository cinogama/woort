#include "woort.h"

#include "woort_codeenv.h"
#include "woort_disassembly.h"

#include <stdio.h>

void dump_Code(woort_CodeEnv* cenv)
{
    const woort_Bytecode* pc = cenv->m_code_begin;

    printf("\n");

    while (pc < cenv->m_code_end)
        pc = woort_Disassembly(pc);

    printf("\n");

    fflush(stdout);
}

int main(void)
{
    woort_init();
    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, &f_main);

    woort_IRConstantIndex c_233 = woort_IRCompiler_add_constant(irc);
    const woort_IRValue* c_v_233 = woort_IRFunction_load_const(f_main, c_233);
    (void)woort_IR_ret(f_main, c_v_233);

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    dump_Code(cenv);

    woort_CodeEnv_drop(cenv);

    woort_IRCompiler_close(irc);
    woort_shutdown();
}