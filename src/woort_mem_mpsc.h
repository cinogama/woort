#pragma once

/*
woort_mem_mpsc.h
Lock-free bounded MPSC (multi-producer, single-consumer) ring buffer
for gray GC unit queue. Sequence-based; capacity must be power of 2.
*/

#include "woort_mem_unit.h"
#include "woort_atomic.h"

#include <stddef.h>
#include <stdbool.h>

#define WOORT_MEM_GRAY_QUEUE_CAPACITY ((size_t)8192)
#define WOORT_MEM_GRAY_QUEUE_MASK     (WOORT_MEM_GRAY_QUEUE_CAPACITY - 1)

_Static_assert(
    WOORT_MEM_GRAY_QUEUE_CAPACITY > 0
        && (WOORT_MEM_GRAY_QUEUE_CAPACITY
            & (WOORT_MEM_GRAY_QUEUE_CAPACITY - 1)) == 0,
    "Capacity must be power of 2");

typedef struct woort_mem_MpscSlot
{
    woort_AtomicUInt64      sequence;
    woort_mem_UnitHead*     item;

} woort_mem_MpscSlot;

typedef struct woort_mem_MpscGrayQueue
{
    _Alignas(64) woort_mem_MpscSlot m_slots[WOORT_MEM_GRAY_QUEUE_CAPACITY];

    _Alignas(64) woort_AtomicUInt64 m_enqueue_pos;
    _Alignas(64) woort_AtomicUInt64 m_dequeue_pos;

} woort_mem_MpscGrayQueue;

static inline void woort_mem_mpsc_init(woort_mem_MpscGrayQueue* self)
{
    for (size_t i = 0; i < WOORT_MEM_GRAY_QUEUE_CAPACITY; ++i)
        woort_atomic_init(&self->m_slots[i].sequence, i);
    woort_atomic_init(&self->m_enqueue_pos, 0);
    woort_atomic_init(&self->m_dequeue_pos, 0);
}

WOORT_NODISCARD static inline bool woort_mem_mpsc_try_enqueue(
    woort_mem_MpscGrayQueue* self, woort_mem_UnitHead* item)
{
    uint64_t pos = woort_atomic_load_explicit(
        &self->m_enqueue_pos, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    for (;;)
    {
        woort_mem_MpscSlot* slot = &self->m_slots[pos & WOORT_MEM_GRAY_QUEUE_MASK];

        if (woort_atomic_load_explicit(
                &slot->sequence, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE)
            < pos)
            return false;

        if (woort_atomic_compare_exchange_weak_explicit(
                &self->m_enqueue_pos, &pos, pos + 1,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
        {
            slot->item = item;
            woort_atomic_store_explicit(
                &slot->sequence, pos + 1,
                WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
            return true;
        }
    }
}

WOORT_NODISCARD static inline size_t woort_mem_mpsc_drain(
    woort_mem_MpscGrayQueue* self,
    woort_mem_UnitHead** output, size_t max_count)
{
    size_t count = 0;
    for (; count < max_count; ++count)
    {
        const uint64_t pos = woort_atomic_load_explicit(
            &self->m_dequeue_pos, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

        woort_mem_MpscSlot* slot =
            &self->m_slots[pos & WOORT_MEM_GRAY_QUEUE_MASK];

        if (woort_atomic_load_explicit(
                &slot->sequence, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE)
            != pos + 1)
            break;

        output[count] = slot->item;
        woort_atomic_store_explicit(
            &slot->sequence, pos + WOORT_MEM_GRAY_QUEUE_CAPACITY,
            WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
        woort_atomic_store_explicit(
            &self->m_dequeue_pos, pos + 1,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }
    return count;
}

/* NOTE: 不能用 const 形参——atomic 泛型宏对 const 限定的原子指针没有兼容分支，
   任何未启用 /experimental:c11atomics 的编译单元（如测试）包含本头都会失败。 */
WOORT_NODISCARD static inline bool woort_mem_mpsc_empty(woort_mem_MpscGrayQueue* self)
{
    const uint64_t pos = woort_atomic_load_explicit(
        &self->m_dequeue_pos, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    const woort_mem_MpscSlot* slot =
        &self->m_slots[pos & WOORT_MEM_GRAY_QUEUE_MASK];

    return woort_atomic_load_explicit(
        (woort_AtomicUInt64*)&slot->sequence, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE) != pos + 1;
}
