#include "woort.h"

#include "ir/woort_ir.h"
#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_gc_string.h"

#include <stdio.h>
#include <stdbool.h>

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

    woort_CodeEnv* codeenv;
    if (!woort_IRModule_codegen(module, &codeenv))
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
        woort_CodeEnv_drop(codeenv);
        woort_IRBuilder_destroy(builder);
        woort_IRModule_destroy(module);
        return false;
    }

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

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

    woort_CodeEnv* codeenv;
    if (!woort_IRModule_codegen(module, &codeenv))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(codeenv);
        goto fail;
    }

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

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

    woort_CodeEnv* codeenv;
    if (!woort_IRModule_codegen(module, &codeenv))
    {
        printf("FAIL: Cannot generate code\n");
        goto fail;
    }

    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
    {
        printf("FAIL: Cannot create VM\n");
        woort_CodeEnv_drop(codeenv);
        goto fail;
    }

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

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
    int total = 3;

    if (test_simple_function()) passed++;
    if (test_binary_op()) passed++;
    if (test_local_variable()) passed++;

    printf("=== IR Test Summary: %d/%d tests passed ===\n", passed, total);
}