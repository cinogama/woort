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

void _woort_GCVec_assure_vec_space(woort_GCVec* vec, size_t size)
{
    if (vec->m_space >= size)
        return;

    size_t new_space = vec->m_space * 2;
    if (new_space < size)
        new_space = size;

    woort_Value* new_datas = vec->m_datas == NULL
        // 此处假定分配必然成功，我们会在之后再处理其他情况
        ? woort_GCUnit_alloc_attrib(A, new_space * sizeof(woort_Value))
        : woomem_realloc(vec->m_datas, new_space * sizeof(woort_Value));

    vec->m_datas = new_datas;
    vec->m_space = new_space;
}

void woort_GCVec_resize(woort_GCVec* vec, size_t size)
{
    _woort_GCVec_assure_vec_space(vec, size);
    vec->m_length = size;
}

void woort_GCVec_push_back(woort_GCVec* vec, woort_Value value)
{
    _woort_GCVec_assure_vec_space(vec, vec->m_length + 1);
    vec->m_datas[vec->m_length++] = value;
}


