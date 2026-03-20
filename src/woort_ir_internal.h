#pragma once

/*
 * woort_ir_internal.h
 * 
 * IR 编译器内部头文件，定义内部数据结构。
 */

#include "woort_ir.h"
#include "woort_vector.h"
#include "woort_hashmap.h"

#include <stddef.h>
#include <stdbool.h>

/*******************************************************************************
 * IRValue 内部结构
 ******************************************************************************/

/*
 * woort_IRValueKind
 * 
 * IRValue 的类型标记。
 */
typedef enum woort_IRValueKind
{
    WOORT_IRVALUE_KIND_INVALID = 0,
    WOORT_IRVALUE_KIND_CONST,       /* 常量（全局存储索引） */
    WOORT_IRVALUE_KIND_ARGUMENT,    /* 函数参数 */
    WOORT_IRVALUE_KIND_INSTRUCTION, /* 指令结果 */
    WOORT_IRVALUE_KIND_STORAGE_LOAD /* 从 Storage 加载的值 */
} woort_IRValueKind;

/*
 * woort_IRInstructionKind
 * 
 * IR 指令类型。
 */
typedef enum woort_IRInstructionKind
{
    WOORT_IR_INST_NOP = 0,
    
    /* 算术运算 */
    WOORT_IR_INST_ADDI, WOORT_IR_INST_SUBI, WOORT_IR_INST_MULI,
    WOORT_IR_INST_DIVI, WOORT_IR_INST_MODI, WOORT_IR_INST_NEGI,
    WOORT_IR_INST_ADDR, WOORT_IR_INST_SUBR, WOORT_IR_INST_MULR,
    WOORT_IR_INST_DIVR, WOORT_IR_INST_MODR, WOORT_IR_INST_NEGR,
    WOORT_IR_INST_ADDS,
    
    /* 比较运算 */
    WOORT_IR_INST_LTI, WOORT_IR_INST_GTI, WOORT_IR_INST_LEI, WOORT_IR_INST_GEI,
    WOORT_IR_INST_EQI, WOORT_IR_INST_NEI,
    WOORT_IR_INST_LTR, WOORT_IR_INST_GTR, WOORT_IR_INST_LER, WOORT_IR_INST_GER,
    WOORT_IR_INST_EQR, WOORT_IR_INST_NER,
    WOORT_IR_INST_LTS, WOORT_IR_INST_GTS, WOORT_IR_INST_LES, WOORT_IR_INST_GES,
    WOORT_IR_INST_EQS, WOORT_IR_INST_NES,
    
    /* 逻辑运算 */
    WOORT_IR_INST_LAND, WOORT_IR_INST_LOR, WOORT_IR_INST_LNOT,
    
    /* 类型转换 */
    WOORT_IR_INST_CASTI_TO_R, WOORT_IR_INST_CASTR_TO_I,
    WOORT_IR_INST_CASTI_TO_S, WOORT_IR_INST_CASTR_TO_S,
    
    /* 数据构造 */
    WOORT_IR_INST_MKVEC, WOORT_IR_INST_MKMAP, WOORT_IR_INST_MKSTRUCT,
    WOORT_IR_INST_MKCLOSURE,
    
    /* 动态类型 */
    WOORT_IR_INST_BOXDYN, WOORT_IR_INST_UNBOXDYN, WOORT_IR_INST_CHECKDYN,
    
    /* 索引访问 */
    WOORT_IR_INST_LDIDXVEC, WOORT_IR_INST_LDIDXVECX,
    WOORT_IR_INST_LDIDXMAP,
    WOORT_IR_INST_LDIDSTRUCT, WOORT_IR_INST_LDIDSTRING,
    
    /* 索引存储 */
    WOORT_IR_INST_STIDXVEC, WOORT_IR_INST_STIDXVECX,
    WOORT_IR_INST_STIDXMAP,
    WOORT_IR_INST_STIDSTRUCT
} woort_IRInstructionKind;

/*
 * woort_IRInstruction
 * 
 * IR 指令结构。
 */
typedef struct woort_IRInstruction
{
    woort_IRValueKind m_kind;  /* 继承自 IRValue，总是 WOORT_IRVALUE_KIND_INSTRUCTION */
    
    woort_IRInstructionKind m_inst_kind;
    
    /* 操作数（最多 3 个） */
    const woort_IRValue* m_operand0;
    const woort_IRValue* m_operand1;
    const woort_IRValue* m_operand2;
    
    /* 额外数据（如元素数量、字段索引、类型标记等） */
    size_t m_extra_size;
    woort_IRGlobalIndex m_extra_global_index;
    woort_IRValue_TypeTag m_extra_type_tag;
    
    /* 所属基本块 */
    struct woort_IRBlock* m_parent_block;
} woort_IRInstruction;

/*
 * woort_IRValueData
 * 
 * IRValue 的联合体数据。
 */
typedef union woort_IRValueData
{
    woort_IRGlobalIndex m_global_index;  /* 常量的全局索引 */
    size_t m_argument_index;              /* 参数索引 */
    woort_IRInstruction* m_instruction;   /* 指令指针 */
    struct woort_IRStorage* m_storage;    /* Storage 指针 */
} woort_IRValueData;

/*
 * woort_IRValue 实际结构
 */
struct woort_IRValue
{
    woort_IRValueKind m_kind;
    woort_IRValueData m_data;
};

/*******************************************************************************
 * IRStorage 内部结构
 ******************************************************************************/

struct woort_IRStorage
{
    woort_IRFunction* m_function;
    
    /* 当前值（在每个块中可能不同） */
    woort_Vector m_values_per_block;  /* Vector<block_id, IRValue*> */
};

/*******************************************************************************
 * IRTerminatorKind
 * 
 * 基本块终结指令类型。
 */
typedef enum woort_IRTerminatorKind
{
    WOORT_IR_TERMINATOR_NONE = 0,
    WOORT_IR_TERMINATOR_BR,         /* 无条件跳转 */
    WOORT_IR_TERMINATOR_CONDBR,     /* 条件跳转 */
    WOORT_IR_TERMINATOR_RET,        /* 返回值 */
    WOORT_IR_TERMINATOR_RET_VOID    /* 返回 void */
} woort_IRTerminatorKind;

/*
 * woort_IRCondBrKind
 * 
 * 条件跳转的比较类型。
 */
typedef enum woort_IRCondBrKind
{
    WOORT_IR_CONDBR_LESS_THEN = 0,
    WOORT_IR_CONDBR_GREATER_THEN,
    WOORT_IR_CONDBR_LESS_EQUAL,
    WOORT_IR_CONDBR_GREATER_EQUAL,
    WOORT_IR_CONDBR_EQUAL,
    WOORT_IR_CONDBR_NOT_EQUAL,
    WOORT_IR_CONDBR_TRUE,
    WOORT_IR_CONDBR_FALSE
} woort_IRCondBrKind;

/*
 * woort_IRTerminator
 * 
 * 终结指令结构。
 */
typedef struct woort_IRTerminator
{
    woort_IRTerminatorKind m_kind;
    
    union
    {
        struct
        {
            woort_IRBlock* m_target;
        } m_br;
        
        struct
        {
            woort_IRCondBrKind m_cond_kind;
            const woort_IRValue* m_lhs;
            const woort_IRValue* m_rhs;
            woort_IRBlock* m_then_block;
            woort_IRBlock* m_else_block;
        } m_condbr;
        
        struct
        {
            const woort_IRValue* m_value;
        } m_ret;
    } m_data;
} woort_IRTerminator;

/*******************************************************************************
 * IRBlock 内部结构
 ******************************************************************************/

struct woort_IRBlock
{
    woort_IRFunction* m_function;
    
    /* 块 ID（在函数内唯一） */
    size_t m_block_id;
    
    /* 指令列表 */
    woort_Vector m_instructions;  /* Vector<IRInstruction*> */
    
    /* 终结指令 */
    woort_IRTerminator m_terminator;
    
    /* 前驱和后继块 */
    woort_Vector m_predecessors;  /* Vector<IRBlock*> */
    woort_Vector m_successors;    /* Vector<IRBlock*> */
    
    /* 是否是入口块 */
    bool m_is_entry;
};

/*******************************************************************************
 * IRFunction 内部结构
 ******************************************************************************/

struct woort_IRFunction
{
    woort_IRCompiler* m_compiler;
    
    /* 基本块列表 */
    woort_Vector m_blocks;  /* Vector<IRBlock*> */
    
    /* 入口块 */
    woort_IRBlock* m_entry_block;
    
    /* 参数值缓存 */
    woort_Vector m_argument_values;  /* Vector<IRValue*> */
    
    /* Storage 列表 */
    woort_Vector m_storages;  /* Vector<IRStorage*> */
    
    /* 下一个块 ID */
    size_t m_next_block_id;
};

/*******************************************************************************
 * IRCompiler 内部结构
 ******************************************************************************/

struct woort_IRCompiler
{
    /* 内存池 - 每个编译器独立管理 */
    woort_Vector m_value_pool;        /* Vector<IRValue*> */
    woort_Vector m_instruction_pool;  /* Vector<IRInstruction*> */
    woort_Vector m_block_pool;        /* Vector<IRBlock*> */
    woort_Vector m_function_pool;     /* Vector<IRFunction*> */
    woort_Vector m_storage_pool;      /* Vector<IRStorage*> */
    
    /* 全局存储计数 */
    size_t m_global_count;
    
    /* 函数列表 */
    woort_Vector m_functions;  /* Vector<IRFunction*> */
    
    /* 常量值缓存（按 global_index） */
    woort_Vector m_const_values;  /* Vector<IRValue*> */
};

/*******************************************************************************
 * 内部辅助函数
 ******************************************************************************/

/*
 * _woort_IRValue_create_const
 * 
 * 创建常量 IRValue。
 */
WOORT_NODISCARD woort_IRValue* _woort_IRValue_create_const(
    woort_IRCompiler* compiler,
    woort_IRGlobalIndex global_index);

/*
 * _woort_IRValue_create_argument
 * 
 * 创建参数 IRValue。
 */
WOORT_NODISCARD woort_IRValue* _woort_IRValue_create_argument(
    woort_IRCompiler* compiler,
    size_t argument_index);

/*
 * _woort_IRInstruction_create
 * 
 * 创建 IR 指令。
 */
WOORT_NODISCARD woort_IRInstruction* _woort_IRInstruction_create(
    woort_IRCompiler* compiler,
    woort_IRInstructionKind kind,
    woort_IRBlock* parent_block);

/*
 * _woort_IRBlock_add_instruction
 * 
 * 向基本块添加指令。
 */
void _woort_IRBlock_add_instruction(
    woort_IRBlock* block,
    woort_IRInstruction* inst);

/*
 * _woort_IRBlock_add_successor
 * 
 * 添加后继块（同时更新前驱关系）。
 */
void _woort_IRBlock_add_successor(
    woort_IRBlock* block,
    woort_IRBlock* successor);

/*
 * _woort_IRValue_is_valid
 * 
 * 检查 IRValue 是否有效。
 */
WOORT_NODISCARD bool _woort_IRValue_is_valid(
    const woort_IRValue* value);
