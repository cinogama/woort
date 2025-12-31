#pragma once

/*
woort_vector.h
*/

#include <stddef.h>
#include <stdbool.h>

typedef struct woort_Vector
{
    char*       m_data;
    size_t      m_size;
    size_t      m_capacity;

    size_t      m_element_size;

} woort_Vector;

void woort_vector_init(woort_Vector* vector, size_t element_size);
void woort_vector_deinit(woort_Vector* vector);

bool woort_vector_reserve(woort_Vector* vector, size_t new_capacity);
bool woort_vector_emplace_back(woort_Vector* vector, size_t count, void** out_element);
bool woort_vector_push_back(woort_Vector* vector, size_t count, const void* element);

bool woort_vector_index(woort_Vector* vector, size_t index, void** out_element);
