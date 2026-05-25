#pragma once

/*
woort_gc_units.h
*/

#include "woomem.h"

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
#define WOORT_GCUNIT_ALLOC_ATTRIB_A WOOMEM_ATTRIB_AUTO_MARK
#define WOORT_GCUNIT_ALLOC_ATTRIB_M WOOMEM_ATTRIB_MARK_CALLBACK
#define WOORT_GCUNIT_ALLOC_ATTRIB_F WOOMEM_ATTRIB_FREE_CALLBACK

#define WOORT_GCUNIT_ALLOC_ATTRIB_AM \
    (WOORT_GCUNIT_ALLOC_ATTRIB_A | WOORT_GCUNIT_ALLOC_ATTRIB_M)
#define WOORT_GCUNIT_ALLOC_ATTRIB_AF \
    (WOORT_GCUNIT_ALLOC_ATTRIB_A | WOORT_GCUNIT_ALLOC_ATTRIB_F)
#define WOORT_GCUNIT_ALLOC_ATTRIB_MF \
    (WOORT_GCUNIT_ALLOC_ATTRIB_M | WOORT_GCUNIT_ALLOC_ATTRIB_F)
#define WOORT_GCUNIT_ALLOC_ATTRIB_AMF \
    (WOORT_GCUNIT_ALLOC_ATTRIB_AM | WOORT_GCUNIT_ALLOC_ATTRIB_F)

void _woort_GCUnit_alloc_failed(void);

inline static void* woort_GCUnit_realloc(void* ptr, size_t sz)
{
    do
    {
        void* p = woomem_reallocate(ptr, sz);
        if (p != NULL)
            return p;

        // Out of memory.
        _woort_GCUnit_alloc_failed();

    } while (1);
}
inline static void* woort_GCUnit_alloc_delay_init(size_t sz)
{
    do
    {
        void* p = woomem_allocate_begin(sz);
        if (p != NULL)
            return p;

        // Out of memory.
        _woort_GCUnit_alloc_failed();

    } while (1);
}

#define woort_GCUnit_init_delay_alloc(ATTRIB, PTR)  \
    woomem_allocate_end(                            \
        PTR,                                        \
        WOOMEM_ATTRIB_NEED_SWEEP                    \
        | (WOORT_GCUNIT_ALLOC_ATTRIB_##ATTRIB))
