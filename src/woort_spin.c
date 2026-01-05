#include <threads.h>

#include "woort_spin.h"

void _woort_spin_loop_hint()
{
    // If in msvc
#if defined(_MSC_VER) && _MSC_VER >= 1900
#   if defined(_M_ARM64) || defined(__aarch64__)
    __yield();
#   elif defined(_M_X64) || defined(__x86_64__)
    _mm_pause();
#   else
    thrd_yield();
#   endif
#elif defined(__GNUC__) || defined(__clang__)
#   if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield");
#   elif defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("pause");
#   else
    thrd_yield();
#   endif
#else
    // No specific pause instruction available, use a generic hint
    thrd_yield();
#endif
}

/* ============================================== */
/*                  Spinlock                      */
/* ============================================== */

void woort_Spinlock_init(woort_Spinlock* lock)
{
    atomic_flag_clear(&lock->m_flag);
}
void woort_Spinlock_deinit(woort_Spinlock* lock)
{
    // Nothing to do for deinitialization.
    (void)lock;
}

void woort_Spinlock_lock(woort_Spinlock* lock)
{
    while (atomic_flag_test_and_set_explicit(&lock->m_flag, memory_order_acquire))
    {
        // Spin until the lock is acquired.
        _woort_spin_loop_hint();
    }
}

bool woort_Spinlock_trylock(woort_Spinlock* lock)
{
    return !atomic_flag_test_and_set_explicit(&lock->m_flag, memory_order_acquire);
}

void woort_Spinlock_unlock(woort_Spinlock* lock)
{
    atomic_flag_clear_explicit(&lock->m_flag, memory_order_release);
}

/* ============================================== */
/*             Read-Write Spinlock                */
/* ============================================== */

void woort_RWSpinlock_init(woort_RWSpinlock* lock)
{
    atomic_init(&lock->m_state, 0);
}
void woort_RWSpinlock_deinit(woort_RWSpinlock* lock)
{
      // Nothing to do for deinitialization.
    (void)lock;
}

void woort_RWSpinlock_read_lock(woort_RWSpinlock* lock)
{
    int expected;
    do
    {
        // Wait until there is no writer.
        while ((expected = atomic_load_explicit(&lock->m_state, memory_order_relaxed)) < 0)
        {
            // Spin while a writer holds the lock.
            _woort_spin_loop_hint();
        }
        // Try to increment the reader count.
    } while (!atomic_compare_exchange_weak_explicit(
        &lock->m_state,
        &expected,
        expected + 1,
        memory_order_acquire,
        memory_order_relaxed));
}

bool woort_RWSpinlock_try_read_lock(woort_RWSpinlock* lock)
{
    int expected = atomic_load_explicit(&lock->m_state, memory_order_relaxed);

    // If a writer holds the lock, fail immediately.
    if (expected < 0)
    {
        return false;
    }

    // Try to increment the reader count.
    return atomic_compare_exchange_strong_explicit(
        &lock->m_state,
        &expected,
        expected + 1,
        memory_order_acquire,
        memory_order_relaxed);
}

void woort_RWSpinlock_read_unlock(woort_RWSpinlock* lock)
{
    atomic_fetch_sub_explicit(&lock->m_state, 1, memory_order_release);
}

void woort_RWSpinlock_write_lock(woort_RWSpinlock* lock)
{
    int expected;
    do
    {
        // Wait until the lock is free.
        while ((expected = atomic_load_explicit(&lock->m_state, memory_order_relaxed)) != 0)
        {
            // Spin while readers or a writer hold the lock.
            _woort_spin_loop_hint();
        }
        // Try to acquire the write lock (set state to -1).
    } while (!atomic_compare_exchange_weak_explicit(
        &lock->m_state,
        &expected,
        -1,
        memory_order_acquire,
        memory_order_relaxed));
}

bool woort_RWSpinlock_try_write_lock(woort_RWSpinlock* lock)
{
    int expected = 0;

    // Try to acquire the write lock (set state to -1) only if it's free.
    return atomic_compare_exchange_strong_explicit(
        &lock->m_state,
        &expected,
        -1,
        memory_order_acquire,
        memory_order_relaxed);
}

void woort_RWSpinlock_write_unlock(woort_RWSpinlock* lock)
{
    atomic_store_explicit(&lock->m_state, 0, memory_order_release);
}
