#include "woort.h"
extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef enum { CK_INT, CK_REAL, CK_STR } CheckKind;

/*
 * 通用 runner：将一个常量按 src_box 装箱后，经 CASTDYN 转为 target，再返回。
 * is_real_const==1 时常量按 real 设置，否则按 int 设置。
 */
static int run(
    woort_BoxValueType src_box,
    int is_real_const,
    woort_Real rval,
    woort_Int ival,
    woort_BoxValueType target,
    CheckKind ck,
    woort_Int ei,
    double er,
    const char* es,
    const char* label)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed, src_box,
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* out = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CASTDYN(f_main, out, target, boxed);
        (void)woort_IR_ret(f_main, out);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    if (is_real_const)
        woort_CodeEnv_set_const_real(cenv, c_val, rval);
    else
        woort_CodeEnv_set_const_int(cenv, c_val, ival);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL)
    {
        printf("FAIL %s: status=%d\n", label, (int)st);
        ++failures;
    }
    else
    {
        switch (ck)
        {
        case CK_INT:
        {
            const woort_Int got = woort_int(sv + 1);
            if (got != ei) { printf("FAIL %s: got %lld expect %lld\n", label, (long long)got, (long long)ei); ++failures; }
            else printf("ok %s\n", label);
            break;
        }
        case CK_REAL:
        {
            const double got = woort_real(sv + 1);
            if (got != er) { printf("FAIL %s: got %g expect %g\n", label, got, er); ++failures; }
            else printf("ok %s\n", label);
            break;
        }
        case CK_STR:
        {
            const char* got = woort_string(sv + 1);
            if (got == NULL || strcmp(got, es) != 0) { printf("FAIL %s: got \"%s\" expect \"%s\"\n", label, got ? got : "(null)", es); ++failures; }
            else printf("ok %s\n", label);
            break;
        }
        }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;

    /* INT 源 */
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, 42, WOORT_BOX_VALUE_TYPE_INT, CK_INT, 42, 0, NULL, "int->int(42)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, -7, WOORT_BOX_VALUE_TYPE_INT, CK_INT, -7, 0, NULL, "int->int(-7)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, 42, WOORT_BOX_VALUE_TYPE_BOOL, CK_INT, 1, 0, NULL, "int->bool(42)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, 0, WOORT_BOX_VALUE_TYPE_BOOL, CK_INT, 0, 0, NULL, "int->bool(0)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, 42, WOORT_BOX_VALUE_TYPE_REAL, CK_REAL, 0, 42.0, NULL, "int->real(42)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, -7, WOORT_BOX_VALUE_TYPE_REAL, CK_REAL, 0, -7.0, NULL, "int->real(-7)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, 42, WOORT_BOX_VALUE_TYPE_STRING, CK_STR, 0, 0, "42", "int->string(42)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, -7, WOORT_BOX_VALUE_TYPE_STRING, CK_STR, 0, 0, "-7", "int->string(-7)");
    failures += run(WOORT_BOX_VALUE_TYPE_INT, 0, 0, 0, WOORT_BOX_VALUE_TYPE_STRING, CK_STR, 0, 0, "0", "int->string(0)");

    /* REAL 源 */
    failures += run(WOORT_BOX_VALUE_TYPE_REAL, 1, 3.14, 0, WOORT_BOX_VALUE_TYPE_REAL, CK_REAL, 0, 3.14, NULL, "real->real(3.14)");
    failures += run(WOORT_BOX_VALUE_TYPE_REAL, 1, -2.5, 0, WOORT_BOX_VALUE_TYPE_REAL, CK_REAL, 0, -2.5, NULL, "real->real(-2.5)");
    failures += run(WOORT_BOX_VALUE_TYPE_REAL, 1, 3.9, 0, WOORT_BOX_VALUE_TYPE_INT, CK_INT, 3, 0, NULL, "real->int(3.9)");
    failures += run(WOORT_BOX_VALUE_TYPE_REAL, 1, -2.7, 0, WOORT_BOX_VALUE_TYPE_INT, CK_INT, -2, 0, NULL, "real->int(-2.7)");
    failures += run(WOORT_BOX_VALUE_TYPE_REAL, 1, 3.14, 0, WOORT_BOX_VALUE_TYPE_BOOL, CK_INT, 1, 0, NULL, "real->bool(3.14)");
    failures += run(WOORT_BOX_VALUE_TYPE_REAL, 1, 0.0, 0, WOORT_BOX_VALUE_TYPE_BOOL, CK_INT, 0, 0, NULL, "real->bool(0.0)");

    /* BOOL 源 */
    failures += run(WOORT_BOX_VALUE_TYPE_BOOL, 0, 0, 1, WOORT_BOX_VALUE_TYPE_INT, CK_INT, 1, 0, NULL, "bool->int(true)");
    failures += run(WOORT_BOX_VALUE_TYPE_BOOL, 0, 0, 0, WOORT_BOX_VALUE_TYPE_INT, CK_INT, 0, 0, NULL, "bool->int(false)");
    failures += run(WOORT_BOX_VALUE_TYPE_BOOL, 0, 0, 1, WOORT_BOX_VALUE_TYPE_REAL, CK_REAL, 0, 1.0, NULL, "bool->real(true)");
    failures += run(WOORT_BOX_VALUE_TYPE_BOOL, 0, 0, 0, WOORT_BOX_VALUE_TYPE_REAL, CK_REAL, 0, 0.0, NULL, "bool->real(false)");
    failures += run(WOORT_BOX_VALUE_TYPE_BOOL, 0, 0, 1, WOORT_BOX_VALUE_TYPE_STRING, CK_STR, 0, 0, "true", "bool->string(true)");
    failures += run(WOORT_BOX_VALUE_TYPE_BOOL, 0, 0, 0, WOORT_BOX_VALUE_TYPE_STRING, CK_STR, 0, 0, "false", "bool->string(false)");

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_castdyn: ALL PASS\n");
        return 0;
    }
    printf("test_jit_castdyn: %d FAILURES\n", failures);
    return 1;
}
