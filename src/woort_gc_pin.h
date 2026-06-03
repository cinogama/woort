#pragma once

/*
woort_gc_pin.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

struct woort_GCPin
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

    size_t          m_size;
    woort_GCPin*    m_prev;
    woort_GCPin*    m_next;
    woort_Value     m_datas[];

};

extern const woort_GCUnitProxy WOORT_GCPIN_UNIT_PROXY;

void woort_GCPin_bootup(void);
void woort_GCPin_shutdown(void);
