/*
 * test_ir.c
 * 
 * IR 编译器接口测试。
 */

#include "woort.h"
#include "woort_ir.h"
#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"

#include "woort_disassembly.h"

#include <stdio.h>
#include <string.h>

/*
 * print_int
 * 
 * 打印整数的原生函数。
 */
woort_api print_int(woort_vm vm, woort_value* args)
{
    (void)vm;
    printf("%lld\n", ((woort_Value*)args)->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

/*
 * test_ir_basic
 * 
 * 测试 IR 基本功能：创建编译器、分配全局索引、创建函数。
 */
static bool test_ir_basic(void)
{
    printf("=== Test IR Basic ===\n");
    
    /* 初始化 IR */
    if (!woort_IRCompiler_bootup())
    {
        printf("FAIL: woort_IRCompiler_bootup failed\n");
        return false;
    }
    printf("  woort_IRCompiler_bootup: OK\n");
    
    /* 创建编译器 */
    woort_IRCompiler* irc;
    if (!woort_IRCompiler_create(&irc))
    {
        printf("FAIL: woort_IRCompiler_create failed\n");
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRCompiler_create: OK\n");
    
    /* 分配全局索引 */
    woort_IRGlobalIndex idx0 = woort_IRCompiler_allocate_global(irc);
    woort_IRGlobalIndex idx1 = woort_IRCompiler_allocate_global(irc);
    woort_IRGlobalIndex idx2 = woort_IRCompiler_allocate_global(irc);
    
    if (idx0 != 0 || idx1 != 1 || idx2 != 2)
    {
        printf("FAIL: global index allocation wrong\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRCompiler_allocate_global: OK (indices: %zu, %zu, %zu)\n", 
           (size_t)idx0, (size_t)idx1, (size_t)idx2);
    
    /* 分配连续全局索引 */
    woort_IRGlobalIndex range_begin;
    if (!woort_IRCompiler_allocate_global_range(irc, 5, &range_begin))
    {
        printf("FAIL: woort_IRCompiler_allocate_global_range failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    if (range_begin != 3)
    {
        printf("FAIL: range_begin should be 3, got %zu\n", (size_t)range_begin);
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRCompiler_allocate_global_range: OK (begin: %zu, count: 5)\n", 
           (size_t)range_begin);
    
    /* 创建函数 */
    woort_IRFunction* func;
    if (!woort_IRCompiler_add_function(irc, &func))
    {
        printf("FAIL: woort_IRCompiler_add_function failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRCompiler_add_function: OK\n");
    
    /* 获取入口块 */
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    if (!entry)
    {
        printf("FAIL: woort_IRFunction_get_entry_block returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRFunction_get_entry_block: OK\n");
    
    /* 检查入口块未终结 */
    if (woort_IRBlock_is_terminated(entry))
    {
        printf("FAIL: entry block should not be terminated yet\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_is_terminated (before): OK (not terminated)\n");
    
    /* 添加 void 返回 */
    if (!woort_IRBlock_ret_void(entry))
    {
        printf("FAIL: woort_IRBlock_ret_void failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_ret_void: OK\n");
    
    /* 检查入口块已终结 */
    if (!woort_IRBlock_is_terminated(entry))
    {
        printf("FAIL: entry block should be terminated now\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_is_terminated (after): OK (terminated)\n");
    
    /* 检查函数数量 */
    if (woort_IRCompiler_get_function_count(irc) != 1)
    {
        printf("FAIL: function count should be 1\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRCompiler_get_function_count: OK (count: 1)\n");
    
    /* 销毁编译器 */
    woort_IRCompiler_destroy(irc);
    printf("  woort_IRCompiler_destroy: OK\n");
    
    /* 关闭 IR */
    woort_IRCompiler_shutdown();
    printf("  woort_IRCompiler_shutdown: OK\n");
    
    printf("=== Test IR Basic: PASS ===\n\n");
    return true;
}

/*
 * test_ir_function_and_blocks
 * 
 * 测试 IRFunction 和 IRBlock 功能。
 */
static bool test_ir_function_and_blocks(void)
{
    printf("=== Test IR Function and Blocks ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    /* 获取入口块 */
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    /* 添加新基本块 */
    woort_IRBlock* block1;
    woort_IRBlock* block2;
    if (!woort_IRFunction_add_block(func, &block1) ||
        !woort_IRFunction_add_block(func, &block2))
    {
        printf("FAIL: woort_IRFunction_add_block failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRFunction_add_block: OK (added 2 blocks)\n");
    
    /* 检查块所属函数 */
    if (woort_IRBlock_get_function(entry) != func ||
        woort_IRBlock_get_function(block1) != func)
    {
        printf("FAIL: woort_IRBlock_get_function returned wrong function\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_get_function: OK\n");
    
    /* 检查前驱/后继数量（初始状态） */
    if (woort_IRBlock_get_predecessor_count(block1) != 0 ||
        woort_IRBlock_get_successor_count(entry) != 0)
    {
        printf("FAIL: initial predecessor/successor count wrong\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Initial predecessor/successor count: OK\n");
    
    /* 测试无条件跳转 */
    if (!woort_IRBlock_br(entry, block1))
    {
        printf("FAIL: woort_IRBlock_br failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_br: OK\n");
    
    /* 检查前驱/后继数量（跳转后） */
    if (woort_IRBlock_get_successor_count(entry) != 1 ||
        woort_IRBlock_get_predecessor_count(block1) != 1)
    {
        printf("FAIL: predecessor/successor count after br wrong\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Predecessor/successor count after br: OK\n");
    
    /* 添加返回 */
    if (!woort_IRBlock_ret_void(block1))
    {
        printf("FAIL: woort_IRBlock_ret_void on block1 failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_ret_void: OK\n");
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Function and Blocks: PASS ===\n\n");
    return true;
}

/*
 * test_ir_values
 * 
 * 测试 IRValue 相关功能。
 */
static bool test_ir_values(void)
{
    printf("=== Test IR Values ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    /* 分配全局索引 */
    woort_IRGlobalIndex const_idx = woort_IRCompiler_allocate_global(irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    /* 测试加载常量 */
    const woort_IRValue* const_val = woort_IRFunction_load_const(func, const_idx);
    if (!const_val)
    {
        printf("FAIL: woort_IRFunction_load_const returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRFunction_load_const: OK\n");
    
    /* 检查常量属性 */
    if (!woort_IRValue_is_const(const_val))
    {
        printf("FAIL: woort_IRValue_is_const returned false for const\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRValue_is_const: OK\n");
    
    if (woort_IRValue_get_global_index(const_val) != const_idx)
    {
        printf("FAIL: woort_IRValue_get_global_index returned wrong index\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRValue_get_global_index: OK\n");
    
    /* 测试加载参数 */
    const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
    const woort_IRValue* arg1 = woort_IRFunction_load_argument(func, 1);
    if (!arg0 || !arg1)
    {
        printf("FAIL: woort_IRFunction_load_argument returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRFunction_load_argument: OK\n");
    
    /* 检查参数属性 */
    if (!woort_IRValue_is_argument(arg0))
    {
        printf("FAIL: woort_IRValue_is_argument returned false for argument\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRValue_is_argument: OK\n");
    
    if (woort_IRValue_get_argument_index(arg1) != 1)
    {
        printf("FAIL: woort_IRValue_get_argument_index returned wrong index\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRValue_get_argument_index: OK\n");
    
    /* 常量不应该是参数 */
    if (woort_IRValue_is_argument(const_val))
    {
        printf("FAIL: const should not be argument\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    
    /* 参数不应该是常量 */
    if (woort_IRValue_is_const(arg0))
    {
        printf("FAIL: argument should not be const\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  is_const/is_argument consistency: OK\n");
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    woort_IRBlock_ret_void(entry);
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Values: PASS ===\n\n");
    return true;
}

/*
 * test_ir_arithmetic
 * 
 * 测试算术指令。
 */
static bool test_ir_arithmetic(void)
{
    printf("=== Test IR Arithmetic ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    /* 创建一些值 */
    const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
    const woort_IRValue* arg1 = woort_IRFunction_load_argument(func, 1);
    
    /* 测试整数加法 */
    const woort_IRValue* add_result = woort_IRBlock_ADDI(entry, arg0, arg1);
    if (!add_result)
    {
        printf("FAIL: woort_IRBlock_ADDI returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_ADDI: OK\n");
    
    /* 测试整数减法 */
    const woort_IRValue* sub_result = woort_IRBlock_SUBI(entry, arg0, arg1);
    if (!sub_result)
    {
        printf("FAIL: woort_IRBlock_SUBI returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_SUBI: OK\n");
    
    /* 测试整数乘法 */
    const woort_IRValue* mul_result = woort_IRBlock_MULI(entry, arg0, arg1);
    if (!mul_result)
    {
        printf("FAIL: woort_IRBlock_MULI returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_MULI: OK\n");
    
    /* 测试整数除法 */
    const woort_IRValue* div_result = woort_IRBlock_DIVI(entry, arg0, arg1);
    if (!div_result)
    {
        printf("FAIL: woort_IRBlock_DIVI returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_DIVI: OK\n");
    
    /* 测试整数取模 */
    const woort_IRValue* mod_result = woort_IRBlock_MODI(entry, arg0, arg1);
    if (!mod_result)
    {
        printf("FAIL: woort_IRBlock_MODI returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_MODI: OK\n");
    
    /* 测试整数取负 */
    const woort_IRValue* neg_result = woort_IRBlock_NEGI(entry, arg0);
    if (!neg_result)
    {
        printf("FAIL: woort_IRBlock_NEGI returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_NEGI: OK\n");
    
    /* 测试实数加法 */
    const woort_IRValue* addr_result = woort_IRBlock_ADDR(entry, arg0, arg1);
    if (!addr_result)
    {
        printf("FAIL: woort_IRBlock_ADDR returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_ADDR: OK\n");
    
    /* 测试比较指令 */
    const woort_IRValue* lt_result = woort_IRBlock_LTI(entry, arg0, arg1);
    const woort_IRValue* gt_result = woort_IRBlock_GTI(entry, arg0, arg1);
    const woort_IRValue* eq_result = woort_IRBlock_EQI(entry, arg0, arg1);
    if (!lt_result || !gt_result || !eq_result)
    {
        printf("FAIL: comparison instructions returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Comparison instructions (LTI, GTI, EQI): OK\n");
    
    /* 测试逻辑指令 */
    const woort_IRValue* and_result = woort_IRBlock_LAND(entry, arg0, arg1);
    const woort_IRValue* or_result = woort_IRBlock_LOR(entry, arg0, arg1);
    const woort_IRValue* not_result = woort_IRBlock_LNOT(entry, arg0);
    if (!and_result || !or_result || !not_result)
    {
        printf("FAIL: logical instructions returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Logical instructions (LAND, LOR, LNOT): OK\n");
    
    woort_IRBlock_ret_void(entry);
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Arithmetic: PASS ===\n\n");
    return true;
}

/*
 * test_ir_condbr
 * 
 * 测试条件跳转。
 */
static bool test_ir_condbr(void)
{
    printf("=== Test IR Conditional Branch ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    /* 创建基本块 */
    woort_IRBlock* then_block;
    woort_IRBlock* else_block;
    woort_IRFunction_add_block(func, &then_block);
    woort_IRFunction_add_block(func, &else_block);
    
    /* 创建值 */
    const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
    
    woort_IRGlobalIndex const_idx = woort_IRCompiler_allocate_global(irc);
    const woort_IRValue* const_val = woort_IRFunction_load_const(func, const_idx);
    
    /* 测试条件跳转（小于） */
    if (!woort_IRBlock_condbr_less_then(entry, arg0, const_val, then_block, else_block))
    {
        printf("FAIL: woort_IRBlock_condbr_less_then failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_condbr_less_then: OK\n");
    
    /* 检查后继数量 */
    if (woort_IRBlock_get_successor_count(entry) != 2)
    {
        printf("FAIL: entry should have 2 successors\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Successor count after condbr: OK (2 successors)\n");
    
    /* 检查前驱数量 */
    if (woort_IRBlock_get_predecessor_count(then_block) != 1 ||
        woort_IRBlock_get_predecessor_count(else_block) != 1)
    {
        printf("FAIL: then/else blocks should have 1 predecessor\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Predecessor count after condbr: OK\n");
    
    /* 终结基本块 */
    woort_IRBlock_ret_void(then_block);
    woort_IRBlock_ret_void(else_block);
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Conditional Branch: PASS ===\n\n");
    return true;
}

/*
 * test_ir_storage
 * 
 * 测试 Storage 功能。
 */
static bool test_ir_storage(void)
{
    printf("=== Test IR Storage ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    /* 创建 Storage */
    woort_IRStorage* storage = woort_IRFunction_create_storage(func);
    if (!storage)
    {
        printf("FAIL: woort_IRFunction_create_storage returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRFunction_create_storage: OK\n");
    
    /* 检查 Storage 所属函数 */
    if (woort_IRStorage_get_function(storage) != func)
    {
        printf("FAIL: woort_IRStorage_get_function returned wrong function\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRStorage_get_function: OK\n");
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    woort_IRBlock_ret_void(entry);
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Storage: PASS ===\n\n");
    return true;
}

/*
 * test_ir_data_operations
 * 
 * 测试数据构造和访问指令。
 */
static bool test_ir_data_operations(void)
{
    printf("=== Test IR Data Operations ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    
    /* 测试 MKVEC */
    const woort_IRValue* vec = woort_IRBlock_MKVEC(entry, 3);
    if (!vec)
    {
        printf("FAIL: woort_IRBlock_MKVEC returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_MKVEC: OK\n");
    
    /* 测试 MKMAP */
    const woort_IRValue* map = woort_IRBlock_MKMAP(entry, 5);
    if (!map)
    {
        printf("FAIL: woort_IRBlock_MKMAP returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_MKMAP: OK\n");
    
    /* 测试 MKSTRUCT */
    const woort_IRValue* strct = woort_IRBlock_MKSTRUCT(entry, 2);
    if (!strct)
    {
        printf("FAIL: woort_IRBlock_MKSTRUCT returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_MKSTRUCT: OK\n");
    
    /* 测试 MKCLOSURE */
    woort_IRGlobalIndex func_idx = woort_IRCompiler_allocate_global(irc);
    const woort_IRValue* closure = woort_IRBlock_MKCLOSURE(entry, func_idx, 1);
    if (!closure)
    {
        printf("FAIL: woort_IRBlock_MKCLOSURE returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_MKCLOSURE: OK\n");
    
    /* 测试类型转换 */
    const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
    
    const woort_IRValue* cast_i_to_r = woort_IRBlock_CASTI_TO_R(entry, arg0);
    const woort_IRValue* cast_r_to_i = woort_IRBlock_CASTR_TO_I(entry, arg0);
    const woort_IRValue* cast_i_to_s = woort_IRBlock_CASTI_TO_S(entry, arg0);
    const woort_IRValue* cast_r_to_s = woort_IRBlock_CASTR_TO_S(entry, arg0);
    
    if (!cast_i_to_r || !cast_r_to_i || !cast_i_to_s || !cast_r_to_s)
    {
        printf("FAIL: cast instructions returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Cast instructions: OK\n");
    
    /* 测试索引访问 */
    woort_IRGlobalIndex idx = woort_IRCompiler_allocate_global(irc);
    const woort_IRValue* idx_val = woort_IRFunction_load_const(func, idx);
    
    const woort_IRValue* ldvec = woort_IRBlock_LDIDXVEC(entry, vec, idx_val);
    if (!ldvec)
    {
        printf("FAIL: woort_IRBlock_LDIDXVEC returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_LDIDXVEC: OK\n");
    
    if (!woort_IRBlock_STIDXVEC(entry, vec, idx_val, arg0))
    {
        printf("FAIL: woort_IRBlock_STIDXVEC failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_STIDXVEC: OK\n");
    
    /* 测试结构体字段访问 */
    const woort_IRValue* ldstruct = woort_IRBlock_LDIDSTRUCT(entry, strct, 0);
    if (!ldstruct)
    {
        printf("FAIL: woort_IRBlock_LDIDSTRUCT returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_LDIDSTRUCT: OK\n");
    
    if (!woort_IRBlock_STIDSTRUCT(entry, strct, 1, arg0))
    {
        printf("FAIL: woort_IRBlock_STIDSTRUCT failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_STIDSTRUCT: OK\n");
    
    woort_IRBlock_ret_void(entry);
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Data Operations: PASS ===\n\n");
    return true;
}

/*
 * test_ir_dynamic_types
 * 
 * 测试动态类型指令。
 */
static bool test_ir_dynamic_types(void)
{
    printf("=== Test IR Dynamic Types ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    woort_IRCompiler_create(&irc);
    
    woort_IRFunction* func;
    woort_IRCompiler_add_function(irc, &func);
    
    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
    const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
    
    /* 测试 BOXDYN */
    const woort_IRValue* boxed = woort_IRBlock_BOXDYN(entry, WOORT_IRVALUE_TYPE_TAG_I, arg0);
    if (!boxed)
    {
        printf("FAIL: woort_IRBlock_BOXDYN returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_BOXDYN: OK\n");
    
    /* 测试 CHECKDYN */
    const woort_IRValue* check = woort_IRBlock_CHECKDYN(entry, WOORT_IRVALUE_TYPE_TAG_I, boxed);
    if (!check)
    {
        printf("FAIL: woort_IRBlock_CHECKDYN returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_CHECKDYN: OK\n");
    
    /* 测试 UNBOXDYN */
    const woort_IRValue* unboxed = woort_IRBlock_UNBOXDYN(entry, WOORT_IRVALUE_TYPE_TAG_I, boxed);
    if (!unboxed)
    {
        printf("FAIL: woort_IRBlock_UNBOXDYN returned NULL\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  woort_IRBlock_UNBOXDYN: OK\n");
    
    woort_IRBlock_ret_void(entry);
    
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Dynamic Types: PASS ===\n\n");
    return true;
}

/*
 * test_ir_fib_codegen
 * 
 * 测试完整的斐波那契代码生成：fib 和 main 函数。
 */
static bool test_ir_fib_codegen(void)
{
    printf("=== Test IR Fib CodeGen ===\n");
    
    woort_IRCompiler_bootup();
    
    woort_IRCompiler* irc;
    if (!woort_IRCompiler_create(&irc))
    {
        printf("FAIL: woort_IRCompiler_create failed\n");
        woort_IRCompiler_shutdown();
        return false;
    }
    
    woort_IRGlobalIndex const_val_2 = woort_IRCompiler_allocate_global(irc);
    woort_IRGlobalIndex const_val_1 = woort_IRCompiler_allocate_global(irc);
    woort_IRGlobalIndex const_val_func_fib = woort_IRCompiler_allocate_global(irc);
    woort_IRGlobalIndex const_val_10 = woort_IRCompiler_allocate_global(irc);
    woort_IRGlobalIndex const_val_print_int = woort_IRCompiler_allocate_global(irc);
    
    printf("  Allocated global indices: 2=%zu, 1=%zu, fib=%zu, 10=%zu, print_int=%zu\n",
           (size_t)const_val_2, (size_t)const_val_1, (size_t)const_val_func_fib,
           (size_t)const_val_10, (size_t)const_val_print_int);
    
    /***************************************************************************
     * 创建 fib 函数
     **************************************************************************/
    woort_IRFunction* irfunc_fib;
    if (!woort_IRCompiler_add_function(irc, &irfunc_fib))
    {
        printf("FAIL: woort_IRCompiler_add_function for fib failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Created fib function\n");
    
    woort_IRBlock* irblock_fib_entry = woort_IRFunction_get_entry_block(irfunc_fib);
    
    woort_IRBlock* irblock_less_then_2;
    woort_IRBlock* irblock_greater_then_2;
    if (!woort_IRFunction_add_block(irfunc_fib, &irblock_less_then_2) ||
        !woort_IRFunction_add_block(irfunc_fib, &irblock_greater_then_2))
    {
        printf("FAIL: woort_IRFunction_add_block for fib failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Created fib blocks\n");
    
    const woort_IRValue* val1 = woort_IRFunction_load_const(irfunc_fib, const_val_1);
    const woort_IRValue* val2 = woort_IRFunction_load_const(irfunc_fib, const_val_2);
    const woort_IRValue* arg0 = woort_IRFunction_load_argument(irfunc_fib, 0);
    
    if (!woort_IRBlock_condbr_greater_equal(irblock_fib_entry, arg0, val2, 
            irblock_greater_then_2, irblock_less_then_2))
    {
        printf("FAIL: woort_IRBlock_condbr_greater_equal failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  fib entry: condbr (n >= 2)\n");
    
    if (!woort_IRBlock_ret(irblock_less_then_2, val1))
    {
        printf("FAIL: woort_IRBlock_ret for less_then_2 failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  fib less_then_2: ret 1\n");
    
    const woort_IRValue* n_sub_1 = woort_IRBlock_SUBI(irblock_greater_then_2, arg0, val1);
    
    if (!woort_IRBlock_PUSH(irblock_greater_then_2, n_sub_1))
    {
        printf("FAIL: PUSH n_sub_1 failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    
    const woort_IRValue* fib_n_sub_1;
    if (!woort_IRBlock_CALLNWO(irblock_greater_then_2, const_val_func_fib, 1, &fib_n_sub_1))
    {
        printf("FAIL: CALLNWO fib(n-1) failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  fib greater_then_2: CALLNWO fib(n-1)\n");
    
    const woort_IRValue* n_sub_2 = woort_IRBlock_SUBI(irblock_greater_then_2, n_sub_1, val1);
    
    if (!woort_IRBlock_PUSH(irblock_greater_then_2, n_sub_2))
    {
        printf("FAIL: PUSH n_sub_2 failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    
    const woort_IRValue* fib_n_sub_2;
    if (!woort_IRBlock_CALLNWO(irblock_greater_then_2, const_val_func_fib, 1, &fib_n_sub_2))
    {
        printf("FAIL: CALLNWO fib(n-2) failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  fib greater_then_2: CALLNWO fib(n-2)\n");
    
    const woort_IRValue* result = woort_IRBlock_ADDI(irblock_greater_then_2, fib_n_sub_1, fib_n_sub_2);
    
    if (!woort_IRBlock_ret(irblock_greater_then_2, result))
    {
        printf("FAIL: woort_IRBlock_ret for greater_then_2 failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  fib greater_then_2: ret fib(n-1)+fib(n-2)\n");
    
    /***************************************************************************
     * 创建 main 函数
     **************************************************************************/
    woort_IRFunction* irfunc_main;
    if (!woort_IRCompiler_add_function(irc, &irfunc_main))
    {
        printf("FAIL: woort_IRCompiler_add_function for main failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Created main function\n");
    
    woort_IRBlock* irblock_main_entry = woort_IRFunction_get_entry_block(irfunc_main);
    
    const woort_IRValue* const_10 = woort_IRFunction_load_const(irfunc_main, const_val_10);
    
    if (!woort_IRBlock_PUSH(irblock_main_entry, const_10))
    {
        printf("FAIL: PUSH 10 failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  main: PUSH 10\n");
    
    const woort_IRValue* fib_result;
    if (!woort_IRBlock_CALLNWO(irblock_main_entry, const_val_func_fib, 1, &fib_result))
    {
        printf("FAIL: CALLNWO fib(10) failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  main: CALLNWO fib(10)\n");
    
    if (!woort_IRBlock_PUSH(irblock_main_entry, fib_result))
    {
        printf("FAIL: PUSH fib_result failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    
    if (!woort_IRBlock_CALLNFP(irblock_main_entry, const_val_print_int, 1, NULL))
    {
        printf("FAIL: CALLNFP print_int failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  main: CALLNFP print_int\n");
    
    if (!woort_IRBlock_ret_void(irblock_main_entry))
    {
        printf("FAIL: woort_IRBlock_ret_void for main failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  main: ret void\n");
    
    /***************************************************************************
     * 生成 CodeEnv
     **************************************************************************/
    woort_CodeEnv* code_env;
    if (!woort_IRCompiler_finish(irc, &code_env))
    {
        printf("FAIL: woort_IRCompiler_finish failed\n");
        woort_IRCompiler_destroy(irc);
        woort_IRCompiler_shutdown();
        return false;
    }
    printf("  Generated CodeEnv (bytecode count: %zu)\n",
           (size_t)(code_env->m_code_end - code_env->m_code_begin));
    
    code_env->m_data_begin[const_val_1].m_integer = 1;
    code_env->m_data_begin[const_val_2].m_integer = 2;
    code_env->m_data_begin[const_val_10].m_integer = 10;
    code_env->m_data_begin[const_val_func_fib].m_script_function = code_env->m_code_begin;
    code_env->m_data_begin[const_val_print_int].m_native_or_jit_function = &print_int;
    printf("  Filled constants\n");
    
    woort_CodeEnv_drop(code_env);
    woort_IRCompiler_destroy(irc);
    woort_IRCompiler_shutdown();
    
    printf("=== Test IR Fib CodeGen: PASS ===\n\n");
    return true;
}

/*
 * main
 * 
 * 测试入口。
 */
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    woort_init();
    
    printf("\n========================================\n");
    printf("WooRT IR Compiler Test Suite\n");
    printf("========================================\n\n");
    
    int failed = 0;
    
    if (!test_ir_basic()) failed++;
    if (!test_ir_function_and_blocks()) failed++;
    if (!test_ir_values()) failed++;
    if (!test_ir_arithmetic()) failed++;
    if (!test_ir_condbr()) failed++;
    if (!test_ir_storage()) failed++;
    if (!test_ir_data_operations()) failed++;
    if (!test_ir_dynamic_types()) failed++;
    if (!test_ir_fib_codegen()) failed++;
    
    printf("========================================\n");
    if (failed == 0)
    {
        printf("All tests PASSED!\n");
    }
    else
    {
        printf("%d test(s) FAILED!\n", failed);
    }
    printf("========================================\n\n");
    
    woort_shutdown();
    
    return failed;
}
