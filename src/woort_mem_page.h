#pragma once

/*
woort_mem_page.h
Page header layout for the GC memory allocator.
*/

#include "woort_atomic.h"

#include <stdint.h>
#include <stddef.h>

#define WOORT_MEM_NORMAL_PAGE_SIZE ((size_t)32768)

typedef struct woort_mem_PageHead
{
    _Alignas(8) size_t                     m_page_count_if_huge;
    _Alignas(8) struct woort_mem_PageHead* m_next_page;
    _Alignas(8) woort_AtomicUInt8          m_page_just_allocated;

} woort_mem_PageHead;

_Static_assert(sizeof(woort_mem_PageHead) == 24, "PageHead must be 24 bytes");
