#pragma once

/*
woort_mem_global_context.h
Global singleton managing Chunk + GlobalPageCollection + thread registry.
*/

#include "woort_mem_chunk.h"
#include "woort_mem_global_page_collection.h"
#include "woort_mem_page.h"
#include "woort_spin.h"
#include "woort_hashmap.h"
#include "woort_atomic.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct woort_mem_ThreadContext;
struct woort_mem_GC;

typedef struct woort_mem_GlobalContext
{
    bool            m_globalcontext_alive;
    bool            m_globalcontext_inited;
    bool            m_thread_entries_inited;

    woort_Spinlock  m_thread_entries_mx;
    woort_HashMap   m_thread_entries;

    woort_AtomicPtr m_all_page_list;

    woort_mem_Chunk                m_chunk;
    woort_mem_GlobalPageCollection m_gpc;

} woort_mem_GlobalContext;

extern woort_mem_GlobalContext g_woort_mem_global_context;

void woort_mem_global_context_ensure_thread_entries_init(void);
void woort_mem_global_context_thread_entries_insert(struct woort_mem_ThreadContext* ctx);
void woort_mem_global_context_thread_entries_remove(struct woort_mem_ThreadContext* ctx);

WOORT_NODISCARD bool woort_mem_global_context_init(size_t reserved_chunk_size);
void woort_mem_global_context_shutdown(void);
void woort_mem_global_context_deinit(void);

void woort_mem_global_context_add_new_page_into_chain(woort_mem_PageHead* page);
void woort_mem_global_context_add_page_back_to_into_chain(woort_mem_PageHead* page);
WOORT_NODISCARD woort_mem_PageHead* woort_mem_global_context_allocate_huge_page(size_t size);

/* Called by GC constructor/destructor to update thread marking contexts. */
void woort_mem_global_context_assign_marking_contexts(struct woort_mem_GC* gc);
void woort_mem_global_context_clear_marking_contexts(void);
