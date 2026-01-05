#include "woort_spin.h"
#include "woort_atomic.h"
#include "woort_threads.h"

inline void _woort_spin_loop_hint()
{
    // If in msvc
#if defined(_MSC_VER) && _MSC_VER >= 1900
#   if defined(_M_ARM64) || defined(__aarch64__)
    __yield();
#   elif defined(_M_X64) || defined(__x86_64__)
    _mm_pause();
#   else
    woort_thread_yield();
#   endif
#elif defined(__GNUC__) || defined(__clang__)
#   if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#   elif defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("pause");
#   else
    woort_thread_yield();
#   endif
#else
    // No specific pause instruction available, use a generic hint
    woort_thread_yield();
#endif
}

/* ============================================== */
/*                  Spinlock                      */
/* ============================================== */

void woort_spinlock_init(woort_Spinlock* lock)
{
    woort_atomic_flag_clear(&lock->m_flag);
}
void woort_spinlock_deinit(woort_Spinlock* lock)
{
    // Nothing to do for deinitialization.
    (void)lock;
}

void woort_spinlock_lock(woort_Spinlock* lock)
{
    while (woort_atomic_flag_test_and_set_explicit(
        &lock->m_flag,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
    {
        // Spin until the lock is acquired.
        _woort_spin_loop_hint();
    }    
}

bool woort_spinlock_trylock(woort_Spinlock* lock)
{
    return !woort_atomic_flag_test_and_set_explicit(
        &lock->m_flag,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

void woort_spinlock_unlock(woort_Spinlock* lock)
{
    woort_atomic_flag_clear_explicit(
        &lock->m_flag,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}

/* ============================================== */
/*             Read-Write Spinlock                */
/* ============================================== */

void woort_rwspinlock_init(woort_RWSpinlock* lock)
{
    woort_atomic_init(&lock->m_state, 0);
}
void woort_rwspinlock_deinit(woort_RWSpinlock* lock)
{
      // Nothing to do for deinitialization.
    (void)lock;
}

void woort_rwspinlock_read_lock(woort_RWSpinlock* lock)
{
    int expected;
    do
    {
        // Wait until there is no writer.
        while ((expected = woort_atomic_load_explicit(
            &lock->m_state, 
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED)) < 0)
        {
            // Spin while a writer holds the lock.
            _woort_spin_loop_hint();
        }
        // Try to increment the reader count.
    } while (!woort_atomic_compare_exchange_weak_explicit(
        &lock->m_state,
        &expected,
        expected + 1,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED));
}

bool woort_rwspinlock_try_read_lock(woort_RWSpinlock* lock)
{
    int expected = woort_atomic_load_explicit(
        &lock->m_state,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    // If a writer holds the lock, fail immediately.
    if (expected < 0)
    {
        return false;
    }

    // Try to increment the reader count.
    return woort_atomic_compare_exchange_strong_explicit(
        &lock->m_state,
        &expected,
        expected + 1,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
}

void woort_rwspinlock_read_unlock(woort_RWSpinlock* lock)
{
    woort_atomic_fetch_sub_explicit(&lock->m_state, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}

void woort_rwspinlock_write_lock(woort_RWSpinlock* lock)
{
    int expected;
    do
    {
        // Wait until the lock is free.
        while ((expected = woort_atomic_load_explicit(
            &lock->m_state, 
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED)) != 0)
        {
            // Spin while readers or a writer hold the lock.
            _woort_spin_loop_hint();
        }
        // Try to acquire the write lock (set state to -1).
    } while (!woort_atomic_compare_exchange_weak_explicit(
        &lock->m_state,
        &expected,
        -1,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED));
}

bool woort_rwspinlock_try_write_lock(woort_RWSpinlock* lock)
{
    int expected = 0;

    // Try to acquire the write lock (set state to -1) only if it's free.
    return woort_atomic_compare_exchange_strong_explicit(
        &lock->m_state,
        &expected,
        -1,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
}

void woort_rwspinlock_write_unlock(woort_RWSpinlock* lock)
{
    woort_atomic_store_explicit(&lock->m_state, 0, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}
