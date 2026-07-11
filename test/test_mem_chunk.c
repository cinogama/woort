/*
test_mem_chunk.c — Chunk allocator unit and concurrency tests.
Ported from woomem/test/test_chunk.cpp to C11.
*/

#include "test_mem_chunk_common.h"

int g_failures = 0;

/* ================================================================ */
/* Single-threaded tests                                            */
/* ================================================================ */

TEST(construct_zero_size)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 0));
    woort_mem_chunk_deinit(&chunk);
}

TEST(construct_small_size)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 100));
    woort_mem_chunk_deinit(&chunk);
}

TEST(construct_non_power_of_two)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 100 * 1024));
    woort_mem_chunk_deinit(&chunk);
}

TEST(construct_exact_power_of_two_pages)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_chunk_deinit(&chunk);
}

TEST(allocate_single_page)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p != NULL);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(allocate_exhaust_all_pages)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 64 * 1024));
    woort_mem_PageHead* pages[2];
    for (int i = 0; i < 2; i++)
    {
        pages[i] = woort_mem_chunk_allocate_page(&chunk);
        CHECK(pages[i] != NULL);
    }
    CHECK(woort_mem_chunk_allocate_page(&chunk) == NULL);
    for (int i = 0; i < 2; i++)
        woort_mem_chunk_free_page(&chunk, pages[i]);
    woort_mem_chunk_deinit(&chunk);
}

TEST(allocate_reuse_same_page_after_free)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p1 = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p1 != NULL);
    woort_mem_chunk_free_page(&chunk, p1);

    woort_mem_PageHead* p2 = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p2 != NULL);
    CHECK_EQ(p1, p2);
    woort_mem_chunk_free_page(&chunk, p2);
    woort_mem_chunk_deinit(&chunk);
}

TEST(allocate_many_pages)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* pages[32];
    for (int i = 0; i < 32; i++)
    {
        pages[i] = woort_mem_chunk_allocate_page(&chunk);
        CHECK(pages[i] != NULL);
    }
    CHECK(woort_mem_chunk_allocate_page(&chunk) == NULL);
    for (int i = 0; i < 32; i++)
        woort_mem_chunk_free_page(&chunk, pages[i]);
    woort_mem_chunk_deinit(&chunk);
}

TEST(allocate_pages_non_overlapping)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* a = woort_mem_chunk_allocate_page(&chunk);
    woort_mem_PageHead* b = woort_mem_chunk_allocate_page(&chunk);
    CHECK(a != NULL);
    CHECK(b != NULL);
    CHECK_NE(a, b);
    woort_mem_chunk_free_page(&chunk, a);
    woort_mem_chunk_free_page(&chunk, b);
    woort_mem_chunk_deinit(&chunk);
}

TEST(huge_page_smaller_than_one_page)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(&chunk, 100);
    CHECK(p != NULL);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(huge_page_exact_one_page)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(&chunk, K_PAGE_SIZE);
    CHECK(p != NULL);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(huge_page_two_pages)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(&chunk, 2 * K_PAGE_SIZE);
    CHECK(p != NULL);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(huge_page_large_allocation)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 4 * 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(&chunk, 2 * 1024 * 1024);
    CHECK(p != NULL);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(huge_page_exceeds_available)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 64 * 1024));
    CHECK(woort_mem_chunk_allocate_huge_page(&chunk, 128 * 1024) == NULL);
    woort_mem_chunk_deinit(&chunk);
}

TEST(validate_nullptr_returns_null)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    CHECK(woort_mem_chunk_validate(&chunk, NULL) == NULL);
    woort_mem_chunk_deinit(&chunk);
}

TEST(validate_outside_range_returns_null)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    int dummy = 42;
    CHECK(woort_mem_chunk_validate(&chunk, &dummy) == NULL);
    woort_mem_chunk_deinit(&chunk);
}

TEST(validate_exact_page_start)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p != NULL);
    CHECK_EQ(woort_mem_chunk_validate(&chunk, p), p);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(validate_interior_of_page)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p != NULL);
    void* interior = (char*)p + 100;
    CHECK_EQ(woort_mem_chunk_validate(&chunk, interior), p);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(validate_freed_returns_null)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p != NULL);
    woort_mem_chunk_free_page(&chunk, p);
    CHECK(woort_mem_chunk_validate(&chunk, p) == NULL);
    woort_mem_chunk_deinit(&chunk);
}

TEST(validate_huge_page_interior)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(&chunk, 4 * K_PAGE_SIZE);
    CHECK(p != NULL);
    void* interior = (char*)p + 3 * K_PAGE_SIZE + 10;
    CHECK_EQ(woort_mem_chunk_validate(&chunk, interior), p);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

TEST(buddy_coalesce_all_to_max_order)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* pages[32];
    for (int i = 0; i < 32; i++)
    {
        pages[i] = woort_mem_chunk_allocate_page(&chunk);
        CHECK(pages[i] != NULL);
    }
    for (int i = 0; i < 32; i++)
        woort_mem_chunk_free_page(&chunk, pages[i]);
    woort_mem_PageHead* huge =
        woort_mem_chunk_allocate_huge_page(&chunk, 32 * K_PAGE_SIZE);
    CHECK(huge != NULL);
    woort_mem_chunk_free_page(&chunk, huge);
    woort_mem_chunk_deinit(&chunk);
}

TEST(buddy_coalesce_to_order_1)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p0 = woort_mem_chunk_allocate_page(&chunk);
    woort_mem_PageHead* p1 = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p0 != NULL);
    CHECK(p1 != NULL);
    woort_mem_chunk_free_page(&chunk, p0);
    woort_mem_chunk_free_page(&chunk, p1);
    woort_mem_PageHead* p2 =
        woort_mem_chunk_allocate_huge_page(&chunk, 2 * K_PAGE_SIZE);
    CHECK(p2 != NULL);
    woort_mem_chunk_free_page(&chunk, p2);
    woort_mem_chunk_deinit(&chunk);
}

TEST(buddy_no_coalesce_when_still_allocated)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    woort_mem_PageHead* p0 = woort_mem_chunk_allocate_page(&chunk);
    woort_mem_PageHead* p1 = woort_mem_chunk_allocate_page(&chunk);
    CHECK(p0 != NULL);
    CHECK(p1 != NULL);
    CHECK_EQ(p1, (woort_mem_PageHead*)((char*)p0 + K_PAGE_SIZE));
    woort_mem_chunk_free_page(&chunk, p1);
    woort_mem_PageHead* p2 =
        woort_mem_chunk_allocate_huge_page(&chunk, 2 * K_PAGE_SIZE);
    CHECK(p2 != NULL);
    CHECK_NE(p2, p0);
    woort_mem_chunk_free_page(&chunk, p0);
    woort_mem_chunk_free_page(&chunk, p2);
    woort_mem_chunk_deinit(&chunk);
}

TEST(multi_chunk_isolation)
{
    woort_mem_Chunk chunk1, chunk2;
    CHECK(woort_mem_chunk_init(&chunk1, 1024 * 1024));
    CHECK(woort_mem_chunk_init(&chunk2, 1024 * 1024));
    woort_mem_PageHead* p1 = woort_mem_chunk_allocate_page(&chunk1);
    woort_mem_PageHead* p2 = woort_mem_chunk_allocate_page(&chunk2);
    CHECK(p1 != NULL);
    CHECK(p2 != NULL);
    CHECK_NE(p1, p2);
    CHECK_EQ(woort_mem_chunk_validate(&chunk1, p1), p1);
    CHECK(woort_mem_chunk_validate(&chunk1, p2) == NULL);
    CHECK_EQ(woort_mem_chunk_validate(&chunk2, p2), p2);
    CHECK(woort_mem_chunk_validate(&chunk2, p1) == NULL);
    woort_mem_chunk_free_page(&chunk1, p1);
    woort_mem_chunk_free_page(&chunk2, p2);
    woort_mem_chunk_deinit(&chunk1);
    woort_mem_chunk_deinit(&chunk2);
}

TEST(alloc_free_alloc_cycle)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    for (int round = 0; round < 10; round++)
    {
        woort_mem_PageHead* a = woort_mem_chunk_allocate_page(&chunk);
        woort_mem_PageHead* b = woort_mem_chunk_allocate_page(&chunk);
        woort_mem_PageHead* c = woort_mem_chunk_allocate_page(&chunk);
        CHECK(a != NULL);
        CHECK(b != NULL);
        CHECK(c != NULL);
        woort_mem_chunk_free_page(&chunk, a);
        woort_mem_chunk_free_page(&chunk, b);
        woort_mem_chunk_free_page(&chunk, c);
    }
    woort_mem_chunk_deinit(&chunk);
}

TEST(huge_page_boundary_case)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 1024 * 1024));
    size_t sz = K_PAGE_SIZE + 1;
    woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(&chunk, sz);
    CHECK(p != NULL);
    CHECK_EQ(woort_mem_chunk_validate(&chunk, p), p);
    woort_mem_chunk_free_page(&chunk, p);
    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Concurrent tests                                                 */
/* ================================================================ */

/* --- concurrent_alloc_free_128_pages --- */

#define CAF_K_PAGES   128
#define CAF_K_ALLOC   4
#define CAF_K_FREE    2

typedef struct {
    woort_mem_Chunk*  chunk;
    woort_AtomicPtr*  slots;
    woort_AtomicInt32* next_idx;
    woort_AtomicInt32* alloc_count;
    woort_AtomicInt32* free_count;
    woort_AtomicUInt8* done;
    int role; /* 0=alloc, 1=free */
} CAFCtx;

static void caf_worker(void* user_data)
{
    CAFCtx* ctx = (CAFCtx*)user_data;

    if (ctx->role == 0)
    {
        for (;;)
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (!p)
            {
                if (woort_atomic_load_explicit(
                    ctx->done, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
                    return;
                continue;
            }
            int slot = (int)woort_atomic_fetch_add_explicit(
                ctx->next_idx, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            if (slot >= CAF_K_PAGES)
            {
                woort_mem_chunk_free_page(ctx->chunk, p);
                woort_atomic_store_explicit(
                    ctx->done, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
                return;
            }
            woort_atomic_store_explicit(
                &ctx->slots[slot], p, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
            woort_atomic_fetch_add_explicit(
                ctx->alloc_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
    }
    else
    {
        for (;;)
        {
            bool any_freed = false;
            for (int i = 0; i < CAF_K_PAGES; i++)
            {
                void* old = woort_atomic_exchange_explicit(
                    &ctx->slots[i], NULL, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);
                woort_mem_PageHead* p = (woort_mem_PageHead*)old;
                if (p)
                {
                    CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
                    woort_mem_chunk_free_page(ctx->chunk, p);
                    woort_atomic_fetch_add_explicit(
                        ctx->free_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
                    any_freed = true;
                }
            }
            if (!any_freed
                && woort_atomic_load_explicit(
                    ctx->done, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE)
                && woort_atomic_load_explicit(
                    ctx->alloc_count, WOORT_ATOMIC_MEMORY_ORDER_RELAXED)
                    >= CAF_K_PAGES)
                return;
        }
    }
}

TEST(concurrent_alloc_free_128_pages)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 4 * 1024 * 1024));

    woort_AtomicPtr slots[CAF_K_PAGES];
    for (int i = 0; i < CAF_K_PAGES; i++)
        woort_atomic_init(&slots[i], NULL);

    woort_AtomicInt32 next_idx, alloc_count, free_count;
    woort_AtomicUInt8 done;
    woort_atomic_init(&next_idx, 0);
    woort_atomic_init(&alloc_count, 0);
    woort_atomic_init(&free_count, 0);
    woort_atomic_init(&done, 0);

    CAFCtx ctxs[CAF_K_ALLOC + CAF_K_FREE];
    woort_Thread* threads[CAF_K_ALLOC + CAF_K_FREE];

    for (int i = 0; i < CAF_K_ALLOC; i++)
    {
        ctxs[i] = (CAFCtx){
            .chunk = &chunk, .slots = slots,
            .next_idx = &next_idx, .alloc_count = &alloc_count,
            .free_count = &free_count, .done = &done,
            .role = 0,
        };
        woort_thread_start(caf_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < CAF_K_FREE; i++)
    {
        int idx = CAF_K_ALLOC + i;
        ctxs[idx] = (CAFCtx){
            .chunk = &chunk, .slots = slots,
            .next_idx = &next_idx, .alloc_count = &alloc_count,
            .free_count = &free_count, .done = &done,
            .role = 1,
        };
        woort_thread_start(caf_worker, &ctxs[idx], &threads[idx]);
    }

    for (int i = 0; i < CAF_K_ALLOC + CAF_K_FREE; i++)
        woort_thread_join(threads[i]);

    CHECK(woort_atomic_load_explicit(
        &alloc_count, WOORT_ATOMIC_MEMORY_ORDER_RELAXED) >= CAF_K_PAGES);

    woort_mem_chunk_deinit(&chunk);
}

/* --- concurrent_mixed_alloc_free --- */

#define CMAF_K_ITERS   500
#define CMAF_K_THREADS 4

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* ops;
} CMAFCtx;

static void cmaf_worker(void* user_data)
{
    CMAFCtx* ctx = (CMAFCtx*)user_data;
    for (int i = 0; i < CMAF_K_ITERS; i++)
    {
        woort_mem_PageHead* a = woort_mem_chunk_allocate_page(ctx->chunk);
        if (!a)
            continue;

        woort_mem_PageHead* b = woort_mem_chunk_allocate_page(ctx->chunk);
        if (!b)
        {
            woort_mem_chunk_free_page(ctx->chunk, a);
            continue;
        }

        CHECK_NE(a, b);
        CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, a), a);
        CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, b), b);

        woort_mem_chunk_free_page(ctx->chunk, a);
        woort_mem_chunk_free_page(ctx->chunk, b);
        woort_atomic_fetch_add_explicit(
            ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }
}

TEST(concurrent_mixed_alloc_free)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 2 * 1024 * 1024));

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    CMAFCtx ctxs[CMAF_K_THREADS];
    woort_Thread* threads[CMAF_K_THREADS];

    for (int i = 0; i < CMAF_K_THREADS; i++)
    {
        ctxs[i] = (CMAFCtx){ .chunk = &chunk, .ops = &ops };
        woort_thread_start(cmaf_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < CMAF_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK(woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED) > 0);

    woort_mem_chunk_deinit(&chunk);
}

/* --- concurrent_huge_page_and_validate --- */

typedef struct {
    woort_mem_Chunk* chunk;
    int role; /* 0=alloc, 1=huge */
} CHFCtx;

static void chf_worker(void* user_data)
{
    CHFCtx* ctx = (CHFCtx*)user_data;

    if (ctx->role == 0)
    {
        for (int i = 0; i < 200; i++)
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (p)
            {
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
                woort_mem_chunk_free_page(ctx->chunk, p);
            }
            else
                break;
        }
    }
    else
    {
        for (int i = 0; i < 50; i++)
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(
                ctx->chunk, 2 * K_PAGE_SIZE);
            if (p)
            {
                void* interior = (char*)p + K_PAGE_SIZE + 100;
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, interior), p);
                woort_mem_chunk_free_page(ctx->chunk, p);
            }
            else
                break;
        }
    }
}

TEST(concurrent_huge_page_and_validate)
{
    woort_mem_Chunk chunk;
    CHECK(woort_mem_chunk_init(&chunk, 4 * 1024 * 1024));

    CHFCtx ctxs[4];
    woort_Thread* threads[4];

    for (int i = 0; i < 3; i++)
    {
        ctxs[i] = (CHFCtx){ .chunk = &chunk, .role = 0 };
        woort_thread_start(chf_worker, &ctxs[i], &threads[i]);
    }
    ctxs[3] = (CHFCtx){ .chunk = &chunk, .role = 1 };
    woort_thread_start(chf_worker, &ctxs[3], &threads[3]);

    for (int i = 0; i < 4; i++)
        woort_thread_join(threads[i]);

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* main                                                            */
/* ================================================================ */

int main(void)
{
    printf("=== Chunk Tests ===\n\n");

    RUN_TEST(construct_zero_size);
    RUN_TEST(construct_small_size);
    RUN_TEST(construct_non_power_of_two);
    RUN_TEST(construct_exact_power_of_two_pages);
    RUN_TEST(allocate_single_page);
    RUN_TEST(allocate_exhaust_all_pages);
    RUN_TEST(allocate_reuse_same_page_after_free);
    RUN_TEST(allocate_many_pages);
    RUN_TEST(allocate_pages_non_overlapping);
    RUN_TEST(huge_page_smaller_than_one_page);
    RUN_TEST(huge_page_exact_one_page);
    RUN_TEST(huge_page_two_pages);
    RUN_TEST(huge_page_large_allocation);
    RUN_TEST(huge_page_exceeds_available);
    RUN_TEST(validate_nullptr_returns_null);
    RUN_TEST(validate_outside_range_returns_null);
    RUN_TEST(validate_exact_page_start);
    RUN_TEST(validate_interior_of_page);
    RUN_TEST(validate_freed_returns_null);
    RUN_TEST(validate_huge_page_interior);
    RUN_TEST(buddy_coalesce_all_to_max_order);
    RUN_TEST(buddy_coalesce_to_order_1);
    RUN_TEST(buddy_no_coalesce_when_still_allocated);
    RUN_TEST(multi_chunk_isolation);
    RUN_TEST(alloc_free_alloc_cycle);
    RUN_TEST(huge_page_boundary_case);
    RUN_TEST(concurrent_alloc_free_128_pages);
    RUN_TEST(concurrent_mixed_alloc_free);
    RUN_TEST(concurrent_huge_page_and_validate);

    printf("\n=== %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
