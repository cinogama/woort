#pragma once

/*
woort_gc_units_types.h
*/

#include "woort.h"

typedef struct woort_GCUnit woort_GCUnit;

typedef void(*woort_GCUnitProxy_MarkCallback)(woort_GCUnit*);
typedef void(*woort_GCUnitProxy_DestructCallback)(woort_GCUnit*);

typedef struct woort_GCUnitProxy
{
    /* OPTIONAL */ woort_GCUnitProxy_MarkCallback m_marker;
    /* OPTIONAL */ woort_GCUnitProxy_DestructCallback m_destructor;

}woort_GCUnitProxy;

struct woort_GCUnit
{
    const woort_GCUnitProxy* m_proxy;
};
