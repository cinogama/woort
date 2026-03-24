#include "woort_ir_function.h"
#include "woort_ir_block.h"

#include <stddef.h>

void woort_IRFunction_init(woort_IRFunction* ir_function, uint32_t param_count)
{
    ir_function->m_param_count = param_count;
    ir_function->m_entry_block = NULL;

    woort_linklist_init(&ir_function->m_ir_values, sizeof(woort_IRValue));
    woort_linklist_init(&ir_function->m_ir_blocks, sizeof(woort_IRBlock));
}

void woort_IRFunction_deinit(woort_IRFunction* ir_function)
{
    for (woort_IRBlock* block = woort_linklist_iter(&ir_function->m_ir_blocks);
        block != NULL;
        block = woort_linklist_next(block))
    {
        woort_IRBlock_deinit(block);
    }

    woort_linklist_deinit(&ir_function->m_ir_values);
    woort_linklist_deinit(&ir_function->m_ir_blocks);
}

/* OPTIONAL */ woort_IRBlock* woort_IRFunction_entry_block(woort_IRFunction* f)
{
    if (f->m_entry_block == NULL)
        f->m_entry_block = woort_IRFuntion_add_block(f);

    return f->m_entry_block;
}

/* OPTIONAL */ woort_IRBlock* woort_IRFuntion_add_block(woort_IRFunction* f)
{
    woort_IRBlock* block;
    if (!woort_linklist_emplace_back(&f->m_ir_blocks, (void**)&block))
        return NULL;

    woort_IRBlock_init(block, f);

    if (f->m_entry_block == NULL)
        f->m_entry_block = block;

    return block;
}

/* OPTIONAL */ woort_IRValue* _woort_IRFunction_new_value(woort_IRFunction* f)
{
    woort_IRValue* value;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&value))
        return NULL;

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFuntion_load_constant(
    woort_IRFunction* f, woort_IRConstantIndex c)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_constant(value, c);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f, uint32_t param_idx)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_argument(value, param_idx);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_operate_result(
    woort_IRFunction* f, woort_IROp* modify_op)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_operate(value, modify_op);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_phi_value(woort_IRFunction* f)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_phi(value);

    return value;
}
