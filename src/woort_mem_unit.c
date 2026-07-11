/*
woort_mem_unit.c
*/

#include "woort_mem_unit.h"
#include "woort_mem_global_context.h"

#include <assert.h>

void woort_mem_init_page_for_unit_allocating(
    woort_mem_PageHead* page, woort_mem_UnitAllocGroup group_type)
{
    woort_mem_PageUnitAlloc* const page_alloc_head =
        (woort_mem_PageUnitAlloc*)(page + 1);

    woort_atomic_store_explicit(
        &page_alloc_head->m_run_out,
        0,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    page_alloc_head->m_mark_as_run_out_in_global_pool = false;
    woort_atomic_store_explicit(
        &page_alloc_head->m_freed_unit_offset,
        0,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    page_alloc_head->m_next_allocate_unit_offset =
        (uint16_t)sizeof(woort_mem_PageUnitAlloc);
    page_alloc_head->m_unit_size_in_page = (uint16_t)
        WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[group_type];

    const size_t group_unit_size_include_unit_head =
        sizeof(woort_mem_UnitHead)
        + page_alloc_head->m_unit_size_in_page;

    const size_t available_for_units =
        WOORT_MEM_NORMAL_PAGE_SIZE
        - (sizeof(woort_mem_PageHead) + sizeof(woort_mem_PageUnitAlloc));
    const size_t unit_count =
        available_for_units / group_unit_size_include_unit_head;

    size_t offset = sizeof(woort_mem_PageUnitAlloc);
    for (size_t i = 0; i < unit_count; i++)
    {
        woort_mem_UnitHead* const current_unit =
            (woort_mem_UnitHead*)(
                (char*)page_alloc_head + offset);

        offset += group_unit_size_include_unit_head;

        current_unit->m_next_free_unit_offset =
            (i + 1 < unit_count) ? (uint16_t)offset : 0;
        current_unit->m_age = 0;
        current_unit->m_timing = 0;
        current_unit->m_attribute = 0;
        woort_atomic_store_explicit(
            &current_unit->m_life,
            WOORT_MEM_UNIT_LIFE_RELEASED,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }

    /*
     * NOTE: No need for fence. A newly allocated page will be used for
     * the current thread. If it is dropped back to the global list,
     * there will be a release/acquire order.
     */
    woort_mem_global_context_add_new_page_into_chain(page);
}
