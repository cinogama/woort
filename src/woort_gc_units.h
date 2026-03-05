#pragma once

/*
woort_gc_units.h
*/

#include <stddef.h>
#include <stdint.h>

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

#define WOORT_GCUNIT_ALLOC_ATTRIB_O 0
#define WOORT_GCUNIT_ALLOC_ATTRIB_A WOOMEM_GC_UNIT_TYPE_AUTO_MARK
#define WOORT_GCUNIT_ALLOC_ATTRIB_M WOOMEM_GC_UNIT_TYPE_HAS_MARKER
#define WOORT_GCUNIT_ALLOC_ATTRIB_F WOOMEM_GC_UNIT_TYPE_HAS_FINALIZER

#define WOORT_GCUNIT_ALLOC_ATTRIB_AM \
    (WOORT_GCUNIT_ALLOC_ATTRIB_A | WOORT_GCUNIT_ALLOC_ATTRIB_M)
#define WOORT_GCUNIT_ALLOC_ATTRIB_AF \
    (WOORT_GCUNIT_ALLOC_ATTRIB_A | WOORT_GCUNIT_ALLOC_ATTRIB_F)
#define WOORT_GCUNIT_ALLOC_ATTRIB_MF \
    (WOORT_GCUNIT_ALLOC_ATTRIB_M | WOORT_GCUNIT_ALLOC_ATTRIB_F)
#define WOORT_GCUNIT_ALLOC_ATTRIB_AMF \
    (WOORT_GCUNIT_ALLOC_ATTRIB_AM | WOORT_GCUNIT_ALLOC_ATTRIB_F)

// Before using this macro, you must include "woomem.h".
#define woort_GCUnit_alloc_attrib(ATTRIB, SIZE) \
    woomem_alloc_attrib(                        \
        (SIZE),                                 \
        WOOMEM_GC_UNIT_TYPE_NEED_SWEEP          \
        | (WOORT_GCUNIT_ALLOC_ATTRIB_##ATTRIB))

typedef struct woort_GCString
{
    woort_GCUnit m_gc_unit;

    size_t      m_length;
    const char* m_content;

}woort_GCString;

extern const woort_GCUnitProxy g_gcstring_unit_proxy;

const woort_GCString* woort_GCString_make_string(const char* str, size_t len);
const woort_GCString* woort_GCString_add_string(const woort_GCString* a, const woort_GCString* b);
