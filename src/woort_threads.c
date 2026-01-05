#include "woort_threads.h"
#include "woort_log.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
 * Platform/Compiler detection for threads support
 * ============================================================================ */

/*
 * Check for C11 threads support:
 * - __STDC_NO_THREADS__ is defined if C11 threads are NOT supported
 * - Some compilers (like MSVC) may not define this but still not support C11 threads
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_THREADS__)
    /* Try to use C11 threads */
#   if defined(_MSC_VER)
        /* MSVC does not support C11 threads even in C11 mode */
#       define WOORT_THREADS_USE_WIN32 1
#   elif defined(__APPLE__)
        /* macOS does not support C11 threads */
#       define WOORT_THREADS_USE_PTHREAD 1
#   else
        /* Assume C11 threads are available */
#       define WOORT_THREADS_USE_C11 1
#   endif
#elif defined(_WIN32) || defined(_WIN64)
#   define WOORT_THREADS_USE_WIN32 1
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__MACH__)
#   define WOORT_THREADS_USE_PTHREAD 1
#else
#   error "Unsupported platform for threading"
#endif

/* ============================================================================
 * Include platform-specific headers
 * ============================================================================ */

#if defined(WOORT_THREADS_USE_C11)
#   include <threads.h>
#   include <time.h>
#elif defined(WOORT_THREADS_USE_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#elif defined(WOORT_THREADS_USE_PTHREAD)
#   include <pthread.h>
#   include <unistd.h>
#   include <errno.h>
#   include <sys/time.h>
#   include <time.h>
#endif

/* ============================================================================
 * C11 Threads Implementation
 * ============================================================================ */

#if defined(WOORT_THREADS_USE_C11)

/* ----------- Thread ----------- */

typedef struct woort_ThreadData
{
    woort_ThreadJobFunc job;
    void* user_data;
} woort_ThreadData;

struct woort_Thread
{
    thrd_t handle;
    woort_ThreadData* data;
};

static int _woort_thread_entry_c11(void* arg)
{
    woort_ThreadData* data = (woort_ThreadData*)arg;
    data->job(data->user_data);
    return 0;
}

bool woort_thread_start(
    woort_ThreadJobFunc job,
    void* user_data,
    woort_Thread** out_thread)
{
    assert(NULL != job);
    assert(NULL != out_thread);

    woort_Thread* thread = malloc(sizeof(woort_Thread));
    if (!thread)
    {
        WOORT_DEBUG("failed to allocate woort_Thread");
        return false;
    }

    thread->data = malloc(sizeof(woort_ThreadData));
    if (!thread->data)
    {
        WOORT_DEBUG("failed to allocate woort_ThreadData");
        free(thread);
        return false;
    }

    thread->data->job = job;
    thread->data->user_data = user_data;

    if (thrd_create(&thread->handle, _woort_thread_entry_c11, thread->data) != thrd_success)
    {
        free(thread->data);
        free(thread);
        return false;
    }

    *out_thread = thread;
    return true;
}

void woort_thread_join(woort_Thread* thread)
{
    assert(NULL != thread);

    int result;
    if (thrd_join(thread->handle, &result) != thrd_success)
    {
        WOORT_DEBUG("thrd_join failed");
        abort();
    }

    free(thread->data);
    free(thread);
}

void woort_thread_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    thrd_sleep(&ts, NULL);
}

void woort_thread_yield(void)
{
    thrd_yield();
}

/* ----------- Mutex ----------- */

struct woort_Mutex
{
    mtx_t handle;
};

bool woort_mutex_create(woort_Mutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_Mutex* mutex = malloc(sizeof(woort_Mutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_Mutex");
        return false;
    }

    if (mtx_init(&mutex->handle, mtx_plain) != thrd_success)
    {
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_mutex_destroy(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    mtx_destroy(&mutex->handle);
    free(mutex);
}

void woort_mutex_lock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    mtx_lock(&mutex->handle);
}

bool woort_mutex_trylock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    return mtx_trylock(&mutex->handle) == thrd_success;
}

void woort_mutex_unlock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    mtx_unlock(&mutex->handle);
}

/* ----------- Timed Mutex ----------- */

struct woort_TimeMutex
{
    mtx_t handle;
};

bool woort_time_mutex_create(woort_TimeMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_TimeMutex* mutex = malloc(sizeof(woort_TimeMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_TimeMutex");
        return false;
    }

    if (mtx_init(&mutex->handle, mtx_timed) != thrd_success)
    {
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_time_mutex_destroy(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    mtx_destroy(&mutex->handle);
    free(mutex);
}

void woort_time_mutex_lock(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    mtx_lock(&mutex->handle);
}

bool woort_time_mutex_trylock(woort_TimeMutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != mutex);

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return mtx_timedlock(&mutex->handle, &ts) == thrd_success;
}

void woort_time_mutex_unlock(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    mtx_unlock(&mutex->handle);
}

/* ----------- Recursive Mutex ----------- */

struct woort_RecursiveMutex
{
    mtx_t handle;
};

bool woort_recursive_mutex_create(woort_RecursiveMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_RecursiveMutex* mutex = malloc(sizeof(woort_RecursiveMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_RecursiveMutex");
        return false;
    }

    if (mtx_init(&mutex->handle, mtx_plain | mtx_recursive) != thrd_success)
    {
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_recursive_mutex_destroy(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    mtx_destroy(&mutex->handle);
    free(mutex);
}

void woort_recursive_mutex_lock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    mtx_lock(&mutex->handle);
}

bool woort_recursive_mutex_trylock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    return mtx_trylock(&mutex->handle) == thrd_success;
}

void woort_recursive_mutex_unlock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    mtx_unlock(&mutex->handle);
}

/* ----------- Timed Recursive Mutex ----------- */

struct woort_TimeRecursiveMutex
{
    mtx_t handle;
};

bool woort_time_recursive_mutex_create(woort_TimeRecursiveMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_TimeRecursiveMutex* mutex = malloc(sizeof(woort_TimeRecursiveMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_TimeRecursiveMutex");
        return false;
    }

    if (mtx_init(&mutex->handle, mtx_timed | mtx_recursive) != thrd_success)
    {
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_time_recursive_mutex_destroy(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    mtx_destroy(&mutex->handle);
    free(mutex);
}

bool woort_time_recursive_mutex_lock(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    return mtx_lock(&mutex->handle) == thrd_success;
}

bool woort_time_recursive_mutex_trylock(woort_TimeRecursiveMutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != mutex);

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return mtx_timedlock(&mutex->handle, &ts) == thrd_success;
}

void woort_time_recursive_mutex_unlock(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    mtx_unlock(&mutex->handle);
}

/* ----------- Condition Variable ----------- */

struct woort_ConditionVariable
{
    cnd_t handle;
};

bool woort_condition_variable_create(woort_ConditionVariable** out_cv)
{
    assert(NULL != out_cv);

    woort_ConditionVariable* cv = malloc(sizeof(woort_ConditionVariable));
    if (!cv)
    {
        WOORT_DEBUG("failed to allocate woort_ConditionVariable");
        return false;
    }

    if (cnd_init(&cv->handle) != thrd_success)
    {
        free(cv);
        return false;
    }

    *out_cv = cv;
    return true;
}

void woort_condition_variable_destroy(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    cnd_destroy(&cv->handle);
    free(cv);
}

void woort_condition_variable_wait(woort_ConditionVariable* cv, woort_Mutex* mutex)
{
    assert(NULL != cv);
    assert(NULL != mutex);
    cnd_wait(&cv->handle, &mutex->handle);
}

bool woort_condition_variable_timed_wait(woort_ConditionVariable* cv, woort_Mutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != cv);
    assert(NULL != mutex);

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return cnd_timedwait(&cv->handle, &mutex->handle, &ts) == thrd_success;
}

void woort_condition_variable_signal(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    cnd_signal(&cv->handle);
}

void woort_condition_variable_broadcast(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    cnd_broadcast(&cv->handle);
}

/* ============================================================================
 * Win32 Threads Implementation
 * ============================================================================ */

#elif defined(WOORT_THREADS_USE_WIN32)

/* ----------- Thread ----------- */

typedef struct woort_ThreadData
{
    woort_ThreadJobFunc job;
    void* user_data;
} woort_ThreadData;

struct woort_Thread
{
    HANDLE handle;
    woort_ThreadData* data;
};

static DWORD WINAPI _woort_thread_entry_win32(LPVOID arg)
{
    woort_ThreadData* data = (woort_ThreadData*)arg;
    data->job(data->user_data);
    return 0;
}

bool woort_thread_start(
    woort_ThreadJobFunc job,
    void* user_data,
    woort_Thread** out_thread)
{
    assert(NULL != job);
    assert(NULL != out_thread);

    woort_Thread* thread = malloc(sizeof(woort_Thread));
    if (!thread)
    {
        WOORT_DEBUG("failed to allocate woort_Thread");
        return false;
    }

    thread->data = malloc(sizeof(woort_ThreadData));
    if (!thread->data)
    {
        WOORT_DEBUG("failed to allocate woort_ThreadData");
        free(thread);
        return false;
    }

    thread->data->job = job;
    thread->data->user_data = user_data;

    thread->handle = CreateThread(
        NULL,                       /* default security attributes */
        0,                          /* default stack size */
        _woort_thread_entry_win32,  /* thread function */
        thread->data,               /* argument to thread function */
        0,                          /* default creation flags */
        NULL);                      /* don't need thread ID */

    if (thread->handle == NULL)
    {
        free(thread->data);
        free(thread);
        return false;
    }

    *out_thread = thread;
    return true;
}

void woort_thread_join(woort_Thread* thread)
{
    assert(NULL != thread);

    DWORD result = WaitForSingleObject(thread->handle, INFINITE);
    if (result != WAIT_OBJECT_0)
    {
        WOORT_DEBUG("WaitForSingleObject failed with result %lu", (unsigned long)result);
        abort();
    }

    CloseHandle(thread->handle);
    free(thread->data);
    free(thread);
}

void woort_thread_sleep_ms(uint32_t ms)
{
    Sleep((DWORD)ms);
}

void woort_thread_yield(void)
{
    SwitchToThread();
}

/* ----------- Mutex ----------- */

struct woort_Mutex
{
    SRWLOCK lock;
};

bool woort_mutex_create(woort_Mutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_Mutex* mutex = malloc(sizeof(woort_Mutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_Mutex");
        return false;
    }

    InitializeSRWLock(&mutex->lock);

    *out_mutex = mutex;
    return true;
}

void woort_mutex_destroy(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    /* SRWLock does not need explicit destruction */
    free(mutex);
}

void woort_mutex_lock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    AcquireSRWLockExclusive(&mutex->lock);
}

bool woort_mutex_trylock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    return TryAcquireSRWLockExclusive(&mutex->lock) != 0;
}

void woort_mutex_unlock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    ReleaseSRWLockExclusive(&mutex->lock);
}

/* ----------- Timed Mutex ----------- */

/*
 * Win32 does not have a native timed mutex, so we use CRITICAL_SECTION
 * combined with a manual timeout mechanism using WaitForSingleObjectEx.
 * Alternatively, we use a simpler approach with spinning and Sleep.
 */
struct woort_TimeMutex
{
    CRITICAL_SECTION cs;
    HANDLE event;
};

bool woort_time_mutex_create(woort_TimeMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_TimeMutex* mutex = malloc(sizeof(woort_TimeMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_TimeMutex");
        return false;
    }

    InitializeCriticalSection(&mutex->cs);
    mutex->event = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (mutex->event == NULL)
    {
        DeleteCriticalSection(&mutex->cs);
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_time_mutex_destroy(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    DeleteCriticalSection(&mutex->cs);
    CloseHandle(mutex->event);
    free(mutex);
}

void woort_time_mutex_lock(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    WaitForSingleObject(mutex->event, INFINITE);
    EnterCriticalSection(&mutex->cs);
}

bool woort_time_mutex_trylock(woort_TimeMutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != mutex);

    DWORD result = WaitForSingleObject(mutex->event, (DWORD)timeout_ms);
    if (result == WAIT_OBJECT_0)
    {
        EnterCriticalSection(&mutex->cs);
        return true;
    }
    return false;
}

void woort_time_mutex_unlock(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    LeaveCriticalSection(&mutex->cs);
    SetEvent(mutex->event);
}

/* ----------- Recursive Mutex ----------- */

struct woort_RecursiveMutex
{
    CRITICAL_SECTION cs;
};

bool woort_recursive_mutex_create(woort_RecursiveMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_RecursiveMutex* mutex = malloc(sizeof(woort_RecursiveMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_RecursiveMutex");
        return false;
    }

    InitializeCriticalSection(&mutex->cs);

    *out_mutex = mutex;
    return true;
}

void woort_recursive_mutex_destroy(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    DeleteCriticalSection(&mutex->cs);
    free(mutex);
}

void woort_recursive_mutex_lock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    EnterCriticalSection(&mutex->cs);
}

bool woort_recursive_mutex_trylock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    return TryEnterCriticalSection(&mutex->cs) != 0;
}

void woort_recursive_mutex_unlock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    LeaveCriticalSection(&mutex->cs);
}

/* ----------- Timed Recursive Mutex ----------- */

struct woort_TimeRecursiveMutex
{
    CRITICAL_SECTION cs;
    HANDLE event;
    DWORD owner_thread;
    int lock_count;
};

bool woort_time_recursive_mutex_create(woort_TimeRecursiveMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_TimeRecursiveMutex* mutex = malloc(sizeof(woort_TimeRecursiveMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_TimeRecursiveMutex");
        return false;
    }

    InitializeCriticalSection(&mutex->cs);
    mutex->event = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (mutex->event == NULL)
    {
        DeleteCriticalSection(&mutex->cs);
        free(mutex);
        return false;
    }
    mutex->owner_thread = 0;
    mutex->lock_count = 0;

    *out_mutex = mutex;
    return true;
}

void woort_time_recursive_mutex_destroy(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    DeleteCriticalSection(&mutex->cs);
    CloseHandle(mutex->event);
    free(mutex);
}

bool woort_time_recursive_mutex_lock(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);

    DWORD current_thread = GetCurrentThreadId();

    EnterCriticalSection(&mutex->cs);
    if (mutex->owner_thread == current_thread)
    {
        mutex->lock_count++;
        LeaveCriticalSection(&mutex->cs);
        return true;
    }
    LeaveCriticalSection(&mutex->cs);

    WaitForSingleObject(mutex->event, INFINITE);

    EnterCriticalSection(&mutex->cs);
    mutex->owner_thread = current_thread;
    mutex->lock_count = 1;
    LeaveCriticalSection(&mutex->cs);

    return true;
}

bool woort_time_recursive_mutex_trylock(woort_TimeRecursiveMutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != mutex);

    DWORD current_thread = GetCurrentThreadId();

    EnterCriticalSection(&mutex->cs);
    if (mutex->owner_thread == current_thread)
    {
        mutex->lock_count++;
        LeaveCriticalSection(&mutex->cs);
        return true;
    }
    LeaveCriticalSection(&mutex->cs);

    DWORD result = WaitForSingleObject(mutex->event, (DWORD)timeout_ms);
    if (result != WAIT_OBJECT_0)
    {
        return false;
    }

    EnterCriticalSection(&mutex->cs);
    mutex->owner_thread = current_thread;
    mutex->lock_count = 1;
    LeaveCriticalSection(&mutex->cs);

    return true;
}

void woort_time_recursive_mutex_unlock(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);

    EnterCriticalSection(&mutex->cs);
    if (mutex->lock_count > 0)
    {
        mutex->lock_count--;
        if (mutex->lock_count == 0)
        {
            mutex->owner_thread = 0;
            SetEvent(mutex->event);
        }
    }
    LeaveCriticalSection(&mutex->cs);
}

/* ----------- Condition Variable ----------- */

struct woort_ConditionVariable
{
    CONDITION_VARIABLE cv;
};

bool woort_condition_variable_create(woort_ConditionVariable** out_cv)
{
    assert(NULL != out_cv);

    woort_ConditionVariable* cv = malloc(sizeof(woort_ConditionVariable));
    if (!cv)
    {
        WOORT_DEBUG("failed to allocate woort_ConditionVariable");
        return false;
    }

    InitializeConditionVariable(&cv->cv);

    *out_cv = cv;
    return true;
}

void woort_condition_variable_destroy(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    /* CONDITION_VARIABLE does not need explicit destruction */
    free(cv);
}

void woort_condition_variable_wait(woort_ConditionVariable* cv, woort_Mutex* mutex)
{
    assert(NULL != cv);
    assert(NULL != mutex);
    SleepConditionVariableSRW(&cv->cv, &mutex->lock, INFINITE, 0);
}

bool woort_condition_variable_timed_wait(woort_ConditionVariable* cv, woort_Mutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != cv);
    assert(NULL != mutex);
    return SleepConditionVariableSRW(&cv->cv, &mutex->lock, (DWORD)timeout_ms, 0) != 0;
}

void woort_condition_variable_signal(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    WakeConditionVariable(&cv->cv);
}

void woort_condition_variable_broadcast(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    WakeAllConditionVariable(&cv->cv);
}

/* ============================================================================
 * POSIX Threads (pthread) Implementation
 * ============================================================================ */

#elif defined(WOORT_THREADS_USE_PTHREAD)

/* ----------- Thread ----------- */

typedef struct woort_ThreadData
{
    woort_ThreadJobFunc job;
    void* user_data;
} woort_ThreadData;

struct woort_Thread
{
    pthread_t handle;
    woort_ThreadData* data;
};

static void* _woort_thread_entry_pthread(void* arg)
{
    woort_ThreadData* data = (woort_ThreadData*)arg;
    data->job(data->user_data);
    return NULL;
}

bool woort_thread_start(
    woort_ThreadJobFunc job,
    void* user_data,
    woort_Thread** out_thread)
{
    assert(NULL != job);
    assert(NULL != out_thread);

    woort_Thread* thread = malloc(sizeof(woort_Thread));
    if (!thread)
    {
        WOORT_DEBUG("failed to allocate woort_Thread");
        return false;
    }

    thread->data = malloc(sizeof(woort_ThreadData));
    if (!thread->data)
    {
        WOORT_DEBUG("failed to allocate woort_ThreadData");
        free(thread);
        return false;
    }

    thread->data->job = job;
    thread->data->user_data = user_data;

    if (pthread_create(&thread->handle, NULL, _woort_thread_entry_pthread, thread->data) != 0)
    {
        free(thread->data);
        free(thread);
        return false;
    }

    *out_thread = thread;
    return true;
}

void woort_thread_join(woort_Thread* thread)
{
    assert(NULL != thread);

    int ret = pthread_join(thread->handle, NULL);
    if (ret != 0)
    {
        WOORT_DEBUG("pthread_join failed with error %d", ret);
        abort();
    }

    free(thread->data);
    free(thread);
}

void woort_thread_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void woort_thread_yield(void)
{
    sched_yield();
}

/* ----------- Mutex ----------- */

struct woort_Mutex
{
    pthread_mutex_t handle;
};

bool woort_mutex_create(woort_Mutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_Mutex* mutex = malloc(sizeof(woort_Mutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_Mutex");
        return false;
    }

    if (pthread_mutex_init(&mutex->handle, NULL) != 0)
    {
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_mutex_destroy(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

void woort_mutex_lock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_lock(&mutex->handle);
}

bool woort_mutex_trylock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    return pthread_mutex_trylock(&mutex->handle) == 0;
}

void woort_mutex_unlock(woort_Mutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_unlock(&mutex->handle);
}

/* ----------- Timed Mutex ----------- */

struct woort_TimeMutex
{
    pthread_mutex_t handle;
};

static void _woort_get_abs_timeout(struct timespec* ts, uint32_t timeout_ms)
{
#if defined(__APPLE__)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + (timeout_ms / 1000);
    ts->tv_nsec = tv.tv_usec * 1000 + (timeout_ms % 1000) * 1000000L;
#else
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (timeout_ms % 1000) * 1000000L;
#endif
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

bool woort_time_mutex_create(woort_TimeMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_TimeMutex* mutex = malloc(sizeof(woort_TimeMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_TimeMutex");
        return false;
    }

    if (pthread_mutex_init(&mutex->handle, NULL) != 0)
    {
        free(mutex);
        return false;
    }

    *out_mutex = mutex;
    return true;
}

void woort_time_mutex_destroy(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

void woort_time_mutex_lock(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_lock(&mutex->handle);
}

bool woort_time_mutex_trylock(woort_TimeMutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != mutex);

#if defined(__APPLE__)
    /* macOS doesn't have pthread_mutex_timedlock, use polling */
    uint32_t elapsed = 0;
    const uint32_t sleep_interval = 1; /* 1 ms */
    while (elapsed < timeout_ms)
    {
        if (pthread_mutex_trylock(&mutex->handle) == 0)
        {
            return true;
        }
        usleep(sleep_interval * 1000);
        elapsed += sleep_interval;
    }
    return pthread_mutex_trylock(&mutex->handle) == 0;
#else
    struct timespec ts;
    _woort_get_abs_timeout(&ts, timeout_ms);
    return pthread_mutex_timedlock(&mutex->handle, &ts) == 0;
#endif
}

void woort_time_mutex_unlock(woort_TimeMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_unlock(&mutex->handle);
}

/* ----------- Recursive Mutex ----------- */

struct woort_RecursiveMutex
{
    pthread_mutex_t handle;
};

bool woort_recursive_mutex_create(woort_RecursiveMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_RecursiveMutex* mutex = malloc(sizeof(woort_RecursiveMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_RecursiveMutex");
        return false;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(&mutex->handle, &attr) != 0)
    {
        pthread_mutexattr_destroy(&attr);
        free(mutex);
        return false;
    }

    pthread_mutexattr_destroy(&attr);
    *out_mutex = mutex;
    return true;
}

void woort_recursive_mutex_destroy(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

void woort_recursive_mutex_lock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_lock(&mutex->handle);
}

bool woort_recursive_mutex_trylock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    return pthread_mutex_trylock(&mutex->handle) == 0;
}

void woort_recursive_mutex_unlock(woort_RecursiveMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_unlock(&mutex->handle);
}

/* ----------- Timed Recursive Mutex ----------- */

struct woort_TimeRecursiveMutex
{
    pthread_mutex_t handle;
};

bool woort_time_recursive_mutex_create(woort_TimeRecursiveMutex** out_mutex)
{
    assert(NULL != out_mutex);

    woort_TimeRecursiveMutex* mutex = malloc(sizeof(woort_TimeRecursiveMutex));
    if (!mutex)
    {
        WOORT_DEBUG("failed to allocate woort_TimeRecursiveMutex");
        return false;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(&mutex->handle, &attr) != 0)
    {
        pthread_mutexattr_destroy(&attr);
        free(mutex);
        return false;
    }

    pthread_mutexattr_destroy(&attr);
    *out_mutex = mutex;
    return true;
}

void woort_time_recursive_mutex_destroy(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

bool woort_time_recursive_mutex_lock(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    return pthread_mutex_lock(&mutex->handle) == 0;
}

bool woort_time_recursive_mutex_trylock(woort_TimeRecursiveMutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != mutex);

#if defined(__APPLE__)
    /* macOS doesn't have pthread_mutex_timedlock, use polling */
    uint32_t elapsed = 0;
    const uint32_t sleep_interval = 1; /* 1 ms */
    while (elapsed < timeout_ms)
    {
        if (pthread_mutex_trylock(&mutex->handle) == 0)
        {
            return true;
        }
        usleep(sleep_interval * 1000);
        elapsed += sleep_interval;
    }
    return pthread_mutex_trylock(&mutex->handle) == 0;
#else
    struct timespec ts;
    _woort_get_abs_timeout(&ts, timeout_ms);
    return pthread_mutex_timedlock(&mutex->handle, &ts) == 0;
#endif
}

void woort_time_recursive_mutex_unlock(woort_TimeRecursiveMutex* mutex)
{
    assert(NULL != mutex);
    pthread_mutex_unlock(&mutex->handle);
}

/* ----------- Condition Variable ----------- */

struct woort_ConditionVariable
{
    pthread_cond_t handle;
};

bool woort_condition_variable_create(woort_ConditionVariable** out_cv)
{
    assert(NULL != out_cv);

    woort_ConditionVariable* cv = malloc(sizeof(woort_ConditionVariable));
    if (!cv)
    {
        WOORT_DEBUG("failed to allocate woort_ConditionVariable");
        return false;
    }

    if (pthread_cond_init(&cv->handle, NULL) != 0)
    {
        free(cv);
        return false;
    }

    *out_cv = cv;
    return true;
}

void woort_condition_variable_destroy(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    pthread_cond_destroy(&cv->handle);
    free(cv);
}

void woort_condition_variable_wait(woort_ConditionVariable* cv, woort_Mutex* mutex)
{
    assert(NULL != cv);
    assert(NULL != mutex);
    pthread_cond_wait(&cv->handle, &mutex->handle);
}

bool woort_condition_variable_timed_wait(woort_ConditionVariable* cv, woort_Mutex* mutex, uint32_t timeout_ms)
{
    assert(NULL != cv);
    assert(NULL != mutex);

    struct timespec ts;
    _woort_get_abs_timeout(&ts, timeout_ms);

    return pthread_cond_timedwait(&cv->handle, &mutex->handle, &ts) == 0;
}

void woort_condition_variable_signal(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    pthread_cond_signal(&cv->handle);
}

void woort_condition_variable_broadcast(woort_ConditionVariable* cv)
{
    assert(NULL != cv);
    pthread_cond_broadcast(&cv->handle);
}
#endif /* Platform selection */
