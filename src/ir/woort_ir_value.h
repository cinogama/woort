#pragma once

/*
 * woort_ir_value.h
 */

#include <stdint.h>

typedef struct woort_IRBlock woort_IRBlock;
typedef struct woort_IRInst woort_IRInst;

typedef struct woort_IRValue
{
    uint32_t            m_id;
    woort_IRBlock*      m_def_block;
    woort_IRInst*       m_def_inst;

} woort_IRValue;