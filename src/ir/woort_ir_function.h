#pragma once

/*
 * woort_ir_function.h
 */

#include "woort_ir_block.h"
#include "woort_ir_local.h"
#include <stdint.h>

typedef struct woort_IRModule woort_IRModule;

typedef struct woort_IRFunction
{
    woort_IRModule*         m_module;
    const char*             m_name;
    uint32_t                m_id;
    uint32_t                m_param_count;

    woort_IRBlock*          m_entry_block;
    woort_IRBlock*          m_block_list;
    uint32_t                m_block_count;

    woort_IRLocal**         m_locals;
    uint32_t                m_local_count;
    uint32_t                m_local_capacity;

    uint32_t                m_next_value_id;
    uint32_t                m_next_block_id;

} woort_IRFunction;