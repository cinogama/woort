#pragma once

/*
woort_lir_block.h
*/
#include "woort_diagnosis.h"
#include "woort_lir.h"

#include <stdbool.h>

typedef struct woort_LIRBlock
{
    // LIR codes
    woort_Vector /* woort_LIR */ m_lir_list;

    woort_Vector /* struct woort_LIRBlock* */
        m_prev_blocks;

    /* OPTIONAL */ woort_LIRRegister* 
        m_cond;
    /* OPTIONAL */ struct woort_LIRBlock* 
        m_cond_next_block;
    /* OPTIONAL, NULL if last block */ struct woort_LIRBlock*
        m_next_block;

} woort_LIRBlock;

void woort_LIRBlock_init(woort_LIRBlock* block);
void woort_LIRBlock_deinit(woort_LIRBlock* block);

void woort_LIRBlock_jmp(
    woort_LIRBlock* block,
    woort_LIRBlock* dst_block);

void woort_LIRBlock_cond_jmp(
    woort_LIRBlock* block,
    woort_LIRRegister* cond,
    woort_LIRBlock* dst_block_if_cond,
    woort_LIRBlock* dst_block_else);
