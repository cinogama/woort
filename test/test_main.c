#include "woort_lir_compiler.h"

int main(int argc, char ** argv) {
    woort_LIRCompiler lir_compiler;

    for (;;)
    {
        woort_LIRCompiler_init(&lir_compiler);
        {
            woort_LIRFunction* function;
            woort_LIRCompiler_add_function(&lir_compiler, &function);

            // Further testing can be done here.
            woort_LIRLabel* label;
            woort_LIRFunction_alloc_label(function, &label);

            woort_LIRFunction_bind(function, label);
            woort_LIRFunction_emit_jmp(function, label);

            woort_LIRCompiler_commit(&lir_compiler);
        }
        woort_LIRCompiler_deinit(&lir_compiler);
    }
    return 0;
}