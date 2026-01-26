#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "woort_util.h"

WOORT_NODISCARD size_t woort_util_abs_diff(
    size_t a,
    size_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

WOORT_NODISCARD size_t woort_util_ptr_hash(const void* ptr_addr)
{
    size_t hash = (size_t)(intptr_t)*(void**)ptr_addr;
    
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    
    return hash;
}

WOORT_NODISCARD bool woort_util_ptr_equal(
    const void* ptr_a_addr, const void* ptr_b_addr)
{
    return *(void**)ptr_a_addr == *(void**)ptr_b_addr;
}
