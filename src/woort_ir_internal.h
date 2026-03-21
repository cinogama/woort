#pragma once

/*
 * woort_ir_internal.h
 */

#include "woort_ir_types.h"
#include "woort_diagnosis.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * IR 指令类型枚举
 */
typedef enum woort_IRInstrKind
{
    /* 常量加载 */
    WOORT_IR_INSTR_LOAD_CONST,
    WOORT_IR_INSTR_LOAD,
    WOORT_IR_INSTR_STORE,

    /* 整数算术运算 */
    WOORT_IR_INSTR_ADD_I,
    WOORT_IR_INSTR_SUB_I,
    WOORT_IR_INSTR_MUL_I,
    WOORT_IR_INSTR_DIV_I,
    WOORT_IR_INSTR_MOD_I,
    WOORT_IR_INSTR_NEG_I,

    /* 实数算术运算 */
    WOORT_IR_INSTR_ADD_R,
    WOORT_IR_INSTR_SUB_R,
    WOORT_IR_INSTR_MUL_R,
    WOORT_IR_INSTR_DIV_R,
    WOORT_IR_INSTR_MOD_R,
    WOORT_IR_INSTR_NEG_R,

    /* 字符串算术运算 */
    WOORT_IR_INSTR_ADD_S,

    /* 整数比较 */
    WOORT_IR_INSTR_LT_I,
    WOORT_IR_INSTR_LE_I,
    WOORT_IR_INSTR_GT_I,
    WOORT_IR_INSTR_GE_I,
    WOORT_IR_INSTR_EQ_I,
    WOORT_IR_INSTR_NE_I,

    /* 实数比较 */
    WOORT_IR_INSTR_LT_R,
    WOORT_IR_INSTR_LE_R,
    WOORT_IR_INSTR_GT_R,
    WOORT_IR_INSTR_GE_R,
    WOORT_IR_INSTR_EQ_R,
    WOORT_IR_INSTR_NE_R,

    /* 字符串比较 */
    WOORT_IR_INSTR_LT_S,
    WOORT_IR_INSTR_LE_S,
    WOORT_IR_INSTR_GT_S,
    WOORT_IR_INSTR_GE_S,
    WOORT_IR_INSTR_EQ_S,
    WOORT_IR_INSTR_NE_S,

    /* 布尔比较 */
    WOORT_IR_INSTR_EQ_B,
    WOORT_IR_INSTR_NE_B,

    /* 动态类型比较 */
    WOORT_IR_INSTR_EQ_X,
    WOORT_IR_INSTR_NE_X,

    /* 逻辑运算 */
    WOORT_IR_INSTR_LAND,
    WOORT_IR_INSTR_LOR,
    WOORT_IR_INSTR_LNOT,

    /* 类型转换 */
    WOORT_IR_INSTR_ITOR,
    WOORT_IR_INSTR_RTOI,
    WOORT_IR_INSTR_ITOS,
    WOORT_IR_INSTR_STOI,
    WOORT_IR_INSTR_STOR,
    WOORT_IR_INSTR_RTOS,

    /* 容器构造 */
    WOORT_IR_INSTR_MKVEC,
    WOORT_IR_INSTR_MKMAP,
    WOORT_IR_INSTR_MKSTRUCT,

    /* 索引加载 */
    WOORT_IR_INSTR_LDIDXVEC,
    WOORT_IR_INSTR_LDIDXVECX,
    WOORT_IR_INSTR_LDIDSTRUCT,
    WOORT_IR_INSTR_LDIDSTRING,
    WOORT_IR_INSTR_LDIDXDICT_I,
    WOORT_IR_INSTR_LDIDXDICT_R,
    WOORT_IR_INSTR_LDIDXDICT_B,
    WOORT_IR_INSTR_LDIDXDICT_X,

    /* 索引存储 - 向量 */
    WOORT_IR_INSTR_STIDXVEC_I,
    WOORT_IR_INSTR_STIDXVEC_R,
    WOORT_IR_INSTR_STIDXVEC_B,
    WOORT_IR_INSTR_STIDXVEC_X,

    /* 索引存储 - 结构体 */
    WOORT_IR_INSTR_STIDSTRUCT,

    /* 索引存储 - 字典 */
    WOORT_IR_INSTR_STIDXDICT_II,
    WOORT_IR_INSTR_STIDXDICT_IR,
    WOORT_IR_INSTR_STIDXDICT_IB,
    WOORT_IR_INSTR_STIDXDICT_IX,
    WOORT_IR_INSTR_STIDXDICT_RI,
    WOORT_IR_INSTR_STIDXDICT_RR,
    WOORT_IR_INSTR_STIDXDICT_RB,
    WOORT_IR_INSTR_STIDXDICT_RX,
    WOORT_IR_INSTR_STIDXDICT_BI,
    WOORT_IR_INSTR_STIDXDICT_BR,
    WOORT_IR_INSTR_STIDXDICT_BB,
    WOORT_IR_INSTR_STIDXDICT_BX,
    WOORT_IR_INSTR_STIDXDICT_XI,
    WOORT_IR_INSTR_STIDXDICT_XR,
    WOORT_IR_INSTR_STIDXDICT_XB,
    WOORT_IR_INSTR_STIDXDICT_XX,

    /* 闭包 */
    WOORT_IR_INSTR_MKCLOSURE,

    /* 函数调用 */
    WOORT_IR_INSTR_PUSH,
    WOORT_IR_INSTR_CALLNWO,
    WOORT_IR_INSTR_CALLNFP,
    WOORT_IR_INSTR_CALLNJIT,
    WOORT_IR_INSTR_CALL,

    /* 终止指令 */
    WOORT_IR_INSTR_BR,
    WOORT_IR_INSTR_BR_LT,
    WOORT_IR_INSTR_BR_LE,
    WOORT_IR_INSTR_BR_GT,
    WOORT_IR_INSTR_BR_GE,
    WOORT_IR_INSTR_BR_EQ,
    WOORT_IR_INSTR_BR_NE,
    WOORT_IR_INSTR_BR_COND,
    WOORT_IR_INSTR_RET,
    WOORT_IR_INSTR_RET_VOID,

} woort_IRInstrKind;

/*
 * IR 指令结构
 */
typedef struct woort_IRInstr
{
    woort_IRInstrKind m_kind;

    union
    {
        struct
        {
            woort_IRGlobalIndex m_global_idx;
        } m_load_const;

        struct
        {
            woort_IRGlobalIndex m_global_idx;
        } m_load;

        struct
        {
            woort_IRGlobalIndex m_global_idx;
            const struct woort_IRValue* m_val;
        } m_store;

        struct
        {
            const struct woort_IRValue* m_a;
            const struct woort_IRValue* m_b;
        } m_binop;

        struct
        {
            const struct woort_IRValue* m_a;
        } m_unop;

        struct
        {
            uint32_t m_count;
        } m_mkcontainer;

        struct
        {
            const struct woort_IRValue* m_container;
            const struct woort_IRValue* m_idx;
        } m_ldidx;

        struct
        {
            const struct woort_IRValue* m_container;
            uint32_t m_field_idx;
        } m_ldidstruct;

        struct
        {
            const struct woort_IRValue* m_dict;
            const struct woort_IRValue* m_key;
        } m_ldidxdict;

        struct
        {
            const struct woort_IRValue* m_container;
            const struct woort_IRValue* m_idx;
            const struct woort_IRValue* m_val;
        } m_stidx;

        struct
        {
            const struct woort_IRValue* m_container;
            uint32_t m_field_idx;
            const struct woort_IRValue* m_val;
        } m_stidstruct;

        struct
        {
            const struct woort_IRValue* m_dict;
            const struct woort_IRValue* m_key;
            const struct woort_IRValue* m_val;
        } m_stidxdict;

        struct
        {
            woort_IRGlobalIndex m_func_idx;
            uint32_t m_capture_count;
        } m_mkclosure;

        struct
        {
            const struct woort_IRValue* m_val;
        } m_push;

        struct
        {
            woort_IRGlobalIndex m_func_idx;
            uint32_t m_argc;
        } m_call_imm;

        struct
        {
            const struct woort_IRValue* m_func;
            uint32_t m_argc;
        } m_call;

        struct
        {
            struct woort_IRBlock* m_target;
        } m_br;

        struct
        {
            const struct woort_IRValue* m_a;
            const struct woort_IRValue* m_b;
            struct woort_IRBlock* m_true_block;
            struct woort_IRBlock* m_false_block;
        } m_br_cmp;

        struct
        {
            const struct woort_IRValue* m_cond;
            struct woort_IRBlock* m_true_block;
            struct woort_IRBlock* m_false_block;
        } m_br_cond;

        struct
        {
            const struct woort_IRValue* m_val;
        } m_ret;

    } m_op;

    struct woort_IRValue* m_result;

} woort_IRInstr;

/*
 * IRValue 结构
 */
typedef struct woort_IRValue
{
    struct woort_IRBlock* m_defining_block;
    uint32_t m_index;
    const woort_IRInstr* m_defining_instr;
} woort_IRValue;

/*
 * IRPHI Incoming
 */
typedef struct woort_IRPHIIncoming
{
    struct woort_IRBlock* m_from_block;
    const woort_IRValue* m_value;
} woort_IRPHIIncoming;

/*
 * IRPHI 结构
 */
typedef struct woort_IRPHI
{
    struct woort_IRBlock* m_block;
    woort_IRValue m_value;

    woort_IRPHIIncoming* m_incomings;
    uint32_t m_incoming_count;
    uint32_t m_incoming_capacity;
} woort_IRPHI;

/*
 * IRBlock 结构
 */
typedef struct woort_IRBlock
{
    struct woort_IRFunction* m_func;
    uint32_t m_index;

    woort_IRInstr* m_instrs;
    uint32_t m_instr_count;
    uint32_t m_instr_capacity;

    woort_IRInstr m_terminator;
    bool m_has_terminator;

    woort_IRValue* m_params;
    uint32_t m_param_count;

    struct woort_IRBlock** m_predecessors;
    uint32_t m_predecessor_count;
    uint32_t m_predecessor_capacity;

    struct woort_IRBlock** m_successors;
    uint32_t m_successor_count;
    uint32_t m_successor_capacity;

    bool m_is_entry;
} woort_IRBlock;

/*
 * IRFunction 结构
 */
typedef struct woort_IRFunction
{
    struct woort_IRCompiler* m_compiler;
    uint32_t m_index;

    uint32_t m_param_count;
    woort_IRValue* m_params;

    woort_IRBlock* m_entry_block;

    woort_IRBlock** m_blocks;
    uint32_t m_block_count;
    uint32_t m_block_capacity;

    woort_IRPHI** m_phis;
    uint32_t m_phi_count;
    uint32_t m_phi_capacity;

    uint32_t m_next_value_index;
} woort_IRFunction;

/*
 * IRCompiler 结构
 */
typedef struct woort_IRCompiler
{
    woort_IRFunction** m_functions;
    uint32_t m_function_count;
    uint32_t m_function_capacity;

    uint32_t m_global_count;

    char m_error_buffer[512];
    bool m_has_error;
} woort_IRCompiler;

/*
 * 内部辅助函数 - Compiler
 */
WOORT_NODISCARD bool _woort_ir_compiler_set_error(woort_IRCompiler* compiler, const char* fmt, ...);

/*
 * 内部辅助函数 - Function
 */
WOORT_NODISCARD bool _woort_ir_function_init(
    woort_IRFunction** out_func,
    woort_IRCompiler* compiler,
    uint32_t param_count,
    uint32_t func_index);

void _woort_ir_function_drop(woort_IRFunction* func);

WOORT_NODISCARD bool _woort_ir_function_add_block_internal(
    woort_IRFunction* func,
    woort_IRBlock** out_block);

/*
 * 内部辅助函数 - Block
 */
WOORT_NODISCARD bool _woort_ir_block_init(
    woort_IRBlock** out_block,
    woort_IRFunction* func,
    uint32_t index,
    bool is_entry);

void _woort_ir_block_drop(woort_IRBlock* block);

WOORT_NODISCARD woort_IRInstr* _woort_ir_block_append_instr(woort_IRBlock* block);

void _woort_ir_block_add_successor(woort_IRBlock* block, woort_IRBlock* successor);
void _woort_ir_block_add_predecessor(woort_IRBlock* block, woort_IRBlock* predecessor);

/*
 * 内部辅助函数 - PHI
 */
WOORT_NODISCARD bool _woort_ir_phi_init(
    woort_IRPHI** out_phi,
    woort_IRBlock* block,
    uint32_t value_index);

void _woort_ir_phi_drop(woort_IRPHI* phi);
