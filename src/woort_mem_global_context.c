/*
woort_mem_global_context.c
*/

#include "woort_mem_global_context.h"
#include "woort_mem_thread_context.h"
#include "woort_util.h"

#include <stdlib.h>
#include <assert.h>

woort_mem_GlobalContext g_woort_mem_global_context;

void woort_mem_global_context_thread_entries_insert(
    woort_mem_ThreadContext* ctx)
{
    woort_spinlock_lock(
        &g_woort_mem_global_context.m_thread_entries_mx);
    {
        (void)woort_hashmap_insert(
            &g_woort_mem_global_context.m_thread_entries,
            &ctx, NULL);
    }
    woort_spinlock_unlock(
        &g_woort_mem_global_context.m_thread_entries_mx);
}

void woort_mem_global_context_thread_entries_remove(
    woort_mem_ThreadContext* ctx)
{
    woort_spinlock_lock(
        &g_woort_mem_global_context.m_thread_entries_mx);
    {
        (void)woort_hashmap_remove(
            &g_woort_mem_global_context.m_thread_entries,
            &ctx);
    }
    woort_spinlock_unlock(
        &g_woort_mem_global_context.m_thread_entries_mx);
}

typedef struct _woort_mem_init_te_ctx
{
    woort_mem_GlobalPageCollection* gpc;
} _woort_mem_init_te_ctx;

WOORT_NODISCARD static bool _woort_mem_init_te_callback(
    const void* key, void* value, void* user_data)
{
    (void)value;
    woort_mem_ThreadContext* thread_ctx =
        *(woort_mem_ThreadContext* const*)key;
    _woort_mem_init_te_ctx* ctx =
        (_woort_mem_init_te_ctx*)user_data;

    woort_mem_tpc_init_manually(
        &thread_ctx->m_thread_page_collection,
        ctx->gpc);
    return true;
}

typedef struct _woort_mem_shutdown_te_ctx
{
    int unused;
} _woort_mem_shutdown_te_ctx;

WOORT_NODISCARD static bool _woort_mem_shutdown_te_callback(
    const void* key, void* value, void* user_data)
{
    (void)value;
    (void)user_data;
    woort_mem_ThreadContext* thread_ctx =
        *(woort_mem_ThreadContext* const*)key;

    woort_mem_tpc_shutdown_manually(
        &thread_ctx->m_thread_page_collection);

    /* reset tls ptr when global shutdown. */
    *thread_ctx->m_this_tls_storage_ptr = NULL;

    return true;
}

WOORT_NODISCARD bool woort_mem_global_context_init(size_t reserved_chunk_size)
{
    assert(NULL == woort_atomic_load_explicit(
        &g_woort_mem_global_context.m_all_page_list,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    if (!woort_mem_chunk_init(
        &g_woort_mem_global_context.m_chunk, reserved_chunk_size))
    {
        woort_mem_chunk_deinit(&g_woort_mem_global_context.m_chunk);
        return false;
    }

    woort_mem_gpc_init(
        &g_woort_mem_global_context.m_gpc,
        &g_woort_mem_global_context.m_chunk);

    woort_spinlock_init(&g_woort_mem_global_context.m_thread_entries_mx);
    woort_hashmap_init(
        &g_woort_mem_global_context.m_thread_entries,
        sizeof(woort_mem_ThreadContext*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    return true;
}

void woort_mem_global_context_deinit(void)
{
    woort_spinlock_lock(
        &g_woort_mem_global_context.m_thread_entries_mx);
    {
        _woort_mem_shutdown_te_ctx ctx = { .unused = 0 };
        (void)woort_hashmap_foreach(
            &g_woort_mem_global_context.m_thread_entries,
            _woort_mem_shutdown_te_callback,
            &ctx);
    }
    woort_spinlock_unlock(
        &g_woort_mem_global_context.m_thread_entries_mx);

    woort_mem_gpc_deinit(&g_woort_mem_global_context.m_gpc);
    woort_mem_chunk_deinit(&g_woort_mem_global_context.m_chunk);

    woort_atomic_store_explicit(
        &g_woort_mem_global_context.m_all_page_list,
        NULL,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    woort_hashmap_deinit(
        &g_woort_mem_global_context.m_thread_entries);
    woort_spinlock_deinit(
        &g_woort_mem_global_context.m_thread_entries_mx);
}

void woort_mem_global_context_add_new_page_into_chain(
    woort_mem_PageHead* page)
{
    assert(woort_atomic_load_explicit(
        &page->m_page_just_allocated,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_atomic_store_explicit(
        &page->m_page_just_allocated,
        false,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    woort_mem_global_context_add_page_back_to_into_chain(page);
}

void woort_mem_global_context_add_page_back_to_into_chain(
    woort_mem_PageHead* page)
{
    void* expected = woort_atomic_load_explicit(
        &g_woort_mem_global_context.m_all_page_list,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    page->m_next_page = (woort_mem_PageHead*)expected;

    while (!woort_atomic_compare_exchange_weak_explicit(
        &g_woort_mem_global_context.m_all_page_list,
        &expected,
        page,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
    {
        page->m_next_page = (woort_mem_PageHead*)expected;
    }
}

WOORT_NODISCARD woort_mem_PageHead* woort_mem_global_context_allocate_huge_page(
    size_t size)
{
    return woort_mem_chunk_allocate_huge_page(
        &g_woort_mem_global_context.m_chunk, size);
}
