#pragma once

/*
woort_threads.h
*/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct woort_Thread woort_Thread;
typedef void (*woort_ThreadJobFunc)(void*);

bool woort_thread_start(
    woort_ThreadJobFunc job,
    void* user_data,
    woort_Thread** out_thread);
void woort_thread_join(woort_Thread* thread);

void woort_thread_sleep_ms(uint32_t ms);
void woort_thread_yield(void);

typedef struct woort_Mutex woort_Mutex;
typedef struct woort_TimeMutex woort_TimeMutex;
typedef struct woort_RecursiveMutex woort_RecursiveMutex;
typedef struct woort_TimeRecursiveMutex woort_TimeRecursiveMutex;

bool woort_mutex_create(woort_Mutex** out_mutex);
void woort_mutex_destroy(woort_Mutex* mutex);
void woort_mutex_lock(woort_Mutex* mutex);
bool woort_mutex_trylock(woort_Mutex* mutex);
void woort_mutex_unlock(woort_Mutex* mutex);

bool woort_time_mutex_create(woort_TimeMutex** out_mutex);
void woort_time_mutex_destroy(woort_TimeMutex* mutex);
void woort_time_mutex_lock(woort_TimeMutex* mutex);
bool woort_time_mutex_trylock(woort_TimeMutex* mutex, uint32_t timeout_ms);
void woort_time_mutex_unlock(woort_TimeMutex* mutex);

bool woort_recursive_mutex_create(woort_RecursiveMutex** out_mutex);
void woort_recursive_mutex_destroy(woort_RecursiveMutex* mutex);
void woort_recursive_mutex_lock(woort_RecursiveMutex* mutex);
bool woort_recursive_mutex_trylock(woort_RecursiveMutex* mutex);
void woort_recursive_mutex_unlock(woort_RecursiveMutex* mutex);

bool woort_time_recursive_mutex_create(woort_TimeRecursiveMutex** out_mutex);
void woort_time_recursive_mutex_destroy(woort_TimeRecursiveMutex* mutex);
bool woort_time_recursive_mutex_lock(woort_TimeRecursiveMutex* mutex);
bool woort_time_recursive_mutex_trylock(woort_TimeRecursiveMutex* mutex, uint32_t timeout_ms);
void woort_time_recursive_mutex_unlock(woort_TimeRecursiveMutex* mutex);

typedef struct woort_ConditionVariable woort_ConditionVariable;
bool woort_condition_variable_create(woort_ConditionVariable** out_cv);
void woort_condition_variable_destroy(woort_ConditionVariable* cv);
void woort_condition_variable_wait(woort_ConditionVariable* cv, woort_Mutex* mutex);
bool woort_condition_variable_timed_wait(woort_ConditionVariable* cv, woort_Mutex* mutex, uint32_t timeout_ms);
void woort_condition_variable_signal(woort_ConditionVariable* cv);
void woort_condition_variable_broadcast(woort_ConditionVariable* cv);

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_THREADS__)
#   define WOORT_THREADS_USE_C11 1
#elif defined(_WIN32) || defined(_WIN64)
#   define WOORT_THREADS_USE_WIN32 1
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__MACH__)
#   define WOORT_THREADS_USE_PTHREAD 1
#else
#   error "Unsupported platform for threading"
#endif

#ifdef WOORT_THREADS_USE_C11
#   define WOORT_THREAD_LOCAL _Thread_local
#elif defined(WOORT_THREADS_USE_WIN32)
#   define WOORT_THREAD_LOCAL __declspec(thread)
#elif defined(WOORT_THREADS_USE_PTHREAD)
#   define WOORT_THREAD_LOCAL __thread
#else
#   define WOORT_THREAD_LOCAL
#endif