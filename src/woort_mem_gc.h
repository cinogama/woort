#pragma once

/*
woort_mem_gc.h
Concurrent mark-sweep garbage collector engine.
*/

#include "woort_mem_unit.h"
#include "woort_mem_page.h"
#include "woort_mem_mpsc.h"
#include "woort_mem_thread_context.h"
#include "woort_spin.h"
#include "woort_vector.h"
#include "woort_hashmap.h"
#include "woort_threads.h"
#include "woort_atomic.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum woort_mem_WorkerThresholdState
{
    WOORT_MEM_WORKER_THRESHOLD_PENDING,
    WOORT_MEM_WORKER_THRESHOLD_PARALLEL_MARK,
    WOORT_MEM_WORKER_THRESHOLD_FINAL_MARK,
    WOORT_MEM_WORKER_THRESHOLD_SWEEP,

} woort_mem_WorkerThresholdState;

struct woort_mem_GC;

typedef struct woort_mem_GCWorker
{
    struct woort_mem_GC*    m_gc_ctx;

    woort_mem_MpscGrayQueue m_gray_queue;

    woort_Spinlock          m_local_work_spin_for_root;
    woort_Vector            m_local_work;
    woort_AtomicUInt8       m_is_draining;
    woort_mem_UnitHead*     m_drain_buf[WOORT_MEM_GRAY_QUEUE_CAPACITY];

    woort_mem_PageHead*     m_sweep_page_list;
    size_t                  m_alive_memory_size_counter;

    woort_Thread*           m_gc_worker_thread;
    uint64_t                m_worker_thread_id;

} woort_mem_GCWorker;

typedef struct woort_mem_GC
{
    size_t              m_gc_worker_count;
    woort_AtomicSize    m_gc_assigned_thread_idx;
    woort_AtomicUInt8   m_shutdown;
    woort_AtomicUInt8   m_worker_shutdown;

    void                (*m_gc_callback_at_begin)(void);
    void                (*m_gc_callback_at_stop_marking)(void);
    void                (*m_user_mark_callback)(void*);
    void                (*m_user_free_callback)(void*);
    void                (*m_main_entry_callback)(void);
    void                (*m_worker_entry_callback)(void);

    woort_mem_WorkerThresholdState m_gc_worker_threshold_launch_state;
    size_t              m_gc_worker_threshold_finish_counter;
    woort_Mutex*        m_gc_worker_threshold_mx;
    woort_ConditionVariable* m_gc_worker_threshold_cv;

    woort_mem_GCWorker* m_gc_worker_threads;
    woort_Thread*       m_gc_main_thread;

    woort_AtomicUInt8   m_force_trigger_gc;
    woort_Mutex*        m_trigger_mx;
    woort_ConditionVariable* m_trigger_cv;
    woort_AtomicSize    m_gc_cycle_count;

    woort_Spinlock      m_root_gc_unit_set_mx;
    woort_HashMap       m_root_gc_unit_set;

    woort_AtomicSize    m_new_allocated_size_since_last_gc;

} woort_mem_GC;

extern woort_mem_GC* g_woort_mem_gc;

woort_mem_GC* woort_mem_gc_create(
    size_t worker_count,
    void (*callback_for_marking_root)(void),
    void (*callback_stop_marking)(void),
    void (*user_mark_callback)(void*),
    void (*user_free_callback)(void*),
    void (*main_entry_callback)(void),
    void (*worker_entry_callback)(void));
void woort_mem_gc_destroy(woort_mem_GC* self);

void woort_mem_gc_trigger_gc(woort_mem_GC* self, bool async);

void woort_mem_gc_register_root_unit_head(
    woort_mem_GC* self, woort_mem_UnitHead* unit_head);
void woort_mem_gc_unregister_root_unit_head(
    woort_mem_GC* self, woort_mem_UnitHead* unit_head);

void woort_mem_gc_mark_root_unit_to_gray(
    woort_mem_GC* self, woort_mem_UnitHead* unit_head);
woort_mem_GCWorker* woort_mem_gc_fetch_thread_worker(woort_mem_GC* self);

/* GCWorker methods */
void woort_mem_gcworker_mark_unit_to_gray(
    woort_mem_GCWorker* self, woort_mem_UnitHead* unit_head);
