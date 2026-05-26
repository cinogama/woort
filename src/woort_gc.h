#pragma once

/*
woort_gc.h
*/

#include "woomem.h"
#include "woort_diagnosis.h"
#include "woort_vm.h"
#include "woort_threads.h"
#include "woort_value.h"

#include <stdbool.h>

WOORT_NODISCARD bool woort_GC_bootup(size_t reserving_memory_size);
void woort_GC_shutdown(void);

void _woort_GC_debug_callback_all_vm(void);

WOORT_NODISCARD bool woort_GC_register_root_vm(
    struct woort_VMRuntime* vmruntime);
void woort_GC_unregister_root_vm(
    struct woort_VMRuntime* vmruntime);

typedef bool (*woort_GC_ForeachRootVMCallback)(woort_VMRuntime* vm, void* user_data);

void woort_GC_foreach_root_vm(
    woort_GC_ForeachRootVMCallback callback,
    void* user_data);

static inline void woort_GC_mixed_write_barrier_gcaddr(
    void** modified_unit_addr, void* src_unit)
{
    if (woomem_gc_marking_state_flag)
    {
        woomem_mark_fuzzy_unit(src_unit);
        woomem_mark_fuzzy_unit(*modified_unit_addr);
    }
    *modified_unit_addr = src_unit;
}

static inline void woort_GC_mixed_write_barrier_gcunit(
    void** modified_unit_addr, void* src_unit)
{
    if (woomem_gc_marking_state_flag)
    {
        woomem_mark_unit_head(src_unit);
        woomem_mark_fuzzy_unit_head(*modified_unit_addr);
    }
    *modified_unit_addr = src_unit;
}

static inline void woort_GC_mixed_write_barrier_value(
    woort_Value* modified_value, woort_Value src_value)
{
    if (woomem_gc_marking_state_flag)
    {
        woomem_mark_fuzzy_unit(src_value.m_gcinstance);
        woomem_mark_fuzzy_unit(modified_value->m_gcinstance);
    }
    *modified_value = src_value;
}

static inline void woort_GC_mixed_write_barrier_dynbox(
    woort_DynBox* modified_box, woort_DynBox src_box)
{
    if (woomem_gc_marking_state_flag)
    {
        if (src_box.m_boxed != 0
            && 0 == (src_box.m_boxed & 0b0111))
        {
            woomem_mark_unit_head(_woort_boxed_to_gcunit(src_box.m_boxed));
        }
        if (modified_box->m_boxed != 0
            && 0 == (modified_box->m_boxed & 0b0111))
        {
            woomem_mark_fuzzy_unit_head(_woort_boxed_to_gcunit(modified_box->m_boxed));
        }
    }
    *modified_box = src_box;
}

static inline void woort_GC_delete_barrier_gcaddr(
    void* addr)
{
    if (woomem_gc_marking_state_flag)
        woomem_mark_fuzzy_unit(addr);
}

static inline void woort_GC_delete_barrier_gcunit(
    void* unit)
{
    if (woomem_gc_marking_state_flag)
        woomem_mark_fuzzy_unit_head(unit);
}

static inline void woort_GC_delete_barrier_value(
    woort_Value value)
{
    if (woomem_gc_marking_state_flag)
        woomem_mark_fuzzy_unit(value.m_gcinstance);
}

static inline void woort_GC_delete_barrier_dynbox(
    woort_DynBox box)
{
    if (woomem_gc_marking_state_flag
        && box.m_boxed != 0 
        && 0 == (box.m_boxed & 0b0111))
    {
        woomem_mark_fuzzy_unit_head(_woort_boxed_to_gcunit(box.m_boxed));
    }
}

static inline void woort_GC_init_write_barrier_gcaddr(
    void** modified_unit_addr, void* src_unit)
{
    if (woomem_gc_marking_state_flag)
    {
        woomem_mark_fuzzy_unit(src_unit);
    }
    *modified_unit_addr = src_unit;
}

static inline void woort_GC_init_write_barrier_gcunit(
    void** modified_unit_addr, void* src_unit)
{
    if (woomem_gc_marking_state_flag)
    {
        woomem_mark_unit_head(src_unit);
    }
    *modified_unit_addr = src_unit;
}

static inline void woort_GC_init_write_barrier_value(
    woort_Value* modified_value, woort_Value src_value)
{
    if (woomem_gc_marking_state_flag)
    {
        woomem_mark_fuzzy_unit(src_value.m_gcinstance);
    }
    *modified_value = src_value;
}

static inline void woort_GC_init_write_barrier_dynbox(
    woort_DynBox* modified_box, woort_DynBox src_box)
{
    if (woomem_gc_marking_state_flag)
    {
        if (src_box.m_boxed != 0
            && 0 == (src_box.m_boxed & 0b0111))
        {
            woomem_mark_unit_head(_woort_boxed_to_gcunit(src_box.m_boxed));
        }
    }
    *modified_box = src_box;
}