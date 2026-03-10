#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "woomem.h"
#include "woort_gc_string.h"

const woort_GCUnitProxy g_gcstring_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(const char* str, size_t len)
{
    woort_GCString* const gcstr = 
        woort_GCUnit_alloc_attrib(O, sizeof(woort_GCString) + len + 1);

    gcstr->m_gc_unit.m_proxy = &g_gcstring_unit_proxy;
    gcstr->m_length = len;

    memcpy(gcstr->m_content, str, len);
    gcstr->m_content[len] = '\0';

    return gcstr;
}

WOORT_NODISCARD const woort_GCString* woort_GCString_add_string(const woort_GCString* a, const woort_GCString* b)
{
    woort_GCString* const gcstr = 
        woort_GCUnit_alloc_attrib(O, sizeof(woort_GCString) + a->m_length + b->m_length + 1);

    gcstr->m_gc_unit.m_proxy = &g_gcstring_unit_proxy;
    gcstr->m_length = a->m_length + b->m_length;

    memcpy(gcstr->m_content, a->m_content, a->m_length);
    memcpy(gcstr->m_content + a->m_length, b->m_content, b->m_length);
    gcstr->m_content[a->m_length + b->m_length] = '\0';

    return gcstr;
}

WOORT_NODISCARD int woort_GCString_compare(const woort_GCString* a, const woort_GCString* b)
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

WOORT_NODISCARD size_t woort_GCString_hash(const woort_GCString* str)
{
    // FNV-1a hash algorithm
    size_t hash = 14695981039346656037ULL; // FNV offset basis
    const unsigned char* ptr = (const unsigned char*)str->m_content;
    const unsigned char* end = ptr + str->m_length;

    while (ptr < end)
    {
        hash ^= (size_t)*ptr++;
        hash *= 1099511628211ULL; // FNV prime
    }

    return hash;
}