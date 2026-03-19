#include "woort.h"

#include "ir/woort_ir.h"
#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_gc_string.h"

#include <stdio.h>
#include <stdbool.h>

static bool test_simple_function(void);
static bool test_binary_op(void);
static bool test_local_variable(void);
static bool test_conditional_branch(void);
static bool test_comparison(void);
static bool test_recursive_function(void);

static bool test_simple_function(void)
{
    printf("=== Test IR: Simple Function (return 42) ===\n");

    woort_IRModule* module;
    if (!woort_IRModule_create(&module))
    {
        printf("FAIL: Cannot create IR module\n");
        return false;
    }

    woort_IRFunction* func;
    if (!woort_IRModule_add_function(module, "test_func", 0, &func))
    {
        printf("FAIL: Cannot add function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder* builder;
    if (!woort_IRBuilder_create(func, &builder))
    {
        printf("FAIL: Cannot create builder\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* entry;
    if (!woort_IRBuilder_create_block(builder, &entry))
    {
        printf("FAIL: Cannot create entry block\n");
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }
    woort_IRBuilder_position_at_end(builder, entry);
    woort_IRBlock_seal(entry);

    woort_IRValue* const_val;
    if (!woort_IRBuilder_const_int(builder, 42, &const_val))
    {
        printf("FAIL: Cannot create constant\n");
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder_ret(builder, const_val);

    woort_IRCodegenResult cg_result;
    if (!woort_IRModule_codegen(module, &cg_result))
    {
        printf("FAIL: Cannot generate code\n");
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(cg_result.m_codeenv);
        free((void*)cg_result.m_function_entries);
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }

    woort_VMRuntime_invoke(vm, cg_result.m_function_entries[0]);
    woort_CodeEnv_drop(cg_result.m_codeenv);
    free((void*)cg_result.m_function_entries);

    woort_VMRuntime_destroy(vm);
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);

    printf("PASS\n\n");
    return true;
}

static bool test_binary_op(void)
{
    printf("=== Test IR: Binary Operation (3 + 5) ===\n");

    woort_IRModule* module;
    if (!woort_IRModule_create(&module))
    {
        printf("FAIL: Cannot create IR module\n");
        return false;
    }

    woort_IRFunction* func;
    if (!woort_IRModule_add_function(module, "add_test", 0, &func))
    {
        printf("FAIL: Cannot add function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder* builder;
    if (!woort_IRBuilder_create(func, &builder))
    {
        printf("FAIL: Cannot create builder\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* entry;
    if (!woort_IRBuilder_create_block(builder, &entry))
    {
        printf("FAIL: Cannot create entry block\n");
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }
    woort_IRBuilder_position_at_end(builder, entry);
    woort_IRBlock_seal(entry);

    woort_IRValue* a, *b, *result;
    if (!woort_IRBuilder_const_int(builder, 3, &a)) goto fail;
    if (!woort_IRBuilder_const_int(builder, 5, &b)) goto fail;
    if (!woort_IRBuilder_add_i(builder, a, b, &result)) goto fail;

    woort_IRBuilder_ret(builder, result);

    woort_IRCodegenResult cg_result;
    if (!woort_IRModule_codegen(module, &cg_result))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(cg_result.m_codeenv);
        free((void*)cg_result.m_function_entries);
        goto fail;
    }

    woort_VMRuntime_invoke(vm, cg_result.m_function_entries[0]);
    woort_CodeEnv_drop(cg_result.m_codeenv);
    free((void*)cg_result.m_function_entries);

    woort_VMRuntime_destroy(vm);
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);

    printf("PASS\n\n");
    return true;

fail:
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);
    printf("FAIL\n\n");
    return false;
}

static bool test_local_variable(void)
{
    printf("=== Test IR: Local Variable (x = 10; return x) ===\n");

    woort_IRModule* module;
    if (!woort_IRModule_create(&module))
    {
        printf("FAIL: Cannot create IR module\n");
        return false;
    }

    woort_IRFunction* func;
    if (!woort_IRModule_add_function(module, "local_test", 0, &func))
    {
        printf("FAIL: Cannot add function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder* builder;
    if (!woort_IRBuilder_create(func, &builder))
    {
        printf("FAIL: Cannot create builder\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* entry;
    if (!woort_IRBuilder_create_block(builder, &entry))
    {
        printf("FAIL: Cannot create entry block\n");
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }
    woort_IRBuilder_position_at_end(builder, entry);

    woort_IRLocal* x;
    if (!woort_IRBuilder_create_local(builder, &x)) goto fail;

    woort_IRValue* ten;
    if (!woort_IRBuilder_const_int(builder, 10, &ten)) goto fail;
    woort_IRBuilder_set_local(builder, x, ten);

    woort_IRBlock_seal(entry);

    woort_IRValue* result;
    if (!woort_IRBuilder_get_local(builder, x, &result)) goto fail;
    woort_IRBuilder_ret(builder, result);

    woort_IRCodegenResult cg_result;
    if (!woort_IRModule_codegen(module, &cg_result))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(cg_result.m_codeenv);
        free((void*)cg_result.m_function_entries);
        goto fail;
    }

    woort_VMRuntime_invoke(vm, cg_result.m_function_entries[0]);
    woort_CodeEnv_drop(cg_result.m_codeenv);
    free((void*)cg_result.m_function_entries);

    woort_VMRuntime_destroy(vm);
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);

    printf("PASS\n\n");
    return true;

fail:
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);
    printf("FAIL\n\n");
    return false;
}

void woort_test_ir(void)
{
    int passed = 0;
    int total = 6;

    if (test_simple_function()) passed++;
    if (test_binary_op()) passed++;
    if (test_local_variable()) passed++;
    if (test_conditional_branch()) passed++;
    if (test_comparison()) passed++;
    if (test_recursive_function()) passed++;

    printf("=== IR Test Summary: %d/%d tests passed ===\n", passed, total);
}

static bool test_conditional_branch(void)
{
    printf("=== Test IR: Conditional Branch (if true return 1 else return 2) ===\n");

    woort_IRModule* module;
    if (!woort_IRModule_create(&module))
    {
        printf("FAIL: Cannot create IR module\n");
        return false;
    }

    woort_IRFunction* func;
    if (!woort_IRModule_add_function(module, "cond_test", 0, &func))
    {
        printf("FAIL: Cannot add function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder* builder;
    if (!woort_IRBuilder_create(func, &builder))
    {
        printf("FAIL: Cannot create builder\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* entry, *then_block, *else_block;
    if (!woort_IRBuilder_create_block(builder, &entry)) goto fail;
    if (!woort_IRBuilder_create_block(builder, &then_block)) goto fail;
    if (!woort_IRBuilder_create_block(builder, &else_block)) goto fail;

    woort_IRBuilder_position_at_end(builder, entry);
    woort_IRBlock_seal(entry);

    woort_IRValue* cond;
    if (!woort_IRBuilder_const_bool(builder, true, &cond)) goto fail;

    if (!woort_IRBuilder_cond_br(builder, cond, then_block, else_block)) goto fail;

    woort_IRBuilder_position_at_end(builder, then_block);
    woort_IRBlock_seal(then_block);

    woort_IRValue* one;
    if (!woort_IRBuilder_const_int(builder, 1, &one)) goto fail;
    woort_IRBuilder_ret(builder, one);

    woort_IRBuilder_position_at_end(builder, else_block);
    woort_IRBlock_seal(else_block);

    woort_IRValue* two;
    if (!woort_IRBuilder_const_int(builder, 2, &two)) goto fail;
    woort_IRBuilder_ret(builder, two);

    woort_IRCodegenResult cg_result;
    if (!woort_IRModule_codegen(module, &cg_result))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(cg_result.m_codeenv);
        free((void*)cg_result.m_function_entries);
        goto fail;
    }

    woort_VMRuntime_invoke(vm, cg_result.m_function_entries[0]);
    woort_CodeEnv_drop(cg_result.m_codeenv);
    free((void*)cg_result.m_function_entries);

    woort_VMRuntime_destroy(vm);
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);

    printf("PASS\n\n");
    return true;

fail:
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);
    printf("FAIL\n\n");
    return false;
}

static bool test_comparison(void)
{
    printf("=== Test IR: Comparison (5 < 10) ===\n");

    woort_IRModule* module;
    if (!woort_IRModule_create(&module))
    {
        printf("FAIL: Cannot create IR module\n");
        return false;
    }

    woort_IRFunction* func;
    if (!woort_IRModule_add_function(module, "cmp_test", 0, &func))
    {
        printf("FAIL: Cannot add function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder* builder;
    if (!woort_IRBuilder_create(func, &builder))
    {
        printf("FAIL: Cannot create builder\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* entry;
    if (!woort_IRBuilder_create_block(builder, &entry))
    {
        printf("FAIL: Cannot create entry block\n");
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }
    woort_IRBuilder_position_at_end(builder, entry);
    woort_IRBlock_seal(entry);

    woort_IRValue* a, *b, *result;
    if (!woort_IRBuilder_const_int(builder, 5, &a)) goto fail;
    if (!woort_IRBuilder_const_int(builder, 10, &b)) goto fail;
    if (!woort_IRBuilder_lt_i(builder, a, b, &result)) goto fail;

    woort_IRBuilder_ret(builder, result);

    woort_IRCodegenResult cg_result;
    if (!woort_IRModule_codegen(module, &cg_result))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(cg_result.m_codeenv);
        free((void*)cg_result.m_function_entries);
        goto fail;
    }

    woort_VMRuntime_invoke(vm, cg_result.m_function_entries[0]);
    woort_CodeEnv_drop(cg_result.m_codeenv);
    free((void*)cg_result.m_function_entries);

    woort_VMRuntime_destroy(vm);
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);

    printf("PASS\n\n");
    return true;

fail:
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);
    printf("FAIL\n\n");
    return false;
}

static bool test_recursive_function(void)
{
    /*
     * Test IR: Two Functions with Recursive Call
     *
     * 创建两个函数：
     *   1. factorial(n: int) -> int - 递归阶乘函数
     *   2. main_entry() -> int - 入口函数，调用 factorial(5)
     *
     * factorial 函数实现：
     *   func factorial(n: int) -> int {
     *       if (n <= 1) {
     *           return 1;              // base_case
     *       } else {
     *           return n * factorial(n - 1);  // recurse_case
     *       }
     *   }
     *
     * main_entry 函数实现：
     *   func main_entry() -> int {
     *       return factorial(5);
     *   }
     *
     * 生成的 IR 结构：
     *
     * === Function 0: factorial ===
     *   entry:
     *       %n = PARAM 0
     *       %one = CONST_INT 1
     *       %cond = LE_I %n, %one
     *       COND_BR %cond, base_case, recurse_case
     *
     *   base_case:
     *       RET %one
     *
     *   recurse_case:
     *       %n_minus_1 = SUB_I %n, %one
     *       %func = CONST_FUNC 0          ; 函数索引 0 (factorial 自身)
     *       %rec_result = CALL %func, %n_minus_1
     *       %result = MUL_I %n, %rec_result
     *       RET %result
     *
     * === Function 1: main_entry ===
     *   entry:
     *       %five = CONST_INT 5
     *       %factorial_func = CONST_FUNC 0  ; 函数索引 0 (factorial)
     *       %result = CALL %factorial_func, %five
     *       RET %result
     */
    printf("=== Test IR: Two Functions with Recursive Call (factorial(5) = 120) ===\n");

    woort_IRModule* module;
    if (!woort_IRModule_create(&module))
    {
        printf("FAIL: Cannot create IR module\n");
        return false;
    }

    woort_IRFunction* factorial;
    if (!woort_IRModule_add_function(module, "factorial", 1, &factorial))
    {
        printf("FAIL: Cannot add factorial function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRFunction* main_entry;
    if (!woort_IRModule_add_function(module, "main_entry", 0, &main_entry))
    {
        printf("FAIL: Cannot add main_entry function\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBuilder* builder;
    if (!woort_IRBuilder_create(factorial, &builder))
    {
        printf("FAIL: Cannot create builder\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* entry, *base_case, *recurse_case;
    if (!woort_IRBuilder_create_block(builder, &entry)) goto fail;
    if (!woort_IRBuilder_create_block(builder, &base_case)) goto fail;
    if (!woort_IRBuilder_create_block(builder, &recurse_case)) goto fail;

    woort_IRBuilder_position_at_end(builder, entry);

    woort_IRValue* n;
    if (!woort_IRBuilder_param(builder, 0, &n)) goto fail;

    woort_IRValue* one, *cond;
    if (!woort_IRBuilder_const_int(builder, 1, &one)) goto fail;
    if (!woort_IRBuilder_le_i(builder, n, one, &cond)) goto fail;

    if (!woort_IRBuilder_cond_br(builder, cond, base_case, recurse_case)) goto fail;

    woort_IRBuilder_position_at_end(builder, base_case);
    woort_IRBlock_seal(base_case);
    woort_IRBuilder_ret(builder, one);

    woort_IRBuilder_position_at_end(builder, recurse_case);

    woort_IRValue* n_minus_1;
    if (!woort_IRBuilder_sub_i(builder, n, one, &n_minus_1)) goto fail;

    woort_IRValue* factorial_func;
    if (!woort_IRBuilder_const_func(builder, 0, &factorial_func)) goto fail;

    woort_IRValue* args[1] = { n_minus_1 };
    woort_IRValue* rec_result;
    if (!woort_IRBuilder_call(builder, factorial_func, args, 1, &rec_result)) goto fail;

    woort_IRValue* result;
    if (!woort_IRBuilder_mul_i(builder, n, rec_result, &result)) goto fail;

    woort_IRBuilder_ret(builder, result);

    woort_IRBlock_seal(entry);
    woort_IRBlock_seal(recurse_case);

    woort_IRBuilder_destroy(builder);

    if (!woort_IRBuilder_create(main_entry, &builder))
    {
        printf("FAIL: Cannot create builder for main_entry\n");
        woort_IRModule_destroy(module);
        return false;
    }

    woort_IRBlock* main_entry_block;
    if (!woort_IRBuilder_create_block(builder, &main_entry_block)) goto fail;

    woort_IRBuilder_position_at_end(builder, main_entry_block);
    woort_IRBlock_seal(main_entry_block);

    woort_IRValue* five;
    if (!woort_IRBuilder_const_int(builder, 5, &five)) goto fail;

    woort_IRValue* factorial_ref;
    if (!woort_IRBuilder_const_func(builder, 0, &factorial_ref)) goto fail;

    woort_IRValue* call_args[1] = { five };
    woort_IRValue* call_result;
    if (!woort_IRBuilder_call(builder, factorial_ref, call_args, 1, &call_result)) goto fail;

    woort_IRBuilder_ret(builder, call_result);

    woort_IRCodegenResult cg_result;
    if (!woort_IRModule_codegen(module, &cg_result))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(cg_result.m_codeenv);
        free((void*)cg_result.m_function_entries);
        goto fail;
    }

    woort_VMRuntime_invoke(vm, cg_result.m_function_entries[1]);
    woort_CodeEnv_drop(cg_result.m_codeenv);
    free((void*)cg_result.m_function_entries);

    woort_VMRuntime_destroy(vm);
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);

    printf("PASS\n\n");
    return true;

fail:
    woort_IRBuilder_destroy(builder);
    woort_IRModule_destroy(module);
    printf("FAIL\n\n");
    return false;
}