#include "woort.h"

#include "woort_lir_compiler.h"

int main(int argc, char ** argv) {
    woort_init();

    woort_LIRCompiler lir_compiler;

    for (;;)
    {
        woort_LIRCompiler_init(&lir_compiler);
        {
            woort_LIRFunction* function;
            (void)woort_LIRCompiler_add_function(&lir_compiler, &function);

            //woort_LIRRegister* arg0;
            //woort_LIRRegister* val0, *va11;
            //(void)woort_LIRFunction_get_argument_register(function, 0, &arg0);
            //(void)woort_LIRFunction_alloc_register(function, &val0);
            //(void)woort_LIRFunction_alloc_register(function, &va11);

            // Further testing can be done here.
            woort_LIRLabel* label;
            (void)woort_LIRFunction_alloc_label(function, &label);

            (void)woort_LIRFunction_bind(function, label);
            //(void)woort_LIRFunction_emit_push(function, arg0);
            //(void)woort_LIRFunction_emit_push(function, val0);
            //(void)woort_LIRFunction_emit_push(function, va11);
            (void)woort_LIRFunction_emit_jmp(function, label);

            woort_CodeEnv* code_env;
            (void)woort_LIRCompiler_commit(&lir_compiler, &code_env);
            woort_CodeEnv_unshare(code_env);
        }
        woort_LIRCompiler_deinit(&lir_compiler);
    }


    woort_shutdown();
    return 0;
}