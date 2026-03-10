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

inline void woort_GC_barrier_mark(intptr_t may_valid_ptr)
{
    if (g_gc_in_marking)
        woomem_try_mark_unit(may_valid_ptr);
}

inline void woort_GC_barrier_mark_value(woort_Value value)
{
    woort_GC_barrier_mark((intptr_t)value.m_gcinstance);
}

inline void woort_GC_barrier_mark_dynbox(woort_DynBox box)
{
    if (g_gc_in_marking && 0 == (box.m_boxed & 0b0111))
        woomem_mark_unit_head(box.m_boxed_gc_unit);
}
