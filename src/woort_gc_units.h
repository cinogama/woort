#pragma once

/*
woort_gc_units.h
*/

typedef void(*woort_GCUnitProxy_MarkCallback)(struct woort_GCUnit*);
typedef void(*woort_GCUnitProxy_DestructCallback)(struct woort_GCUnit*);

typedef struct woort_GCUnitProxy
{
    /* OPTIONAL */ woort_GCUnitProxy_MarkCallback m_marker;
    /* OPTIONAL */ woort_GCUnitProxy_DestructCallback m_destructor;

}woort_GCUnitProxy;

typedef struct woort_GCUnit
{
    const woort_GCUnitProxy* m_proxy;

}woort_GCUnit;

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

#define woort_GCUnit_alloc_attrib(ATTRIB, SIZE) \
    woomem_alloc_attrib(                        \
        (SIZE),                                 \
        WOOMEM_GC_UNIT_TYPE_NEED_SWEEP          \
        | (WOORT_GCUNIT_ALLOC_ATTRIB_##ATTRIB))
