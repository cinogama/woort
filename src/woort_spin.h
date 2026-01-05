#pragma once

/*
woort_spin.h
Spinlock and Read-Write Spinlock implementation using C11 atomics.
*/

#include <stdbool.h>
#include <stdatomic.h>

/* ============================================== */
/*                  Spinlock                      */
/* ============================================== */

typedef struct woort_Spinlock
{
    atomic_flag m_flag;
} woort_Spinlock;

// Initialize a spinlock.
void woort_Spinlock_init(woort_Spinlock* lock);
void woort_Spinlock_deinit(woort_Spinlock* lock);

// Acquire the spinlock (blocking).
void woort_Spinlock_lock(woort_Spinlock* lock);

// Try to acquire the spinlock (non-blocking).
// Returns true if the lock was acquired, false otherwise.
bool woort_Spinlock_trylock(woort_Spinlock* lock);

// Release the spinlock.
void woort_Spinlock_unlock(woort_Spinlock* lock);

/* ============================================== */
/*             Read-Write Spinlock                */
/* ============================================== */

typedef struct woort_RWSpinlock
{
    // Positive value: number of readers holding the lock.
    // -1: a writer is holding the lock.
    // 0: lock is free.
    atomic_int m_state;
} woort_RWSpinlock;

// Initialize a read-write spinlock.
void woort_RWSpinlock_init(woort_RWSpinlock* lock);
void woort_RWSpinlock_deinit(woort_RWSpinlock* lock);

// Acquire read lock (blocking).
// Multiple readers can hold the lock simultaneously.
void woort_RWSpinlock_read_lock(woort_RWSpinlock* lock);

// Try to acquire read lock (non-blocking).
// Returns true if the lock was acquired, false otherwise.
bool woort_RWSpinlock_try_read_lock(woort_RWSpinlock* lock);

// Release read lock.
void woort_RWSpinlock_read_unlock(woort_RWSpinlock* lock);

// Acquire write lock (blocking).
// Only one writer can hold the lock, and no readers can hold it.
void woort_RWSpinlock_write_lock(woort_RWSpinlock* lock);

// Try to acquire write lock (non-blocking).
// Returns true if the lock was acquired, false otherwise.
bool woort_RWSpinlock_try_write_lock(woort_RWSpinlock* lock);

// Release write lock.
void woort_RWSpinlock_write_unlock(woort_RWSpinlock* lock);
