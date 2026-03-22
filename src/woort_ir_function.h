#pragma once

/*
woort_ir_function.h
*/

#include "woort_diagnosis.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"

#include <stdbool.h>

typedef struct woort_IRFunction{
    char _;

}woort_IRFunction;

void woort_IRFunction_init(woort_IRFunction* ir_function);
void woort_IRFunction_deinit(woort_IRFunction* ir_function);

woort_IRBlock* woort_IRFunction_entry_block(woort_IRFunction* f);
woort_IRBlock* woort_IRFuntion_add_block(woort_IRFunction* f);

woort_IRValue* woort_IRFuntion_load_constant(
    woort_IRFunction* f, woort_IRConstantIndex c);
woort_IRValue* _woort_IRFunction_new_value(woort_IRFunction* f);



