#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "woomem.h"
#include "woort_gc.h"
#include "woort_gc_vec.h"
#include "woort_diagnosis.h"

const woort_GCUnitProxy WOORT_GCVEC_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

woort_GCVec* woort_GCVec_new(void)
{
    woort_GCVec* const gcvec = woort_GCUnit_alloc_attrib(A, sizeof(woort_GCVec));
    gcvec->m_gc_unit.m_proxy = &WOORT_GCVEC_UNIT_PROXY;
    gcvec->m_space = 0;
    gcvec->m_length = 0;
    gcvec->m_datas = NULL;

    return gcvec;
}

void _woort_GCVec_assure_vec_space(woort_GCVec* vec, size_t size)
{
    if (vec->m_space >= size)
        return;

    size_t new_space = vec->m_space * 2;
    if (new_space < size)
        new_space = size;

    woort_DynBox* const new_datas = vec->m_datas == NULL
        // 此处假定分配必然成功，我们会在之后再处理其他情况
        ? woort_GCUnit_alloc_attrib(A, new_space * sizeof(woort_DynBox))
        : woomem_realloc(vec->m_datas, new_space * sizeof(woort_DynBox));

    woort_GC_mixed_write_barrier_gcaddr(&vec->m_datas, new_datas);
    vec->m_space = new_space;
}

void woort_GCVec_resize(woort_GCVec* vec, size_t size)
{
    _woort_GCVec_assure_vec_space(vec, size);
    vec->m_length = size;
}

void woort_GCVec_push_back(woort_GCVec* vec, woort_DynBox boxed_value)
{
    _woort_GCVec_assure_vec_space(vec, vec->m_length + 1);

    woort_GC_mixed_write_barrier_dynbox(
        &vec->m_datas[vec->m_length++], boxed_value);
}

WOORT_NODISCARD woort_DynBox* woort_GCVec_emplace_back(woort_GCVec* vec, size_t count)
{
    _woort_GCVec_assure_vec_space(vec, vec->m_length + count);

    woort_DynBox* const result = &vec->m_datas[vec->m_length];
    vec->m_length += count;

    return result;
}

WOORT_NODISCARD bool woort_GCVec_get(const woort_GCVec* vec, size_t index, woort_DynBox* out_boxval)
{
    if (index >= vec->m_length)
        return false;

    *out_boxval = vec->m_datas[index];
    return true;
}

WOORT_NODISCARD bool woort_GCVec_set(woort_GCVec* vec, size_t index, woort_DynBox boxed_value)
{
    if (index >= vec->m_length)
        return false;

    woort_GC_mixed_write_barrier_dynbox(
        &vec->m_datas[index], boxed_value);
    return true;
}

void woort_GCVec_pop_back(woort_GCVec* vec)
{
    assert(vec->m_length > 0);

    woort_GC_delete_barrier_dynbox(
        vec->m_datas[vec->m_length - 1]);
    vec->m_length--;
}

void woort_GCVec_insert(woort_GCVec* vec, size_t index, woort_DynBox boxed_value)
{
    if (index > vec->m_length)
        woort_panic(WOORT_PANIC_INDEX_OUT_OF_RANGE, "vec insert index out of range");

    _woort_GCVec_assure_vec_space(vec, vec->m_length + 1);

    for (size_t i = vec->m_length; i > index; i--)
    {
        woort_GC_mixed_write_barrier_dynbox(
            &vec->m_datas[i], vec->m_datas[i - 1]);
    }

    woort_GC_mixed_write_barrier_dynbox(
        &vec->m_datas[index], boxed_value);
    vec->m_length++;
}

void woort_GCVec_erase(woort_GCVec* vec, size_t index)
{
    if (index >= vec->m_length)
        woort_panic(WOORT_PANIC_INDEX_OUT_OF_RANGE, "vec erase index out of range");

    woort_GC_delete_barrier_dynbox(
        vec->m_datas[index]);

    for (size_t i = index; i < vec->m_length - 1; i++)
    {
        woort_GC_mixed_write_barrier_dynbox(
            &vec->m_datas[i], vec->m_datas[i + 1]);
    }

    vec->m_length--;
    vec->m_datas[vec->m_length].m_boxed_gc_unit = NULL;
}

void woort_GCVec_clear(woort_GCVec* vec)
{
    vec->m_length = 0;
}


