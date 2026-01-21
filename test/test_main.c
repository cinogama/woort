#include "woort.h"

#include "woort_lir_compiler.h"
#include "woort_vm.h"

int main(int argc, char** argv) {
    woort_init();

    woort_LIRCompiler lir_compiler;

    woort_LIRCompiler_init(&lir_compiler);
    {
        woort_LIR_ConstantStorage c0;
        (void)woort_LIRCompiler_allocate_constant(&lir_compiler, &c0);

        woort_Value* cvp;
        (void)woort_LIRCompiler_get_constant(&lir_compiler, c0, &cvp);

        cvp->m_integer = 123321;

        woort_LIR_StaticStorage s0 =
            woort_LIRCompiler_allocate_static_storage(&lir_compiler);

        woort_LIRFunction* function;
        (void)woort_LIRCompiler_add_function(&lir_compiler, &function);

        //woort_LIRRegister* arg0;
        woort_LIRRegister* val0;
        //(void)woort_LIRFunction_get_argument_register(function, 0, &arg0);
        (void)woort_LIRFunction_alloc_register(function, &val0);

        // Further testing can be done here.
        woort_LIRLabel* label;
        (void)woort_LIRFunction_alloc_label(function, &label);

        (void)woort_LIRFunction_bind(function, label);
        //(void)woort_LIRFunction_emit_push(function, arg0);
        (void)woort_LIRFunction_emit_loadconst(function, val0, c0);
        (void)woort_LIRFunction_emit_store(function, s0, val0);
        (void)woort_LIRFunction_emit_jmp(function, label);

        woort_CodeEnv* code_env;
        (void)woort_LIRCompiler_commit(&lir_compiler, &code_env);

        woort_VMRuntime vm;
        (void)woort_VMRuntime_init(&vm);

        (void)woort_VMRuntime_invoke(&vm, code_env->m_code_begin);

        woort_VMRuntime_deinit(&vm);
        woort_CodeEnv_unshare(code_env);
    }
    woort_LIRCompiler_deinit(&lir_compiler);

    woort_shutdown();
    return 0;
}