#pragma once

/*
test_mem_chunk_common.h
Shared utilities for woort_mem Chunk tests (pure C11).
*/

#include "woort_mem.h"
#include "woort_mem_chunk.h"
#include "woort_threads.h"
#include "woort_atomic.h"
#include "woort_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   include <unistd.h>
#endif

#define K_PAGE_SIZE WOORT_MEM_NORMAL_PAGE_SIZE

extern int g_failures;

/* ============================================================ */
/* xorshift64 PRNG (per-thread, seeded from context)            */
/* ============================================================ */

typedef struct { uint64_t state; } prng_t;

static inline void prng_seed(prng_t* p, uint64_t seed)
{
    p->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static inline uint32_t prng_u32(prng_t* p)
{
    uint64_t x = p->state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    p->state = x;
    return (uint32_t)(x >> 32);
}

static inline int prng_range(prng_t* p, int lo, int hi)
{
    return lo + (int)(prng_u32(p) % (uint32_t)(hi - lo + 1));
}

static inline void spin_yield(void)
{
    woort_thread_yield();
}

static inline unsigned test_hardware_concurrency(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (unsigned)si.dwNumberOfProcessors;
#elif defined(WOORT_PLATFORM_OS_POSIX)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (unsigned)n : 4u;
#else
    return 4u;
#endif
}

/* ============================================================ */
/* Test macros                                                   */
/* ============================================================ */

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  RUN  %s\n", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("  OK   %s\n", #name); \
    fflush(stdout); \
} while (0)

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
        return; \
    } \
} while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))
