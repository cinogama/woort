#include "woort_value.h"

typedef struct woort_GCClosure
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    
    woort_Value m_captured[];

}woort_GCClosure;

extern const woort_GCUnitProxy g_gcclosure_unit_proxy;

woort_GCClosure* woort_GCClosure_make_closure(size_t captured_count);
