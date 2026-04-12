#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_disassembly.h"

#include <stdio.h>
#include <time.h>

woort_api print_int(void)
{
    printf("%lld\n", (long long)woort_int(0));
    return woort_ret_void();
}

woort_api print_string(void)
{
    printf("%s\n", woort_string(0));
    return woort_ret_void();
}

woort_api print_current_time(void)
{
    return woort_ret_int((woort_Int)clock());
}

woort_api bar(void)
{
    return woort_ret_void();
}

int main(int argc, char** argv) {
    woort_init();

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_fib;
    woort_IRFunction* f_main;

    woort_IRConstantIndex c_f_fib = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_c_main = woort_IRCompiler_add_constant(irc);

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

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_function(cenv, c_f_fib, fib_addr);
    woort_CodeEnv_set_const_script_closure(cenv, c_c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_1, 1);
    woort_CodeEnv_set_const_int(cenv, c_2, 2);
    woort_CodeEnv_set_const_int(cenv, c_35, 40);
    woort_CodeEnv_unlock(cenv);

    woort_dump_codes(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);

    woort_load_const(sv, cenv, c_c_main);

    const clock_t b0 = clock();
    (void)woort_invoke(sv + 1, sv);
    const clock_t e0 = clock();

    woort_pop(2);

    printf("%lld\n", (long long)woort_int(sv + 1));

    (void)woort_VMRuntime_swap(NULL);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    woort_shutdown();
    return 0;
}
