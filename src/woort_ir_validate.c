/*
 * woort_ir_validate.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_compiler.h"

#include <stdlib.h>
#include <string.h>

static WOORT_NODISCARD bool _woort_ir_validate_block_terminator(
    woort_IRCompiler* compiler,
    woort_IRBlock* block)
{
    if (!block->m_has_terminator)
    {
        _woort_ir_compiler_set_error(compiler, "Block %u in function %u has no terminator",
            block->m_index, block->m_func->m_index);
        return false;
    }

    return true;
}

static WOORT_NODISCARD bool _woort_ir_validate_function_blocks(woort_IRCompiler* compiler, woort_IRFunction* func)
{
    for (uint32_t i = 0; i < func->m_block_count; ++i)
    {
        if (!_woort_ir_validate_block_terminator(compiler, func->m_blocks[i]))
        {
            return false;
        }
    }

    return true;
}

static WOORT_NODISCARD bool _woort_ir_validate_phi_incomings(
    woort_IRCompiler* compiler,
    woort_IRPHI* phi)
{
    if (phi->m_incoming_count == 0)
    {
        _woort_ir_compiler_set_error(compiler, "PHI in block %u has no incoming values",
            phi->m_block->m_index);
        return false;
    }

    for (uint32_t i = 0; i < phi->m_incoming_count; ++i)
    {
        woort_IRBlock* from_block = phi->m_incomings[i].m_from_block;
        if (from_block == NULL)
        {
            _woort_ir_compiler_set_error(compiler, "PHI incoming %u has null from_block", i);
            return false;
        }

        if (phi->m_incomings[i].m_value == NULL)
        {
            _woort_ir_compiler_set_error(compiler, "PHI incoming %u has null value", i);
            return false;
        }

        bool found_predecessor = false;
        for (uint32_t j = 0; j < phi->m_block->m_predecessor_count; ++j)
        {
            if (phi->m_block->m_predecessors[j] == from_block)
            {
                found_predecessor = true;
                break;
            }
        }

        if (!found_predecessor)
        {
            _woort_ir_compiler_set_error(compiler,
                "PHI incoming from block %u is not a predecessor of PHI block %u",
                from_block->m_index, phi->m_block->m_index);
            return false;
        }
    }

    return true;
}

static WOORT_NODISCARD bool _woort_ir_validate_function_phis(woort_IRCompiler* compiler, woort_IRFunction* func)
{
    for (uint32_t i = 0; i < func->m_phi_count; ++i)
    {
        if (!_woort_ir_validate_phi_incomings(compiler, func->m_phis[i]))
        {
            return false;
        }
    }

    return true;
}

WOORT_NODISCARD bool _woort_ir_validate(woort_IRCompiler* compiler)
{
    for (uint32_t i = 0; i < compiler->m_function_count; ++i)
    {
        woort_IRFunction* func = compiler->m_functions[i];
        if (func == NULL)
        {
            _woort_ir_compiler_set_error(compiler, "Function %u is null", i);
            return false;
        }

        if (!_woort_ir_validate_function_blocks(compiler, func))
        {
            return false;
        }

        if (!_woort_ir_validate_function_phis(compiler, func))
        {
            return false;
        }
    }

    return true;
}
