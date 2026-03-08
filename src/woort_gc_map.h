#pragma once

/*
woort_gc_map.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

struct woort_GCMap
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

};

extern const woort_GCUnitProxy g_gcmap_unit_proxy;
