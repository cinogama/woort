/*
test_mem_chunk_parallel.c — Parallel stress tests for Chunk allocator.
Ported from woomem/test/test_chunk_parallel.cpp to C11.
*/

#include "test_mem_chunk_common.h"
#include "woort_spin.h"
#include "woort_hashmap.h"
#include "woort_util.h"

#include <time.h>

int g_failures = 0;

/* ================================================================ */
/* BoundedQueue for producer_consumer_pattern                      */
/* ================================================================ */

typedef struct {
    woort_mem_PageHead** buf;
    int head, tail, count, cap;
    bool closed;
    woort_Mutex* mtx;
    woort_ConditionVariable* not_empty;
    woort_ConditionVariable* not_full;
} BoundedQueue;

static void bq_init(BoundedQueue* q, int cap)
{
    q->buf = (woort_mem_PageHead**)calloc((size_t)cap, sizeof(woort_mem_PageHead*));
    q->head = q->tail = q->count = 0;
    q->cap = cap;
    q->closed = false;
    woort_mutex_create(&q->mtx);
    woort_condition_variable_create(&q->not_empty);
    woort_condition_variable_create(&q->not_full);
}

static void bq_deinit(BoundedQueue* q)
{
    woort_condition_variable_destroy(q->not_empty);
    woort_condition_variable_destroy(q->not_full);
    woort_mutex_destroy(q->mtx);
    free(q->buf);
}

static void bq_push(BoundedQueue* q, woort_mem_PageHead* p)
{
    woort_mutex_lock(q->mtx);
    while (q->count >= q->cap)
        woort_condition_variable_wait(q->not_full, q->mtx);
    q->buf[q->tail] = p;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    woort_condition_variable_signal(q->not_empty);
    woort_mutex_unlock(q->mtx);
}

static bool bq_pop(BoundedQueue* q, woort_mem_PageHead** out)
{
    woort_mutex_lock(q->mtx);
    while (q->count == 0 && !q->closed)
        woort_condition_variable_wait(q->not_empty, q->mtx);
    if (q->count == 0)
    {
        woort_mutex_unlock(q->mtx);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    woort_condition_variable_signal(q->not_full);
    woort_mutex_unlock(q->mtx);
    return true;
}

static void bq_close(BoundedQueue* q)
{
    woort_mutex_lock(q->mtx);
    q->closed = true;
    woort_condition_variable_broadcast(q->not_empty);
    woort_mutex_unlock(q->mtx);
}

/* ================================================================ */
/* Test 1: massive_parallel_alloc_free                              */
/* ================================================================ */

#define T1_K_THREADS 16
#define T1_K_ITERS   2000

typedef struct {
    woort_mem_Chunk* chunk;
    int tid;
    woort_AtomicInt32* total_ops;
} T1Ctx;

static void t1_worker(void* user_data)
{
    T1Ctx* ctx = (T1Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, (uint64_t)(ctx->tid + 1000));

    for (int i = 0; i < T1_K_ITERS; i++)
    {
        if (prng_range(&rng, 0, 1) == 0)
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (p)
            {
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
                woort_mem_chunk_free_page(ctx->chunk, p);
                woort_atomic_fetch_add_explicit(
                    ctx->total_ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            }
        }
        else
        {
            woort_mem_PageHead* a = woort_mem_chunk_allocate_page(ctx->chunk);
            woort_mem_PageHead* b = woort_mem_chunk_allocate_page(ctx->chunk);
            if (a && b)
            {
                CHECK_NE(a, b);
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, a), a);
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, b), b);
                woort_mem_chunk_free_page(ctx->chunk, a);
                woort_mem_chunk_free_page(ctx->chunk, b);
                woort_atomic_fetch_add_explicit(
                    ctx->total_ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            }
            else
            {
                if (a) woort_mem_chunk_free_page(ctx->chunk, a);
                if (b) woort_mem_chunk_free_page(ctx->chunk, b);
            }
        }
    }
}

TEST(massive_parallel_alloc_free)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 32 * 1024 * 1024);

    woort_AtomicInt32 total_ops;
    woort_atomic_init(&total_ops, 0);

    T1Ctx ctxs[T1_K_THREADS];
    woort_Thread* threads[T1_K_THREADS];

    for (int i = 0; i < T1_K_THREADS; i++)
    {
        ctxs[i] = (T1Ctx){ .chunk = &chunk, .tid = i, .total_ops = &total_ops };
        woort_thread_start(t1_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T1_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&total_ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED),
             T1_K_THREADS * T1_K_ITERS / 4);
    printf("    total_ops=%d\n",
        (int)woort_atomic_load_explicit(&total_ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 2: multi_chunk_parallel_isolated                            */
/* ================================================================ */

#define T2_K_CHUNKS         4
#define T2_K_THREADS_PER    4
#define T2_K_ITERS          1000

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* ops;
} T2Ctx;

static void t2_worker(void* user_data)
{
    T2Ctx* ctx = (T2Ctx*)user_data;
    for (int i = 0; i < T2_K_ITERS; i++)
    {
        woort_mem_PageHead* a = woort_mem_chunk_allocate_page(ctx->chunk);
        woort_mem_PageHead* b = woort_mem_chunk_allocate_page(ctx->chunk);
        if (a && b)
        {
            CHECK_NE(a, b);
            CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, a), a);
            woort_mem_chunk_free_page(ctx->chunk, a);
            woort_mem_chunk_free_page(ctx->chunk, b);
            woort_atomic_fetch_add_explicit(
                ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
        else
        {
            if (a) woort_mem_chunk_free_page(ctx->chunk, a);
            if (b) woort_mem_chunk_free_page(ctx->chunk, b);
        }
    }
}

TEST(multi_chunk_parallel_isolated)
{
    woort_mem_Chunk chunks[T2_K_CHUNKS];
    for (int i = 0; i < T2_K_CHUNKS; i++)
        woort_mem_chunk_init(&chunks[i], 4 * 1024 * 1024);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    int total_threads = T2_K_CHUNKS * T2_K_THREADS_PER;
    T2Ctx ctxs[16];
    woort_Thread* threads[16];

    int idx = 0;
    for (int c = 0; c < T2_K_CHUNKS; c++)
    {
        for (int t = 0; t < T2_K_THREADS_PER; t++)
        {
            ctxs[idx] = (T2Ctx){ .chunk = &chunks[c], .ops = &ops };
            woort_thread_start(t2_worker, &ctxs[idx], &threads[idx]);
            idx++;
        }
    }
    for (int i = 0; i < total_threads; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED),
             T2_K_CHUNKS * T2_K_THREADS_PER * T2_K_ITERS / 4);
    printf("    total_ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    for (int i = 0; i < T2_K_CHUNKS; i++)
        woort_mem_chunk_deinit(&chunks[i]);
}

/* ================================================================ */
/* Test 3: producer_consumer_pattern                                */
/* ================================================================ */

#define T3_K_PRODUCERS  6
#define T3_K_CONSUMERS  4
#define T3_K_QUEUE      256
#define T3_K_ITEMS      10000

typedef struct {
    woort_mem_Chunk* chunk;
    BoundedQueue* queue;
    woort_AtomicInt32* produced;
    woort_AtomicInt32* consumed;
    woort_AtomicInt32* active_producers;
    woort_Spinlock* alloc_mx;
    woort_HashMap* allocated;
} T3Ctx;

static void t3_producer(void* user_data)
{
    T3Ctx* ctx = (T3Ctx*)user_data;
    for (;;)
    {
        int old = (int)woort_atomic_fetch_add_explicit(
            ctx->produced, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        if (old >= T3_K_ITEMS) break;

        woort_mem_PageHead* p = NULL;
        while (!p)
        {
            p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (!p) spin_yield();
        }

        CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);

        woort_spinlock_lock(ctx->alloc_mx);
        (void)woort_hashmap_insert(ctx->allocated, &p, NULL);
        woort_spinlock_unlock(ctx->alloc_mx);

        bq_push(ctx->queue, p);
    }
    if (woort_atomic_fetch_sub_explicit(
        ctx->active_producers, 1, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL) == 1)
        bq_close(ctx->queue);
}

static void t3_consumer(void* user_data)
{
    T3Ctx* ctx = (T3Ctx*)user_data;
    woort_mem_PageHead* p;
    while (bq_pop(ctx->queue, &p))
    {
        CHECK_NE(p, NULL);

        woort_spinlock_lock(ctx->alloc_mx);
        bool found = woort_hashmap_contains(ctx->allocated, &p);
        CHECK(found);
        if (found)
            (void)woort_hashmap_remove(ctx->allocated, &p);
        woort_spinlock_unlock(ctx->alloc_mx);

        woort_mem_chunk_free_page(ctx->chunk, p);
        woort_atomic_fetch_add_explicit(
            ctx->consumed, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }
}

TEST(producer_consumer_pattern)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 8 * 1024 * 1024);

    BoundedQueue queue;
    bq_init(&queue, T3_K_QUEUE);

    woort_AtomicInt32 produced, consumed, active;
    woort_atomic_init(&produced, 0);
    woort_atomic_init(&consumed, 0);
    woort_atomic_init(&active, T3_K_PRODUCERS);

    woort_Spinlock alloc_mx;
    woort_spinlock_init(&alloc_mx);

    woort_HashMap allocated;
    woort_hashmap_init(&allocated, sizeof(woort_mem_PageHead*), 0,
        woort_util_ptr_hash, woort_util_ptr_equal);

    T3Ctx pctxs[T3_K_PRODUCERS], cctxs[T3_K_CONSUMERS];
    woort_Thread* threads[T3_K_PRODUCERS + T3_K_CONSUMERS];

    for (int i = 0; i < T3_K_PRODUCERS; i++)
    {
        pctxs[i] = (T3Ctx){
            .chunk = &chunk, .queue = &queue,
            .produced = &produced, .consumed = &consumed,
            .active_producers = &active,
            .alloc_mx = &alloc_mx, .allocated = &allocated,
        };
        woort_thread_start(t3_producer, &pctxs[i], &threads[i]);
    }
    for (int i = 0; i < T3_K_CONSUMERS; i++)
    {
        cctxs[i] = pctxs[0]; /* copy shared fields */
        woort_thread_start(t3_consumer, &cctxs[i], &threads[T3_K_PRODUCERS + i]);
    }

    for (int i = 0; i < T3_K_PRODUCERS + T3_K_CONSUMERS; i++)
        woort_thread_join(threads[i]);

    CHECK_EQ((int)woort_atomic_load_explicit(&consumed, WOORT_ATOMIC_MEMORY_ORDER_RELAXED),
             T3_K_ITEMS);
    printf("    produced=%d consumed=%d\n", T3_K_ITEMS,
        (int)woort_atomic_load_explicit(&consumed, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_hashmap_deinit(&allocated);
    woort_spinlock_deinit(&alloc_mx);
    bq_deinit(&queue);
    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 4: mixed_order_concurrent                                   */
/* ================================================================ */

#define T4_K_THREADS 8
#define T4_K_ITERS   800

typedef struct {
    woort_mem_Chunk* chunk;
    int tid;
    woort_AtomicInt32* ops;
} T4Ctx;

static void t4_worker(void* user_data)
{
    T4Ctx* ctx = (T4Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, (uint64_t)(ctx->tid + 42));

    for (int i = 0; i < T4_K_ITERS; i++)
    {
        int choice = prng_range(&rng, 0, 4);
        woort_mem_PageHead* p = NULL;

        switch (choice)
        {
        case 0:
            p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (p) CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
            break;
        case 1:
            p = woort_mem_chunk_allocate_huge_page(ctx->chunk, K_PAGE_SIZE * 2);
            if (p)
            {
                void* interior = (char*)p + K_PAGE_SIZE + 32;
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, interior), p);
            }
            break;
        case 2:
            p = woort_mem_chunk_allocate_huge_page(ctx->chunk, K_PAGE_SIZE * 4);
            if (p)
            {
                void* interior = (char*)p + K_PAGE_SIZE * 3 + 11;
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, interior), p);
            }
            break;
        default:
        {
            woort_mem_PageHead* a = woort_mem_chunk_allocate_page(ctx->chunk);
            woort_mem_PageHead* b = woort_mem_chunk_allocate_page(ctx->chunk);
            if (a && b)
            {
                CHECK_NE(a, b);
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, a), a);
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, b), b);
                woort_mem_chunk_free_page(ctx->chunk, b);
                woort_atomic_fetch_add_explicit(
                    ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            }
            else
            {
                if (b) woort_mem_chunk_free_page(ctx->chunk, b);
            }
            if (a) woort_mem_chunk_free_page(ctx->chunk, a);
            continue;
        }
        }

        if (p)
        {
            woort_mem_chunk_free_page(ctx->chunk, p);
            woort_atomic_fetch_add_explicit(
                ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
    }
}

TEST(mixed_order_concurrent)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 16 * 1024 * 1024);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    T4Ctx ctxs[T4_K_THREADS];
    woort_Thread* threads[T4_K_THREADS];

    for (int i = 0; i < T4_K_THREADS; i++)
    {
        ctxs[i] = (T4Ctx){ .chunk = &chunk, .tid = i, .ops = &ops };
        woort_thread_start(t4_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T4_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED),
             T4_K_THREADS * T4_K_ITERS / 4);
    printf("    ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 5: validate_under_pressure (timed)                         */
/* ================================================================ */

#define T5_K_ALLOC    6
#define T5_K_VALIDATE 4
#define T5_DURATION   1500

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicUInt8* stop;
    woort_AtomicInt32* validate_ops;
    woort_AtomicInt32* validate_ok;
    int tid;
    int role; /* 0=alloc, 1=validate */
} T5Ctx;

static void t5_worker(void* user_data)
{
    T5Ctx* ctx = (T5Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, ctx->role == 0
        ? (uint64_t)(time(NULL) + ctx->tid)
        : (uint64_t)(ctx->tid + 5000));

    if (ctx->role == 0)
    {
        while (!woort_atomic_load_explicit(ctx->stop, WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
        {
            woort_mem_PageHead* p;
            if (prng_range(&rng, 0, 2) == 0)
                p = woort_mem_chunk_allocate_huge_page(ctx->chunk, K_PAGE_SIZE * 2);
            else
                p = woort_mem_chunk_allocate_page(ctx->chunk);

            if (p)
            {
                spin_yield();
                woort_mem_chunk_free_page(ctx->chunk, p);
            }
        }
    }
    else
    {
        while (!woort_atomic_load_explicit(ctx->stop, WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (!p)
            {
                spin_yield();
                continue;
            }

            woort_mem_PageHead* result = woort_mem_chunk_validate(ctx->chunk, p);
            woort_atomic_fetch_add_explicit(
                ctx->validate_ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            if (result != NULL)
                woort_atomic_fetch_add_explicit(
                    ctx->validate_ok, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

            ptrdiff_t off = (ptrdiff_t)prng_range(&rng, 1, (int)K_PAGE_SIZE - 1);
            void* interior = (char*)p + off;
            result = woort_mem_chunk_validate(ctx->chunk, interior);
            woort_atomic_fetch_add_explicit(
                ctx->validate_ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            if (result != NULL)
                woort_atomic_fetch_add_explicit(
                    ctx->validate_ok, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

            woort_mem_chunk_free_page(ctx->chunk, p);
        }
    }
}

TEST(validate_under_pressure)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 8 * 1024 * 1024);

    woort_AtomicUInt8 stop;
    woort_AtomicInt32 validate_ops, validate_ok;
    woort_atomic_init(&stop, 0);
    woort_atomic_init(&validate_ops, 0);
    woort_atomic_init(&validate_ok, 0);

    T5Ctx ctxs[T5_K_ALLOC + T5_K_VALIDATE];
    woort_Thread* threads[T5_K_ALLOC + T5_K_VALIDATE];

    for (int i = 0; i < T5_K_ALLOC; i++)
    {
        ctxs[i] = (T5Ctx){
            .chunk = &chunk, .stop = &stop,
            .validate_ops = &validate_ops, .validate_ok = &validate_ok,
            .tid = i, .role = 0,
        };
        woort_thread_start(t5_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T5_K_VALIDATE; i++)
    {
        int idx = T5_K_ALLOC + i;
        ctxs[idx] = (T5Ctx){
            .chunk = &chunk, .stop = &stop,
            .validate_ops = &validate_ops, .validate_ok = &validate_ok,
            .tid = i, .role = 1,
        };
        woort_thread_start(t5_worker, &ctxs[idx], &threads[idx]);
    }

    woort_thread_sleep_ms(T5_DURATION);
    woort_atomic_store_explicit(&stop, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    for (int i = 0; i < T5_K_ALLOC + T5_K_VALIDATE; i++)
        woort_thread_join(threads[i]);

    int vops = (int)woort_atomic_load_explicit(&validate_ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    int vok = (int)woort_atomic_load_explicit(&validate_ok, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    CHECK_GE(vops, 1000);
    CHECK_LE(vok, vops);
    printf("    validate_ops=%d validate_ok=%d\n", vops, vok);

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 6: near_exhaustion_thrash                                   */
/* ================================================================ */

#define T6_K_THREADS 4
#define T6_K_CYCLES  100

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* successes;
} T6Ctx;

static void t6_worker(void* user_data)
{
    T6Ctx* ctx = (T6Ctx*)user_data;
    for (int cycle = 0; cycle < T6_K_CYCLES; cycle++)
    {
        for (int i = 0; i < 32; i++)
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (p)
            {
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
                woort_atomic_fetch_add_explicit(
                    ctx->successes, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            }
        }
        spin_yield();
    }
}

TEST(near_exhaustion_thrash)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 1 * 1024 * 1024);

    woort_AtomicInt32 successes;
    woort_atomic_init(&successes, 0);

    T6Ctx ctxs[T6_K_THREADS];
    woort_Thread* threads[T6_K_THREADS];

    for (int i = 0; i < T6_K_THREADS; i++)
    {
        ctxs[i] = (T6Ctx){ .chunk = &chunk, .successes = &successes };
        woort_thread_start(t6_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T6_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_EQ((int)woort_atomic_load_explicit(&successes, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 32);
    printf("    successes=%d\n",
        (int)woort_atomic_load_explicit(&successes, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 7: random_power2_allocations                                */
/* ================================================================ */

#define T7_K_THREADS 8
#define T7_K_ITERS   500

typedef struct {
    woort_mem_Chunk* chunk;
    int tid;
    woort_AtomicInt32* ops;
    woort_AtomicInt32* huge_ops;
} T7Ctx;

static void t7_worker(void* user_data)
{
    T7Ctx* ctx = (T7Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, (uint64_t)(ctx->tid * 7919 + 12345));

    for (int i = 0; i < T7_K_ITERS; i++)
    {
        int order = prng_range(&rng, 0, 5);
        size_t sz = K_PAGE_SIZE * ((size_t)1 << order);
        woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(ctx->chunk, sz);

        if (p)
        {
            if (order > 0)
                woort_atomic_fetch_add_explicit(
                    ctx->huge_ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

            ptrdiff_t off = (ptrdiff_t)(prng_u32(&rng) % (sz > 100 ? 100 : 1));
            void* interior = (char*)p + off;
            CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, interior), p);

            woort_mem_chunk_free_page(ctx->chunk, p);
            woort_atomic_fetch_add_explicit(
                ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
    }
}

TEST(random_power2_allocations)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 32 * 1024 * 1024);

    woort_AtomicInt32 ops, huge_ops;
    woort_atomic_init(&ops, 0);
    woort_atomic_init(&huge_ops, 0);

    T7Ctx ctxs[T7_K_THREADS];
    woort_Thread* threads[T7_K_THREADS];

    for (int i = 0; i < T7_K_THREADS; i++)
    {
        ctxs[i] = (T7Ctx){
            .chunk = &chunk, .tid = i,
            .ops = &ops, .huge_ops = &huge_ops,
        };
        woort_thread_start(t7_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T7_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED),
             T7_K_THREADS * T7_K_ITERS / 4);
    printf("    ops=%d huge_ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED),
        (int)woort_atomic_load_explicit(&huge_ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 8: alloc_free_interleaved_stress                            */
/* ================================================================ */

#define T8_K_THREADS 12
#define T8_K_ITERS   1500
#define T8_K_SLOTS   128

typedef struct {
    woort_mem_Chunk* chunk;
    int tid;
    woort_AtomicPtr* slots;
    woort_AtomicInt32* ops;
} T8Ctx;

static void t8_worker(void* user_data)
{
    T8Ctx* ctx = (T8Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, (uint64_t)(ctx->tid + 9999));

    for (int i = 0; i < T8_K_ITERS; i++)
    {
        int slot = prng_range(&rng, 0, T8_K_SLOTS - 1);
        void* old = woort_atomic_exchange_explicit(
            &ctx->slots[slot], NULL, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);
        woort_mem_PageHead* oldp = (woort_mem_PageHead*)old;

        if (oldp)
        {
            CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, oldp), oldp);
            woort_mem_chunk_free_page(ctx->chunk, oldp);
        }
        else
        {
            woort_mem_PageHead* p = woort_mem_chunk_allocate_page(ctx->chunk);
            if (p)
            {
                CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
                void* expected = NULL;
                if (woort_atomic_compare_exchange_strong_explicit(
                    &ctx->slots[slot], &expected, p,
                    WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
                    WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
                {
                    woort_atomic_fetch_add_explicit(
                        ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
                }
                else
                {
                    woort_mem_chunk_free_page(ctx->chunk, p);
                }
            }
        }
    }

    for (int s = 0; s < T8_K_SLOTS; s++)
    {
        void* p = woort_atomic_exchange_explicit(
            &ctx->slots[s], NULL, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);
        if (p)
            woort_mem_chunk_free_page(ctx->chunk, (woort_mem_PageHead*)p);
    }
}

TEST(alloc_free_interleaved_stress)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 8 * 1024 * 1024);

    woort_AtomicPtr slots[T8_K_SLOTS];
    for (int i = 0; i < T8_K_SLOTS; i++)
        woort_atomic_init(&slots[i], NULL);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    T8Ctx ctxs[T8_K_THREADS];
    woort_Thread* threads[T8_K_THREADS];

    for (int i = 0; i < T8_K_THREADS; i++)
    {
        ctxs[i] = (T8Ctx){
            .chunk = &chunk, .tid = i,
            .slots = slots, .ops = &ops,
        };
        woort_thread_start(t8_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T8_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 100);
    printf("    ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 9: no_double_alloc                                          */
/* ================================================================ */

#define T9_K_THREADS 8
#define T9_K_ITERS   300

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* ops;
} T9Ctx;

static void t9_worker(void* user_data)
{
    T9Ctx* ctx = (T9Ctx*)user_data;
    for (int i = 0; i < T9_K_ITERS; i++)
    {
        woort_mem_PageHead* pages[3] = { NULL, NULL, NULL };
        int count = 0;

        for (int j = 0; j < 3; j++)
        {
            pages[j] = woort_mem_chunk_allocate_page(ctx->chunk);
            if (pages[j])
                count++;
            else
                break;
        }

        for (int a = 0; a < count; a++)
            for (int b = a + 1; b < count; b++)
                CHECK_NE(pages[a], pages[b]);

        for (int j = 0; j < count; j++)
            woort_mem_chunk_free_page(ctx->chunk, pages[j]);

        woort_atomic_fetch_add_explicit(
            ctx->ops, count, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }
}

TEST(no_double_alloc)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 2 * 1024 * 1024);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    T9Ctx ctxs[T9_K_THREADS];
    woort_Thread* threads[T9_K_THREADS];

    for (int i = 0; i < T9_K_THREADS; i++)
    {
        ctxs[i] = (T9Ctx){ .chunk = &chunk, .ops = &ops };
        woort_thread_start(t9_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T9_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 100);
    printf("    ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 10: huge_page_non_overlapping                              */
/* ================================================================ */

#define T10_K_THREADS 6
#define T10_K_ITERS   200

typedef struct {
    woort_mem_Chunk* chunk;
    int tid;
    woort_AtomicInt32* ops;
    woort_Spinlock* addr_mx;
    woort_HashMap* allocated;
} T10Ctx;

static void t10_worker(void* user_data)
{
    T10Ctx* ctx = (T10Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, (uint64_t)(ctx->tid * 31337));

    for (int i = 0; i < T10_K_ITERS; i++)
    {
        int order = prng_range(&rng, 0, 4);
        size_t sz = K_PAGE_SIZE * ((size_t)1 << order);
        woort_mem_PageHead* p = woort_mem_chunk_allocate_huge_page(ctx->chunk, sz);

        if (p)
        {
            CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);

            woort_spinlock_lock(ctx->addr_mx);
            {
                bool contains = woort_hashmap_contains(ctx->allocated, &p);
                CHECK(!contains);
                (void)woort_hashmap_insert(ctx->allocated, &p, NULL);
                (void)woort_hashmap_remove(ctx->allocated, &p);
            }
            woort_spinlock_unlock(ctx->addr_mx);

            woort_mem_chunk_free_page(ctx->chunk, p);
            woort_atomic_fetch_add_explicit(
                ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
    }
}

TEST(huge_page_non_overlapping)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 16 * 1024 * 1024);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    woort_Spinlock addr_mx;
    woort_spinlock_init(&addr_mx);

    woort_HashMap allocated;
    woort_hashmap_init(&allocated, sizeof(woort_mem_PageHead*), 0,
        woort_util_ptr_hash, woort_util_ptr_equal);

    T10Ctx ctxs[T10_K_THREADS];
    woort_Thread* threads[T10_K_THREADS];

    for (int i = 0; i < T10_K_THREADS; i++)
    {
        ctxs[i] = (T10Ctx){
            .chunk = &chunk, .tid = i, .ops = &ops,
            .addr_mx = &addr_mx, .allocated = &allocated,
        };
        woort_thread_start(t10_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T10_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 100);
    printf("    ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_hashmap_deinit(&allocated);
    woort_spinlock_deinit(&addr_mx);
    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 11: long_running_stress (timed)                             */
/* ================================================================ */

#define T11_K_THREADS  10
#define T11_DURATION   3000

typedef struct {
    woort_mem_Chunk* chunk;
    int tid;
    woort_AtomicUInt8* stop;
    woort_AtomicInt64* total_alloc;
    woort_AtomicInt64* total_free;
    woort_AtomicInt64* alloc_fail;
} T11Ctx;

static void t11_worker(void* user_data)
{
    T11Ctx* ctx = (T11Ctx*)user_data;
    prng_t rng;
    prng_seed(&rng, (uint64_t)(ctx->tid + 0xDEAD));

    while (!woort_atomic_load_explicit(ctx->stop, WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
    {
        woort_mem_PageHead* p = NULL;
        int c = prng_range(&rng, 0, 3);

        if (c == 0)
        {
            p = woort_mem_chunk_allocate_page(ctx->chunk);
        }
        else if (c <= 2)
        {
            p = woort_mem_chunk_allocate_huge_page(ctx->chunk, K_PAGE_SIZE * 2);
        }
        else
        {
            int order = prng_range(&rng, 0, 3);
            p = woort_mem_chunk_allocate_huge_page(
                ctx->chunk, K_PAGE_SIZE * ((size_t)1 << order));
        }

        if (p)
        {
            CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, p), p);
            woort_atomic_fetch_add_explicit(
                ctx->total_alloc, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

            int64_t cur = (int64_t)woort_atomic_load_explicit(
                ctx->total_alloc, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            if (cur % 3 != 0)
                spin_yield();

            woort_mem_chunk_free_page(ctx->chunk, p);
            woort_atomic_fetch_add_explicit(
                ctx->total_free, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
        else
        {
            woort_atomic_fetch_add_explicit(
                ctx->alloc_fail, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
            spin_yield();
        }
    }
}

TEST(long_running_stress)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 32 * 1024 * 1024);

    woort_AtomicUInt8 stop;
    woort_AtomicInt64 total_alloc, total_free, alloc_fail;
    woort_atomic_init(&stop, 0);
    woort_atomic_init(&total_alloc, 0);
    woort_atomic_init(&total_free, 0);
    woort_atomic_init(&alloc_fail, 0);

    T11Ctx ctxs[T11_K_THREADS];
    woort_Thread* threads[T11_K_THREADS];

    for (int i = 0; i < T11_K_THREADS; i++)
    {
        ctxs[i] = (T11Ctx){
            .chunk = &chunk, .tid = i, .stop = &stop,
            .total_alloc = &total_alloc, .total_free = &total_free,
            .alloc_fail = &alloc_fail,
        };
        woort_thread_start(t11_worker, &ctxs[i], &threads[i]);
    }

    woort_thread_sleep_ms(T11_DURATION);
    woort_atomic_store_explicit(&stop, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    for (int i = 0; i < T11_K_THREADS; i++)
        woort_thread_join(threads[i]);

    int64_t alloc_cnt = (int64_t)woort_atomic_load_explicit(
        &total_alloc, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    int64_t free_cnt = (int64_t)woort_atomic_load_explicit(
        &total_free, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    int64_t fail_cnt = (int64_t)woort_atomic_load_explicit(
        &alloc_fail, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    CHECK_GE(alloc_cnt, 100);
    CHECK_EQ(alloc_cnt, free_cnt);
    printf("    alloc=%lld free=%lld fail=%lld\n",
        (long long)alloc_cnt, (long long)free_cnt, (long long)fail_cnt);

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 12: max_concurrency_stress                                  */
/* ================================================================ */

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* ops;
} T12Ctx;

static void t12_worker(void* user_data)
{
    T12Ctx* ctx = (T12Ctx*)user_data;
    for (int i = 0; i < 1000; i++)
    {
        woort_mem_PageHead* a = woort_mem_chunk_allocate_page(ctx->chunk);
        woort_mem_PageHead* b = woort_mem_chunk_allocate_page(ctx->chunk);
        if (a && b)
        {
            CHECK_NE(a, b);
            woort_mem_chunk_free_page(ctx->chunk, b);
            woort_mem_chunk_free_page(ctx->chunk, a);
            woort_atomic_fetch_add_explicit(
                ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
        else
        {
            if (a) woort_mem_chunk_free_page(ctx->chunk, a);
            if (b) woort_mem_chunk_free_page(ctx->chunk, b);
        }
    }
}

TEST(max_concurrency_stress)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 16 * 1024 * 1024);

    unsigned hw = test_hardware_concurrency();
    int k_threads = (int)(hw * 4 > 8 ? hw * 4 : 8);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    /* Cap at 64 threads to avoid excessive stack usage */
    if (k_threads > 64) k_threads = 64;

    T12Ctx ctxs[64];
    woort_Thread* threads[64];

    for (int i = 0; i < k_threads; i++)
    {
        ctxs[i] = (T12Ctx){ .chunk = &chunk, .ops = &ops };
        woort_thread_start(t12_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < k_threads; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 100);
    printf("    threads=%d ops=%d\n", k_threads,
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 13: rapid_alloc_free_burst                                  */
/* ================================================================ */

#define T13_K_THREADS 8
#define T13_K_BURSTS  50

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* ops;
} T13Ctx;

static void t13_worker(void* user_data)
{
    T13Ctx* ctx = (T13Ctx*)user_data;
    woort_mem_PageHead* batch[50];

    for (int burst = 0; burst < T13_K_BURSTS; burst++)
    {
        int count = 0;
        for (int j = 0; j < 50; j++)
        {
            batch[j] = woort_mem_chunk_allocate_page(ctx->chunk);
            if (batch[j])
                count++;
        }

        for (int j = 0; j < count; j++)
        {
            CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, batch[j]), batch[j]);
            if (batch[j])
                woort_mem_chunk_free_page(ctx->chunk, batch[j]);
        }

        woort_atomic_fetch_add_explicit(
            ctx->ops, count, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }
}

TEST(rapid_alloc_free_burst)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 32 * 1024 * 1024);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    T13Ctx ctxs[T13_K_THREADS];
    woort_Thread* threads[T13_K_THREADS];

    for (int i = 0; i < T13_K_THREADS; i++)
    {
        ctxs[i] = (T13Ctx){ .chunk = &chunk, .ops = &ops };
        woort_thread_start(t13_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T13_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 2000);
    printf("    ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 14: sequential_after_parallel                               */
/* ================================================================ */

TEST(sequential_after_parallel)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 4 * 1024 * 1024);

    woort_mem_PageHead* pages[128];
    for (int i = 0; i < 128; i++)
    {
        pages[i] = woort_mem_chunk_allocate_page(&chunk);
        CHECK(pages[i] != NULL);
    }
    CHECK(woort_mem_chunk_allocate_page(&chunk) == NULL);

    for (int i = 0; i < 128; i++)
        woort_mem_chunk_free_page(&chunk, pages[i]);

    woort_mem_PageHead* huge =
        woort_mem_chunk_allocate_huge_page(&chunk, 128 * K_PAGE_SIZE);
    CHECK(huge != NULL);
    woort_mem_chunk_free_page(&chunk, huge);

    for (int i = 0; i < 64; i++)
    {
        woort_mem_PageHead* p = woort_mem_chunk_allocate_page(&chunk);
        CHECK(p != NULL);
        woort_mem_chunk_free_page(&chunk, p);
    }

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* Test 15: zigzag_order_allocation                                 */
/* ================================================================ */

#define T15_K_THREADS 6
#define T15_K_ROUNDS  150

typedef struct {
    woort_mem_Chunk* chunk;
    woort_AtomicInt32* ops;
} T15Ctx;

static void t15_worker(void* user_data)
{
    T15Ctx* ctx = (T15Ctx*)user_data;
    for (int r = 0; r < T15_K_ROUNDS; r++)
    {
        woort_mem_PageHead* huge =
            woort_mem_chunk_allocate_huge_page(ctx->chunk, K_PAGE_SIZE * 8);
        if (!huge) continue;

        CHECK_EQ(woort_mem_chunk_validate(ctx->chunk, huge), huge);

        woort_mem_PageHead* small[4] = { NULL };
        for (int s = 0; s < 4; s++)
            small[s] = woort_mem_chunk_allocate_page(ctx->chunk);

        woort_mem_chunk_free_page(ctx->chunk, huge);

        for (int s = 0; s < 4; s++)
        {
            if (small[s])
                woort_mem_chunk_free_page(ctx->chunk, small[s]);
        }

        woort_atomic_fetch_add_explicit(
            ctx->ops, 1, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
    }
}

TEST(zigzag_order_allocation)
{
    woort_mem_Chunk chunk;
    woort_mem_chunk_init(&chunk, 16 * 1024 * 1024);

    woort_AtomicInt32 ops;
    woort_atomic_init(&ops, 0);

    T15Ctx ctxs[T15_K_THREADS];
    woort_Thread* threads[T15_K_THREADS];

    for (int i = 0; i < T15_K_THREADS; i++)
    {
        ctxs[i] = (T15Ctx){ .chunk = &chunk, .ops = &ops };
        woort_thread_start(t15_worker, &ctxs[i], &threads[i]);
    }
    for (int i = 0; i < T15_K_THREADS; i++)
        woort_thread_join(threads[i]);

    CHECK_GE((int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED), 50);
    printf("    ops=%d\n",
        (int)woort_atomic_load_explicit(&ops, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    woort_mem_chunk_deinit(&chunk);
}

/* ================================================================ */
/* main                                                            */
/* ================================================================ */

int main(void)
{
    printf("=== Chunk Parallel Stress Tests ===\n\n");

    RUN_TEST(massive_parallel_alloc_free);
    RUN_TEST(multi_chunk_parallel_isolated);
    for (int i = 0; i < 50; ++i)
        RUN_TEST(producer_consumer_pattern);
    RUN_TEST(mixed_order_concurrent);
    RUN_TEST(validate_under_pressure);
    RUN_TEST(near_exhaustion_thrash);
    RUN_TEST(random_power2_allocations);
    RUN_TEST(alloc_free_interleaved_stress);
    RUN_TEST(no_double_alloc);
    RUN_TEST(huge_page_non_overlapping);
    RUN_TEST(long_running_stress);
    RUN_TEST(max_concurrency_stress);
    RUN_TEST(rapid_alloc_free_burst);
    RUN_TEST(sequential_after_parallel);
    RUN_TEST(zigzag_order_allocation);

    printf("\n=== %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
