/*
 * woort_ir_codegen.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_compiler.h"
#include "woort_opcode_builder.h"
#include "woort_codeenv.h"

#include <stdlib.h>
#include <string.h>

/*
 * 字节码生成
 */

WOORT_NODISCARD bool _woort_ir_codegen(woort_IRCompiler* compiler, woort_CodeEnv** out_codeenv)
{
    uint32_t total_bytecode_count = 0;
    for (uint32_t func_idx = 0; func_idx < compiler->m_function_count; ++func_idx)
    {
        woort_IRFunction* func = compiler->m_functions[func_idx];

        for (uint32_t block_idx = 0; block_idx < func->m_block_count; ++block_idx)
        {
            woort_IRBlock* block = func->m_blocks[block_idx];
            total_bytecode_count += block->m_instr_count;
            if (block->m_has_terminator)
            {
                total_bytecode_count += 1;
            }
        }
    }

    woort_Bytecode* bytecodes = (woort_Bytecode*)malloc(sizeof(woort_Bytecode) * total_bytecode_count);
    if (bytecodes == NULL)
    {
        _woort_ir_compiler_set_error(compiler, "Failed to allocate bytecode array");
        return false;
    }

    (void)bytecodes;
    (void)out_codeenv;

    _woort_ir_compiler_set_error(compiler, "Bytecode generation not yet fully implemented");
    return false;
}
