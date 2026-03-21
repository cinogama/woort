/*
 * test_ir.c
 * 
 * IR 测试用例
 */

#include "woort_ir.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Dump CodeEnv 的字节码
 */
static void dump_codeenv(const char* name, woort_CodeEnv* codeenv)
{
    printf("\n  === Bytecode dump: %s ===\n", name);
    printf("  Code count: %llu\n", (unsigned long long)(codeenv->m_code_end - codeenv->m_code_begin));
    printf("  Data count: (see woort_Value array)\n\n");
    
    const woort_Bytecode* pc = codeenv->m_code_begin;
    while (pc < codeenv->m_code_end)
    {
        printf("  [%04llu] ", (unsigned long long)(pc - codeenv->m_code_begin));
        pc = woort_Disassembly(pc);
    }
    printf("  === End dump ===\n");
}

/*
 * 测试简单的加法函数
 * 
 * int add(int a, int b) {
 *     return a + b;
 * }
 */
static bool test_simple_add(void)
{
    printf("Testing simple add function...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex const_val = woort_IRCompiler_alloc_global(compiler);
    (void)const_val;
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 2, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    const woort_IRValue* param0 = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* param1 = woort_IRFunction_get_param(func, 1);
    
    const woort_IRValue* result = woort_IRBlock_ADD_I(entry, param0, param1);
    if (result == NULL)
    {
        printf("  FAILED: Could not create ADD instruction\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock_ret(entry, result);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("simple_add", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试条件分支
 * 
 * int abs(int x) {
 *     if (x < 0)
 *         return -x;
 *     else
 *         return x;
 * }
 */
static bool test_conditional(void)
{
    printf("Testing conditional function...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex const_zero = woort_IRCompiler_alloc_global(compiler);
    (void)const_zero;
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 1, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    woort_IRBlock* neg_block;
    if (!woort_IRFunction_add_block(func, &neg_block))
    {
        printf("  FAILED: Could not add neg_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* pos_block;
    if (!woort_IRFunction_add_block(func, &pos_block))
    {
        printf("  FAILED: Could not add pos_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    const woort_IRValue* param = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* zero = woort_IRBlock_load_const(entry, const_zero);
    
    woort_IRBlock_br_lt(entry, param, zero, neg_block, pos_block);
    
    const woort_IRValue* neg_result = woort_IRBlock_NEG_I(neg_block, param);
    woort_IRBlock_ret(neg_block, neg_result);
    
    woort_IRBlock_ret(pos_block, param);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("conditional (abs)", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试函数调用
 */
static bool test_function_call(void)
{
    printf("Testing function call...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex const_val = woort_IRCompiler_alloc_global(compiler);
    woort_IRGlobalIndex func_idx = woort_IRCompiler_alloc_global(compiler);
    (void)const_val;
    (void)func_idx;
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 0, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    const woort_IRValue* arg = woort_IRBlock_load_const(entry, const_val);
    woort_IRBlock_PUSH(entry, arg);
    
    const woort_IRValue* result;
    if (!woort_IRBlock_CALLNWO(entry, func_idx, 1, &result))
    {
        printf("  FAILED: Could not create CALL instruction\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock_ret(entry, result);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("function_call", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试 PHI 节点
 * 
 * int max(int a, int b) {
 *     if (a > b)
 *         return a;
 *     else
 *         return b;
 * }
 * 
 * 简化版本，不使用 PHI
 */
static bool test_phi(void)
{
    printf("Testing PHI node (simple)...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 2, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    woort_IRBlock* a_block;
    if (!woort_IRFunction_add_block(func, &a_block))
    {
        printf("  FAILED: Could not add a_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* b_block;
    if (!woort_IRFunction_add_block(func, &b_block))
    {
        printf("  FAILED: Could not add b_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    const woort_IRValue* param_a = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* param_b = woort_IRFunction_get_param(func, 1);
    
    woort_IRBlock_br_gt(entry, param_a, param_b, a_block, b_block);
    
    woort_IRBlock_ret(a_block, param_a);
    woort_IRBlock_ret(b_block, param_b);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("phi (max - no actual PHI)", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试真正的 PHI 节点
 * 
 * int max_with_phi(int a, int b) {
 *     int result;
 *     if (a > b)
 *         result = a;
 *     else
 *         result = b;
 *     return result;  // PHI 节点在这里
 * }
 */
static bool test_real_phi(void)
{
    printf("Testing real PHI node...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 2, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    woort_IRBlock* a_block;
    if (!woort_IRFunction_add_block(func, &a_block))
    {
        printf("  FAILED: Could not add a_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* b_block;
    if (!woort_IRFunction_add_block(func, &b_block))
    {
        printf("  FAILED: Could not add b_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* merge_block;
    if (!woort_IRFunction_add_block(func, &merge_block))
    {
        printf("  FAILED: Could not add merge_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    const woort_IRValue* param_a = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* param_b = woort_IRFunction_get_param(func, 1);
    
    woort_IRBlock_br_gt(entry, param_a, param_b, a_block, b_block);
    
    woort_IRBlock_br(a_block, merge_block);
    woort_IRBlock_br(b_block, merge_block);
    
    woort_IRPHI* phi = woort_IRFunction_create_phi(func, merge_block);
    if (phi == NULL)
    {
        printf("  FAILED: Could not create PHI node\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRPHI_add_incoming(phi, a_block, param_a);
    woort_IRPHI_add_incoming(phi, b_block, param_b);
    
    const woort_IRValue* phi_value = woort_IRPHI_as_value(phi);
    woort_IRBlock_ret(merge_block, phi_value);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("real PHI (max_with_phi)", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    woort_init();
    
    printf("=== IR Tests ===\n\n");
    
    int passed = 0;
    int total = 0;
    
    total++;
    if (test_simple_add()) passed++;
    
    printf("\n");
    total++;
    if (test_conditional()) passed++;
    
    printf("\n");
    total++;
    if (test_function_call()) passed++;
    
    printf("\n");
    total++;
    if (test_phi()) passed++;
    
    printf("\n");
    total++;
    if (test_real_phi()) passed++;
    
    printf("\n=== Results: %d/%d passed ===\n", passed, total);
    
    woort_shutdown();
    
    return (passed == total) ? 0 : 1;
}