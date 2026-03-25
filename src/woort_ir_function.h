#pragma once

/*
woort_ir_function.h
*/

#include "woort_diagnosis.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_linklist.h"
#include "woort_hashmap.h"
#include "woort_ir_op.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct woort_IRFunction{
    uint32_t m_param_count;

    woort_LinkList /* woort_IRValue */ m_ir_values;
    woort_LinkList /* woort_IRBlock */ m_ir_blocks;
    woort_HashMap /* woort_IRConstantIndex, woort_IRValue* */ m_ir_constant_values;

    /* OPTIONAL */ woort_IRBlock* m_entry_block;
};

void woort_IRFunction_init(woort_IRFunction* ir_function, uint32_t param_count);
void woort_IRFunction_deinit(woort_IRFunction* ir_function);

/* OPTIONAL */ woort_IRBlock* woort_IRFunction_entry_block(woort_IRFunction* f);
/* OPTIONAL */ woort_IRBlock* woort_IRFuntion_add_block(woort_IRFunction* f);

/* OPTIONAL */ woort_IRValue* _woort_IRFunction_new_value(woort_IRFunction* f);

/* OPTIONAL */ woort_IRValue* woort_IRFuntion_load_constant(
    woort_IRFunction* f, woort_IRConstantIndex c);
/* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f, uint32_t param_idx);
/* OPTIONAL */ woort_IRValue* woort_IRFunction_operate_result(
    woort_IRFunction* f, woort_IROp* modify_op);
/* OPTIONAL */ woort_IRValue* woort_IRFunction_phi_value(woort_IRFunction* f);
