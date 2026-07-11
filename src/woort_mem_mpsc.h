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
    woort_AtomicSize        sequence;
    woort_mem_UnitHead*     item;

} woort_mem_MpscSlot;

typedef struct woort_mem_MpscGrayQueue
{
    _Alignas(64) woort_mem_MpscSlot m_slots[WOORT_MEM_GRAY_QUEUE_CAPACITY];

    _Alignas(64) woort_AtomicSize   m_enqueue_pos;
    _Alignas(64) woort_AtomicSize   m_dequeue_pos;

} woort_mem_MpscGrayQueue;

static inline void woort_mem_mpsc_init(woort_mem_MpscGrayQueue* self)
{
    for (size_t i = 0; i < WOORT_MEM_GRAY_QUEUE_CAPACITY; ++i)
        woort_atomic_init(&self->m_slots[i].sequence, i);
}

static inline bool woort_mem_mpsc_try_enqueue(
    woort_mem_MpscGrayQueue* self, woort_mem_UnitHead* item)
{
    size_t pos = woort_atomic_load_explicit(
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

static inline size_t woort_mem_mpsc_drain(
    woort_mem_MpscGrayQueue* self,
    woort_mem_UnitHead** output, size_t max_count)
{
    size_t count = 0;
    for (; count < max_count; ++count)
    {
        const size_t pos = woort_atomic_load_explicit(
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

static inline bool woort_mem_mpsc_empty(const woort_mem_MpscGrayQueue* self)
{
    const size_t pos = woort_atomic_load_explicit(
        &self->m_dequeue_pos, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    const woort_mem_MpscSlot* slot =
        &self->m_slots[pos & WOORT_MEM_GRAY_QUEUE_MASK];

    return woort_atomic_load_explicit(
        &slot->sequence, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE) != pos + 1;
}
