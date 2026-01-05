#pragma once

/*
woort_atomic.h
*/

#ifdef __STDC_NO_ATOMICS__

#   define woort_Atomic volatile

typedef enum woort_atomic_MemoryOrder
{
    WOORT_ATOMIC_MEMORY_ORDER_RELAXED,
    WOORT_ATOMIC_MEMORY_ORDER_CONSUME,
    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
    WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
    WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL,
    WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST,

} woort_atomic_MemoryOrder;

#else
#   include <stdatomic.h>

#   define woort_Atomic _Atomic

typedef atomic_flag woort_AtomicFlag;

#   define woort_atomic_flag_test_and_set atomic_flag_test_and_set
#   define woort_atomic_flag_test_and_set_explicit atomic_flag_test_and_set_explicit
#   define woort_atomic_flag_clear atomic_flag_clear
#   define woort_atomic_flag_clear_explicit atomic_flag_clear_explicit

typedef memory_order woort_atomic_MemoryOrder;

#   define WOORT_ATOMIC_MEMORY_ORDER_RELAXED memory_order_relaxed
#   define WOORT_ATOMIC_MEMORY_ORDER_CONSUME memory_order_consume
#   define WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE memory_order_acquire
#   define WOORT_ATOMIC_MEMORY_ORDER_RELEASE memory_order_release
#   define WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL memory_order_acq_rel
#   define WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST memory_order_seq_cst

#   define woort_atomic_init atomic_init
#   define woort_atomic_store atomic_store
#   define woort_atomic_store_explicit atomic_store_explicit
#   define woort_atomic_load atomic_load
#   define woort_atomic_load_explicit atomic_load_explicit
#   define woort_atomic_fetch_add atomic_fetch_add
#   define woort_atomic_fetch_add_explicit atomic_fetch_add_explicit
#   define woort_atomic_fetch_sub atomic_fetch_sub
#   define woort_atomic_fetch_sub_explicit atomic_fetch_sub_explicit
#   define woort_atomic_fetch_or atomic_fetch_or
#   define woort_atomic_fetch_or_explicit atomic_fetch_or_explicit
#   define woort_atomic_fetch_xor atomic_fetch_xor
#   define woort_atomic_fetch_xor_explicit atomic_fetch_xor_explicit
#   define woort_atomic_fetch_and atomic_fetch_and
#   define woort_atomic_fetch_and_explicit atomic_fetch_and_explicit
#   define woort_atomic_compare_exchange_strong atomic_compare_exchange_strong
#   define woort_atomic_compare_exchange_strong_explicit atomic_compare_exchange_strong_explicit
#   define woort_atomic_compare_exchange_weak atomic_compare_exchange_weak
#   define woort_atomic_compare_exchange_weak_explicit atomic_compare_exchange_weak_explicit

#   define woort_atomic_thread_fence atomic_thread_fence
#   define woort_atomic_signal_fence atomic_signal_fence

#endif
