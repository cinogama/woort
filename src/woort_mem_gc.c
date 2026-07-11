/*
woort_mem_gc.c
*/

#include "woort_mem_gc.h"
#include "woort_mem_global_context.h"
#include "woort_mem_thread_context.h"
#include "woort_mem.h"
#include "woort_platform.h"
#include "woort_log.h"
#include "woort_util.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   include <time.h>
#   include <unistd.h>
#endif

uint8_t woort_mem_gc_marking_round_counter = 0;
bool    woort_mem_gc_marking_state_flag = false;
size_t  woort_mem_gc_memory_size_after_last_round_sweep = 0;

woort_mem_GC* g_woort_mem_gc = NULL;

/* ============================================================ */
/* Helpers                                                       */
/* ============================================================ */

static size_t woort_mem_hardware_concurrency(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwNumberOfProcessors;
#elif defined(WOORT_PLATFORM_OS_POSIX)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (size_t)n : 1;
#else
    return 1;
#endif
}

static uint64_t woort_mem_monotonic_ms(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart * 1000 / freq.QuadPart);
#elif defined(WOORT_PLATFORM_OS_POSIX)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#else
    return 0;
#endif
}

static size_t woort_mem_default_gc_worker_count(void)
{
    const size_t count = woort_mem_hardware_concurrency() / 8;
    return count == 0 ? 1 : count;
}

/* ============================================================ */
/* GC synchronization primitives                                 */
/* ============================================================ */

static void woort_mem_gc_launch_worker_and_wait_until_done(
    woort_mem_GC* self, woort_mem_WorkerThresholdState expected_state)
{
    woort_mutex_lock(self->m_gc_worker_threshold_mx);
    self->m_gc_worker_threshold_finish_counter = 0;
    self->m_gc_worker_threshold_launch_state = expected_state;
    woort_condition_variable_broadcast(self->m_gc_worker_threshold_cv);

    while (self->m_gc_worker_count
        != self->m_gc_worker_threshold_finish_counter)
    {
        woort_condition_variable_wait(
            self->m_gc_worker_threshold_cv,
            self->m_gc_worker_threshold_mx);
    }
    woort_mutex_unlock(self->m_gc_worker_threshold_mx);
}

static void woort_mem_gc_wait_for_worker_launch(
    woort_mem_GC* self, woort_mem_WorkerThresholdState expected_state)
{
    woort_mutex_lock(self->m_gc_worker_threshold_mx);
    while (expected_state != self->m_gc_worker_threshold_launch_state)
    {
        woort_condition_variable_wait(
            self->m_gc_worker_threshold_cv,
            self->m_gc_worker_threshold_mx);
    }
    woort_mutex_unlock(self->m_gc_worker_threshold_mx);
}

static bool woort_mem_gc_wait_for_worker_launch_or_shutdown(
    woort_mem_GC* self, woort_mem_WorkerThresholdState expected_state)
{
    woort_mutex_lock(self->m_gc_worker_threshold_mx);
    while (expected_state != self->m_gc_worker_threshold_launch_state
        && !woort_atomic_load_explicit(
            &self->m_worker_shutdown,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
    {
        woort_condition_variable_wait(
            self->m_gc_worker_threshold_cv,
            self->m_gc_worker_threshold_mx);
    }
    woort_mutex_unlock(self->m_gc_worker_threshold_mx);

    return !woort_atomic_load_explicit(
        &self->m_worker_shutdown,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static void woort_mem_gc_signal_worker_shutdown(woort_mem_GC* self)
{
    woort_mutex_lock(self->m_gc_worker_threshold_mx);
    woort_mutex_unlock(self->m_gc_worker_threshold_mx);
    woort_condition_variable_broadcast(self->m_gc_worker_threshold_cv);
}

static void woort_mem_gc_worker_done_and_notify(woort_mem_GC* self)
{
    woort_mutex_lock(self->m_gc_worker_threshold_mx);
    {
        if (++self->m_gc_worker_threshold_finish_counter
            == self->m_gc_worker_count)
        {
            woort_condition_variable_broadcast(
                self->m_gc_worker_threshold_cv);
        }
    }
    woort_mutex_unlock(self->m_gc_worker_threshold_mx);
}

/* ============================================================ */
/* GC root set + marking                                         */
/* ============================================================ */

void woort_mem_gc_register_root_unit_head(
    woort_mem_GC* self, woort_mem_UnitHead* unit_head)
{
    woort_spinlock_lock(&self->m_root_gc_unit_set_mx);
    (void)woort_hashmap_insert(&self->m_root_gc_unit_set, &unit_head, NULL);
    woort_spinlock_unlock(&self->m_root_gc_unit_set_mx);
}

void woort_mem_gc_unregister_root_unit_head(
    woort_mem_GC* self, woort_mem_UnitHead* unit_head)
{
    woort_spinlock_lock(&self->m_root_gc_unit_set_mx);
    (void)woort_hashmap_remove(&self->m_root_gc_unit_set, &unit_head);
    woort_spinlock_unlock(&self->m_root_gc_unit_set_mx);
}

void woort_mem_gc_mark_root_unit_to_gray(
    woort_mem_GC* self, woort_mem_UnitHead* unit_head)
{
    uint8_t expected = WOORT_MEM_UNIT_LIFE_UNMARKED;
    if (woort_atomic_compare_exchange_strong_explicit(
        &unit_head->m_life, &expected,
        WOORT_MEM_UNIT_LIFE_SELF_MARKED,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
    {
        assert(woort_mem_gc_marking_state_flag);

        const size_t assigned_worker_id =
            woort_atomic_fetch_add_explicit(
                &self->m_gc_assigned_thread_idx,
                1,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

        woort_mem_GCWorker* worker =
            &self->m_gc_worker_threads[
                assigned_worker_id % self->m_gc_worker_count];

        woort_spinlock_lock(&worker->m_local_work_spin_for_root);
        woort_vector_push_back(&worker->m_local_work, 1, &unit_head);
        woort_spinlock_unlock(&worker->m_local_work_spin_for_root);
    }
}

woort_mem_GCWorker* woort_mem_gc_fetch_thread_worker(woort_mem_GC* self)
{
    const size_t assigned_worker_id =
        woort_atomic_fetch_add_explicit(
            &self->m_gc_assigned_thread_idx,
            1,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    return &self->m_gc_worker_threads[
        assigned_worker_id % self->m_gc_worker_count];
}

void woort_mem_gc_trigger_gc(woort_mem_GC* self, bool async)
{
    if (async)
    {
        woort_atomic_store_explicit(
            &self->m_force_trigger_gc, true,
            WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
        woort_condition_variable_signal(self->m_trigger_cv);
    }
    else
    {
        const size_t prev_count = woort_atomic_load_explicit(
            &self->m_gc_cycle_count,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        woort_atomic_store_explicit(
            &self->m_force_trigger_gc, true,
            WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
        woort_condition_variable_signal(self->m_trigger_cv);

        woort_mutex_lock(self->m_trigger_mx);
        while (woort_atomic_load_explicit(
                    &self->m_gc_cycle_count,
                    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE)
                    <= prev_count
            && !woort_atomic_load_explicit(
                    &self->m_shutdown,
                    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
        {
            woort_condition_variable_wait(
                self->m_trigger_cv, self->m_trigger_mx);
        }
        woort_mutex_unlock(self->m_trigger_mx);
    }
}

/* ============================================================ */
/* GCWorker                                                      */
/* ============================================================ */

void woort_mem_gcworker_mark_unit_to_gray(
    woort_mem_GCWorker* self, woort_mem_UnitHead* unit_head)
{
    uint8_t expected = WOORT_MEM_UNIT_LIFE_UNMARKED;
    if (woort_atomic_compare_exchange_strong_explicit(
        &unit_head->m_life, &expected,
        WOORT_MEM_UNIT_LIFE_SELF_MARKED,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
    {
        if (woort_thread_current_id() == self->m_worker_thread_id)
        {
            woort_vector_push_back(&self->m_local_work, 1, &unit_head);
        }
        else
        {
            while (!woort_mem_mpsc_try_enqueue(
                &self->m_gray_queue, unit_head))
            {
                if (!woort_atomic_load_explicit(
                    &self->m_is_draining,
                    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
                {
                    woort_spinlock_lock(&self->m_local_work_spin_for_root);
                    if (!woort_atomic_load_explicit(
                        &self->m_is_draining,
                        WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
                    {
                        woort_vector_push_back(
                            &self->m_local_work, 1, &unit_head);
                        woort_spinlock_unlock(
                            &self->m_local_work_spin_for_root);
                        break;
                    }
                    woort_spinlock_unlock(
                        &self->m_local_work_spin_for_root);
                }
            }
        }
    }
}

static bool woort_mem_gcworker_check_and_free_unmarked_unit(
    woort_mem_GCWorker* self,
    woort_mem_UnitHead* unit, woort_mem_PageHead* page_may_null)
{
    const uint8_t life = woort_atomic_load_explicit(
        &unit->m_life, WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    if (life == WOORT_MEM_UNIT_LIFE_RELEASED)
        return false;

    if (life == WOORT_MEM_UNIT_LIFE_PENDING)
        return true;

    if (life == WOORT_MEM_UNIT_LIFE_UNMARKED)
    {
        if ((unit->m_age != 15
            || unit->m_timing != woort_mem_gc_marking_round_counter)
            && 0 != (unit->m_attribute & WOORT_MEM_ATTRIB_NEED_SWEEP))
        {
            if (unit->m_attribute & WOORT_MEM_ATTRIB_FREE_CALLBACK)
                self->m_gc_ctx->m_user_free_callback(unit + 1);

            woort_atomic_store_explicit(
                &unit->m_life,
                WOORT_MEM_UNIT_LIFE_RELEASED,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

            if (page_may_null != NULL)
                woort_mem_drop_freed_unit_into_page(page_may_null, unit);

            return false;
        }
        /* Fall through to survival path */
    }

    assert(life != WOORT_MEM_UNIT_LIFE_SELF_MARKED);

    if (unit->m_age != 0)
        --unit->m_age;

    woort_atomic_store_explicit(
        &unit->m_life,
        WOORT_MEM_UNIT_LIFE_UNMARKED,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    return true;
}

static void woort_mem_gcworker_sweep_units_in_page(
    woort_mem_GCWorker* self, woort_mem_PageHead* page)
{
    bool drop_page = false;
    assert(!woort_atomic_load_explicit(
        &page->m_page_just_allocated,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

    if (page->m_page_count_if_huge == 0)
    {
        woort_mem_PageUnitAlloc* const page_alloc_head =
            (woort_mem_PageUnitAlloc*)(page + 1);

        const size_t unit_size_with_head =
            page_alloc_head->m_unit_size_in_page
            + sizeof(woort_mem_UnitHead);

        char* unit_storage =
            (char*)(page_alloc_head + 1);

        const size_t unit_count =
            (WOORT_MEM_NORMAL_PAGE_SIZE
             - sizeof(woort_mem_PageHead)
             - sizeof(woort_mem_PageUnitAlloc))
            / unit_size_with_head;

        bool has_survivor = false, has_free_space = false;

        const bool current_running_out =
            (bool)woort_atomic_load_explicit(
                &page_alloc_head->m_run_out,
                WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        for (size_t i = 0; i < unit_count; ++i)
        {
            woort_mem_UnitHead* unit =
                (woort_mem_UnitHead*)(
                    unit_storage + i * unit_size_with_head);

            if (woort_atomic_load_explicit(
                &unit->m_life,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED)
                == WOORT_MEM_UNIT_LIFE_RELEASED)
                continue;

            if (woort_mem_gcworker_check_and_free_unmarked_unit(
                self, unit, page))
            {
                has_survivor = true;
                self->m_alive_memory_size_counter += unit_size_with_head;
            }
            else
                has_free_space = true;
        }

        if (current_running_out && has_free_space)
        {
            if (!has_survivor)
                drop_page = true;
            else
            {
                woort_atomic_store_explicit(
                    &page_alloc_head->m_run_out,
                    0,
                    WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

                woort_mem_gpc_return_page(
                    &g_woort_mem_global_context.m_gpc,
                    page,
                    woort_mem_eval_group_by_small_unit_size(
                        page_alloc_head->m_unit_size_in_page));
            }
        }

        if (has_survivor)
        {
            self->m_alive_memory_size_counter +=
                sizeof(woort_mem_PageHead)
                + sizeof(woort_mem_PageUnitAlloc);
        }
        else
        {
            if (!current_running_out)
                page_alloc_head->m_mark_as_run_out_in_global_pool = true;
        }
    }
    else
    {
        if (!woort_mem_gcworker_check_and_free_unmarked_unit(
            self, (woort_mem_UnitHead*)(page + 1), NULL))
        {
            drop_page = true;
        }
        else
        {
            self->m_alive_memory_size_counter +=
                page->m_page_count_if_huge * WOORT_MEM_NORMAL_PAGE_SIZE;
        }
    }

    if (drop_page)
        woort_mem_chunk_free_page(
            &g_woort_mem_global_context.m_chunk, page);
    else
        woort_mem_global_context_add_page_back_to_into_chain(page);
}

static void woort_mem_gcworker_drain_queue_into_local(
    woort_mem_GCWorker* self)
{
    const size_t count = woort_mem_mpsc_drain(
        &self->m_gray_queue,
        self->m_drain_buf, WOORT_MEM_GRAY_QUEUE_CAPACITY);

    if (count != 0)
    {
        for (size_t i = 0; i < count; ++i)
        {
            woort_vector_push_back(
                &self->m_local_work, 1, &self->m_drain_buf[i]);
        }
    }
}

static void woort_mem_gcworker_process_gray_units(woort_mem_GCWorker* self)
{
    woort_spinlock_lock(&self->m_local_work_spin_for_root);
    woort_atomic_store_explicit(
        &self->m_is_draining, true, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
    woort_spinlock_unlock(&self->m_local_work_spin_for_root);

    while (true)
    {
        if (self->m_local_work.m_size == 0)
            woort_mem_gcworker_drain_queue_into_local(self);

        if (self->m_local_work.m_size == 0)
        {
            woort_atomic_store_explicit(
                &self->m_is_draining,
                false,
                WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
            return;
        }

        woort_mem_UnitHead* const unit =
            *(woort_mem_UnitHead**)woort_vector_at(
                &self->m_local_work, self->m_local_work.m_size - 1);
        woort_vector_erase_at(
            &self->m_local_work, self->m_local_work.m_size - 1);

        assert(WOORT_MEM_UNIT_LIFE_SELF_MARKED
            == woort_atomic_load_explicit(
                &unit->m_life, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));

        if (unit->m_attribute & WOORT_MEM_ATTRIB_MARK_CALLBACK)
        {
            self->m_gc_ctx->m_user_mark_callback(unit + 1);
        }
        if (unit->m_attribute & WOORT_MEM_ATTRIB_AUTO_MARK)
        {
            const size_t auto_mark_step =
                woort_mem_unit_get_available_size(unit) / sizeof(void*);

            void** const p = (void**)(unit + 1);
            for (size_t i = 0; i < auto_mark_step; ++i)
                woort_mem_mark_fuzzy_unit(p[i]);
        }

        woort_atomic_store_explicit(
            &unit->m_life,
            WOORT_MEM_UNIT_LIFE_FULL_MARKED,
            WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
    }
}

static void _woort_mem_gcworker_thread_entry(void* user_data)
{
    woort_mem_GCWorker* self = (woort_mem_GCWorker*)user_data;
    woort_mem_ThreadContext* tc = woort_mem_get_thread_context();

    tc->m_is_gc_worker_context = true;
    tc->m_gc_marking_context = self;
    self->m_worker_thread_id = woort_thread_current_id();

    self->m_gc_ctx->m_worker_entry_callback();

    do
    {
        if (!woort_mem_gc_wait_for_worker_launch_or_shutdown(
            self->m_gc_ctx, WOORT_MEM_WORKER_THRESHOLD_PARALLEL_MARK))
        {
            return;
        }
        else
            woort_mem_gcworker_process_gray_units(self);

        woort_mem_gc_worker_done_and_notify(self->m_gc_ctx);
        woort_mem_gc_wait_for_worker_launch(
            self->m_gc_ctx, WOORT_MEM_WORKER_THRESHOLD_FINAL_MARK);
        {
            woort_mem_gcworker_process_gray_units(self);
        }
        woort_mem_gc_worker_done_and_notify(self->m_gc_ctx);
        woort_mem_gc_wait_for_worker_launch(
            self->m_gc_ctx, WOORT_MEM_WORKER_THRESHOLD_SWEEP);
        {
            self->m_alive_memory_size_counter = 0;

            for (woort_mem_PageHead* page = self->m_sweep_page_list;
                 page != NULL;)
            {
                woort_mem_PageHead* const next_page = page->m_next_page;
                woort_mem_gcworker_sweep_units_in_page(self, page);
                page = next_page;
            }
        }
        woort_mem_gc_worker_done_and_notify(self->m_gc_ctx);

    } while (1);
}

static void woort_mem_gcworker_init(
    woort_mem_GCWorker* self, woort_mem_GC* gc_ctx)
{
    self->m_gc_ctx = gc_ctx;
    self->m_sweep_page_list = NULL;
    self->m_alive_memory_size_counter = 0;
    self->m_worker_thread_id = 0;

    woort_mem_mpsc_init(&self->m_gray_queue);
    woort_spinlock_init(&self->m_local_work_spin_for_root);
    woort_vector_init(&self->m_local_work, sizeof(woort_mem_UnitHead*));
    woort_atomic_init(&self->m_is_draining, 0);
    (void)woort_vector_reserve(
        &self->m_local_work, WOORT_MEM_GRAY_QUEUE_CAPACITY);

    if (!woort_thread_start(
        _woort_mem_gcworker_thread_entry, self, &self->m_gc_worker_thread))
    {
        WOORT_DEBUG("failed to start GC worker thread");
        abort();
    }
}

static void woort_mem_gcworker_deinit(
    woort_mem_GCWorker* self, woort_mem_GC* gc_ctx)
{
    woort_mem_gc_signal_worker_shutdown(gc_ctx);
    woort_thread_join(self->m_gc_worker_thread);

    woort_vector_deinit(&self->m_local_work);
    woort_spinlock_deinit(&self->m_local_work_spin_for_root);
}

/* ============================================================ */
/* Root marking callback for main thread                         */
/* ============================================================ */

typedef struct _woort_mem_root_mark_ctx
{
    woort_mem_GC* gc;
} _woort_mem_root_mark_ctx;

static bool _woort_mem_root_mark_callback(
    const void* key, void* value, void* user_data)
{
    (void)value;
    woort_mem_UnitHead* root_unit =
        *(woort_mem_UnitHead* const*)key;
    _woort_mem_root_mark_ctx* ctx =
        (_woort_mem_root_mark_ctx*)user_data;

    woort_mem_gc_mark_root_unit_to_gray(ctx->gc, root_unit);
    return true;
}

/* ============================================================ */
/* GC main thread                                                */
/* ============================================================ */

static void _woort_mem_gc_main_thread_entry(void* user_data)
{
    woort_mem_GC* self = (woort_mem_GC*)user_data;

    static const size_t GC_TRIGGER_NEW_ALLOC_RATIO_NUM = 1;
    static const size_t GC_TRIGGER_NEW_ALLOC_RATIO_DEN = 3;
    static const size_t GC_TRIGGER_MIN_EDGE = 1024 * 1024;

    self->m_main_entry_callback();

    do
    {
        /* Step 0: Wait with ratio-based early triggering */
        {
            const uint64_t cycle_start = woort_mem_monotonic_ms();

            while (true)
            {
                {
                    woort_mutex_lock(self->m_trigger_mx);
                    while (!woort_atomic_load_explicit(
                                &self->m_force_trigger_gc,
                                WOORT_ATOMIC_MEMORY_ORDER_RELAXED)
                        && !woort_atomic_load_explicit(
                                &self->m_shutdown,
                                WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
                    {
                        if (!woort_condition_variable_timed_wait(
                            self->m_trigger_cv, self->m_trigger_mx, 100))
                            break;
                    }
                    woort_mutex_unlock(self->m_trigger_mx);
                }

                if (woort_atomic_load_explicit(
                    &self->m_shutdown,
                    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
                    return;

                const uint64_t elapsed =
                    woort_mem_monotonic_ms() - cycle_start;
                if (elapsed >= 10000)
                    break;

                if (woort_atomic_load_explicit(
                    &self->m_force_trigger_gc,
                    WOORT_ATOMIC_MEMORY_ORDER_RELAXED))
                    break;

                const size_t alive =
                    GC_TRIGGER_MIN_EDGE
                        > woort_mem_gc_memory_size_after_last_round_sweep
                    ? GC_TRIGGER_MIN_EDGE
                    : woort_mem_gc_memory_size_after_last_round_sweep;

                const size_t new_alloc =
                    woort_atomic_load_explicit(
                        &self->m_new_allocated_size_since_last_gc,
                        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

                if (new_alloc * GC_TRIGGER_NEW_ALLOC_RATIO_DEN
                    >= alive * GC_TRIGGER_NEW_ALLOC_RATIO_NUM)
                    break;
            }
        }

        woort_atomic_store_explicit(
            &self->m_force_trigger_gc,
            false,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        woort_atomic_store_explicit(
            &self->m_new_allocated_size_since_last_gc,
            0,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

        /* Step 1: Update GC round and state */
        ++woort_mem_gc_marking_round_counter;
        woort_mem_gc_marking_state_flag = true;

        /* Step 2: GC start callback */
        self->m_gc_callback_at_begin();

        /* Mark all root units */
        woort_spinlock_lock(&self->m_root_gc_unit_set_mx);
        {
            _woort_mem_root_mark_ctx ctx = { .gc = self };
            (void)woort_hashmap_foreach(
                &self->m_root_gc_unit_set,
                _woort_mem_root_mark_callback,
                &ctx);
        }
        woort_spinlock_unlock(&self->m_root_gc_unit_set_mx);

        /* Step 3: Parallel mark */
        woort_mem_gc_launch_worker_and_wait_until_done(
            self, WOORT_MEM_WORKER_THRESHOLD_PARALLEL_MARK);

        /* Step 4: Stop marking callback */
        woort_mem_gc_marking_state_flag = false;
        self->m_gc_callback_at_stop_marking();

        /* Step 5: Final mark */
        woort_mem_gc_launch_worker_and_wait_until_done(
            self, WOORT_MEM_WORKER_THRESHOLD_FINAL_MARK);

        /* Step 6: Distribute pages for sweep */
        {
            void* old = woort_atomic_exchange_explicit(
                &g_woort_mem_global_context.m_all_page_list,
                NULL,
                WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);

            woort_mem_PageHead* all_pages = (woort_mem_PageHead*)old;

            if (all_pages != NULL)
            {
                size_t total_pages = 0;
                for (woort_mem_PageHead* p = all_pages;
                     p != NULL; p = p->m_next_page)
                    ++total_pages;

                const size_t base_count =
                    total_pages / self->m_gc_worker_count;
                const size_t remainder =
                    total_pages % self->m_gc_worker_count;

                woort_mem_PageHead* current = all_pages;
                for (size_t i = 0; i < self->m_gc_worker_count; ++i)
                {
                    const size_t n =
                        base_count + (i < remainder ? 1 : 0);

                    if (n == 0)
                    {
                        self->m_gc_worker_threads[i]
                            .m_sweep_page_list = NULL;
                        continue;
                    }

                    self->m_gc_worker_threads[i].m_sweep_page_list
                        = current;

                    woort_mem_PageHead* prev = NULL;
                    for (size_t j = 0; j < n; ++j)
                    {
                        prev = current;
                        current = current->m_next_page;
                    }
                    prev->m_next_page = NULL;
                }
            }
        }
        woort_mem_gc_launch_worker_and_wait_until_done(
            self, WOORT_MEM_WORKER_THRESHOLD_SWEEP);

        /* Step 7: Sum alive memory */
        {
            size_t total_alive_memory_size = 0;
            for (size_t i = 0; i < self->m_gc_worker_count; ++i)
            {
                total_alive_memory_size +=
                    self->m_gc_worker_threads[i]
                        .m_alive_memory_size_counter;
            }
            woort_mem_gc_memory_size_after_last_round_sweep =
                total_alive_memory_size;
        }

        woort_mem_gpc_remove_marked_run_out_pages(
            &g_woort_mem_global_context.m_gpc);

        self->m_gc_worker_threshold_launch_state =
            WOORT_MEM_WORKER_THRESHOLD_PENDING;

        woort_atomic_fetch_add_explicit(
            &self->m_gc_cycle_count, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
        woort_condition_variable_broadcast(self->m_trigger_cv);

    } while (1);
}

/* ============================================================ */
/* GC create / destroy                                           */
/* ============================================================ */

woort_mem_GC* woort_mem_gc_create(
    size_t worker_count,
    void (*callback_for_marking_root)(void),
    void (*callback_stop_marking)(void),
    void (*user_mark_callback)(void*),
    void (*user_free_callback)(void*),
    void (*main_entry_callback)(void),
    void (*worker_entry_callback)(void))
{
    woort_mem_GC* self =
        (woort_mem_GC*)malloc(sizeof(woort_mem_GC));
    if (self == NULL)
        return NULL;

    self->m_gc_worker_count =
        worker_count != 0 ? worker_count
                          : woort_mem_default_gc_worker_count();

    woort_atomic_init(&self->m_gc_assigned_thread_idx, 0);
    woort_atomic_init(&self->m_shutdown, false);
    woort_atomic_init(&self->m_worker_shutdown, false);

    self->m_gc_callback_at_begin = callback_for_marking_root;
    self->m_gc_callback_at_stop_marking = callback_stop_marking;
    self->m_user_mark_callback = user_mark_callback;
    self->m_user_free_callback = user_free_callback;
    self->m_main_entry_callback = main_entry_callback;
    self->m_worker_entry_callback = worker_entry_callback;

    self->m_gc_worker_threshold_launch_state =
        WOORT_MEM_WORKER_THRESHOLD_PENDING;
    self->m_gc_worker_threshold_finish_counter = 0;

    woort_atomic_init(&self->m_force_trigger_gc, false);
    woort_atomic_init(&self->m_gc_cycle_count, 0);
    woort_atomic_init(&self->m_new_allocated_size_since_last_gc, 0);

    woort_spinlock_init(&self->m_root_gc_unit_set_mx);
    woort_hashmap_init(
        &self->m_root_gc_unit_set,
        sizeof(woort_mem_UnitHead*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    if (!woort_mutex_create(&self->m_gc_worker_threshold_mx))
    {
        woort_hashmap_deinit(&self->m_root_gc_unit_set);
        woort_spinlock_deinit(&self->m_root_gc_unit_set_mx);
        free(self);
        return NULL;
    }
    if (!woort_condition_variable_create(
        &self->m_gc_worker_threshold_cv))
    {
        woort_mutex_destroy(self->m_gc_worker_threshold_mx);
        woort_hashmap_deinit(&self->m_root_gc_unit_set);
        woort_spinlock_deinit(&self->m_root_gc_unit_set_mx);
        free(self);
        return NULL;
    }

    if (!woort_mutex_create(&self->m_trigger_mx))
    {
        woort_condition_variable_destroy(self->m_gc_worker_threshold_cv);
        woort_mutex_destroy(self->m_gc_worker_threshold_mx);
        woort_hashmap_deinit(&self->m_root_gc_unit_set);
        woort_spinlock_deinit(&self->m_root_gc_unit_set_mx);
        free(self);
        return NULL;
    }
    if (!woort_condition_variable_create(&self->m_trigger_cv))
    {
        woort_mutex_destroy(self->m_trigger_mx);
        woort_condition_variable_destroy(self->m_gc_worker_threshold_cv);
        woort_mutex_destroy(self->m_gc_worker_threshold_mx);
        woort_hashmap_deinit(&self->m_root_gc_unit_set);
        woort_spinlock_deinit(&self->m_root_gc_unit_set_mx);
        free(self);
        return NULL;
    }

    self->m_gc_worker_threads = (woort_mem_GCWorker*)malloc(
        self->m_gc_worker_count * sizeof(woort_mem_GCWorker));
    if (self->m_gc_worker_threads == NULL)
        abort();

    for (size_t i = 0; i < self->m_gc_worker_count; ++i)
        woort_mem_gcworker_init(&self->m_gc_worker_threads[i], self);

    if (!woort_thread_start(
        _woort_mem_gc_main_thread_entry, self, &self->m_gc_main_thread))
    {
        WOORT_DEBUG("failed to start GC main thread");
        abort();
    }

    woort_mem_global_context_assign_marking_contexts(self);

    return self;
}

void woort_mem_gc_destroy(woort_mem_GC* self)
{
    woort_atomic_store_explicit(
        &self->m_shutdown, true, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
    woort_thread_join(self->m_gc_main_thread);

    woort_mem_global_context_clear_marking_contexts();

    woort_atomic_store_explicit(
        &self->m_worker_shutdown, true,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    for (size_t i = 0; i < self->m_gc_worker_count; ++i)
        woort_mem_gcworker_deinit(&self->m_gc_worker_threads[i], self);

    free(self->m_gc_worker_threads);

    woort_condition_variable_destroy(self->m_trigger_cv);
    woort_mutex_destroy(self->m_trigger_mx);
    woort_condition_variable_destroy(self->m_gc_worker_threshold_cv);
    woort_mutex_destroy(self->m_gc_worker_threshold_mx);

    woort_hashmap_deinit(&self->m_root_gc_unit_set);
    woort_spinlock_deinit(&self->m_root_gc_unit_set_mx);

    free(self);
}

/* ============================================================ */
/* Global context marking context helpers                        */
/* ============================================================ */

typedef struct _woort_mem_assign_mc_ctx
{
    woort_mem_GC* gc;
} _woort_mem_assign_mc_ctx;

static bool _woort_mem_assign_mc_callback(
    const void* key, void* value, void* user_data)
{
    (void)value;
    woort_mem_ThreadContext* thread_ctx =
        *(woort_mem_ThreadContext* const*)key;
    _woort_mem_assign_mc_ctx* ctx =
        (_woort_mem_assign_mc_ctx*)user_data;

    if (!thread_ctx->m_is_gc_worker_context)
        thread_ctx->m_gc_marking_context =
            woort_mem_gc_fetch_thread_worker(ctx->gc);

    return true;
}

void woort_mem_global_context_assign_marking_contexts(
    woort_mem_GC* gc)
{
    woort_spinlock_lock(
        &g_woort_mem_global_context.m_thread_entries_mx);
    {
        if (g_woort_mem_global_context.m_thread_entries_inited)
        {
            _woort_mem_assign_mc_ctx ctx = { .gc = gc };
            (void)woort_hashmap_foreach(
                &g_woort_mem_global_context.m_thread_entries,
                _woort_mem_assign_mc_callback,
                &ctx);
        }
    }
    woort_spinlock_unlock(
        &g_woort_mem_global_context.m_thread_entries_mx);
}

static bool _woort_mem_clear_mc_callback(
    const void* key, void* value, void* user_data)
{
    (void)value;
    (void)user_data;
    woort_mem_ThreadContext* thread_ctx =
        *(woort_mem_ThreadContext* const*)key;
    thread_ctx->m_gc_marking_context = NULL;
    return true;
}

void woort_mem_global_context_clear_marking_contexts(void)
{
    woort_spinlock_lock(
        &g_woort_mem_global_context.m_thread_entries_mx);
    {
        if (g_woort_mem_global_context.m_thread_entries_inited)
        {
            (void)woort_hashmap_foreach(
                &g_woort_mem_global_context.m_thread_entries,
                _woort_mem_clear_mc_callback,
                NULL);
        }
    }
    woort_spinlock_unlock(
        &g_woort_mem_global_context.m_thread_entries_mx);
}
