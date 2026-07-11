#pragma once

/*
woort_mem_thread_page_collection.h
Per-thread cached pages for fast small-unit allocation.
*/

#include "woort_mem_chunk.h"
#include "woort_mem_page.h"
#include "woort_mem_unit.h"
#include "woort_mem_global_page_collection.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

typedef struct woort_mem_ThreadPageCollection
{
    /* OPTIONAL */ woort_mem_GlobalPageCollection* m_global_page_collection;
    woort_mem_PageHead* m_cached_pages[WOORT_MEM_MAX_GROUP];

} woort_mem_ThreadPageCollection;

static inline void woort_mem_tpc_init(
    woort_mem_ThreadPageCollection* self,
    /* OPTIONAL */ woort_mem_GlobalPageCollection* global_page_collection)
{
    self->m_global_page_collection = global_page_collection;
    for (size_t i = 0; i < WOORT_MEM_MAX_GROUP; ++i)
        self->m_cached_pages[i] = NULL;
}

static inline void woort_mem_tpc_init_manually(
    woort_mem_ThreadPageCollection* self,
    woort_mem_GlobalPageCollection* global_page_collection)
{
    assert(self->m_global_page_collection == NULL);
    self->m_global_page_collection = global_page_collection;
}

static inline void woort_mem_tpc_shutdown_manually(
    woort_mem_ThreadPageCollection* self)
{
    if (self->m_global_page_collection != NULL)
    {
        for (size_t i = 0; i < WOORT_MEM_MAX_GROUP; ++i)
        {
            woort_mem_PageHead* page = self->m_cached_pages[i];
            if (page != NULL)
                woort_mem_gpc_return_page(
                    self->m_global_page_collection,
                    page,
                    (woort_mem_UnitAllocGroup)i);
        }
        self->m_global_page_collection = NULL;
    }
}

static inline void* woort_mem_tpc_pick_unit_in_page(
    woort_mem_ThreadPageCollection* self, size_t unit_size)
{
    assert(self->m_global_page_collection != NULL
        && unit_size <= WOORT_MEM_MAX_IN_PAGE_UNIT_SIZE);

    const woort_mem_UnitAllocGroup belong_group =
        woort_mem_eval_group_by_small_unit_size(unit_size);

    woort_mem_PageHead** cached_page =
        &self->m_cached_pages[belong_group];
    if (*cached_page != NULL)
    {
    _label_retry_allocate_with_new_page:
        {
            woort_mem_UnitHead* const unit =
                woort_mem_pick_unit_from_page_without_init(*cached_page);
            if (unit != NULL)
                return unit + 1;
        }
    }

    *cached_page =
        woort_mem_gpc_require_normal_page(
            self->m_global_page_collection, belong_group);
    if (*cached_page == NULL)
        return NULL;

    goto _label_retry_allocate_with_new_page;
}
