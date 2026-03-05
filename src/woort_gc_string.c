#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "woomem.h"
#include "woort_gc_string.h"

const woort_GCUnitProxy g_gcstring_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

const woort_GCString* woort_GCString_make_string(const char* str, size_t len)
{
    woort_GCString* gcstr = woort_GCUnit_alloc_attrib(A, sizeof(woort_GCString) + len + 1);
    gcstr->m_gc_unit.m_proxy = &g_gcstring_unit_proxy;
    gcstr->m_length = len;

    memcpy(gcstr->m_content, str, len);
    gcstr->m_content[len] = '\0';

    return gcstr;
}

const woort_GCString* woort_GCString_add_string(const woort_GCString* a, const woort_GCString* b)
{
    woort_GCString* gcstr = woort_GCUnit_alloc_attrib(A, sizeof(woort_GCString) + a->m_length + b->m_length + 1);
    gcstr->m_gc_unit.m_proxy = &g_gcstring_unit_proxy;
    gcstr->m_length = a->m_length + b->m_length;

    memcpy(gcstr->m_content, a->m_content, a->m_length);
    memcpy(gcstr->m_content + a->m_length, b->m_content, b->m_length);
    gcstr->m_content[a->m_length + b->m_length] = '\0';

    return gcstr;
}

int woort_GCString_compare(const woort_GCString* a, const woort_GCString* b)
{
    const size_t min_len = a->m_length < b->m_length ? a->m_length : b->m_length;
    const int cmp_result = memcmp(a->m_content, b->m_content, min_len);

    if (cmp_result != 0)
        return cmp_result;

    // If prefix matches, shorter string is smaller
    if (a->m_length < b->m_length)
        return -1;
    if (a->m_length > b->m_length)
        return 1;
    return 0;
}