#pragma once

/*
woort_gc_struct.h
*/

#include "woort_gc_struct_types.h"

#include "woort_gc_units.h"
#include "woort_value.h"

#include <stddef.h>

struct woort_CodeEnv;

extern const woort_GCUnitProxy WOORT_GCSTRUCT_UNIT_PROXY;

woort_GCStruct* woort_GCStruct_new(size_t struct_size);
WOORT_NODISCARD woort_GCStruct* woort_GCStruct_new_for_env_constant(
    woort_CodeEnv* cenv, size_t struct_size);
