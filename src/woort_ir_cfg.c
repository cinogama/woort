/*
 * woort_ir_cfg.c
 */

#include "woort_ir_internal.h"

#include <stdlib.h>
#include <string.h>

/*
 * 构建控制流图（CFG）
 *
 * 由于终止指令已经在前驱/后继关系中维护，
 * 这个函数主要用于额外的 CFG 分析。
 */

void _woort_ir_cfg_build(woort_IRCompiler* compiler)
{
    for (uint32_t func_idx = 0; func_idx < compiler->m_function_count; ++func_idx)
    {
        woort_IRFunction* func = compiler->m_functions[func_idx];

        for (uint32_t block_idx = 0; block_idx < func->m_block_count; ++block_idx)
        {
            woort_IRBlock* block = func->m_blocks[block_idx];
            if (!block->m_has_terminator)
            {
                continue;
            }

            switch (block->m_terminator.m_kind)
            {
                case WOORT_IR_INSTR_BR:
                    _woort_ir_block_add_successor(block, block->m_terminator.m_op.m_br.m_target);
                    _woort_ir_block_add_predecessor(block->m_terminator.m_op.m_br.m_target, block);
                    break;

                case WOORT_IR_INSTR_BR_LT:
                case WOORT_IR_INSTR_BR_LE:
                case WOORT_IR_INSTR_BR_GT:
                case WOORT_IR_INSTR_BR_GE:
                case WOORT_IR_INSTR_BR_EQ:
                case WOORT_IR_INSTR_BR_NE:
                    _woort_ir_block_add_successor(block, block->m_terminator.m_op.m_br_cmp.m_true_block);
                    _woort_ir_block_add_successor(block, block->m_terminator.m_op.m_br_cmp.m_false_block);
                    _woort_ir_block_add_predecessor(block->m_terminator.m_op.m_br_cmp.m_true_block, block);
                    _woort_ir_block_add_predecessor(block->m_terminator.m_op.m_br_cmp.m_false_block, block);
                    break;

                case WOORT_IR_INSTR_BR_COND:
                    _woort_ir_block_add_successor(block, block->m_terminator.m_op.m_br_cond.m_true_block);
                    _woort_ir_block_add_successor(block, block->m_terminator.m_op.m_br_cond.m_false_block);
                    _woort_ir_block_add_predecessor(block->m_terminator.m_op.m_br_cond.m_true_block, block);
                    _woort_ir_block_add_predecessor(block->m_terminator.m_op.m_br_cond.m_false_block, block);
                    break;

                case WOORT_IR_INSTR_RET:
                case WOORT_IR_INSTR_RET_VOID:
                    break;
            }
        }
    }
}

/*
 * 检查是否所有块都可以从入口块到达（可达性分析）
 */
WOORT_NODISCARD bool _woort_ir_cfg_check_reachability(woort_IRCompiler* compiler)
{
    for (uint32_t func_idx = 0; func_idx < compiler->m_function_count; ++func_idx)
    {
        woort_IRFunction* func = compiler->m_functions[func_idx];

        bool* visited = (bool*)calloc(func->m_block_count, sizeof(bool));
        if (visited == NULL)
        {
            return false;
        }

        for (uint32_t i = 0; i < func->m_block_count; ++i)
        {
            visited[i] = false;
        }

        woort_IRBlock** stack = (woort_IRBlock**)malloc(sizeof(woort_IRBlock*) * func->m_block_count);
        if (stack == NULL)
        {
            free(visited);
            return false;
        }

        uint32_t stack_top = 0;
        stack[stack_top++] = func->m_entry_block;
        visited[0] = true;

        while (stack_top > 0)
        {
            woort_IRBlock* current = stack[--stack_top];
            for (uint32_t i = 0; i < current->m_successor_count; ++i)
            {
                woort_IRBlock* succ = current->m_successors[i];
                uint32_t succ_idx = succ->m_index;
                if (!visited[succ_idx])
                {
                    visited[succ_idx] = true;
                    stack[stack_top++] = succ;
                }
            }
        }

        free(stack);

        for (uint32_t i = 0; i < func->m_block_count; ++i)
        {
            if (!visited[i])
            {
                _woort_ir_compiler_set_error(compiler,
                    "Block %u in function %u is unreachable",
                    i, func_idx);
                free(visited);
                return false;
            }
        }

        free(visited);
    }

    return true;
}
