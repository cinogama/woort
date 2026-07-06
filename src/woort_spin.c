#include "woort_spin.h"
#include "woort_atomic.h"
#include "woort_threads.h"
#include "woort_platform.h"

static void _woort_spin_loop_hint()
{
    /* If in msvc */
#if defined(WOORT_COMPILER_MSVC) && _MSC_VER >= 1900
#   if defined(WOORT_PLATFORM_ARM64)
    __yield();
#   elif defined(WOORT_PLATFORM_X64)
    _mm_pause();
#   else
    woort_thread_yield();
#   endif
#elif defined(WOORT_COMPILER_GCC_COMPAT)
#   if defined(WOORT_PLATFORM_ARM64)
    __asm__ __volatile__("yield");
#   elif defined(WOORT_PLATFORM_X64)
    __asm__ __volatile__("pause");
#   else
    woort_thread_yield();
#   endif
#else
    /* No specific pause instruction available, use a generic hint */
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
    /* Nothing to do for deinitialization. */
    (void)lock;
}

void woort_spinlock_lock(woort_Spinlock* lock)
{
    while (woort_atomic_flag_test_and_set_explicit(
        &lock->m_flag,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
    {
        /* Spin until the lock is acquired. */
        _woort_spin_loop_hint();
    }    
}

WOORT_NODISCARD bool woort_spinlock_trylock(woort_Spinlock* lock)
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
      /* Nothing to do for deinitialization. */
    (void)lock;
}

void woort_rwspinlock_read_lock(woort_RWSpinlock* lock)
{
    uint32_t prev;
    do
    {
        /* Load current state */
        prev = woort_atomic_load_explicit(
            &lock->m_state, 
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
        
        if (!(prev & WOORT_RWSPIN_WRITE_BIT))
        {
            /* No write bit, try to increment reader count */
            do
            {
                if (woort_atomic_compare_exchange_strong_explicit(
                    &lock->m_state,
                    &prev,
                    prev + 1,
                    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
                    WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
                {
                    /* Successfully acquired read lock */
                    return;
                }
                
                /* CAS failed, check if write bit appeared */
                if (prev & WOORT_RWSPIN_WRITE_BIT)
                    break; /* Writer arrived, retry from outer loop */
                
                _woort_spin_loop_hint();
            } while (true);
        }
        _woort_spin_loop_hint();
    } while (true);
}

WOORT_NODISCARD bool woort_rwspinlock_try_read_lock(woort_RWSpinlock* lock)
{
    uint32_t expected = woort_atomic_load_explicit(
        &lock->m_state,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    /* If a writer holds the lock (write bit is set), fail immediately. */
    if (expected & WOORT_RWSPIN_WRITE_BIT)
    {
        return false;
    }

    /* Try to increment the reader count. */
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
    do
    {
        /* Phase 1: Set write bit atomically (prevents new readers) */
        uint32_t prev_status =
            woort_atomic_fetch_or_explicit(
                &lock->m_state,
                WOORT_RWSPIN_WRITE_BIT,
                WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);

        if (!(prev_status & WOORT_RWSPIN_WRITE_BIT))
        {
            /* Successfully set write bit
               Phase 2: Wait for all readers to finish */
            if (prev_status != 0)
            {
                /* Still have other readers */
                do
                {
                    _woort_spin_loop_hint();
                    prev_status = woort_atomic_load_explicit(
                        &lock->m_state,
                        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

                } while (prev_status != WOORT_RWSPIN_WRITE_BIT);
            }
            break; /* Lock acquired */
        }

        /* Another writer is holding the lock, wait */
        _woort_spin_loop_hint();
    } while (true);
}

WOORT_NODISCARD bool woort_rwspinlock_try_write_lock(woort_RWSpinlock* lock)
{
    uint32_t expected = 0;

    /* Try to acquire the write lock (set write bit) only if it's completely free. */
    return woort_atomic_compare_exchange_strong_explicit(
        &lock->m_state,
        &expected,
        WOORT_RWSPIN_WRITE_BIT,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
}

void woort_rwspinlock_write_unlock(woort_RWSpinlock* lock)
{
    woort_atomic_store_explicit(&lock->m_state, 0, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}
