#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>

#include "woort_vector.h"
#include "woort_log.h"

void woort_vector_init(woort_Vector* vector, size_t element_size)
{
    vector->m_element_size = element_size;
    vector->m_size = 0;
    vector->m_capacity = 0; // Initial capacity.

    vector->m_data = NULL;
}
void woort_vector_deinit(woort_Vector* vector)
{
    if (vector->m_data != NULL)
    {
        free(vector->m_data);
        vector->m_data = NULL;
    }
}

WOORT_NODISCARD bool woort_vector_reserve(woort_Vector* vector, size_t new_capacity)
{
    if (new_capacity <= vector->m_capacity)
        return true; // No need to reserve.

    // Make capacity to the next power of two.
    while (new_capacity > vector->m_capacity)
    {
        if (vector->m_capacity == 0)
            vector->m_capacity = 8; // Initial capacity.
        else
            vector->m_capacity *= 2;
    }
    void* new_data = realloc(
        vector->m_data,
        vector->m_capacity * vector->m_element_size);
    if (new_data == NULL)
    {
        WOORT_DEBUG("Reallocation failed.");
        return false;
    }

    vector->m_data = new_data;
    return true;
}
WOORT_NODISCARD bool woort_vector_resize(woort_Vector* vector, size_t new_size)
{
    if (!woort_vector_reserve(vector, new_size))
        return false;
    vector->m_size = new_size;
    return true;
}
WOORT_NODISCARD bool woort_vector_emplace_back(woort_Vector* vector, size_t count, void** out_element)
{
    if (!woort_vector_reserve(vector, vector->m_size + count))
        return false;

    *out_element =
        (void*)(vector->m_data + vector->m_size * vector->m_element_size);
    vector->m_size += count;

    return true;
}
WOORT_NODISCARD bool woort_vector_push_back(woort_Vector* vector, size_t count, const void* element)
{
    void* new_element;
    if (!woort_vector_emplace_back(vector, count, &new_element))
        return false;
    memcpy(new_element, element, count * vector->m_element_size);
    return true;
}
void woort_vector_clear(woort_Vector* vector)
{
    vector->m_size = 0;
}
WOORT_NODISCARD bool woort_vector_index(woort_Vector* vector, size_t index, void** out_element)
{
    if (index >= vector->m_size)
    {
        WOORT_DEBUG("Index out of bounds.");
        return false;
    }
    *out_element =
        (void*)(vector->m_data + index * vector->m_element_size);
    return true;
}
WOORT_NODISCARD void* woort_vector_at(woort_Vector* vector, size_t index)
{
    void* result;
    if (!woort_vector_index(vector, index, &result))
        abort();

    return result;
}
void woort_vector_erase_at(woort_Vector* vector, size_t index)
{
    if (index >= vector->m_size)
        return; // Index out of bounds, do nothing.

    // 如果不是最后一个元素，需要移动后续元素
    if (index < vector->m_size - 1)
    {
        size_t elements_to_move = vector->m_size - index - 1;
        void* dest = vector->m_data + index * vector->m_element_size;
        void* src = vector->m_data + (index + 1) * vector->m_element_size;
        memmove(dest, src, elements_to_move * vector->m_element_size);
    }

    // 减少大小
    vector->m_size--;
}
void* woort_vector_move_out(woort_Vector* vector, size_t* out_count)
{
    *out_count = vector->m_size;
    void* const result = vector->m_data;

    vector->m_size = 0;
    vector->m_capacity = 0;
    vector->m_data = NULL;

    return result;
}
