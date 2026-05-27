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
    woort_GCVec* const gcvec = woort_GCUnit_alloc_delay_init(sizeof(woort_GCVec));
    gcvec->m_gc_unit.m_proxy = &WOORT_GCVEC_UNIT_PROXY;
    gcvec->m_space = 0;
    gcvec->m_length = 0;
    gcvec->m_datas = NULL;

    woort_GCUnit_init_delay_alloc(A, gcvec);

    return gcvec;
}

void _woort_GCVec_assure_vec_space(woort_GCVec* vec, size_t size)
{
    if (vec->m_space >= size)
        return;

    size_t new_space = vec->m_space * 2;
    if (new_space < size)
        new_space = size;

    woort_DynBox* new_datas;
    if (vec->m_datas == NULL)
    {
        // 此处假定分配必然成功，我们会在之后再处理其他情况
        new_datas = woort_GCUnit_alloc_delay_init(
            new_space * sizeof(woort_DynBox));
        woort_GCUnit_init_delay_alloc(A, new_datas);
    }
    else
        new_datas = woort_GCUnit_realloc(
            vec->m_datas, new_space * sizeof(woort_DynBox));

    woort_GC_mixed_write_barrier_gcunit((void**)&vec->m_datas, new_datas);
    vec->m_space = new_space;
}

void _woort_GCVec_extern(woort_GCVec* vec, size_t size)
{
    assert(vec->m_length <= size);

    _woort_GCVec_assure_vec_space(vec, size);
    vec->m_length = size;
}
void woort_GCVec_resize_without_init(woort_GCVec* vec, size_t size)
{
    const size_t origin_size = vec->m_length;
    if(size < origin_size)
    {
        for (size_t i = size; i < origin_size; i++)
        {
            woort_GC_delete_barrier_dynbox(vec->m_datas[i]);
        }
        vec->m_datas[size].m_boxed = 0;
    }

    _woort_GCVec_assure_vec_space(vec, size);
    vec->m_length = size;
}

void woort_GCVec_resize_with(woort_GCVec* vec, size_t size, woort_DynBox init_val)
{
    const size_t origin_size = vec->m_length;

    if (size < origin_size)
    {
        for (size_t i = size; i < origin_size; i++)
        {
            woort_GC_delete_barrier_dynbox(vec->m_datas[i]);
        }
        vec->m_datas[size].m_boxed = 0;
    }

    _woort_GCVec_assure_vec_space(vec, size);

    if (size > origin_size)
    {
        for (size_t i = origin_size; i < size; i++)
        {
            woort_GC_init_write_barrier_dynbox(
                &vec->m_datas[i], init_val);
        }
    }

    vec->m_length = size;
}

WOORT_NODISCARD bool woort_GCVec_shrink(woort_GCVec* vec, size_t new_size)
{
    if (new_size > vec->m_length)
        return false;

    for (size_t i = new_size; i < vec->m_length; i++)
    {
        woort_GC_delete_barrier_dynbox(vec->m_datas[i]);
    }

    vec->m_length = new_size;

    if (new_size == 0 && vec->m_datas != NULL)
    {
        woort_GC_delete_barrier_gcunit(vec->m_datas);
        vec->m_datas = NULL;
        vec->m_space = 0;
    }
    else if (new_size > 0 && new_size < vec->m_space / 4)
    {
        woort_DynBox* new_datas = woort_GCUnit_realloc(
            vec->m_datas, new_size * sizeof(woort_DynBox));
        woort_GC_mixed_write_barrier_gcunit(
            (void**)&vec->m_datas, new_datas);
        vec->m_space = new_size;
    }

    return true;
}

void woort_GCVec_push_back(woort_GCVec* vec, woort_DynBox boxed_value)
{
    _woort_GCVec_assure_vec_space(vec, vec->m_length + 1);

    woort_GC_init_write_barrier_dynbox(
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

WOORT_NODISCARD bool woort_GCVec_pop_back(woort_GCVec* vec)
{
    if (vec->m_length == 0)
        return false;

    woort_GC_delete_barrier_dynbox(
        vec->m_datas[vec->m_length - 1]);
    vec->m_length--;
    return true;
}

WOORT_NODISCARD bool woort_GCVec_insert(woort_GCVec* vec, size_t index, woort_DynBox boxed_value)
{
    if (index > vec->m_length)
        return false;

    _woort_GCVec_assure_vec_space(vec, vec->m_length + 1);

    for (size_t i = vec->m_length; i > index; i--)
    {
        woort_GC_mixed_write_barrier_dynbox(
            &vec->m_datas[i], vec->m_datas[i - 1]);
    }

    woort_GC_mixed_write_barrier_dynbox(
        &vec->m_datas[index], boxed_value);
    vec->m_length++;
    return true;
}

WOORT_NODISCARD bool woort_GCVec_erase(woort_GCVec* vec, size_t index)
{
    if (index >= vec->m_length)
        return false;

    woort_GC_delete_barrier_dynbox(
        vec->m_datas[index]);

    for (size_t i = index; i < vec->m_length - 1; i++)
    {
        woort_GC_mixed_write_barrier_dynbox(
            &vec->m_datas[i], vec->m_datas[i + 1]);
    }

    vec->m_length--;
    vec->m_datas[vec->m_length].m_boxed = 0;
    return true;
}

void woort_GCVec_clear(woort_GCVec* vec)
{
    for (size_t i = 0; i < vec->m_length; i++)
    {
        woort_GC_delete_barrier_dynbox(vec->m_datas[i]);
        vec->m_datas[i].m_boxed = 0;
    }

    vec->m_length = 0;
}

void woort_GCVec_copy(woort_GCVec* dst, const woort_GCVec* src)
{
    woort_GCVec_clear(dst);
    _woort_GCVec_extern(dst, src->m_length);

    for (size_t i = 0; i < src->m_length; i++)
    {
        woort_GC_init_write_barrier_dynbox(
            &dst->m_datas[i], src->m_datas[i]);
    }
}

void woort_GCVec_swap(woort_GCVec* a, woort_GCVec* b)
{
    size_t tmp_space = a->m_space;
    size_t tmp_length = a->m_length;
    woort_DynBox* tmp_datas = a->m_datas;
    woort_GC_delete_barrier_gcunit(a->m_datas);

    a->m_space = b->m_space;
    a->m_length = b->m_length;
    woort_GC_mixed_write_barrier_gcunit((void**)&a->m_datas, b->m_datas);

    b->m_space = tmp_space;
    b->m_length = tmp_length;
    woort_GC_mixed_write_barrier_gcunit((void**)&b->m_datas, tmp_datas);
}


