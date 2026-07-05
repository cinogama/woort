#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * JIT 执行测试：验证整数复合算术指令（CSUBI/CMULI/CDIVI/CMODI）。
 * 通过将同一 vreg 同时作为 dst 与 src[0]，IR 编译器会选出复合形式，
 * 经 woort_CodeEnv_jit 编译后在 JIT 后端执行。
 */

typedef enum { OP_SUB, OP_MUL, OP_DIV, OP_MOD } OpKind;

static const char* op_name(OpKind op)
{
    switch (op)
    {
        case OP_SUB: return "CSUBI";
        case OP_MUL: return "CMULI";
        case OP_DIV: return "CDIVI";
        case OP_MOD: return "CMODI";
    }
    return "?";
}

static int run_compound(OpKind op, woort_Int x, woort_Int y, woort_Int expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_x = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_y = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        const woort_IRValue* vx = woort_IRFunction_fetch_const(f, c_x);
        const woort_IRValue* vy = woort_IRFunction_fetch_const(f, c_y);

        (void)woort_IR_MOV(f, a, vx);

        bool ok = false;
        switch (op)
        {
            case OP_SUB: ok = woort_IR_SUBI(f, a, a, vy); break;
            case OP_MUL: ok = woort_IR_MULI(f, a, a, vy); break;
            case OP_DIV: ok = woort_IR_DIVI(f, a, a, vy); break;
            case OP_MOD: ok = woort_IR_MODI(f, a, a, vy); break;
        }
        (void)ok;

        (void)woort_IR_ret(f, a);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* entry_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &entry_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, entry_addr);
    woort_CodeEnv_set_const_int(cenv, c_x, x);
    woort_CodeEnv_set_const_int(cenv, c_y, y);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL)
    {
        printf("FAIL %s(%lld,%lld): status=%d\n", op_name(op), (long long)x, (long long)y, (int)st);
        ++failures;
    }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect)
        {
            printf("FAIL %s(%lld,%lld): expected %lld got %lld\n",
                op_name(op), (long long)x, (long long)y, (long long)expect, (long long)got);
            ++failures;
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

    /* CSUBI */
    failures += run_compound(OP_SUB, 10, 3, 7);
    failures += run_compound(OP_SUB, 0, 5, -5);
    failures += run_compound(OP_SUB, -7, -3, -4);
    failures += run_compound(OP_SUB, 7, 7, 0);          /* a -= a */

    /* CMULI */
    failures += run_compound(OP_MUL, 6, 7, 42);
    failures += run_compound(OP_MUL, -6, 7, -42);
    failures += run_compound(OP_MUL, -6, -7, 42);
    failures += run_compound(OP_MUL, 0, 99, 0);
    failures += run_compound(OP_MUL, 1LL << 20, 1LL << 20, (1LL << 40));
    failures += run_compound(OP_MUL, 9, 9, 81);          /* a *= a */

    /* CDIVI (C 截断向零) */
    failures += run_compound(OP_DIV, 100, 7, 14);
    failures += run_compound(OP_DIV, -17, 5, -3);
    failures += run_compound(OP_DIV, 17, -5, -3);
    failures += run_compound(OP_DIV, -17, -5, 3);
    failures += run_compound(OP_DIV, 7, 7, 1);           /* a /= a */

    /* CMODI (C 余数符号跟随被除数) */
    failures += run_compound(OP_MOD, 100, 7, 2);
    failures += run_compound(OP_MOD, -17, 5, -2);
    failures += run_compound(OP_MOD, 17, -5, 2);
    failures += run_compound(OP_MOD, -17, -5, -2);
    failures += run_compound(OP_MOD, 7, 7, 0);           /* a %= a */

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_ciasmd: ALL PASS\n");
        return 0;
    }
    printf("test_jit_ciasmd: %d FAILURES\n", failures);
    return 1;
}
