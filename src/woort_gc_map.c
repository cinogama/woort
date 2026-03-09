#include "woomem.h"
#include "woort_gc_map.h"

const woort_GCUnitProxy g_gcmap_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void)
{
    woort_GCMap* const gcmap = woort_GCUnit_alloc_attrib(
        A, sizeof(woort_GCMap));

    gcmap->m_gc_unit.m_proxy = &g_gcmap_unit_proxy;
    gcmap->m_entries = NULL;
    gcmap->m_mask = 0;
    gcmap->m_size = 0;

    return gcmap;
}