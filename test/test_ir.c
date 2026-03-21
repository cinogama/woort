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

/*
 * 测试循环 - 使用 PHI 实现累加
 * 
 * int sum_to_n(int n) {
 *     int sum = 0;
 *     int i = 1;
 *     while (i <= n) {
 *         sum = sum + i;
 *         i = i + 1;
 *     }
 *     return sum;
 * }
 * 
 * 需要 PHI 节点来跟踪循环变量
 */
static bool test_loop(void)
{
    printf("Testing loop with PHI...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex const_0 = woort_IRCompiler_alloc_global(compiler);
    woort_IRGlobalIndex const_1 = woort_IRCompiler_alloc_global(compiler);
    (void)const_0;
    (void)const_1;
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 1, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    woort_IRBlock* loop_header;
    if (!woort_IRFunction_add_block(func, &loop_header))
    {
        printf("  FAILED: Could not add loop_header\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* loop_body;
    if (!woort_IRFunction_add_block(func, &loop_body))
    {
        printf("  FAILED: Could not add loop_body\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* exit_block;
    if (!woort_IRFunction_add_block(func, &exit_block))
    {
        printf("  FAILED: Could not add exit_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    const woort_IRValue* param_n = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* const0 = woort_IRBlock_load_const(entry, const_0);
    const woort_IRValue* const1 = woort_IRBlock_load_const(entry, const_1);
    
    woort_IRBlock_br(entry, loop_header);
    
    woort_IRPHI* phi_sum = woort_IRFunction_create_phi(func, loop_header);
    woort_IRPHI* phi_i = woort_IRFunction_create_phi(func, loop_header);
    
    if (phi_sum == NULL || phi_i == NULL)
    {
        printf("  FAILED: Could not create PHI nodes\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRPHI_add_incoming(phi_sum, entry, const0);
    woort_IRPHI_add_incoming(phi_i, entry, const1);
    
    const woort_IRValue* sum_val = woort_IRPHI_as_value(phi_sum);
    const woort_IRValue* i_val = woort_IRPHI_as_value(phi_i);
    
    woort_IRBlock_br_le(loop_header, i_val, param_n, loop_body, exit_block);
    
    const woort_IRValue* new_sum = woort_IRBlock_ADD_I(loop_body, sum_val, i_val);
    const woort_IRValue* new_i = woort_IRBlock_ADD_I(loop_body, i_val, const1);
    
    woort_IRPHI_add_incoming(phi_sum, loop_body, new_sum);
    woort_IRPHI_add_incoming(phi_i, loop_body, new_i);
    
    woort_IRBlock_br(loop_body, loop_header);
    
    woort_IRBlock_ret(exit_block, sum_val);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("loop (sum_to_n)", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试嵌套条件 - 简化版嵌套 if-else
 * 
 * int sign(int x) {
 *     if (x > 0)
 *         return 1;
 *     else if (x < 0)
 *         return -1;
 *     else
 *         return 0;
 * }
 */
static bool test_nested_conditional(void)
{
    printf("Testing nested conditional...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex const_0 = woort_IRCompiler_alloc_global(compiler);
    woort_IRGlobalIndex const_1 = woort_IRCompiler_alloc_global(compiler);
    woort_IRGlobalIndex const_neg1 = woort_IRCompiler_alloc_global(compiler);
    (void)const_0;
    (void)const_1;
    (void)const_neg1;
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 1, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    woort_IRBlock* pos_block;
    if (!woort_IRFunction_add_block(func, &pos_block))
    {
        printf("  FAILED: Could not add pos_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* non_pos_block;
    if (!woort_IRFunction_add_block(func, &non_pos_block))
    {
        printf("  FAILED: Could not add non_pos_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* neg_block;
    if (!woort_IRFunction_add_block(func, &neg_block))
    {
        printf("  FAILED: Could not add neg_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* zero_block;
    if (!woort_IRFunction_add_block(func, &zero_block))
    {
        printf("  FAILED: Could not add zero_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    const woort_IRValue* param_x = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* zero = woort_IRBlock_load_const(entry, const_0);
    const woort_IRValue* one = woort_IRBlock_LOAD(entry, const_1);
    const woort_IRValue* neg_one = woort_IRBlock_LOAD(entry, const_neg1);
    
    woort_IRBlock_br_gt(entry, param_x, zero, pos_block, non_pos_block);
    
    woort_IRBlock_br_lt(non_pos_block, param_x, zero, neg_block, zero_block);
    
    woort_IRBlock_ret(pos_block, one);
    woort_IRBlock_ret(neg_block, neg_one);
    woort_IRBlock_ret(zero_block, zero);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("nested_conditional (sign)", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试多函数 - 两个函数相互独立
 * 
 * int double_val(int x) {
 *     return x + x;
 * }
 * 
 * int quadruple(int x) {
 *     return double_val(double_val(x));
 * }
 */
static bool test_multiple_functions(void)
{
    printf("Testing multiple functions...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex func_double = woort_IRCompiler_alloc_global(compiler);
    (void)func_double;
    
    woort_IRFunction* double_func;
    if (!woort_IRCompiler_add_function(compiler, 1, &double_func))
    {
        printf("  FAILED: Could not add double_func: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* double_entry = woort_IRFunction_get_entry_block(double_func);
    const woort_IRValue* param_x = woort_IRFunction_get_param(double_func, 0);
    const woort_IRValue* double_result = woort_IRBlock_ADD_I(double_entry, param_x, param_x);
    woort_IRBlock_ret(double_entry, double_result);
    
    woort_IRFunction* quad_func;
    if (!woort_IRCompiler_add_function(compiler, 1, &quad_func))
    {
        printf("  FAILED: Could not add quad_func: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* quad_entry = woort_IRFunction_get_entry_block(quad_func);
    const woort_IRValue* param_val = woort_IRFunction_get_param(quad_func, 0);
    
    woort_IRBlock_PUSH(quad_entry, param_val);
    const woort_IRValue* first_call;
    woort_IRBlock_CALLNWO(quad_entry, func_double, 1, &first_call);
    
    woort_IRBlock_PUSH(quad_entry, first_call);
    const woort_IRValue* second_call;
    woort_IRBlock_CALLNWO(quad_entry, func_double, 1, &second_call);
    
    woort_IRBlock_ret(quad_entry, second_call);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("multiple_functions (double, quadruple)", codeenv);
    
    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(compiler);
    
    return true;
}

/*
 * 测试复杂 PHI 链 - 多个 PHI 节点相互依赖
 * 
 * int fibonacci(int n) {
 *     if (n <= 1)
 *         return n;
 *     
 *     int a = 0, b = 1;
 *     for (int i = 2; i <= n; i++) {
 *         int temp = a + b;
 *         a = b;
 *         b = temp;
 *     }
 *     return b;
 * }
 * 
 * 需要 3 个 PHI 节点：a, b, i
 */
static bool test_fibonacci(void)
{
    printf("Testing fibonacci (complex PHI chain)...\n");
    
    woort_IRCompiler* compiler;
    if (!woort_IRCompiler_init(&compiler))
    {
        printf("  FAILED: Could not init compiler\n");
        return false;
    }
    
    woort_IRGlobalIndex const_0 = woort_IRCompiler_alloc_global(compiler);
    woort_IRGlobalIndex const_1 = woort_IRCompiler_alloc_global(compiler);
    woort_IRGlobalIndex const_2 = woort_IRCompiler_alloc_global(compiler);
    (void)const_0;
    (void)const_1;
    (void)const_2;
    
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(compiler, 1, &func))
    {
        printf("  FAILED: Could not add function: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    woort_IRBlock* base_case;
    if (!woort_IRFunction_add_block(func, &base_case))
    {
        printf("  FAILED: Could not add base_case\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* loop_init;
    if (!woort_IRFunction_add_block(func, &loop_init))
    {
        printf("  FAILED: Could not add loop_init\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* loop_header;
    if (!woort_IRFunction_add_block(func, &loop_header))
    {
        printf("  FAILED: Could not add loop_header\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* loop_body;
    if (!woort_IRFunction_add_block(func, &loop_body))
    {
        printf("  FAILED: Could not add loop_body\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRBlock* exit_block;
    if (!woort_IRFunction_add_block(func, &exit_block))
    {
        printf("  FAILED: Could not add exit_block\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    const woort_IRValue* param_n = woort_IRFunction_get_param(func, 0);
    const woort_IRValue* const0 = woort_IRBlock_load_const(entry, const_0);
    const woort_IRValue* const1 = woort_IRBlock_load_const(entry, const_1);
    const woort_IRValue* const2 = woort_IRBlock_load_const(entry, const_2);
    
    woort_IRBlock_br_le(entry, param_n, const1, base_case, loop_init);
    
    woort_IRBlock_ret(base_case, param_n);
    
    woort_IRBlock_br(loop_init, loop_header);
    
    woort_IRPHI* phi_a = woort_IRFunction_create_phi(func, loop_header);
    woort_IRPHI* phi_b = woort_IRFunction_create_phi(func, loop_header);
    woort_IRPHI* phi_i = woort_IRFunction_create_phi(func, loop_header);
    
    if (phi_a == NULL || phi_b == NULL || phi_i == NULL)
    {
        printf("  FAILED: Could not create PHI nodes\n");
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    woort_IRPHI_add_incoming(phi_a, loop_init, const0);
    woort_IRPHI_add_incoming(phi_b, loop_init, const1);
    woort_IRPHI_add_incoming(phi_i, loop_init, const2);
    
    const woort_IRValue* a_val = woort_IRPHI_as_value(phi_a);
    const woort_IRValue* b_val = woort_IRPHI_as_value(phi_b);
    const woort_IRValue* i_val = woort_IRPHI_as_value(phi_i);
    
    woort_IRBlock_br_le(loop_header, i_val, param_n, loop_body, exit_block);
    
    const woort_IRValue* new_temp = woort_IRBlock_ADD_I(loop_body, a_val, b_val);
    const woort_IRValue* new_i = woort_IRBlock_ADD_I(loop_body, i_val, const1);
    
    woort_IRPHI_add_incoming(phi_a, loop_body, b_val);
    woort_IRPHI_add_incoming(phi_b, loop_body, new_temp);
    woort_IRPHI_add_incoming(phi_i, loop_body, new_i);
    
    woort_IRBlock_br(loop_body, loop_header);
    
    woort_IRBlock_ret(exit_block, b_val);
    
    woort_CodeEnv* codeenv;
    if (!woort_IRCompiler_finish(compiler, &codeenv))
    {
        printf("  FAILED: Could not finish compilation: %s\n", woort_IRCompiler_get_error(compiler));
        woort_IRCompiler_drop(compiler);
        return false;
    }
    
    printf("  SUCCESS: CodeEnv created\n");
    
    dump_codeenv("fibonacci (complex PHI chain)", codeenv);
    
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
    
    //total++;
    //if (test_simple_add()) passed++;
    //
    //printf("\n");
    //total++;
    //if (test_conditional()) passed++;
    //
    //printf("\n");
    //total++;
    //if (test_function_call()) passed++;
    //
    //printf("\n");
    //total++;
    //if (test_phi()) passed++;
    
    printf("\n");
    total++;
    if (test_real_phi()) passed++;
    
    //printf("\n");
    //total++;
    //if (test_loop()) passed++;
    //
    //printf("\n");
    //total++;
    //if (test_nested_conditional()) passed++;
    //
    //printf("\n");
    //total++;
    //if (test_multiple_functions()) passed++;
    //
    //printf("\n");
    //total++;
    //if (test_fibonacci()) passed++;
    
    printf("\n=== Results: %d/%d passed ===\n", passed, total);
    
    woort_shutdown();
    
    return (passed == total) ? 0 : 1;
}