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

Deterministic parking strategy used here:

  * The mutator thread feeds its own TLS-assigned worker with filler grays
    right after a round starts, so that worker surely enters (and later
    leaves) the parallel-mark drain with a wide, observable window.
  * The holder GCHandle sits in the last unit of a deep root chain, so its
    mark callback only fires when that chain gets drained (late in the
    parallel mark), or when the parked gray is drained (final mark).
  * Once the assigned worker has LEFT the parallel-mark drain while the
    marking flag is still up, the mutator moves the GCHandle out of and
    back into the chain slot. The write barrier performs the unit's first
    graying of this round, and the gray is enqueued into the sleeping
    worker's queue -> parked until final mark.

Expected results:

  * Before the fix: the abort-walk (stop-marking phase) consumes GC_CHECK
    before the parked callback runs, the held weak VM is TERMINATEd and
    this test FAILS.
  * After the fix (abort-walk moved after final mark): the parked callback
    runs in final mark while GC_CHECK is still set, the weak VM survives
    and this test PASSES.
  * An orphan weak VM with no holder at all must still be TERMINATEd, so
    the genuine reclamation path stays intact.
*/

#ifdef WOORT_STATIC_LIB

/* 混含内部头（woort_mem_gc.h 等）需要完整版 woort_Value，
   与库内编译单元一致地声明 WOORT_IMPL；静态库下 WOORT_API 为空，链接不受影响 */
#define WOORT_IMPL 1

#include "woort.h"

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

enum
{
    TEST_ROUNDS  = 300,
    CHAIN_DEPTH  = 4000,
    FILLER_COUNT = 2000,
};

static woort_VMRuntime* g_held_weak_vm   = NULL;
static woort_VMRuntime* g_orphan_weak_vm = NULL;

static const woort_GCHandle* g_holder_handle = NULL;
static void**                g_holder_slot   = NULL;

static void** g_filler_units[FILLER_COUNT];

static volatile bool          g_running      = false;
static volatile int           g_parked_moves = 0;
static woort_mem_GCWorker*    g_mutator_worker = NULL;

static void holder_mark_function(void* user)
{
    (void)user;
    if (g_held_weak_vm != NULL)
        woort_GC_mark_weak_vm_manually(g_held_weak_vm);
}

static void holder_destruct_function(void* user)
{
    (void)user;
}

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

static void mutator_thread_entry(void* user_data)
{
    (void)user_data;

    /* Fix this thread's TLS-assigned GC worker. */
    g_mutator_worker =
        woort_mem_get_thread_context()->m_gc_marking_context;

    while (g_running)
    {
        /* Wait for the next GC round. */
        while (g_running && !marking_is_on())
            woort_thread_yield();
        if (!g_running)
            break;

        /* Feed the assigned worker so its parallel-mark drain lasts long
           enough to observe, and detect the drain directly while feeding:
           a worker that drains faster than we enqueue may leave in the
           middle of the loop, so waiting for a "draining" edge AFTER the
           loop would miss it entirely.
           Every mark is flag-checked per iteration like a real write
           barrier, otherwise a straddling loop could enqueue grays after
           the final-mark drain and trip the sweep assertion. */
        bool saw_draining = false;
        for (size_t i = 0; i < FILLER_COUNT && marking_is_on(); ++i)
        {
            if (worker_is_draining(g_mutator_worker))
                saw_draining = true;
            else if (saw_draining)
                break;
            woort_mem_mark_unit_head(g_filler_units[i]);
        }

        if (saw_draining)
        {
            /* Drain the tail: wait for the worker to leave the drain. */
            while (g_running && marking_is_on()
                && worker_is_draining(g_mutator_worker))
                woort_thread_yield();

            if (g_running && marking_is_on())
            {
                /* The assigned worker is asleep now: move the holder
                   handle out of and back into the chain slot. The write
                   barrier does the unit's first graying of this round,
                   which parks in the sleeping worker's queue until
                   final mark. */
                woort_GC_mixed_write_barrier_gcaddr(g_holder_slot, NULL);
                woort_GC_mixed_write_barrier_gcaddr(
                    g_holder_slot, (void*)g_holder_handle);

                ++g_parked_moves;
            }
        }

        /* Wait for the round to finish. */
        while (g_running && marking_is_on())
            woort_thread_yield();
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    int failed = 0;
    static char dummy_user_handle;

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

    g_held_weak_vm   = woort_vm_create();
    g_orphan_weak_vm = woort_vm_create();
    if (g_held_weak_vm == NULL || g_orphan_weak_vm == NULL)
    {
        printf("test_gc_weakvm_late_mark: FAILED (weak vms)\n");
        return 1;
    }
    woort_VMRuntime_weaken(g_held_weak_vm);
    woort_VMRuntime_weaken(g_orphan_weak_vm);

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

    /* Deep root chain: root -> u1 -> ... -> uCHAIN_DEPTH,
       the last slot holds the holder GCHandle. */
    void** chain_root = (void**)woort_GC_allocate_as_root(
        sizeof(void*),
        WOORT_MEM_ATTRIB_NEED_SWEEP | WOORT_MEM_ATTRIB_AUTO_MARK);
    if (chain_root == NULL)
    {
        printf("test_gc_weakvm_late_mark: FAILED (chain root)\n");
        return 1;
    }
    void** chain_slot = chain_root;
    for (size_t i = 0; i < CHAIN_DEPTH; ++i)
    {
        void** unit = (void**)woort_mem_allocate_begin(sizeof(void*));
        if (unit == NULL)
        {
            printf("test_gc_weakvm_late_mark: FAILED (chain unit)\n");
            return 1;
        }
        woort_mem_allocate_end(
            unit, WOORT_MEM_ATTRIB_NEED_SWEEP | WOORT_MEM_ATTRIB_AUTO_MARK);
        *chain_slot = unit;
        chain_slot = unit;
    }
    *chain_slot = (void*)g_holder_handle;
    g_holder_slot = chain_slot;

    for (size_t i = 0; i < FILLER_COUNT; ++i)
    {
        g_filler_units[i] = (void**)woort_mem_allocate_begin(sizeof(void*));
        if (g_filler_units[i] == NULL)
        {
            printf("test_gc_weakvm_late_mark: FAILED (filler)\n");
            return 1;
        }
        /* NEED_SWEEP is required, otherwise the sweep never reclaims
           them and woort_shutdown() waits for zero memory forever. */
        woort_mem_allocate_end(
            g_filler_units[i], WOORT_MEM_ATTRIB_NEED_SWEEP);
        *g_filler_units[i] = NULL;
    }

    (void)woort_vm_swap(NULL);

    g_running = true;
    woort_Thread* mutator = NULL;
    if (!woort_thread_start(&mutator_thread_entry, NULL, &mutator))
    {
        g_running = false;
        printf("test_gc_weakvm_late_mark: FAILED (mutator thread)\n");
        return 1;
    }

    for (size_t round = 0;
         round < TEST_ROUNDS && g_parked_moves < 30;
         ++round)
    {
        woort_mem_trigger_gc(false);
        woort_thread_sleep_ms(1);
    }

    g_running = false;
    woort_thread_join(mutator);

    printf(
        "test_gc_weakvm_late_mark: rounds=%d parked_moves=%d\n",
        TEST_ROUNDS, g_parked_moves);

    if (g_parked_moves == 0)
    {
        printf("  [FAIL] never managed to park a late gray, "
               "round timing did not line up.\n");
        failed = 1;
    }

    if (woort_VMRuntime_request_check(
            g_held_weak_vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE))
    {
        printf("  [FAIL] held weak VM got TERMINATEd: its holder's mark "
               "callback only ran after the abort-walk.\n");
        failed = 1;
    }

    if (!woort_VMRuntime_request_check(
            g_orphan_weak_vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE))
    {
        printf("  [FAIL] orphan weak VM was NOT terminated: the genuine "
               "reclamation path is broken.\n");
        failed = 1;
    }

    printf("checks done (failed=%d).\n", failed);
    fflush(stdout);

    /* Drop the holder's reference before closing: the GCHandle's mark
       callback must not touch a freed VM during shutdown's final GC. */
    {
        woort_VMRuntime* const held_vm = g_held_weak_vm;
        g_held_weak_vm = NULL;
        woort_vm_close(held_vm);
    }
    woort_vm_close(g_orphan_weak_vm);
    woort_vm_close(setup_vm);

    /* Drop the root chain so shutdown's final rounds can reclaim every
       remaining unit (it loops until alive memory reaches zero). */
    woort_mem_remove_from_root_set(chain_root);

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
