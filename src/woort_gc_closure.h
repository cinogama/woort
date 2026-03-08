#pragma once

/*
woort_gc_map.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

struct woort_GCClosure
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

    woort_RuntimeFunction m_func;
    size_t      m_size;
    woort_Value m_datas[];

};

extern const woort_GCUnitProxy g_gcclosure_unit_proxy;

woort_GCClosure* woort_GCClosure_new(
    woort_RuntimeFunction func, size_t captured_count);
