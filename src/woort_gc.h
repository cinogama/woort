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

void woort_GC_bootup(void);
void woort_GC_shutdown(void);

WOORT_NODISCARD bool woort_GC_register_root_vm(
    struct woort_VMRuntime* vmruntime);
void woort_GC_unregister_root_vm(
    struct woort_VMRuntime* vmruntime);

static inline void woort_GC_mixed_write_barrier_gcaddr(
    const void** modified_unit_addr, const void* src_unit)
{
    if (g_gc_in_marking)
    {
        woomem_try_mark_unit((intptr_t)src_unit);
        woomem_try_mark_unit((intptr_t)*modified_unit_addr);
    }
    *modified_unit_addr = src_unit;
}

static inline void woort_GC_mixed_write_barrier_gcunit(
    const void** modified_unit_addr, const void* src_unit)
{
    if (g_gc_in_marking)
    {
        woomem_mark_unit_head(src_unit);
        woomem_try_mark_unit_head((intptr_t)*modified_unit_addr);
    }
    *modified_unit_addr = src_unit;
}

static inline void woort_GC_mixed_write_barrier_value(
    woort_Value* modified_value, woort_Value src_value)
{
    if (g_gc_in_marking)
    {
        woomem_try_mark_unit((intptr_t)src_value.m_gcinstance);
        woomem_try_mark_unit((intptr_t)modified_value->m_gcinstance);
    }
    *modified_value = src_value;
}

static inline void woort_GC_mixed_write_barrier_dynbox(
    woort_DynBox* modified_box, woort_DynBox src_box)
{
    if (g_gc_in_marking)
    {
        if (src_box.m_boxed_gc_unit != NULL
            && 0 == (src_box.m_boxed & 0b0111))
        {
            woomem_mark_unit_head(src_box.m_boxed_gc_unit);
        }
        if (modified_box->m_boxed_gc_unit != NULL
            && 0 == (modified_box->m_boxed & 0b0111))
            woomem_try_mark_unit_head((intptr_t)modified_box->m_boxed_gc_unit);
    }
    *modified_box = src_box;
}

static inline void woort_GC_delete_barrier_value(
    woort_Value value)
{
    if (g_gc_in_marking)
        woomem_try_mark_unit((intptr_t)value.m_gcinstance);
}

static inline void woort_GC_delete_barrier_dynbox(
    woort_DynBox box)
{
    if (box.m_boxed_gc_unit != NULL
        && g_gc_in_marking && 0 == (box.m_boxed & 0b0111))
    {
        woomem_mark_unit_head(box.m_boxed_gc_unit);
    }
}
