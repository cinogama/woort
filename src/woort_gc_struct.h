#pragma once

/*
woort_gc_struct.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

struct woort_GCStruct
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

    size_t          m_size;
    woort_Value     m_datas[];

};

extern const woort_GCUnitProxy WOORT_GCSTRUCT_UNIT_PROXY;

woort_GCStruct* woort_GCStruct_new(size_t struct_size);
