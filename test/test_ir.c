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
    int total = 5;

    if (test_simple_function()) passed++;
    if (test_binary_op()) passed++;
    if (test_local_variable()) passed++;
    if (test_conditional_branch()) passed++;
    if (test_comparison()) passed++;

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