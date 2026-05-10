#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "woort_util.h"
#include "woort_platform.h"

WOORT_NODISCARD size_t woort_util_abs_diff(
    size_t a,
    size_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

WOORT_NODISCARD size_t woort_util_ptr_hash(const void* ptr_addr)
{
    size_t hash = (size_t)(intptr_t)*(void**)ptr_addr;

#ifdef WOORT_PLATFORM_64
    /* Murmur3 64-bit finalizer */
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
#else
    /* Murmur3 32-bit finalizer */
    hash ^= hash >> 16;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35U;
    hash ^= hash >> 16;
#endif

    return hash;
}

WOORT_NODISCARD bool woort_util_ptr_equal(
    const void* ptr_a_addr, const void* ptr_b_addr)
{
    return *(void**)ptr_a_addr == *(void**)ptr_b_addr;
}

WOORT_NODISCARD size_t woort_hash_string(const char* str, size_t len)
{
    const unsigned char* ptr = (const unsigned char*)str;
    const unsigned char* const end = ptr + len;

#ifdef WOORT_PLATFORM_64
    /* FNV-1a 64-bit */
    size_t hash = 14695981039346656037ULL;
    while (ptr < end)
    {
        hash ^= (size_t)*ptr++;
        hash *= 1099511628211ULL;
    }
#else
    /* FNV-1a 32-bit */
    size_t hash = 2166136261U;
    while (ptr < end)
    {
        hash ^= (size_t)*ptr++;
        hash *= 16777619U;
    }
#endif

    return hash;
}

WOORT_NODISCARD size_t woort_util_cstr_hash(const void* key)
{
    const char* str = *(const char* const*)key;
    return woort_hash_string(str, strlen(str));
}

WOORT_NODISCARD bool woort_util_cstr_equal(
    const void* key1,
    const void* key2)
{
    const char* s1 = *(const char* const*)key1;
    const char* s2 = *(const char* const*)key2;
    return strcmp(s1, s2) == 0;
}