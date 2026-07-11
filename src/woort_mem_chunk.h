#pragma once

/*
woort_mem_chunk.h
Chunk: manages a large reserved virtual memory region with a
buddy-list-style page allocator.
*/

#include "woort_mem_page.h"
#include "woort_spin.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define WOORT_MEM_CHUNK_INDEX_NULL     UINT32_MAX
#define WOORT_MEM_CHUNK_ALLOCATED_FLAG 0x80000000u
#define WOORT_MEM_CHUNK_COUNT_MASK     0x7FFFFFFFu

typedef struct woort_mem_Chunk
{
    void*           base;
    size_t          reserved_size;
    size_t          total_pages;

    uint32_t*       count_arr;
    uint32_t*       free_prev;
    uint32_t*       free_next;
    uint32_t        free_head;
    uint8_t*        commit_arr;

    woort_RWSpinlock rwlock;

} woort_mem_Chunk;

void woort_mem_chunk_init(woort_mem_Chunk* self, size_t reserved_size);
void woort_mem_chunk_deinit(woort_mem_Chunk* self);

WOORT_NODISCARD bool woort_mem_chunk_is_init_failed(const woort_mem_Chunk* self);

WOORT_NODISCARD  /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_allocate_page(woort_mem_Chunk* self);
WOORT_NODISCARD  /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_allocate_huge_page(
    woort_mem_Chunk* self, size_t size);
void woort_mem_chunk_free_page(
    woort_mem_Chunk* self, woort_mem_PageHead* page);

WOORT_NODISCARD  /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_validate(
    woort_mem_Chunk* self, void* ptr);

WOORT_NODISCARD size_t woort_mem_chunk_get_total_size(const woort_mem_Chunk* self);
WOORT_NODISCARD size_t woort_mem_chunk_get_total_page_count(const woort_mem_Chunk* self);
