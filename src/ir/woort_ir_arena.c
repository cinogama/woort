#include "woort_ir_arena.h"

#include <stdlib.h>
#include <string.h>

#define WOORT_IR_ARENA_DEFAULT_BLOCK_SIZE 4096
#define WOORT_IR_ARENA_INITIAL_BLOCK_CAPACITY 16

WOORT_NODISCARD bool woort_IRArena_create(
    size_t initial_capacity,
    woort_IRArena** out_arena)
{
    if (initial_capacity == 0)
    {
        initial_capacity = WOORT_IR_ARENA_DEFAULT_BLOCK_SIZE;
    }

    woort_IRArena* arena = (woort_IRArena*)malloc(sizeof(woort_IRArena));
    if (!arena)
    {
        return false;
    }

    arena->m_memory = (char*)malloc(initial_capacity);
    if (!arena->m_memory)
    {
        free(arena);
        return false;
    }

    arena->m_size = 0;
    arena->m_capacity = initial_capacity;
    arena->m_block_size = initial_capacity;

    arena->m_blocks = (char**)malloc(sizeof(char*) * WOORT_IR_ARENA_INITIAL_BLOCK_CAPACITY);
    if (!arena->m_blocks)
    {
        free(arena->m_memory);
        free(arena);
        return false;
    }

    arena->m_blocks[0] = arena->m_memory;
    arena->m_block_count = 1;
    arena->m_block_capacity = WOORT_IR_ARENA_INITIAL_BLOCK_CAPACITY;

    *out_arena = arena;
    return true;
}

void woort_IRArena_destroy(woort_IRArena* arena)
{
    if (!arena)
    {
        return;
    }

    for (size_t i = 0; i < arena->m_block_count; ++i)
    {
        free(arena->m_blocks[i]);
    }
    free(arena->m_blocks);
    free(arena);
}

void* woort_IRArena_alloc(woort_IRArena* arena, size_t size)
{
    size = (size + 7) & ~((size_t)7);

    if (arena->m_size + size <= arena->m_capacity)
    {
        void* result = arena->m_memory + arena->m_size;
        arena->m_size += size;
        return result;
    }

    if (size > arena->m_block_size)
    {
        char* new_block = (char*)malloc(size);
        if (!new_block)
        {
            return NULL;
        }

        if (arena->m_block_count >= arena->m_block_capacity)
        {
            size_t new_capacity = arena->m_block_capacity * 2;
            char** new_blocks = (char**)realloc(arena->m_blocks, sizeof(char*) * new_capacity);
            if (!new_blocks)
            {
                free(new_block);
                return NULL;
            }
            arena->m_blocks = new_blocks;
            arena->m_block_capacity = new_capacity;
        }

        arena->m_blocks[arena->m_block_count++] = new_block;
        return new_block;
    }

    char* new_block = (char*)malloc(arena->m_block_size);
    if (!new_block)
    {
        return NULL;
    }

    if (arena->m_block_count >= arena->m_block_capacity)
    {
        size_t new_capacity = arena->m_block_capacity * 2;
        char** new_blocks = (char**)realloc(arena->m_blocks, sizeof(char*) * new_capacity);
        if (!new_blocks)
        {
            free(new_block);
            return NULL;
        }
        arena->m_blocks = new_blocks;
        arena->m_block_capacity = new_capacity;
    }

    arena->m_blocks[arena->m_block_count++] = new_block;
    arena->m_memory = new_block;
    arena->m_size = size;
    arena->m_capacity = arena->m_block_size;

    return new_block;
}

void woort_IRArena_reset(woort_IRArena* arena)
{
    if (arena->m_block_count > 1)
    {
        for (size_t i = 1; i < arena->m_block_count; ++i)
        {
            free(arena->m_blocks[i]);
        }
        arena->m_block_count = 1;
    }

    arena->m_memory = arena->m_blocks[0];
    arena->m_size = 0;
    arena->m_capacity = arena->m_block_size;
}