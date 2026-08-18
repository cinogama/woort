/*
 * 超长捕获闭包（captured_count > 126）在 JIT 路径下的回归测试。
 *
 * 背景见 test_ir_stack_margin.c：捕获区解包覆盖 a8 临时槽窗口
 * -126/-127/-128 后，编译器在函数序言用 MOVST 把窗口内捕获值搬到
 * 尾接区。解释器路径已有端到端覆盖；本测试确认 JIT 对序言 MOVST
 * （任意 a8/bc16 偏移的通用 load/store 翻译）与重映射后的深偏移
 * 访问同样正确。
 *
 * 结构与 test_ir_stack_margin 的 run_large_capture_case 相同：
 * outer 压栈 1..C -> MKCLOSURE -> CALL -> 返回结果，核对精确值。
 */
#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_CAPTURED 130
#define MAX_PROBES 8

typedef struct _Probe
{
    uint32_t m_idx;             /* 探测的 captured_idx */
    woort_IRFunction* m_inner;
    woort_IRFunction* m_outer;
    woort_IRConstantIndex m_tmpl;
    woort_IRConstantIndex m_entry;
} _Probe;

/*
 * inner_vregs：内层同时活跃的 vreg 数（0 = 直接返回 captured[IDX]，
 * >0 = 返回 (n+1)*(IDX+1)），迫使尾接区与 vreg 区在帧内紧邻布局。
 * 返回失败数。
 */
static int run_case(
    uint32_t captured_count,
    const uint32_t* probe_idx,
    uint32_t probe_count,
    uint32_t inner_vregs)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    _Probe probes[MAX_PROBES];
    woort_IRConstantIndex c_vals[MAX_CAPTURED];

    for (uint32_t k = 0; k < captured_count; ++k)
        c_vals[k] = woort_IRCompiler_add_constant(irc);

    for (uint32_t i = 0; i < probe_count; ++i)
    {
        probes[i].m_idx = probe_idx[i];
        probes[i].m_tmpl = woort_IRCompiler_add_constant(irc);
        probes[i].m_entry = woort_IRCompiler_add_constant(irc);
    }

    for (uint32_t i = 0; i < probe_count; ++i)
    {
        /* inner: captured = C, return captured[IDX]（或其 (n+1) 倍和） */
        if (!woort_IRCompiler_add_function(irc, 0, captured_count, &probes[i].m_inner))
        {
            printf("FAIL: add_function(inner) idx=%u\n", probes[i].m_idx);
            ++failures;
            break;
        }
        {
            woort_IRFunction* inner = probes[i].m_inner;
            const woort_IRValue* v = woort_IRFunction_get_captured(inner, probes[i].m_idx);
            if (v == NULL)
            {
                printf("FAIL: get_captured idx=%u\n", probes[i].m_idx);
                ++failures;
                break;
            }

            if (inner_vregs == 0)
            {
                (void)woort_IR_ret(inner, v);
            }
            else
            {
                woort_IRValue** vs =
                    (woort_IRValue**)malloc(inner_vregs * sizeof(woort_IRValue*));
                if (vs == NULL)
                {
                    printf("FAIL: OOM\n");
                    ++failures;
                    break;
                }

                for (uint32_t n = 0; n < inner_vregs; ++n)
                    vs[n] = woort_IRFunction_new_vreg(inner);
                for (uint32_t n = 0; n < inner_vregs; ++n)
                    (void)woort_IR_MOV(inner, vs[n], v);

                woort_IRValue* r = woort_IRFunction_new_vreg(inner);
                (void)woort_IR_MOV(inner, r, v);
                for (uint32_t n = 0; n < inner_vregs; ++n)
                {
                    woort_IRValue* r2 = woort_IRFunction_new_vreg(inner);
                    (void)woort_IR_ADDI(inner, r2, r, vs[n]);
                    r = r2;
                }

                free(vs);
                (void)woort_IR_ret(inner, r);
            }
        }

        /* outer: push 1..C; mkclosure; call; return result */
        if (!woort_IRCompiler_add_function(irc, 0, 0, &probes[i].m_outer))
        {
            printf("FAIL: add_function(outer) idx=%u\n", probes[i].m_idx);
            ++failures;
            break;
        }
        {
            woort_IRFunction* outer = probes[i].m_outer;

            for (uint32_t k = 0; k < captured_count; ++k)
                (void)woort_IR_PUSHCHK(
                    outer, woort_IRFunction_fetch_const(outer, c_vals[k]));

            woort_IRValue* clo = woort_IRFunction_new_vreg(outer);
            (void)woort_IR_MKCLOSURE(outer, clo, captured_count, probes[i].m_tmpl);

            woort_IRValue* r = woort_IRFunction_new_vreg(outer);
            (void)woort_IR_CALL(outer, clo, 0, r);
            (void)woort_IR_ret(outer, r);
        }
    }

    if (failures != 0)
    {
        woort_IRCompiler_close(irc);
        return failures;
    }

    woort_CodeEnv* cenv;
    if (!woort_IRCompiler_finish(irc, &cenv))
    {
        printf("FAIL: finish\n");
        woort_IRCompiler_close(irc);
        return 1;
    }

    woort_CodeEnv_lock(cenv);
    for (uint32_t k = 0; k < captured_count; ++k)
        woort_CodeEnv_set_const_int(cenv, c_vals[k], (woort_Int)(k + 1));
    for (uint32_t i = 0; i < probe_count; ++i)
    {
        const woort_Bytecode* addr;
        (void)woort_CodeEnv_query_function(cenv, probes[i].m_inner, &addr);
        woort_CodeEnv_set_const_script_closure(cenv, probes[i].m_tmpl, addr);
        (void)woort_CodeEnv_query_function(cenv, probes[i].m_outer, &addr);
        woort_CodeEnv_set_const_script_closure(cenv, probes[i].m_entry, addr);
    }
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    for (uint32_t i = 0; i < probe_count; ++i)
    {
        woort_StackValue sv;
        (void)woort_push_reserve(2, &sv);
        woort_load_const(sv, cenv, probes[i].m_entry);
        const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

        if (st != WOORT_VM_CALL_STATUS_NORMAL)
        {
            printf("FAIL: probe idx=%u status=%d\n", probes[i].m_idx, (int)st);
            ++failures;
        }
        else
        {
            const woort_Int expected =
                (woort_Int)((inner_vregs + 1) * (probes[i].m_idx + 1));
            const woort_Int got = woort_int(sv + 1);
            if (got != expected)
            {
                printf("FAIL: probe idx=%u expected %lld got %lld\n",
                    probes[i].m_idx, (long long)expected, (long long)got);
                ++failures;
            }
        }
        woort_pop(2);
    }

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_jit_c130(void)
{
    static const uint32_t idx130[] = { 0, 50, 125, 126, 127, 128, 129 };
    return run_case(130, idx130, 7, 0);
}

static int run_jit_c127_with_vregs(void)
{
    /* 尾接区（-129）与 vreg 区（-130..）紧邻 */
    static const uint32_t idxv127[] = { 0, 126 };
    return run_case(127, idxv127, 2, 5);
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;

    failures += run_jit_c130();
    failures += run_jit_c127_with_vregs();

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_large_capture: ALL PASS\n");
        return 0;
    }
    printf("test_jit_large_capture: %d FAILURES\n", failures);
    return 1;
}
