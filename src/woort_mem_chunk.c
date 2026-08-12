/*
woort_mem_chunk.c
*/

#include "woort_mem_chunk.h"
#include "woort_mem_os.h"
#include "woort_log.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

WOORT_NODISCARD static size_t woort_mem_chunk_page_to_index(
    const woort_mem_Chunk* self, woort_mem_PageHead* page)
{
    return (size_t)(
        ((uintptr_t)page - (uintptr_t)self->base)
        / WOORT_MEM_NORMAL_PAGE_SIZE);
}

WOORT_NODISCARD static woort_mem_PageHead* woort_mem_chunk_index_to_page(
    const woort_mem_Chunk* self, size_t idx)
{
    return (woort_mem_PageHead*)(
        (uintptr_t)self->base
        + idx * WOORT_MEM_NORMAL_PAGE_SIZE);
}

WOORT_NODISCARD static /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_commit_page(
    woort_mem_Chunk* self, size_t idx)
{
    woort_mem_PageHead* const p = woort_mem_chunk_index_to_page(self, idx);
    if (!self->commit_arr[idx])
    {
        if (woort_mem_os_commit_memory(p, WOORT_MEM_NORMAL_PAGE_SIZE) != 0)
        {
            WOORT_DEBUG("woort_mem_os_commit_memory failed");
            return NULL;
        }
        self->commit_arr[idx] = 1;
    }
    return p;
}

static void woort_mem_chunk_free_list_remove(
    woort_mem_Chunk* self, uint32_t idx)
{
    uint32_t prev = self->free_prev[idx];
    uint32_t next = self->free_next[idx];

    if (prev != WOORT_MEM_CHUNK_INDEX_NULL)
        self->free_next[prev] = next;
    else
        self->free_head = next;

    if (next != WOORT_MEM_CHUNK_INDEX_NULL)
        self->free_prev[next] = prev;

    self->free_prev[idx] = WOORT_MEM_CHUNK_INDEX_NULL;
    self->free_next[idx] = WOORT_MEM_CHUNK_INDEX_NULL;
}

static void woort_mem_chunk_free_list_insert(
    woort_mem_Chunk* self, uint32_t idx, uint32_t count)
{
restart:
    {
        uint32_t prev = WOORT_MEM_CHUNK_INDEX_NULL;
        uint32_t next = self->free_head;
        while (next != WOORT_MEM_CHUNK_INDEX_NULL && next < idx)
        {
            prev = next;
            next = self->free_next[next];
        }

        if (prev != WOORT_MEM_CHUNK_INDEX_NULL)
        {
            uint32_t prev_count = self->count_arr[prev];
            if (prev + prev_count == idx)
            {
                woort_mem_chunk_free_list_remove(self, prev);
                count = prev_count + count;
                idx = prev;
                goto restart;
            }
        }

        if (next != WOORT_MEM_CHUNK_INDEX_NULL)
        {
            const uint32_t next_count = self->count_arr[next];
            if (idx + count == next)
            {
                woort_mem_chunk_free_list_remove(self, next);
                count = count + next_count;
                goto restart;
            }
        }

        self->free_prev[idx] = prev;
        self->free_next[idx] = next;

        if (prev != WOORT_MEM_CHUNK_INDEX_NULL)
            self->free_next[prev] = idx;
        else
            self->free_head = idx;

        if (next != WOORT_MEM_CHUNK_INDEX_NULL)
            self->free_prev[next] = idx;

        self->count_arr[idx] = count;
    }
}

WOORT_NODISCARD static uint32_t woort_mem_chunk_free_list_find_block(
    const woort_mem_Chunk* self, uint32_t required)
{
    uint32_t curr = self->free_head;
    while (curr != WOORT_MEM_CHUNK_INDEX_NULL)
    {
        if ((self->count_arr[curr] & WOORT_MEM_CHUNK_COUNT_MASK) >= required)
            return curr;
        curr = self->free_next[curr];
    }
    return WOORT_MEM_CHUNK_INDEX_NULL;
}

WOORT_NODISCARD static  /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_allocate_pages(
    woort_mem_Chunk* self, uint32_t required_pages)
{
    woort_rwspinlock_write_lock(&self->rwlock);
    {
        const uint32_t idx = woort_mem_chunk_free_list_find_block(self, required_pages);
        if (idx == WOORT_MEM_CHUNK_INDEX_NULL)
        {
            woort_rwspinlock_write_unlock(&self->rwlock);
            return NULL;
        }

        for (uint32_t j = 0; j < required_pages; ++j)
        {
            if (!woort_mem_chunk_commit_page(self, idx + j))
            {
                woort_rwspinlock_write_unlock(&self->rwlock);
                return NULL;
            }
        }

        const uint32_t block_count = self->count_arr[idx]
            & WOORT_MEM_CHUNK_COUNT_MASK;

        woort_mem_chunk_free_list_remove(self, idx);

        if (block_count > required_pages)
        {
            const uint32_t left_idx = idx + required_pages;
            const uint32_t left_count = block_count - required_pages;

            self->count_arr[left_idx] = left_count;
            for (uint32_t j = 1; j < left_count; ++j)
                self->count_arr[left_idx + j] = 0;

            woort_mem_chunk_free_list_insert(self, left_idx, left_count);
        }

        self->count_arr[idx] = required_pages
            | WOORT_MEM_CHUNK_ALLOCATED_FLAG;
        for (uint32_t j = 1; j < required_pages; ++j)
            self->count_arr[idx + j] = 0;

        woort_mem_PageHead* const page =
            woort_mem_chunk_index_to_page(self, idx);
        woort_atomic_store_explicit(
            &page->m_page_just_allocated,
            true,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

        woort_rwspinlock_write_unlock(&self->rwlock);
        return page;
    }
}

WOORT_NODISCARD bool woort_mem_chunk_init(woort_mem_Chunk* self, size_t reserved_size)
{
    self->base = NULL;
    self->reserved_size = 0;
    self->total_pages = 0;

    self->count_arr = NULL;
    self->commit_arr = NULL;

    self->free_prev = NULL;
    self->free_next = NULL;
    self->free_head = WOORT_MEM_CHUNK_INDEX_NULL;

    woort_rwspinlock_init(&self->rwlock);

    if (reserved_size == 0)
        return true;

    self->total_pages =
        (reserved_size + WOORT_MEM_NORMAL_PAGE_SIZE - 1)
        / WOORT_MEM_NORMAL_PAGE_SIZE;
    self->reserved_size = self->total_pages * WOORT_MEM_NORMAL_PAGE_SIZE;

    self->base = woort_mem_os_reserve_memory(self->reserved_size);
    if (!self->base)
        return false;

    const size_t u32_bytes = self->total_pages * sizeof(uint32_t);
    const size_t u8_bytes  = self->total_pages * sizeof(uint8_t);
    const size_t meta_size = u32_bytes * 3 + u8_bytes;

    uint8_t* const meta = (uint8_t*)malloc(meta_size);
    if (!meta)
        return false;

    self->count_arr  = (uint32_t*)(meta);
    self->free_prev  = (uint32_t*)(meta + u32_bytes);
    self->free_next  = (uint32_t*)(meta + u32_bytes * 2);
    self->commit_arr = (uint8_t*)(meta + u32_bytes * 3);

    memset(self->count_arr, 0, u32_bytes);
    memset(self->commit_arr, 0, u8_bytes);

    for (size_t i = 0; i < self->total_pages; ++i)
    {
        self->free_prev[i] = WOORT_MEM_CHUNK_INDEX_NULL;
        self->free_next[i] = WOORT_MEM_CHUNK_INDEX_NULL;
    }

    self->count_arr[0] = (uint32_t)self->total_pages;
    self->free_head = 0;

    return true;
}

void woort_mem_chunk_deinit(woort_mem_Chunk* self)
{
    free(self->count_arr);
    if (self->base)
    {
        if (woort_mem_os_release_memory(self->base, self->reserved_size) != 0)
        {
            WOORT_DEBUG("woort_mem_os_release_memory failed");
            abort();
        }
    }
    woort_rwspinlock_deinit(&self->rwlock);

    self->count_arr = NULL;
    self->commit_arr = NULL;
    self->free_prev = NULL;
    self->free_next = NULL;

    self->base = NULL;
}

WOORT_NODISCARD bool woort_mem_chunk_is_init_failed(const woort_mem_Chunk* self)
{
    return self->base == NULL && self->total_pages == 0;
}

WOORT_NODISCARD  /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_allocate_page(woort_mem_Chunk* self)
{
    woort_mem_PageHead* const page = woort_mem_chunk_allocate_pages(self, 1);
    if (page != NULL)
        page->m_page_count_if_huge = 0;
    return page;
}

WOORT_NODISCARD  /* OPTIONAL */ woort_mem_PageHead* woort_mem_chunk_allocate_huge_page(
    woort_mem_Chunk* self, size_t size)
{
    assert(self->base != NULL && size != 0);

    const size_t required_pages =
        (size + WOORT_MEM_NORMAL_PAGE_SIZE - 1)
        / WOORT_MEM_NORMAL_PAGE_SIZE;

    if (required_pages > self->total_pages)
        return NULL;

    woort_mem_PageHead* const page =
        woort_mem_chunk_allocate_pages(self, (uint32_t)required_pages);
    if (page != NULL)
        page->m_page_count_if_huge = required_pages;
    return page;
}

void woort_mem_chunk_free_page(
    woort_mem_Chunk* self, woort_mem_PageHead* page)
{
    assert(self->base != NULL
        && page != NULL
        && woort_mem_chunk_validate(self, page) == page);

    woort_rwspinlock_write_lock(&self->rwlock);
    {
        const size_t idx = woort_mem_chunk_page_to_index(self, page);
        const uint32_t c = self->count_arr[idx];

        if (!(c & WOORT_MEM_CHUNK_ALLOCATED_FLAG))
        {
            woort_rwspinlock_write_unlock(&self->rwlock);
            return;
        }

        const uint32_t block_count = c & WOORT_MEM_CHUNK_COUNT_MASK;

        for (uint32_t j = 0; j < block_count; ++j)
            self->count_arr[idx + j] = 0;

        self->count_arr[idx] = block_count;

        woort_mem_chunk_free_list_insert(self, (uint32_t)idx, block_count);
    }
    woort_rwspinlock_write_unlock(&self->rwlock);
}

WOORT_NODISCARD woort_mem_PageHead* woort_mem_chunk_validate(
    woort_mem_Chunk* self, void* ptr)
{
    assert(self->base != NULL);

    const uintptr_t addr = (uintptr_t)ptr;
    const uintptr_t base_addr = (uintptr_t)self->base;

    if (addr < base_addr || addr >= base_addr + self->reserved_size)
        return NULL;

    size_t idx = (addr - base_addr) / WOORT_MEM_NORMAL_PAGE_SIZE;

    woort_rwspinlock_read_lock(&self->rwlock);
    {
        size_t head_idx = idx;
        while (self->count_arr[head_idx] == 0)
        {
            if (head_idx == 0)
            {
                woort_rwspinlock_read_unlock(&self->rwlock);
                return NULL;
            }
            --head_idx;
        }

        uint32_t c = self->count_arr[head_idx];

        if ((c & WOORT_MEM_CHUNK_ALLOCATED_FLAG) == 0)
        {
            woort_rwspinlock_read_unlock(&self->rwlock);
            return NULL;
        }

        uint32_t block_count = c & WOORT_MEM_CHUNK_COUNT_MASK;
        if (idx >= head_idx + block_count)
        {
            woort_rwspinlock_read_unlock(&self->rwlock);
            return NULL;
        }

        woort_mem_PageHead* result = woort_mem_chunk_index_to_page(self, head_idx);
        woort_rwspinlock_read_unlock(&self->rwlock);
        return result;
    }
}

WOORT_NODISCARD size_t woort_mem_chunk_get_total_size(const woort_mem_Chunk* self)
{
    return self->reserved_size;
}

WOORT_NODISCARD size_t woort_mem_chunk_get_total_page_count(const woort_mem_Chunk* self)
{
    return self->total_pages;
}
