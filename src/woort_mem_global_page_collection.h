#pragma once

/*
woort_mem_global_page_collection.h
Per-size-class free page pool shared by all threads.
*/

#include "woort_mem_chunk.h"
#include "woort_mem_page.h"
#include "woort_mem_unit.h"
#include "woort_spin.h"
#include "woort_vector.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct woort_mem_FreePageList
{
    woort_Spinlock  m_spin;
    woort_Vector    m_pages;

} woort_mem_FreePageList;

typedef struct woort_mem_GlobalPageCollection
{
    woort_mem_Chunk*       m_chunk;
    woort_mem_FreePageList m_free_pages[WOORT_MEM_MAX_GROUP];

} woort_mem_GlobalPageCollection;

void woort_mem_gpc_init(
    woort_mem_GlobalPageCollection* self, woort_mem_Chunk* chunk);
void woort_mem_gpc_deinit(woort_mem_GlobalPageCollection* self);

static inline void woort_mem_free_page_list_init(
    woort_mem_FreePageList* self)
{
    woort_spinlock_init(&self->m_spin);
    woort_vector_init(&self->m_pages, sizeof(void*));
}

static inline void woort_mem_free_page_list_deinit(
    woort_mem_FreePageList* self)
{
    woort_vector_deinit(&self->m_pages);
    woort_spinlock_deinit(&self->m_spin);
}

static inline woort_mem_PageHead* woort_mem_free_page_list_pick(
    woort_mem_FreePageList* self)
{
    woort_spinlock_lock(&self->m_spin);
    {
        woort_mem_PageHead* page = NULL;
        while (self->m_pages.m_size > 0)
        {
            page = *(woort_mem_PageHead**)
                woort_vector_at(&self->m_pages, self->m_pages.m_size - 1);
            woort_vector_erase_at(&self->m_pages, self->m_pages.m_size - 1);

            if (page == NULL)
                continue;

            woort_mem_PageUnitAlloc* const page_alloc_head =
                (woort_mem_PageUnitAlloc*)(page + 1);

            if (page_alloc_head->m_mark_as_run_out_in_global_pool)
            {
                page_alloc_head->m_mark_as_run_out_in_global_pool = false;
                woort_atomic_store_explicit(
                    &page_alloc_head->m_run_out,
                    1,
                    WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

                page = NULL;
                continue;
            }
            break;
        }

        woort_spinlock_unlock(&self->m_spin);
        return page;
    }
}

static inline void woort_mem_free_page_list_return(
    woort_mem_FreePageList* self, woort_mem_PageHead* page)
{
    woort_spinlock_lock(&self->m_spin);
    {
        woort_vector_push_back(&self->m_pages, 1, &page);
    }
    woort_spinlock_unlock(&self->m_spin);
}

static inline woort_mem_PageHead* woort_mem_gpc_require_normal_page(
    woort_mem_GlobalPageCollection* self, woort_mem_UnitAllocGroup group)
{
    woort_mem_PageHead* page =
        woort_mem_free_page_list_pick(&self->m_free_pages[group]);
    if (page != NULL)
    {
        assert(page->m_page_count_if_huge == 0
            && woort_atomic_load_explicit(
                &((woort_mem_PageUnitAlloc*)(page + 1))->m_run_out,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED) == false
            && ((woort_mem_PageUnitAlloc*)(page + 1))->m_unit_size_in_page
                == WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[group]);

        return page;
    }

    page = woort_mem_chunk_allocate_page(self->m_chunk);
    if (page != NULL)
    {
        woort_mem_init_page_for_unit_allocating(page, group);
    }
    return page;
}

static inline void woort_mem_gpc_return_page(
    woort_mem_GlobalPageCollection* self,
    woort_mem_PageHead* page, woort_mem_UnitAllocGroup group)
{
    woort_mem_free_page_list_return(&self->m_free_pages[group], page);
}

static inline void woort_mem_gpc_remove_marked_run_out_pages(
    woort_mem_GlobalPageCollection* self)
{
    for (size_t group = 0; group < WOORT_MEM_MAX_GROUP; ++group)
    {
        woort_mem_FreePageList* free_list = &self->m_free_pages[group];

        woort_spinlock_lock(&free_list->m_spin);
        {
            for (size_t i = 0; i < free_list->m_pages.m_size; ++i)
            {
                woort_mem_PageHead* page =
                    *(woort_mem_PageHead**)
                        woort_vector_at(&free_list->m_pages, i);

                if (page != NULL)
                {
                    woort_mem_PageUnitAlloc* alloc_head =
                        (woort_mem_PageUnitAlloc*)(page + 1);

                    if (alloc_head->m_mark_as_run_out_in_global_pool)
                    {
                        alloc_head->m_mark_as_run_out_in_global_pool = false;
                        woort_atomic_store_explicit(
                            &alloc_head->m_run_out,
                            1,
                            WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

                        page = NULL;
                        *(woort_mem_PageHead**)
                            woort_vector_at(&free_list->m_pages, i) = page;
                    }
                }
            }
        }
        woort_spinlock_unlock(&free_list->m_spin);
    }
}
