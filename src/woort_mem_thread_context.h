#pragma once

/*
woort_mem_thread_context.h
Per-thread allocator context. Lazily initialized via TLS.
*/

#include "woort_mem_thread_page_collection.h"

#include <stdbool.h>

struct woort_mem_GCWorker;

typedef struct woort_mem_ThreadContext
{
    woort_mem_ThreadPageCollection m_thread_page_collection;
    /* OPTIONAL */ struct woort_mem_GCWorker* m_gc_marking_context;
    bool m_is_gc_worker_context;

} woort_mem_ThreadContext;

void woort_mem_thread_context_init(woort_mem_ThreadContext* self);
void woort_mem_thread_context_deinit(woort_mem_ThreadContext* self);

/*
 * Returns the current thread's ThreadContext, initializing it lazily
 * on first call. The C++ thread_guard ensures woort_mem_thread_context_on_exit
 * is invoked at thread exit.
 */
woort_mem_ThreadContext* woort_mem_get_thread_context(void);

/* Called by the C++ thread_guard destructor at thread exit. */
void woort_mem_thread_context_on_exit(void);

/* Touches the C++ thread_local guard to ensure its destructor runs. */
void woort_mem_thread_guard_touch(void);
