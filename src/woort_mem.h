#pragma once

/*
woort_mem.h
Public API for the GC-enabled memory allocator (formerly woomem).
*/

#ifdef __cplusplus
#   include <cstddef>
#   include <cstdint>
extern "C"{
#else
#   include <stddef.h>
#   include <stdint.h>
#   include <stdbool.h>
#endif

extern uint8_t  woort_mem_gc_marking_round_counter;
extern bool     woort_mem_gc_marking_state_flag;
extern size_t   woort_mem_gc_memory_size_after_last_round_sweep;

typedef enum woort_mem_Attrib
{
    WOORT_MEM_ATTRIB_NEED_SWEEP    = 1,
    WOORT_MEM_ATTRIB_AUTO_MARK     = 1 << 1,
    WOORT_MEM_ATTRIB_MARK_CALLBACK = 1 << 2,
    WOORT_MEM_ATTRIB_FREE_CALLBACK = 1 << 3,

} woort_mem_Attrib;

typedef void (*woort_mem_MarkCallback)(void*);
typedef void (*woort_mem_FreeCallback)(void*);
typedef void (*woort_mem_GCCallback)(void);
typedef void (*woort_mem_GCMainThreadEntryCallback)(void);
typedef void (*woort_mem_GCWorkerThreadEntryCallback)(void);

bool woort_mem_init(
    size_t reserved_chunk_size,
    woort_mem_GCCallback gc_callback_at_begin,
    woort_mem_GCCallback gc_callback_at_stop_marking,
    woort_mem_MarkCallback mark_callback,
    woort_mem_FreeCallback free_callback,
    woort_mem_GCMainThreadEntryCallback main_entry_callback,
    woort_mem_GCWorkerThreadEntryCallback worker_entry_callback);
void woort_mem_shutdown(void);

void woort_mem_trigger_gc(bool async);

/* OPTIONAL */ void* woort_mem_allocate_begin(size_t size);
void woort_mem_allocate_end(void* p, int attrib);
void woort_mem_allocate_end_as_root(void* p, int attrib);
void woort_mem_remove_from_root_set(void* p);

/* Do not reallocate a root; the old unit will not be released. */
/* OPTIONAL */ void* woort_mem_reallocate(void* ptr, size_t size);

/* OPTIONAL */ void* woort_mem_validate_addr(/* OPTIONAL */ void* ptr_may_invalid);
/* OPTIONAL */ void* woort_mem_validate_addr_head(/* OPTIONAL */ void* ptr_may_invalid);
size_t woort_mem_get_capacity_of_addr_head(void* ptr);

void woort_mem_mark_unit_head(/* OPTIONAL */ void* ptr_head_may_null);
void woort_mem_mark_fuzzy_unit(/* OPTIONAL */ void* ptr_may_invalid_or_null);
void woort_mem_mark_fuzzy_unit_head(/* OPTIONAL */ void* ptr_head_may_invalid_null);

void woort_mem_mark_root_unit_head(/* OPTIONAL */ void* ptr_head_may_null);
void woort_mem_mark_root_fuzzy_unit(/* OPTIONAL */ void* ptr_may_invalid_or_null);
void woort_mem_mark_root_fuzzy_unit_head(/* OPTIONAL */ void* ptr_head_may_invalid_null);

#ifdef __cplusplus
}
#endif
