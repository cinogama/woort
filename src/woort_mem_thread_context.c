/*
woort_mem_thread_context.c
*/

#include "woort_mem_thread_context.h"
#include "woort_mem_global_context.h"
#include "woort_mem_gc.h"
#include "woort_threads.h"

void woort_mem_thread_context_init(woort_mem_ThreadContext* self)
{
    woort_mem_tpc_init(
        &self->m_thread_page_collection,
        g_woort_mem_global_context.m_globalcontext_inited
            ? &g_woort_mem_global_context.m_gpc
            : NULL);

    self->m_is_gc_worker_context = false;

    if (g_woort_mem_gc != NULL)
        self->m_gc_marking_context =
            woort_mem_gc_fetch_thread_worker(g_woort_mem_gc);
    else
        self->m_gc_marking_context = NULL;

    if (g_woort_mem_global_context.m_globalcontext_alive)
        woort_mem_global_context_thread_entries_insert(self);
}

void woort_mem_thread_context_deinit(woort_mem_ThreadContext* self)
{
    if (g_woort_mem_global_context.m_globalcontext_alive)
        woort_mem_global_context_thread_entries_remove(self);

    woort_mem_tpc_shutdown_manually(&self->m_thread_page_collection);
}

static WOORT_THREAD_LOCAL woort_mem_ThreadContext t_woort_mem_thread_context;
static WOORT_THREAD_LOCAL woort_mem_ThreadContext* t_woort_mem_thread_context_inited = NULL;

WOORT_NODISCARD woort_mem_ThreadContext* woort_mem_get_thread_context(void)
{
    woort_mem_ThreadContext* const tls_ctx = t_woort_mem_thread_context_inited;
    if (tls_ctx == NULL)
    {
        t_woort_mem_thread_context_inited = &t_woort_mem_thread_context;
        woort_mem_thread_context_init(t_woort_mem_thread_context_inited);

        woort_mem_thread_guard_touch();

        return t_woort_mem_thread_context_inited;
    }
    return tls_ctx;
}

void woort_mem_thread_context_on_exit(void)
{
    woort_mem_ThreadContext* const tls_ctx = t_woort_mem_thread_context_inited;
    if (tls_ctx != NULL)
    {
        woort_mem_thread_context_deinit(tls_ctx);
        t_woort_mem_thread_context_inited = NULL;
    }
}
