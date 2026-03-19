#pragma once

#include "woort_value.h"
#include "woort_opcode.h"
#include <stdio.h>

/*
woort_disasm.h - WooRT 字节码反汇编器

将二进制字节码转换为可读的汇编形式，用于调试和分析。
*/

/* 反汇编结果 */
typedef struct woort_DisasmResult {
    const char* m_mnemonic;      /* 助记符 */
    char m_operand_str[128];     /* 操作数字符串 */
    uint32_t m_instruction_size; /* 指令大小（1或2个字节码单位） */
} woort_DisasmResult;

/* 反汇编单条指令
   @param code 指向字节码的指针
   @param out_result 输出结果
*/
void woort_disasm_instruction(
    const woort_Bytecode* code,
    woort_DisasmResult* out_result);

/* 反汇编代码块并输出到 FILE*
   @param code_begin 代码起始位置
   @param code_end 代码结束位置
   @param out 输出文件流
*/
void woort_disasm_dump(
    const woort_Bytecode* code_begin,
    const woort_Bytecode* code_end,
    FILE* out);