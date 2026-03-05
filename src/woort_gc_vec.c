#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "woomem.h"
#include "woort_gc_vec.h"

const woort_GCUnitProxy g_gcvec_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

woort_GCVec* woort_GCVec_make_vec(size_t advise_reserving_sz)
{
    woort_GCVec* gcvec = woort_GCUnit_alloc_attrib(A, sizeof(woort_GCVec));
    gcvec->m_gc_unit.m_proxy = &g_gcvec_unit_proxy;
    gcvec->m_length = 0;

    gcvec->m_datas =
        advise_reserving_sz == 0
        ? NULL
        : /* Might failed if out of memory */ woort_GCUnit_alloc_attrib(
            A, advise_reserving_sz * sizeof(woort_Value));

    if (gcvec->m_datas != NULL)
        gcvec->m_space = advise_reserving_sz;
    else
        gcvec->m_space = 0;

    return gcvec;
}

