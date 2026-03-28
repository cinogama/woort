#include "woort.h"

#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_codeenv.h"
#include "woort_vm.h"

#include <stdio.h>
#include <assert.h>

static void test_ir_compiler_lifecycle(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 0, &func));
    assert(func != NULL);

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&compiler);
    assert(c0 == 0);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&compiler);
    assert(c1 == 1);

    woort_IRStaticIndex s0 = woort_IRCompiler_add_static(&compiler);
    assert(s0 == 0);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_compiler_lifecycle\n");
}

static void test_ir_add_block(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 0, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);
    assert(woort_IRFunction_entry_block(func) == entry);

    woort_IRBlock* block2 = woort_IRFuntion_add_block(func);
    assert(block2 != NULL);
    assert(block2 != entry);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_add_block\n");
}

static void test_ir_get_argument(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 3, &func));

    woort_IRValue* arg0 = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* arg1 = woort_IRFunction_get_argument(func, 1);
    woort_IRValue* arg2 = woort_IRFunction_get_argument(func, 2);

    assert(arg0 != NULL);
    assert(arg1 != NULL);
    assert(arg2 != NULL);

    assert(arg0->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT);
    assert(arg1->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT);
    assert(arg2->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT);

    assert(arg0->m_argument_idx == 0);
    assert(arg1->m_argument_idx == 1);
    assert(arg2->m_argument_idx == 2);

    assert(arg0->m_assigned_stack_offset == 3);
    assert(arg1->m_assigned_stack_offset == 4);
    assert(arg2->m_assigned_stack_offset == 5);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_get_argument\n");
}

static void test_ir_load_constant(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&compiler);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 0, &func));

    woort_IRValue* val0 = woort_IRFuntion_load_constant(func, c0);
    assert(val0 != NULL);
    assert(val0->m_source == WOORT_IRVALUE_SOURCE_CONSTANT);
    assert(val0->m_constant == c0);
    assert(!val0->m_constant_need_stack_slot);

    woort_IRValue* val0_again = woort_IRFuntion_load_constant(func, c0);
    assert(val0_again == val0);

    woort_IRValue* val1 = woort_IRFuntion_load_constant(func, c1);
    assert(val1 != val0);
    assert(val1->m_constant == c1);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_load_constant\n");
}

static void test_ir_arithmetic(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    woort_IRValue* sum = woort_IRBlock_ADDI(entry, a, b);
    assert(sum != NULL);
    assert(sum->m_source == WOORT_IRVALUE_SOURCE_RESULT);

    woort_IRValue* diff = woort_IRBlock_SUBI(entry, a, b);
    assert(diff != NULL);

    woort_IRValue* neg_a = woort_IRBlock_NEGI(entry, a);
    assert(neg_a != NULL);

    woort_IRValue* result = woort_IRBlock_SUBI(entry, sum, a);
    assert(result != NULL);

    woort_IRBlock_ret(entry, result);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_arithmetic\n");
}

static void test_ir_comparison(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_LTI(entry, a, b) != NULL);
    assert(woort_IRBlock_GTI(entry, a, b) != NULL);
    assert(woort_IRBlock_LEI(entry, a, b) != NULL);
    assert(woort_IRBlock_GEI(entry, a, b) != NULL);
    assert(woort_IRBlock_EQI(entry, a, b) != NULL);
    assert(woort_IRBlock_NEI(entry, a, b) != NULL);

    woort_IRBlock_ret(entry, a);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_comparison\n");
}

static void test_ir_branch(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    woort_IRBlock* then_block = woort_IRFuntion_add_block(func);
    woort_IRBlock* else_block = woort_IRFuntion_add_block(func);
    assert(entry != NULL);
    assert(then_block != NULL);
    assert(else_block != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_br_lt(entry, a, b, then_block, else_block));
    woort_IRBlock_ret(then_block, a);
    woort_IRBlock_ret(else_block, b);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_branch\n");
}

static void test_ir_unconditional_branch(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 1, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    woort_IRBlock* target = woort_IRFuntion_add_block(func);
    assert(entry != NULL);
    assert(target != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    assert(woort_IRBlock_br(entry, target));
    woort_IRBlock_ret(target, a);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_unconditional_branch\n");
}

static void test_ir_store_load(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRStaticIndex s0 = woort_IRCompiler_add_static(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 1, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);

    woort_IRBlock_STORE(entry, s0, a);

    woort_IRValue* loaded = woort_IRBlock_LOAD(entry, s0);
    assert(loaded != NULL);

    woort_IRBlock_ret(entry, loaded);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_store_load\n");
}

static void test_ir_push_pop(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 1, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);

    woort_IRBlock_PUSHCHK(entry, a);
    woort_IRValue* popped = woort_IRBlock_POP(entry);
    assert(popped != NULL);

    woort_IRBlock_ret(entry, popped);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_push_pop\n");
}

static void test_ir_logic_ops(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_LAND(entry, a, b) != NULL);
    assert(woort_IRBlock_LOR(entry, a, b) != NULL);
    assert(woort_IRBlock_LNOT(entry, a) != NULL);

    woort_IRBlock_ret(entry, a);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_logic_ops\n");
}

static void test_ir_real_arithmetic(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_ADDR(entry, a, b) != NULL);
    assert(woort_IRBlock_SUBR(entry, a, b) != NULL);
    assert(woort_IRBlock_NEGR(entry, a) != NULL);

    woort_IRBlock_ret(entry, a);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_real_arithmetic\n");
}

static void test_ir_type_conversions(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 1, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);

    woort_IRValue* as_real = woort_IRBlock_ITOR(entry, a);
    assert(as_real != NULL);

    woort_IRValue* back_to_int = woort_IRBlock_RTOI(entry, as_real);
    assert(back_to_int != NULL);

    woort_IRBlock_ret(entry, back_to_int);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_type_conversions\n");
}

static void test_ir_mkvec(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 3, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);
    woort_IRValue* c = woort_IRFunction_get_argument(func, 2);

    woort_IRBlock_PUSHCHK(entry, a);
    woort_IRBlock_PUSHCHK(entry, b);
    woort_IRBlock_PUSHCHK(entry, c);

    woort_IRValue* vec = woort_IRBlock_MKVEC(entry, 3);
    assert(vec != NULL);

    woort_IRBlock_ret(entry, vec);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_mkvec\n");
}

static void test_ir_compile_simple(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRConstantIndex c_42 = woort_IRCompiler_add_constant(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 1, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* val_42 = woort_IRFuntion_load_constant(func, c_42);
    assert(val_42 != NULL);
    woort_IRValue_ensure_constant_stack_slot(val_42);

    woort_IRValue* sum = woort_IRBlock_ADDI(entry, a, val_42);
    assert(sum != NULL);

    woort_IRBlock_ret(entry, sum);

    woort_CodeEnv* codeenv;
    assert(woort_IRCompiler_finish(&compiler, &codeenv));
    assert(codeenv != NULL);

    codeenv->m_data_begin[c_42].m_integer = 42;

    assert(codeenv->m_code_begin != NULL);
    assert(codeenv->m_code_end != NULL);
    assert(codeenv->m_code_end > codeenv->m_code_begin);

    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_compile_simple\n");
}

static void test_ir_compile_branch(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRConstantIndex c_0 = woort_IRCompiler_add_constant(&compiler);
    woort_IRConstantIndex c_1 = woort_IRCompiler_add_constant(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    woort_IRBlock* less_block = woort_IRFuntion_add_block(func);
    woort_IRBlock* ge_block = woort_IRFuntion_add_block(func);
    assert(entry != NULL);
    assert(less_block != NULL);
    assert(ge_block != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_br_lt(entry, a, b, less_block, ge_block));
    woort_IRBlock_ret(less_block, a);
    woort_IRBlock_ret(ge_block, b);

    woort_CodeEnv* codeenv;
    assert(woort_IRCompiler_finish(&compiler, &codeenv));
    assert(codeenv != NULL);

    codeenv->m_data_begin[c_0].m_integer = 0;
    codeenv->m_data_begin[c_1].m_integer = 1;

    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_compile_branch\n");
}

static woort_api print_int_for_test(woort_vm vm, woort_value* args)
{
    (void)vm;
    (void)printf("%lld\n", ((woort_Value*)args)->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static void test_ir_compile_and_run(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    /*
     * 常量池:
     *   [0] = 整数 10
     *   [1] = 整数 20
     *   [2] = 原生函数 print_int
     */
    woort_IRConstantIndex c_10 = woort_IRCompiler_add_constant(&compiler);
    woort_IRConstantIndex c_20 = woort_IRCompiler_add_constant(&compiler);
    woort_IRConstantIndex c_print = woort_IRCompiler_add_constant(&compiler);

    /*
     * 函数 main():
     *   val_10 = load_constant(10)  (需要栈槽)
     *   val_20 = load_constant(20)  (需要栈槽)
     *   sum = ADDI val_10, val_20
     *   PUSHCHK sum
     *   CALLNFP print_int (argc_to_pop=1)
     *   POPR 1
     *   ret_void
     */
    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 0, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* val_10 = woort_IRFuntion_load_constant(func, c_10);
    woort_IRValue* val_20 = woort_IRFuntion_load_constant(func, c_20);
    assert(val_10 != NULL);
    assert(val_20 != NULL);

    woort_IRValue_ensure_constant_stack_slot(val_10);
    woort_IRValue_ensure_constant_stack_slot(val_20);

    woort_IRValue* sum = woort_IRBlock_ADDI(entry, val_10, val_20);
    assert(sum != NULL);

    woort_IRBlock_PUSHCHK(entry, sum);
    woort_IRBlock_CALLNFP(entry, c_print, 1, NULL);
    woort_IRBlock_POPR(entry, 1);
    woort_IRBlock_ret_void(entry);

    woort_CodeEnv* codeenv;
    assert(woort_IRCompiler_finish(&compiler, &codeenv));
    assert(codeenv != NULL);

    codeenv->m_data_begin[c_10].m_integer = 10;
    codeenv->m_data_begin[c_20].m_integer = 20;
    codeenv->m_data_begin[c_print].m_native_or_jit_function = &print_int_for_test;

    woort_VMRuntime* vm;
    assert(woort_VMRuntime_create(&vm));

    (void)woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_compile_and_run\n");
}

static void test_ir_branch_macros(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    woort_IRBlock* gt_block = woort_IRFuntion_add_block(func);
    woort_IRBlock* le_block = woort_IRFuntion_add_block(func);
    assert(entry != NULL);
    assert(gt_block != NULL);
    assert(le_block != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_br_gt(entry, a, b, gt_block, le_block));
    woort_IRBlock_ret(gt_block, a);
    woort_IRBlock_ret(le_block, b);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_branch_macros\n");
}

static void test_ir_string_ops(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 2, &func));

    woort_IRBlock* entry = woort_IRFuntion_add_block(func);
    assert(entry != NULL);

    woort_IRValue* a = woort_IRFunction_get_argument(func, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(func, 1);

    assert(woort_IRBlock_ADDS(entry, a, b) != NULL);
    assert(woort_IRBlock_LTS(entry, a, b) != NULL);
    assert(woort_IRBlock_EQS(entry, a, b) != NULL);

    woort_IRBlock_ret(entry, a);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_string_ops\n");
}

static void test_ir_constant_ensure_stack_slot(void)
{
    woort_IRCompiler compiler;
    woort_IRCompiler_init(&compiler);

    woort_IRFunction* func;
    assert(woort_IRCompiler_add_function(&compiler, 0, &func));

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&compiler);
    woort_IRValue* val = woort_IRFuntion_load_constant(func, c0);
    assert(val != NULL);
    assert(!val->m_constant_need_stack_slot);

    woort_IRValue* result = woort_IRValue_ensure_constant_stack_slot(val);
    assert(result == val);
    assert(val->m_constant_need_stack_slot);

    woort_IRCompiler_deinit(&compiler);
    printf("  [PASS] test_ir_constant_ensure_stack_slot\n");
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init();

    printf("[IR Test Suite]\n");

    test_ir_compiler_lifecycle();
    test_ir_add_block();
    test_ir_get_argument();
    test_ir_load_constant();
    test_ir_constant_ensure_stack_slot();
    test_ir_arithmetic();
    test_ir_comparison();
    test_ir_branch();
    test_ir_unconditional_branch();
    test_ir_branch_macros();
    test_ir_store_load();
    test_ir_push_pop();
    test_ir_logic_ops();
    test_ir_real_arithmetic();
    test_ir_type_conversions();
    test_ir_string_ops();
    test_ir_mkvec();
    test_ir_compile_simple();
    test_ir_compile_branch();
    test_ir_compile_and_run();

    printf("[All IR tests passed!]\n");

    woort_shutdown();
    return 0;
}
