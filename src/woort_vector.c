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

bool woort_vector_reserve(woort_Vector* vector, size_t new_capacity)
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
bool woort_vector_emplace_back(woort_Vector* vector, size_t count, void** out_element)
{
    if (!woort_vector_reserve(vector, vector->m_size + count))
        return false;

    vector->m_size += count;
    *out_element =
        (void*)(vector->m_data + vector->m_size * vector->m_element_size);
    return true;
}
bool woort_vector_push_back(woort_Vector* vector, size_t count, const void* element)
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
bool woort_vector_index(woort_Vector* vector, size_t index, void** out_element)
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
void* woort_vector_at(woort_Vector* vector, size_t index)
{
    void* result;
    if (!woort_vector_index(vector, index, &result))
        abort();

    return result;
}