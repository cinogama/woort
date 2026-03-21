#pragma once

/*
 * woort_ir_block.h
 */

#include "woort_ir_types.h"
#include "woort_diagnosis.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * IRBlock - 基本块
 *
 * 包含一系列指令，以终止指令结束。
 */

/*
 * ============================================================
 * 常量/全局加载
 * ============================================================
 */

/*
 * 加载常量（后端可自由移动位置，如提到循环外）
 *
 * block: 基本块指针
 * global_idx: 全局索引
 * 返回: 值指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_load_const(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_idx);

/*
 * 明确加载（必须在当前位置执行）
 *
 * block: 基本块指针
 * global_idx: 全局索引
 * 返回: 值指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LOAD(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_idx);

/*
 * 存储到全局
 *
 * block: 基本块指针
 * global_idx: 全局索引
 * val: 要存储的值
 */
void woort_IRBlock_STORE(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_idx,
    const woort_IRValue* val);

/*
 * ============================================================
 * 算术运算 - 整数
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ADD_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_SUB_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MUL_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_DIV_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MOD_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NEG_I(woort_IRBlock* block, const woort_IRValue* a);

/*
 * ============================================================
 * 算术运算 - 实数
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ADD_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_SUB_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MUL_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_DIV_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MOD_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NEG_R(woort_IRBlock* block, const woort_IRValue* a);

/*
 * ============================================================
 * 算术运算 - 字符串
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ADD_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);

/*
 * ============================================================
 * 比较运算 - 整数
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LT_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LE_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GT_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GE_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQ_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NE_I(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);

/*
 * ============================================================
 * 比较运算 - 实数
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LT_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LE_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GT_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GE_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQ_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NE_R(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);

/*
 * ============================================================
 * 比较运算 - 字符串
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LT_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LE_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GT_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GE_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQ_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NE_S(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);

/*
 * ============================================================
 * 比较运算 - 布尔
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQ_B(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NE_B(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);

/*
 * ============================================================
 * 比较运算 - 动态类型
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQ_X(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NE_X(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);

/*
 * ============================================================
 * 逻辑运算
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LAND(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LOR(woort_IRBlock* block, const woort_IRValue* a, const woort_IRValue* b);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LNOT(woort_IRBlock* block, const woort_IRValue* a);

/*
 * ============================================================
 * 类型转换
 * ============================================================
 */

WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ITOR(woort_IRBlock* block, const woort_IRValue* a);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_RTOI(woort_IRBlock* block, const woort_IRValue* a);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ITOS(woort_IRBlock* block, const woort_IRValue* a);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_STOI(woort_IRBlock* block, const woort_IRValue* a);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_STOR(woort_IRBlock* block, const woort_IRValue* a);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_RTOS(woort_IRBlock* block, const woort_IRValue* a);

/*
 * ============================================================
 * 容器构造
 * ============================================================
 */

/*
 * 构造向量，从栈顶弹出 n 个值
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKVEC(woort_IRBlock* block, uint32_t n);

/*
 * 构造字典，从栈顶弹出 n 个键值对
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKMAP(woort_IRBlock* block, uint32_t n);

/*
 * 构造结构体，从栈顶弹出 n 个字段值
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKSTRUCT(woort_IRBlock* block, uint32_t n);

/*
 * ============================================================
 * 索引加载
 * ============================================================
 */

/* vec[idx] */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXVEC(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXVECX(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx);

/* struct.field_idx */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDSTRUCT(woort_IRBlock* block, const woort_IRValue* struct_val, uint32_t field_idx);

/* str[idx] */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDSTRING(woort_IRBlock* block, const woort_IRValue* str, const woort_IRValue* idx);

/* dict[key] - 按键类型 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXDICT_I(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXDICT_R(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXDICT_B(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key);
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXDICT_X(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key);

/*
 * ============================================================
 * 索引存储 - 向量
 * ============================================================
 */

/* vec[idx] = val */
void woort_IRBlock_STIDXVEC_I(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx, const woort_IRValue* val);
void woort_IRBlock_STIDXVEC_R(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx, const woort_IRValue* val);
void woort_IRBlock_STIDXVEC_B(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx, const woort_IRValue* val);
void woort_IRBlock_STIDXVEC_X(woort_IRBlock* block, const woort_IRValue* vec, const woort_IRValue* idx, const woort_IRValue* val);

/*
 * ============================================================
 * 索引存储 - 结构体
 * ============================================================
 */

/* struct.field_idx = val */
void woort_IRBlock_STIDSTRUCT(woort_IRBlock* block, const woort_IRValue* struct_val, uint32_t field_idx, const woort_IRValue* val);

/*
 * ============================================================
 * 索引存储 - 字典 (按键/值类型组合)
 * ============================================================
 */

/* dict[key] = val */
void woort_IRBlock_STIDXDICT_II(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_IR(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_IB(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_IX(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);

void woort_IRBlock_STIDXDICT_RI(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_RR(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_RB(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_RX(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);

void woort_IRBlock_STIDXDICT_BI(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_BR(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_BB(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_BX(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);

void woort_IRBlock_STIDXDICT_XI(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_XR(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_XB(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);
void woort_IRBlock_STIDXDICT_XX(woort_IRBlock* block, const woort_IRValue* dict, const woort_IRValue* key, const woort_IRValue* val);

/*
 * ============================================================
 * 闭包
 * ============================================================
 */

/*
 * 创建闭包，捕获 capture_count 个值（从栈顶弹出）
 * func_idx: 闭包函数在全局存储中的索引
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKCLOSURE(woort_IRBlock* block, woort_IRGlobalIndex func_idx, uint32_t capture_count);

/*
 * ============================================================
 * 函数调用
 * ============================================================
 */

/*
 * 压入参数值到调用栈
 */
void woort_IRBlock_PUSH(woort_IRBlock* block, const woort_IRValue* val);

/*
 * 调用 native 函数（无帧指针） - 通过 global index
 * argc: 参数数量（用于生成 RESULT/POPR 指令）
 * out_result: 接收返回值，为 NULL 时不获取返回值
 */
WOORT_NODISCARD bool woort_IRBlock_CALLNWO(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    uint32_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * 调用 native 函数（有帧指针） - 通过 global index
 */
WOORT_NODISCARD bool woort_IRBlock_CALLNFP(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    uint32_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * 调用 JIT 函数 - 通过 global index
 */
WOORT_NODISCARD bool woort_IRBlock_CALLNJIT(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    uint32_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * 间接调用 - 通过 IRValue
 * 后端自动选择 CALLS（栈地址）或 CALLC（常量地址）
 */
WOORT_NODISCARD bool woort_IRBlock_CALL(
    woort_IRBlock* block,
    const woort_IRValue* func,
    uint32_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * ============================================================
 * 终止指令（小写，不是纯粹指令）
 * ============================================================
 */

/*
 * 无条件跳转
 */
void woort_IRBlock_br(woort_IRBlock* block, woort_IRBlock* target);

/*
 * 整数比较条件跳转（特化）
 * a < b  -> lt_block, else -> ge_block
 */
void woort_IRBlock_br_lt(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* lt_block,
    woort_IRBlock* ge_block);

/*
 * a <= b -> le_block, else -> gt_block
 */
void woort_IRBlock_br_le(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* le_block,
    woort_IRBlock* gt_block);

/*
 * a > b -> gt_block, else -> le_block
 */
void woort_IRBlock_br_gt(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* gt_block,
    woort_IRBlock* le_block);

/*
 * a >= b -> ge_block, else -> lt_block
 */
void woort_IRBlock_br_ge(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* ge_block,
    woort_IRBlock* lt_block);

/*
 * a == b -> eq_block, else -> ne_block
 */
void woort_IRBlock_br_eq(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* eq_block,
    woort_IRBlock* ne_block);

/*
 * a != b -> ne_block, else -> eq_block
 */
void woort_IRBlock_br_ne(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* ne_block,
    woort_IRBlock* eq_block);

/*
 * 布尔条件跳转
 * cond 为真 -> true_block, else -> false_block
 */
void woort_IRBlock_br_cond(
    woort_IRBlock* block,
    const woort_IRValue* cond,
    woort_IRBlock* true_block,
    woort_IRBlock* false_block);

/*
 * 返回值
 */
void woort_IRBlock_ret(woort_IRBlock* block, const woort_IRValue* val);

/*
 * 无返回值返回
 */
void woort_IRBlock_ret_void(woort_IRBlock* block);
