#pragma once

/*
 * woort_ir_arena.h
 */

#include "../woort_diagnosis.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct woort_IRArena
{
    char*           m_memory;
    size_t          m_size;
    size_t          m_capacity;
    size_t          m_block_size;
    char**          m_blocks;
    size_t          m_block_count;
    size_t          m_block_capacity;

} woort_IRArena;

WOORT_NODISCARD bool woort_IRArena_create(
    size_t initial_capacity,
    woort_IRArena** out_arena);

void woort_IRArena_destroy(woort_IRArena* arena);

void* woort_IRArena_alloc(woort_IRArena* arena, size_t size);

void woort_IRArena_reset(woort_IRArena* arena);

#define woort_IRArena_alloc_type(arena, type) \
    ((type*)woort_IRArena_alloc((arena), sizeof(type)))

#define woort_IRArena_alloc_array(arena, type, count) \
    ((type*)woort_IRArena_alloc((arena), sizeof(type) * (count)))