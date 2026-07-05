#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ===== ADDS / CADDS / CVADDS：结果为字符串 ===== */

typedef bool (*str_binop_builder)(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

static int run_str_binop(str_binop_builder build, const char* name, const char* a, const char* b, const char* expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    /* 数据常量先于入口闭包常量分配（CodeEnv 约定） */
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)build(f_main, dst,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_buffer(cenv, c_a, a, strlen(a));
    woort_CodeEnv_set_const_buffer(cenv, c_b, b, strlen(b));
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL %s(\"%s\",\"%s\"): status=%d\n", name, a, b, (int)st); ++failures; }
    else
    {
        const char* got = woort_string(sv + 1);
        if (strcmp(got, expect) != 0) { printf("FAIL %s(\"%s\",\"%s\"): expected \"%s\" got \"%s\"\n", name, a, b, expect, got); ++failures; }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

/* ===== LTS/GTS/LES/GES/EQS/NES：结果为整数 0/1 ===== */

static int run_str_cmpop(str_binop_builder build, const char* name, const char* a, const char* b, woort_Int expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    /* 数据常量先于入口闭包常量分配（CodeEnv 约定） */
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)build(f_main, dst,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_buffer(cenv, c_a, a, strlen(a));
    woort_CodeEnv_set_const_buffer(cenv, c_b, b, strlen(b));
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL %s(\"%s\",\"%s\"): status=%d\n", name, a, b, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL %s(\"%s\",\"%s\"): expected %lld got %lld\n", name, a, b, (long long)expect, (long long)got); ++failures; }
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

    /* ===== ADDS ===== */
    failures += run_str_binop(woort_IR_ADDS, "adds", "hello", " world", "hello world");
    failures += run_str_binop(woort_IR_ADDS, "adds", "", "abc", "abc");
    failures += run_str_binop(woort_IR_ADDS, "adds", "abc", "", "abc");
    failures += run_str_binop(woort_IR_ADDS, "adds", "", "", "");
    failures += run_str_binop(woort_IR_ADDS, "adds", "foo", "bar", "foobar");

    /* ===== LTS ===== */
    failures += run_str_cmpop(woort_IR_LTS, "lts", "abc", "abd", 1);
    failures += run_str_cmpop(woort_IR_LTS, "lts", "abd", "abc", 0);
    failures += run_str_cmpop(woort_IR_LTS, "lts", "abc", "abc", 0);
    failures += run_str_cmpop(woort_IR_LTS, "lts", "ab", "abc", 1);   /* 前缀 < 完整串 */

    /* ===== GTS ===== */
    failures += run_str_cmpop(woort_IR_GTS, "gts", "abd", "abc", 1);
    failures += run_str_cmpop(woort_IR_GTS, "gts", "abc", "abd", 0);
    failures += run_str_cmpop(woort_IR_GTS, "gts", "abc", "abc", 0);
    failures += run_str_cmpop(woort_IR_GTS, "gts", "abc", "ab", 1);

    /* ===== LES ===== */
    failures += run_str_cmpop(woort_IR_LES, "les", "abc", "abd", 1);
    failures += run_str_cmpop(woort_IR_LES, "les", "abc", "abc", 1);
    failures += run_str_cmpop(woort_IR_LES, "les", "abd", "abc", 0);

    /* ===== GES ===== */
    failures += run_str_cmpop(woort_IR_GES, "ges", "abc", "abc", 1);
    failures += run_str_cmpop(woort_IR_GES, "ges", "abd", "abc", 1);
    failures += run_str_cmpop(woort_IR_GES, "ges", "abc", "abd", 0);

    /* ===== EQS（不同常量池项走 compare 路径） =====
     * 注意 EQS 语义 = (a==b 指针) || (compare==0)；不同指针时由 compare 决定。 */
    failures += run_str_cmpop(woort_IR_EQS, "eqs", "abc", "abd", 0);
    failures += run_str_cmpop(woort_IR_EQS, "eqs", "abc", "abc", 1);  /* 内容相等，compare==0 */
    failures += run_str_cmpop(woort_IR_EQS, "eqs", "", "", 1);

    /* ===== NES（语义 = (a!=b 指针) && (compare!=0)，即 EQS 的取反） =====
     * 内容相同的等值串即使指针不同也应判定为“相等” -> NES=0。 */
    failures += run_str_cmpop(woort_IR_NES, "nes", "abc", "abd", 1);
    failures += run_str_cmpop(woort_IR_NES, "nes", "abc", "abc", 0);  /* 内容相等 -> 0 */
    failures += run_str_cmpop(woort_IR_NES, "nes", "foo", "bar", 1);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_adds: ALL PASS\n");
        return 0;
    }
    printf("test_jit_adds: %d FAILURES\n", failures);
    return 1;
}
