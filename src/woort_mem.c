/*
woort_mem.c
Public API implementations (formerly woomem.cpp).
*/

#include "woort_mem.h"
#include "woort_mem_global_context.h"
#include "woort_mem_thread_context.h"
#include "woort_mem_gc.h"
#include "woort_mem_unit.h"
#include "woort_mem_page.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

bool woort_mem_init(
    size_t reserved_chunk_size,
    woort_mem_GCCallback gc_callback_at_begin,
    woort_mem_GCCallback gc_callback_at_stop_marking,
    woort_mem_MarkCallback mark_callback,
    woort_mem_FreeCallback free_callback,
    woort_mem_GCMainThreadEntryCallback main_entry_callback,
    woort_mem_GCWorkerThreadEntryCallback worker_entry_callback)
{
    assert(!g_woort_mem_global_context.m_globalcontext_inited
        && g_woort_mem_gc == NULL);

    if (woort_mem_global_context_init(reserved_chunk_size))
    {
        g_woort_mem_gc = woort_mem_gc_create(
            0,
            gc_callback_at_begin,
            gc_callback_at_stop_marking,
            mark_callback,
            free_callback,
            main_entry_callback,
            worker_entry_callback);
        if (g_woort_mem_gc != NULL)
            return true;

        woort_mem_global_context_shutdown();
    }
    return false;
}

void woort_mem_shutdown(void)
{
    assert(g_woort_mem_gc != NULL);

    woort_mem_gc_destroy(g_woort_mem_gc);
    g_woort_mem_gc = NULL;

    woort_mem_global_context_shutdown();
    woort_mem_global_context_deinit();
}

/* ====================================================================== */

void woort_mem_trigger_gc(bool async)
{
    assert(g_woort_mem_gc != NULL);
    woort_mem_gc_trigger_gc(g_woort_mem_gc, async);
}

/* OPTIONAL */ void* woort_mem_allocate_begin(size_t size)
{
    assert(g_woort_mem_gc != NULL);

    woort_atomic_fetch_add_explicit(
        &g_woort_mem_gc->m_new_allocated_size_since_last_gc,
        size,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    if (size <= WOORT_MEM_MAX_IN_PAGE_UNIT_SIZE)
    {
        return woort_mem_tpc_pick_unit_in_page(
            &woort_mem_get_thread_context()->m_thread_page_collection,
            size);
    }

    woort_mem_PageHead* const huge_unit_page =
        woort_mem_global_context_allocate_huge_page(
            sizeof(woort_mem_PageHead)
            + sizeof(woort_mem_UnitHead)
            + size);

    if (huge_unit_page == NULL)
        return NULL;

    woort_mem_UnitHead* const huge_unit_head =
        (woort_mem_UnitHead*)(huge_unit_page + 1);

    huge_unit_head->m_next_free_unit_offset = 0;
    woort_atomic_store_explicit(
        &huge_unit_head->m_life,
        WOORT_MEM_UNIT_LIFE_PENDING,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    woort_mem_global_context_add_new_page_into_chain(huge_unit_page);

    return huge_unit_head + 1;
}

void woort_mem_allocate_end(void* p, int attrib)
{
    woort_mem_UnitHead* const unit_head =
        (woort_mem_UnitHead*)p - 1;

    unit_head->m_age = 15;
    unit_head->m_timing = woort_mem_gc_marking_round_counter;
    unit_head->m_attribute = (uint8_t)attrib;
    woort_atomic_store_explicit(
        &unit_head->m_life,
        WOORT_MEM_UNIT_LIFE_UNMARKED,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}

void woort_mem_allocate_end_as_root(void* p, int attrib)
{
    woort_mem_UnitHead* const unit_head =
        (woort_mem_UnitHead*)p - 1;

    woort_mem_gc_register_root_unit_head(g_woort_mem_gc, unit_head);
    woort_mem_allocate_end(p, attrib);
}

void woort_mem_remove_from_root_set(void* p)
{
    woort_mem_UnitHead* const unit_head =
        (woort_mem_UnitHead*)p - 1;

    woort_mem_gc_unregister_root_unit_head(g_woort_mem_gc, unit_head);
}

/* OPTIONAL */ void* woort_mem_reallocate(void* ptr, size_t size)
{
    assert(ptr != NULL);

    woort_mem_UnitHead* const unit_head =
        (woort_mem_UnitHead*)ptr - 1;

    const size_t existed_unit_available_space =
        woort_mem_unit_get_available_size(unit_head);

    if (size <= existed_unit_available_space)
        return ptr;

    void* new_ptr = woort_mem_allocate_begin(size);
    if (new_ptr == NULL)
        return NULL;

    memcpy(new_ptr, ptr, existed_unit_available_space);

    woort_mem_allocate_end(new_ptr, unit_head->m_attribute);

    return new_ptr;
}

/* OPTIONAL */ void* woort_mem_validate_addr(/* OPTIONAL */ void* ptr_may_invalid)
{
    if (ptr_may_invalid == NULL)
        return NULL;

    woort_mem_PageHead* const page_head =
        woort_mem_chunk_validate(
            &g_woort_mem_global_context.m_chunk, ptr_may_invalid);

    if (page_head != NULL
        && !woort_atomic_load_explicit(
            &page_head->m_page_just_allocated,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
    {
        woort_mem_UnitHead* unit_head;

        if (page_head->m_page_count_if_huge == 0)
        {
            woort_mem_PageUnitAlloc* const page_alloc_head =
                (woort_mem_PageUnitAlloc*)(page_head + 1);

            const size_t unit_size_with_head =
                page_alloc_head->m_unit_size_in_page
                + sizeof(woort_mem_UnitHead);

            const uintptr_t addr = (uintptr_t)ptr_may_invalid;
            const uintptr_t storage_begin =
                (uintptr_t)(page_alloc_head + 1);

            if (addr < storage_begin)
                return NULL;

            const size_t offset_in_units = addr - storage_begin;
            const size_t unit_index =
                offset_in_units / unit_size_with_head;

            unit_head = (woort_mem_UnitHead*)(
                storage_begin + unit_index * unit_size_with_head);
        }
        else
        {
            unit_head = (woort_mem_UnitHead*)(page_head + 1);
        }

        if (woort_atomic_load_explicit(
            &unit_head->m_life,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED)
            > WOORT_MEM_UNIT_LIFE_PENDING)
            return unit_head + 1;
    }

    return NULL;
}

/* OPTIONAL */ void* woort_mem_validate_addr_head(
    /* OPTIONAL */ void* ptr_may_invalid)
{
    if (((intptr_t)ptr_may_invalid & 0b0111) == 0)
    {
        return woort_mem_validate_addr(ptr_may_invalid);
    }
    return NULL;
}

size_t woort_mem_get_capacity_of_addr_head(void* ptr)
{
    return woort_mem_unit_get_available_size((woort_mem_UnitHead*)ptr - 1);
}

void woort_mem_mark_unit_head(/* OPTIONAL */ void* ptr_head_may_null)
{
    if (ptr_head_may_null != NULL)
    {
        woort_mem_GCWorker* const worker =
            woort_mem_get_thread_context()->m_gc_marking_context;
        if (worker != NULL)
            woort_mem_gcworker_mark_unit_to_gray(
                worker,
                (woort_mem_UnitHead*)ptr_head_may_null - 1);
    }
}

void woort_mem_mark_fuzzy_unit(/* OPTIONAL */ void* ptr_may_invalid_or_null)
{
    woort_mem_mark_unit_head(
        woort_mem_validate_addr(ptr_may_invalid_or_null));
}

void woort_mem_mark_fuzzy_unit_head(/* OPTIONAL */ void* ptr_head_may_invalid_null)
{
    woort_mem_mark_unit_head(
        woort_mem_validate_addr_head(ptr_head_may_invalid_null));
}

void woort_mem_mark_root_unit_head(/* OPTIONAL */ void* ptr_head_may_null)
{
    if (ptr_head_may_null != NULL)
    {
        woort_mem_gc_mark_root_unit_to_gray(
            g_woort_mem_gc,
            (woort_mem_UnitHead*)ptr_head_may_null - 1);
    }
}

void woort_mem_mark_root_fuzzy_unit(/* OPTIONAL */ void* ptr_may_invalid_or_null)
{
    woort_mem_mark_root_unit_head(
        woort_mem_validate_addr(ptr_may_invalid_or_null));
}

void woort_mem_mark_root_fuzzy_unit_head(
    /* OPTIONAL */ void* ptr_head_may_invalid_null)
{
    woort_mem_mark_root_unit_head(
        woort_mem_validate_addr_head(ptr_head_may_invalid_null));
}
