#pragma once

/*
woort_mem_unit.h
Unit allocation groups, page-unit allocator header, unit header,
and inline allocation helpers.
*/

#include "woort_mem_page.h"
#include "woort_atomic.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

typedef enum woort_mem_UnitAllocGroup
{
    WOORT_MEM_SMALL_16,
    WOORT_MEM_SMALL_40,
    WOORT_MEM_SMALL_88,
    WOORT_MEM_SMALL_168,
    WOORT_MEM_SMALL_344,
    WOORT_MEM_SMALL_520,
    WOORT_MEM_SMALL_736,
    WOORT_MEM_SMALL_1048,
    WOORT_MEM_MAX_SMALL_GROUP = WOORT_MEM_SMALL_1048,

    WOORT_MEM_MEDIUM_2720,
    WOORT_MEM_MEDIUM_5448,
    WOORT_MEM_MEDIUM_8176,
    WOORT_MEM_MEDIUM_10904,
    WOORT_MEM_MEDIUM_16360,

    WOORT_MEM_MAX_GROUP

} woort_mem_UnitAllocGroup;

#define WOORT_MEM_MAX_SMALL_UNIT_SIZE   ((size_t)1048)
#define WOORT_MEM_MAX_IN_PAGE_UNIT_SIZE ((size_t)16360)

static const size_t WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[WOORT_MEM_MAX_GROUP] = {
    16, 40, 88, 168, 344, 520, 736, 1048, 2720, 5448, 8176, 10904, 16360
};
_Static_assert(
    WOORT_MEM_MEDIUM_16360 + 1 == WOORT_MEM_MAX_GROUP,
    "medium_16360 must be the last group");
_Static_assert(
    16360u == WOORT_MEM_MAX_IN_PAGE_UNIT_SIZE,
    "medium 16360 group size mismatch");

static const woort_mem_UnitAllocGroup
WOORT_MEM_SMALL_UNIT_GROUP_FAST_LOOKUP_TABLE[] = {
    // 0
    WOORT_MEM_SMALL_16,
    // 1~16(2)
    WOORT_MEM_SMALL_16, WOORT_MEM_SMALL_16,
    // 17~40(5)
    WOORT_MEM_SMALL_40, WOORT_MEM_SMALL_40, WOORT_MEM_SMALL_40,
    // 41~88(11)
    WOORT_MEM_SMALL_88, WOORT_MEM_SMALL_88, WOORT_MEM_SMALL_88, WOORT_MEM_SMALL_88, WOORT_MEM_SMALL_88, WOORT_MEM_SMALL_88,
    // 89~168(21)
    WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168,
    WOORT_MEM_SMALL_168, WOORT_MEM_SMALL_168,
    // 169~344(43)
    WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344,
    WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344,
    WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344, WOORT_MEM_SMALL_344,
    // 345~520(65)
    WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520,
    WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520,
    WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520, WOORT_MEM_SMALL_520,
    // 521~736(92)
    WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736,
    WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736,
    WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736,
    WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736, WOORT_MEM_SMALL_736,
    // 737~1048(131)
    WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048,
    WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048,
    WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048,
    WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048,
    WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048, WOORT_MEM_SMALL_1048,
};

#define WOORT_MEM_FAST_LOOKUP_GROUP_INDEX(SIZE) (((SIZE) + 7) >> 3)

_Static_assert(
    sizeof(WOORT_MEM_SMALL_UNIT_GROUP_FAST_LOOKUP_TABLE)
        / sizeof(woort_mem_UnitAllocGroup)
    == WOORT_MEM_FAST_LOOKUP_GROUP_INDEX(WOORT_MEM_MAX_SMALL_UNIT_SIZE) + 1,
    "fast lookup table size mismatch");

/*
 * Page layout: [PageHead(24)] [PageUnitAlloc(8)] [[UnitHead(8)][payload(x)]]...
 * Available per page: 32768 - 24 - 8 = 32736 bytes.
 */

typedef struct woort_mem_PageUnitAlloc
{
    uint16_t            m_next_allocate_unit_offset;
    woort_AtomicUInt16  m_freed_unit_offset;
    woort_AtomicUInt8   m_run_out;
    bool                m_mark_as_run_out_in_global_pool;
    uint16_t            m_unit_size_in_page;

} woort_mem_PageUnitAlloc;

_Static_assert(sizeof(woort_mem_PageUnitAlloc) == 8,
    "PageUnitAlloc must be 8 bytes");

typedef enum woort_mem_UnitLife
{
    WOORT_MEM_UNIT_LIFE_RELEASED,
    WOORT_MEM_UNIT_LIFE_PENDING,
    WOORT_MEM_UNIT_LIFE_UNMARKED,
    WOORT_MEM_UNIT_LIFE_SELF_MARKED,
    WOORT_MEM_UNIT_LIFE_FULL_MARKED,

} woort_mem_UnitLife;

typedef struct woort_mem_UnitHead
{
    uint16_t        m_next_free_unit_offset;
    char            __reserved__[2];
    uint8_t         m_age;
    uint8_t         m_timing;
    uint8_t         m_attribute;
    woort_AtomicUInt8 m_life;

} woort_mem_UnitHead;

_Static_assert(sizeof(woort_mem_UnitHead) == 8,
    "UnitHead must be 8 bytes");

void woort_mem_init_page_for_unit_allocating(
    woort_mem_PageHead* page, woort_mem_UnitAllocGroup group_type);

static inline woort_mem_UnitAllocGroup
woort_mem_eval_group_by_small_unit_size(size_t unit_size)
{
    if (unit_size <= WOORT_MEM_MAX_SMALL_UNIT_SIZE)
    {
        return WOORT_MEM_SMALL_UNIT_GROUP_FAST_LOOKUP_TABLE[
            WOORT_MEM_FAST_LOOKUP_GROUP_INDEX(unit_size)];
    }
    else
    {
        if (unit_size
            <= WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[WOORT_MEM_MEDIUM_2720])
            return WOORT_MEM_MEDIUM_2720;
        else if (unit_size
            <= WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[WOORT_MEM_MEDIUM_5448])
            return WOORT_MEM_MEDIUM_5448;
        else if (unit_size
            <= WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[WOORT_MEM_MEDIUM_8176])
            return WOORT_MEM_MEDIUM_8176;
        else if (unit_size
            <= WOORT_MEM_GROUP_SIZE_LOOKUP_TABLE[WOORT_MEM_MEDIUM_10904])
            return WOORT_MEM_MEDIUM_10904;
        else
            return WOORT_MEM_MEDIUM_16360;
    }
}

static inline size_t
woort_mem_unit_get_available_size(const woort_mem_UnitHead* self)
{
    if (self->m_next_free_unit_offset != 0)
    {
        const woort_mem_PageUnitAlloc* const unit_alloc_page =
            (const woort_mem_PageUnitAlloc*)(
                (const char*)self - self->m_next_free_unit_offset);

        return unit_alloc_page->m_unit_size_in_page;
    }
    else
    {
        const woort_mem_PageHead* const huge_page =
            (const woort_mem_PageHead*)self - 1;

        assert(huge_page->m_page_count_if_huge != 0);
        return huge_page->m_page_count_if_huge * WOORT_MEM_NORMAL_PAGE_SIZE
            - (sizeof(woort_mem_PageHead) + sizeof(woort_mem_UnitHead));
    }
}

static inline woort_mem_UnitHead*
woort_mem_pick_unit_from_page_without_init(woort_mem_PageHead* page)
{
    const uint16_t UNIT_PAGE_HEAD_SIZE = (uint16_t)(
        sizeof(woort_mem_PageHead) + sizeof(woort_mem_PageUnitAlloc));

    (void)UNIT_PAGE_HEAD_SIZE;

    woort_mem_PageUnitAlloc* const page_alloc_head =
        (woort_mem_PageUnitAlloc*)(page + 1);

    uint16_t current_offset = page_alloc_head->m_next_allocate_unit_offset;
    do
    {
        if (current_offset != 0)
        {
            woort_mem_UnitHead* const allocating_unit =
                (woort_mem_UnitHead*)(
                    (char*)page_alloc_head + current_offset);

            page_alloc_head->m_next_allocate_unit_offset =
                allocating_unit->m_next_free_unit_offset;
            allocating_unit->m_next_free_unit_offset =
                current_offset;

            assert(WOORT_MEM_UNIT_LIFE_RELEASED
                == woort_atomic_load_explicit(
                    &allocating_unit->m_life,
                    WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

            woort_atomic_store_explicit(
                &allocating_unit->m_life,
                WOORT_MEM_UNIT_LIFE_PENDING,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            return allocating_unit;
        }

        current_offset = (uint16_t)woort_atomic_exchange_explicit(
            &page_alloc_head->m_freed_unit_offset,
            0,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        if (current_offset == 0)
        {
            woort_atomic_store_explicit(
                &page_alloc_head->m_run_out,
                1,
                WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

            return NULL;
        }

        page_alloc_head->m_next_allocate_unit_offset = current_offset;

    } while (1);
}

static inline void
woort_mem_drop_freed_unit_into_page(
    woort_mem_PageHead* page, woort_mem_UnitHead* unit)
{
    woort_mem_PageUnitAlloc* const page_alloc_head =
        (woort_mem_PageUnitAlloc*)(page + 1);

    const uint16_t unit_offset = (uint16_t)(
        (char*)unit - (char*)page_alloc_head);

    assert(WOORT_MEM_UNIT_LIFE_RELEASED
        == woort_atomic_load_explicit(
            &unit->m_life, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    unit->m_next_free_unit_offset = (uint16_t)woort_atomic_load_explicit(
        &page_alloc_head->m_freed_unit_offset,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    while (!woort_atomic_compare_exchange_weak_explicit(
        &page_alloc_head->m_freed_unit_offset,
        &unit->m_next_free_unit_offset,
        unit_offset,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
        /* Atomic retry */;
}
