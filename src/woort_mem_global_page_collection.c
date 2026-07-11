/*
woort_mem_global_page_collection.c
*/

#include "woort_mem_global_page_collection.h"

void woort_mem_gpc_init(
    woort_mem_GlobalPageCollection* self, woort_mem_Chunk* chunk)
{
    assert(chunk != NULL && !woort_mem_chunk_is_init_failed(chunk));

    self->m_chunk = chunk;

    for (size_t i = 0; i < WOORT_MEM_MAX_GROUP; ++i)
        woort_mem_free_page_list_init(&self->m_free_pages[i]);
}

void woort_mem_gpc_deinit(woort_mem_GlobalPageCollection* self)
{
    for (size_t i = 0; i < WOORT_MEM_MAX_GROUP; ++i)
        woort_mem_free_page_list_deinit(&self->m_free_pages[i]);
}
