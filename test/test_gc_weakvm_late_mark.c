/*
test_gc_weakvm_late_mark.c

Reproduction test for the weak-vm false-termination bug.

A weak VM survives a GC round only if its holder consumes GC_CHECK via
woort_GC_mark_weak_vm_manually() (typically from a GCHandle mark callback)
before the GC's abort-walk. Under concurrent marking, a unit's FIRST graying
performed by a mutator write barrier is enqueued into the TLS-assigned GC
worker's queue; once that worker has finished the parallel-mark drain, the
gray stays parked there and is only drained by the final-mark phase, meaning
the holder's mark callback runs AFTER the stop-marking phase.

If the abort-walk runs before final mark, a weak VM whose only gray was
parked gets TERMINATEd even though its holder did mark it.

How the parked gray is forced here, by synchronization rather than timing:

  * Every GC worker is held inside its parallel-mark drain. Each one is
    given a root unit whose GCHandle child has a mark callback that blocks
    until the test releases it, so the round cannot leave the parallel mark
    on its own.
  * The mutator thread is a RAW thread with its own TLS-assigned worker.
    Once every worker is inside its drain, it takes the mutex through which
    the GC main thread launches the workers and waits for them to report
    completion. While the test holds that mutex no worker can report, so
    the marking flag stays up for as long as the late write takes.
  * It releases the blockers and waits until no worker is draining: every
    worker, the assigned one included, has now left its parallel-mark drain.
  * Only then does it move the holder handle out of and back into a slot
    through the mixed write barrier. The barrier performs the holder unit's
    first graying of the round, the gray lands in the assigned worker's
    queue, and nothing drains that queue again before the final mark.
  * Releasing the mutex lets the round continue: the final mark drains the
    gray and runs the holder's mark callback after the stop-marking phase.

Expected results:

  * After the fix (abort-walk after final mark): the parked callback runs in
    final mark while GC_CHECK is still set, the weak VM survives and this
    test PASSES.
  * Before the fix (abort-walk before final mark): the abort-walk (stop-marking
    phase) consumes GC_CHECK before the parked callback runs, the held weak
    VM is TERMINATEd and this test FAILS.
  * An orphan weak VM with no holder at all must still be TERMINATEd, so the
    genuine reclamation path stays intact.

The scenario is verified rather than assumed: the test fails unless the
holder's callback ran exactly once and with marking already off. An earlier
version chased the window from a spinning mutator thread instead. That
thread lost the scheduling race in every one of its 300 rounds on a loaded
runner ("never managed to park a late gray"), and with a single GC worker,
the default below 16 logical CPUs, the holder was always already marked when
its late write came, so a green run did not show the parked path had been
taken.
*/

#ifdef WOORT_STATIC_LIB

/* 混含内部头（woort_mem_gc.h 等）需要完整版 woort_Value，
   与库内编译单元一致地声明 WOORT_IMPL；静态库下 WOORT_API 为空，链接不受影响 */
#define WOORT_IMPL 1

#include "woort.h"

#include "woort_atomic.h"
#include "woort_gc.h"
#include "woort_gc_gchandle.h"
#include "woort_mem.h"
#include "woort_mem_gc.h"
#include "woort_mem_thread_context.h"
#include "woort_threads.h"
#include "woort_vm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Every wait below gives up after this long, so a GC that no longer behaves
   as this test expects fails visibly instead of hanging the suite. */
enum
{
    WAIT_LIMIT_MS = 10000,
    BLOCKERS_PER_WORKER = 2,
};

static woort_VMRuntime* g_held_weak_vm = NULL;

static const woort_GCHandle* g_holder_handle = NULL;
static void*                g_holder_slot   = NULL;

/* One blocker per root unit: the unit is a root, the GCHandle it points to
   carries the blocking mark callback. */
static void**                 g_blocker_roots = NULL;
static const woort_GCHandle** g_blockers     = NULL;
static size_t                 g_blocker_count = 0;

static woort_AtomicUInt32 g_release_blockers;
static woort_AtomicUInt32 g_mutator_ready;

static int  g_parked_moves = 0;
static int  g_holder_callback_runs = 0;
static bool g_holder_callback_saw_marking = false;

/* Set by the mutator when the GC did not behave as this test expects. */
static const char* g_protocol_error = NULL;

/* Same non-atomic read the write barriers themselves perform. */
static bool marking_is_on(void)
{
    return *(volatile bool*)&woort_mem_gc_marking_state_flag;
}

static bool worker_is_draining(woort_mem_GCWorker* worker)
{
    return 0 != woort_atomic_load_explicit(
        &worker->m_is_draining,
        WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static bool all_workers_draining(void)
{
    for (size_t i = 0; i < g_woort_mem_gc->m_gc_worker_count; ++i)
    {
        if (!worker_is_draining(&g_woort_mem_gc->m_gc_worker_threads[i]))
            return false;
    }
    return true;
}

static bool no_worker_draining(void)
{
    for (size_t i = 0; i < g_woort_mem_gc->m_gc_worker_count; ++i)
    {
        if (worker_is_draining(&g_woort_mem_gc->m_gc_worker_threads[i]))
            return false;
    }
    return true;
}

static bool mutator_is_ready(void)
{
    return 0 != woort_atomic_load_explicit(
        &g_mutator_ready, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static bool wait_until(bool (*condition)(void))
{
    for (int waited = 0; waited < WAIT_LIMIT_MS; ++waited)
    {
        if (condition())
            return true;
        woort_thread_sleep_ms(1);
    }
    return condition();
}

static void holder_mark_function(void* user)
{
    (void)user;

    ++g_holder_callback_runs;
    if (marking_is_on())
        g_holder_callback_saw_marking = true;

    if (g_held_weak_vm != NULL)
        woort_GC_mark_weak_vm_manually(g_held_weak_vm);
}

static void holder_destruct_function(void* user)
{
    (void)user;
}

/* Runs on a GC worker inside its parallel-mark drain and keeps it there
   until the mutator releases the blockers. */
static void blocker_mark_function(void* user)
{
    (void)user;
    while (!woort_atomic_load_explicit(
        &g_release_blockers, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE))
    {
        woort_thread_sleep_ms(1);
    }
}

static void blocker_destruct_function(void* user)
{
    (void)user;
}

static void mutator_thread_entry(void* user_data)
{
    (void)user_data;

    woort_mem_GC* const gc = g_woort_mem_gc;

    /* Fix this thread's TLS-assigned GC worker before the round starts, so
       that nothing shifts the round's root-to-worker assignment. */
    (void)woort_mem_get_thread_context();
    woort_atomic_store_explicit(
        &g_mutator_ready, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    /* Wait for the round, then until every worker is inside its drain, each
       one parked in a blocker's mark callback. This state is stable: the
       round cannot proceed until the blockers are released. */
    if (!wait_until(&marking_is_on))
        g_protocol_error = "marking never turned on";
    else if (!wait_until(&all_workers_draining))
        g_protocol_error = "not every GC worker picked up a blocker root";

    if (g_protocol_error == NULL)
    {
        woort_mutex_lock(gc->m_gc_worker_threshold_mx);

        /* Let the workers finish their drains. No worker can report the end
           of the parallel mark while this thread holds the mutex, so the
           marking flag stays up. */
        woort_atomic_store_explicit(
            &g_release_blockers, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

        if (!wait_until(&no_worker_draining))
        {
            g_protocol_error =
                "a GC worker never left its parallel-mark drain";
        }
        else
        {
            /* Every worker has left its parallel-mark drain and marking is
               still on. The barrier performs the holder unit's first
               graying of the round; the gray parks in the assigned
               worker's queue until the final mark. The slot holds the
               handle to begin with, so taking it out marks the handle and
               putting it back is a no-op. */
            woort_GC_mixed_write_barrier_gcaddr(&g_holder_slot, NULL);
            woort_GC_mixed_write_barrier_gcaddr(
                &g_holder_slot, (void*)g_holder_handle);

            ++g_parked_moves;
        }

        woort_mutex_unlock(gc->m_gc_worker_threshold_mx);
    }

    /* Never leave the round blocked: from here on the blockers return at
       once, whatever happened above. */
    woort_atomic_store_explicit(
        &g_release_blockers, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    int failed = 0;
    static char dummy_user_handle;
    static char dummy_blocker_handle;

    woort_init(0, NULL);

    /* Keep a VM swapped in while building the structures, then leave it:
       a swapped-in VM would block the GC at its checkpoint forever. */
    woort_VMRuntime* const setup_vm = woort_vm_create();
    if (setup_vm == NULL)
    {
        printf("test_gc_weakvm_late_mark: FAILED (setup vm)\n");
        return 1;
    }
    (void)woort_vm_swap(setup_vm);

    woort_VMRuntime* const held_weak_vm = woort_vm_create();
    woort_VMRuntime* const orphan_weak_vm = woort_vm_create();
    if (held_weak_vm == NULL || orphan_weak_vm == NULL)
    {
        printf("test_gc_weakvm_late_mark: FAILED (weak vms)\n");
        return 1;
    }
    woort_VMRuntime_weaken(held_weak_vm);
    woort_VMRuntime_weaken(orphan_weak_vm);
    g_held_weak_vm = held_weak_vm;

    /* The holder is reachable from no root: nothing grays it while the
       workers drain the parallel mark. It survives each round only because
       the final mark drains the parked gray. */
    g_holder_handle = woort_GCHandle_new_with_marker(
        &dummy_user_handle,
        &holder_mark_function,
        &holder_destruct_function,
        NULL);
    if (g_holder_handle == NULL)
    {
        printf("test_gc_weakvm_late_mark: FAILED (gchandle)\n");
        return 1;
    }
    g_holder_slot = (void*)g_holder_handle;

    /* Blocker roots: two per GC worker, so that every worker gets at least
       one even if the round-robin root assignment is shifted. */
    g_blocker_count = BLOCKERS_PER_WORKER * g_woort_mem_gc->m_gc_worker_count;
    g_blocker_roots = (void**)malloc(g_blocker_count * sizeof(void*));
    g_blockers = (const woort_GCHandle**)malloc(
        g_blocker_count * sizeof(woort_GCHandle*));
    if (g_blocker_roots == NULL || g_blockers == NULL)
    {
        printf("test_gc_weakvm_late_mark: FAILED (blockers)\n");
        return 1;
    }
    for (size_t i = 0; i < g_blocker_count; ++i)
    {
        g_blockers[i] = woort_GCHandle_new_with_marker(
            &dummy_blocker_handle,
            &blocker_mark_function,
            &blocker_destruct_function,
            NULL);
        g_blocker_roots[i] = (void**)woort_GC_allocate_as_root(
            sizeof(void*), WOORT_GCALLOCATE_FLAG_AUTO_MARK);
        if (g_blockers[i] == NULL || g_blocker_roots[i] == NULL)
        {
            printf("test_gc_weakvm_late_mark: FAILED (blocker %zu)\n", i);
            return 1;
        }
        woort_GC_init_write_barrier_gcaddr(
            g_blocker_roots[i], (void*)g_blockers[i]);
    }

    (void)woort_vm_swap(NULL);

    woort_atomic_init(&g_release_blockers, 0);
    woort_atomic_init(&g_mutator_ready, 0);

    /* One round is enough: the round waits for the test. */
    woort_Thread* mutator = NULL;
    if (!woort_thread_start(&mutator_thread_entry, NULL, &mutator))
    {
        printf("test_gc_weakvm_late_mark: FAILED (mutator thread)\n");
        return 1;
    }
    (void)wait_until(&mutator_is_ready);

    woort_mem_trigger_gc(false);
    woort_thread_join(mutator);

    if (g_protocol_error != NULL)
    {
        printf("  [FAIL] the GC did not behave as this test expects: %s.\n",
               g_protocol_error);
        failed = 1;
    }

    if (g_parked_moves != 1)
    {
        printf("  [FAIL] the late write was not made after the parallel "
               "mark: parked_moves=%d.\n",
               g_parked_moves);
        failed = 1;
    }

    if (g_holder_callback_runs != 1 || g_holder_callback_saw_marking)
    {
        printf("  [FAIL] the holder's mark callback did not run in the final "
               "mark: runs=%d, saw marking=%d.\n",
               g_holder_callback_runs, (int)g_holder_callback_saw_marking);
        failed = 1;
    }

    if (woort_VMRuntime_request_check(
            held_weak_vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE))
    {
        printf("  [FAIL] held weak VM got TERMINATEd: its holder's mark "
               "callback only ran after the abort-walk.\n");
        failed = 1;
    }

    if (!woort_VMRuntime_request_check(
            orphan_weak_vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE))
    {
        printf("  [FAIL] orphan weak VM was NOT terminated: the genuine "
               "reclamation path is broken.\n");
        failed = 1;
    }

    printf("test_gc_weakvm_late_mark: workers=%zu parked_moves=%d "
           "holder_callback_runs=%d\n",
        g_woort_mem_gc->m_gc_worker_count, g_parked_moves,
        g_holder_callback_runs);
    printf("checks done (failed=%d).\n", failed);
    fflush(stdout);

    /* Drop the holder's reference before closing: the GCHandle's mark
       callback must not touch a freed VM during shutdown's final GC. */
    g_held_weak_vm = NULL;
    woort_vm_close(held_weak_vm);
    woort_vm_close(orphan_weak_vm);
    woort_vm_close(setup_vm);

    /* Drop the blocker roots so that shutdown's final rounds can reclaim
       them and their handles (it loops until alive memory reaches zero). */
    for (size_t i = 0; i < g_blocker_count; ++i)
        woort_mem_remove_from_root_set(g_blocker_roots[i]);
    free(g_blocker_roots);
    free(g_blockers);

    woort_shutdown(NULL, NULL);

    if (failed == 0)
        printf("test_gc_weakvm_late_mark: PASS\n");
    else
        printf("test_gc_weakvm_late_mark: FAILED\n");
    return failed;
}

#else

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
}

#endif
