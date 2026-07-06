#pragma once

/*
woort_spin.h
Spinlock and Read-Write Spinlock implementation using C11 atomics.
*/

#include "woort_diagnosis.h"
#include "woort_atomic.h"

#include <stdbool.h>

typedef struct woort_Spinlock
{
    woort_AtomicFlag m_flag;
} woort_Spinlock;

/* Initialize a spinlock. */
void woort_spinlock_init(woort_Spinlock* lock);
void woort_spinlock_deinit(woort_Spinlock* lock);

/* Acquire the spinlock (blocking). */
void woort_spinlock_lock(woort_Spinlock* lock);

/* Try to acquire the spinlock (non-blocking).
   Returns true if the lock was acquired, false otherwise. */
WOORT_NODISCARD bool woort_spinlock_trylock(woort_Spinlock* lock);

/* Release the spinlock. */
void woort_spinlock_unlock(woort_Spinlock* lock);

typedef struct woort_RWSpinlock
{
    /* Bit 31 (0x80000000): Write lock indicator - set when a writer holds or waits for the lock
       Bits 0-30: Reader count - number of readers currently holding the lock
       0: lock is free
       0x80000000u: writer holds the lock
       Other values: readers holding the lock */
    woort_AtomicUInt32 m_state;
} woort_RWSpinlock;

/* Initialize a read-write spinlock. */
void woort_rwspinlock_init(woort_RWSpinlock* lock);
void woort_rwspinlock_deinit(woort_RWSpinlock* lock);

/* Acquire read lock (blocking).
   Multiple readers can hold the lock simultaneously. */
void woort_rwspinlock_read_lock(woort_RWSpinlock* lock);

/* Try to acquire read lock (non-blocking).
   Returns true if the lock was acquired, false otherwise. */
WOORT_NODISCARD bool woort_rwspinlock_try_read_lock(woort_RWSpinlock* lock);

/* Release read lock. */
void woort_rwspinlock_read_unlock(woort_RWSpinlock* lock);

/* Acquire write lock (blocking).
   Only one writer can hold the lock, and no readers can hold it. */
void woort_rwspinlock_write_lock(woort_RWSpinlock* lock);

/* Try to acquire write lock (non-blocking).
   Returns true if the lock was acquired, false otherwise. */
WOORT_NODISCARD bool woort_rwspinlock_try_write_lock(woort_RWSpinlock* lock);

/* Release write lock. */
void woort_rwspinlock_write_unlock(woort_RWSpinlock* lock);
