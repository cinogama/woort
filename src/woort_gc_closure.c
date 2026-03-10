#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "woomem.h"
#include "woort_gc_closure.h"

const woort_GCUnitProxy g_gcclosure_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

woort_GCClosure* woort_GCClosure_new(
    woort_RuntimeFunction func, size_t captured_count)
{
    woort_GCClosure* const gcclosure = woort_GCUnit_alloc_attrib(
        A,
        sizeof(woort_GCClosure)
        + captured_count * sizeof(woort_Value));

    gcclosure->m_gc_unit.m_proxy = &g_gcclosure_unit_proxy;
    gcclosure->m_func = func;
    gcclosure->m_size = captured_count;

    return gcclosure;    
}