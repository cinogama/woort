#include "woort.h"

#include "woort_gc.h"
#include "woort_vm.h"
#include "woort_hashmap.h"
#include "woort_util.h"
#include "woort_spin.h"
#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_gc_units.h"
#include "woort_codeenv.h"
#include "woort_threads.h"

#include "woort_mem.h"

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

/* Maximum number of stack frames to log per VM during shutdown trace dump. */
#define WOORT_GC_SHUTDOWN_TRACE_FRAMES_PER_VM 4
/* Maximum number of VMs to dump traces for during shutdown. */
#define WOORT_GC_SHUTDOWN_TRACE_VM_QUOTA 3

typedef struct woort_GCContext
{
    woort_RWSpinlock m_gc_stage_switch_lock;
    woort_RWSpinlock m_root_vms_to_mark_mx;
    woort_Spinlock m_not_been_marked_weak_vm_mx;

    woort_HashMap /* struct woort_VMRuntime* */ m_root_vms_to_mark;
    woort_HashMap /* struct woort_VMRuntime* */ m_not_been_marked_weak_vm;

    woort_AtomicFlag m_raise_debug_request_in_next_round_gc_flag;
} woort_GCContext;

static woort_GCContext g_gc_context;
static WOORT_THREAD_LOCAL bool WOORT_t_in_gc_external_marking_sync_guard = false;

#ifndef NDEBUG

WOORT_NODISCARD bool _woort_GC_Debug_current_thread_in_scope(void)
{
    return (WOORT_t_this_thread_vm != NULL || WOORT_t_in_gc_external_marking_sync_guard);
}
#endif

static void _woort_GC_marker_callback(void* unit)
{
    woort_GCUnit* gcunit = unit;
    gcunit->m_proxy->m_marker(gcunit);
}
static void _woort_GC_destroyer_callback(void* unit)
{
    woort_GCUnit* gcunit = unit;
    gcunit->m_proxy->m_destructor(gcunit);
}

static bool _woort_GC_walk_through_to_start_gc_vm_mark(
    const void* key,
    void* value,
    void* pmark_debug_request)
{
    (void)value;

    woort_VMRuntime* const vm_to_request_gc_mark =
        *(woort_VMRuntime* const*)key;

    const bool mark_debug_request = *(const bool*)pmark_debug_request;
    if (mark_debug_request)
        (void)woort_VMRuntime_request_set(
            vm_to_request_gc_mark,
            WOORT_VMRUNTIME_CHECK_REQUEST_EXTERNAL_DEBUG_BREAK);

    const bool r = woort_VMRuntime_request_set(
        vm_to_request_gc_mark,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK);

    (void)r;
    assert(r);

    return true;
}

static void _woort_GC_mark_vm_proxy(woort_VMRuntime* vm_to_request_gc_mark, bool skip_weak)
{
    while (woort_VMRuntime_request_check(
        vm_to_request_gc_mark,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK))
    {
        if (woort_VMRuntime_request_check(
            vm_to_request_gc_mark,
            WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE))
        {
            /* This vm has leaved, we will mark it here. */
            if (!woort_VMRuntime_request_set(
                vm_to_request_gc_mark,
                WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
            {
                /* 就在刚刚检查的一瞬间，VM 已经开始处于标记工作中，跳出循环，等待 VM 标记工作完成。*/
                break;
            }

            /*
            NOTE: 对于 Weak 的虚拟机，我们在此跳过代理标记，并且寄希望于其能够被其
                实际持有者通过 woort_vm_mark_weak_manually 执行标记。

            NOTE: 与旧 Woolang 不同，Woort 没有 VM 池机制，因此不必担心 Weak 标记撤回
                导致的 Missing mark。

            NOTE: 如果内存不足导致无法记录 m_not_been_marked_weak_vm，则直接代理标记
                避免出现奇怪的问题。
            */
            if (skip_weak
                && vm_to_request_gc_mark->m_is_weak)
            {
                woort_spinlock_lock(&g_gc_context.m_not_been_marked_weak_vm_mx);

                const woort_hashmap_Result insert_result = woort_hashmap_insert(
                    &g_gc_context.m_not_been_marked_weak_vm, &vm_to_request_gc_mark, NULL);

                woort_spinlock_unlock(&g_gc_context.m_not_been_marked_weak_vm_mx);

                if (insert_result == WOORT_HASHMAP_RESULT_OK)
                {
                    /* 我们将在所有标记工作结束之后检查此清单，并为所有未标记的 Weak VM 执行后续处理工作. */

                    if (woort_VMRuntime_request_accept(
                        vm_to_request_gc_mark,
                        WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
                        return;

                    /*
                    NOTE: WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 已经被 VM 接收
                        说明此 VM 仍在执行，考虑到：woort_VMRuntime_handle_gc_check_request_and_mark
                        的实现，如果在此时 WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 被接收，VM 的自
                        我标记阶段将不会执行——这会导致大问题。

                        实际上，此处我们有两个选择，一个是因为 WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK
                        依然生效，我们可以简单地把虚拟机唤醒，然后像是等待普通的虚拟机工作一般，直到
                        其在下一个检查点完成标记

                        我们在此处选择第二个方案，直接继续执行代理标记工作。
                    */
                }
                else
                    assert(insert_result == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY);
            }

            if (woort_VMRuntime_request_accept(
                vm_to_request_gc_mark,
                WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK))
            {
                if (skip_weak)
                    woort_VMRuntime_mark_vm_after_sync(vm_to_request_gc_mark);
                else
                    woort_VMRuntime_mark_weak_vm_after_sync(vm_to_request_gc_mark);
            }
            /* else: 否则，注意，如果发现此情况，说明 VM 就在刚刚的一瞬间，全都标记完成了（因为
            WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 能被成功设置，说明 VM 已经不在处理流程
            中），立即取消 WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 流程。 */

            if (!woort_VMRuntime_request_accept(
                vm_to_request_gc_mark,
                WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
            {
                /* WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 已经被 VM 接收，拉起*/
                woort_VMRuntime_wakeup(vm_to_request_gc_mark);
            }

            /* 代理标记结束（或者 VM 已经完成标记，不需要等待）*/
            return;
        }

        /* Wait for some time.*/
        woort_thread_yield();
    }

    /* 执行到此处，说明 VM 正在执行自标记，等待自标记完成*/
    if (woort_VMRuntime_request_accept(
        vm_to_request_gc_mark,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
    {
        /* VM 正在标记流程中，等待直到标记完成*/
        woort_VMRuntime_hangup(vm_to_request_gc_mark);
    }
}

static bool _woort_GC_walk_through_to_sync_vm_mark(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm_to_request_gc_mark =
        *(woort_VMRuntime* const*)key;

    _woort_GC_mark_vm_proxy(vm_to_request_gc_mark, true);

    return true;
}

static void _woort_GC_stage_switch_sync()
{
    woort_rwspinlock_write_lock(&g_gc_context.m_gc_stage_switch_lock);
    woort_rwspinlock_write_unlock(&g_gc_context.m_gc_stage_switch_lock);
}

static void _woort_GC_start_callback(void)
{
    _woort_GC_stage_switch_sync();

    const bool mark_all_vm_debug_break_down =
        !woort_atomic_flag_test_and_set_explicit(
            &g_gc_context.m_raise_debug_request_in_next_round_gc_flag,
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    woort_rwspinlock_read_lock(&g_gc_context.m_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_walk_through_to_start_gc_vm_mark,
            (void*)&mark_all_vm_debug_break_down);

        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_walk_through_to_sync_vm_mark,
            NULL);
    }
    woort_rwspinlock_read_unlock(&g_gc_context.m_root_vms_to_mark_mx);

    woort_CodeEnv_GC_mark_all_envs();
}

static bool _woort_GC_walk_through_to_sync_finish_mark(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_MARK_FINISHED);

    do
    {
        if (woort_VMRuntime_request_check(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE))
        {
            /* This VM leaved, ignore. */
            break;
        }
    } while (woort_VMRuntime_request_check(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_MARK_FINISHED));

    return true;
}

static bool _woort_GC_walk_through_to_abort_vm(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm_to_abort =
        *(woort_VMRuntime* const*)key;

    /* This vm no marked, abort it. */
    if (woort_VMRuntime_request_accept(vm_to_abort, WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK))
    {
        (void)woort_VMRuntime_request_set(
            vm_to_abort, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE);
    }

    return true;
}

static void _woort_GC_stop_mark_callback(void)
{
    woort_spinlock_lock(&g_gc_context.m_not_been_marked_weak_vm_mx);
    {
        (void)woort_hashmap_foreach(
            &g_gc_context.m_not_been_marked_weak_vm,
            &_woort_GC_walk_through_to_abort_vm,
            NULL);

        woort_hashmap_clear(&g_gc_context.m_not_been_marked_weak_vm);
    }
    woort_spinlock_unlock(&g_gc_context.m_not_been_marked_weak_vm_mx);

    woort_rwspinlock_read_lock(&g_gc_context.m_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_walk_through_to_sync_finish_mark,
            NULL);
    }
    woort_rwspinlock_read_unlock(&g_gc_context.m_root_vms_to_mark_mx);

    _woort_GC_stage_switch_sync();
}

static void _woort_GC_thread_entry(void)
{
    assert(!WOORT_t_in_gc_external_marking_sync_guard);
    WOORT_t_in_gc_external_marking_sync_guard = true;
}

WOORT_NODISCARD bool woort_GC_bootup(size_t reserving_memory_size)
{
    if (!woort_mem_init(
        reserving_memory_size,
        &_woort_GC_start_callback,
        &_woort_GC_stop_mark_callback,
        &_woort_GC_marker_callback,
        &_woort_GC_destroyer_callback,
        &_woort_GC_thread_entry,
        &_woort_GC_thread_entry))
    {
        return false;
    }

    woort_rwspinlock_init(&g_gc_context.m_gc_stage_switch_lock);
    woort_rwspinlock_init(&g_gc_context.m_root_vms_to_mark_mx);
    woort_spinlock_init(&g_gc_context.m_not_been_marked_weak_vm_mx);
    woort_hashmap_init(
        &g_gc_context.m_root_vms_to_mark,
        sizeof(struct woort_VMRuntime*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    woort_hashmap_init(
        &g_gc_context.m_not_been_marked_weak_vm,
        sizeof(struct woort_VMRuntime*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    /* Mark flag dirty. */
    (void)woort_atomic_flag_test_and_set(
        &g_gc_context.m_raise_debug_request_in_next_round_gc_flag);

    return true;
}
static bool _woort_GC_shutdown_abort_vm_callback(
    const void* key,
    void* value,
    /* OPTIONAL */ void* user_data)
{
    (void)value;
    (void)user_data;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    (void)woort_VMRuntime_request_set(
        vm,
        WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE);

    return true;
}

typedef struct _woort_GC_shutdown_dump_traces_context
{
    size_t m_remaining_quota;
    size_t m_total_vm_count;
} _woort_GC_shutdown_dump_traces_context;

static bool _woort_GC_shutdown_dump_vm_trace_callback(
    const void* key,
    void* value,
    /* OPTIONAL */ void* user_data)
{
    (void)value;

    _woort_GC_shutdown_dump_traces_context* const ctx =
        (_woort_GC_shutdown_dump_traces_context*)user_data;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    if (ctx->m_remaining_quota == 0)
        return true;

    --ctx->m_remaining_quota;

    woort_log("    VM %p trace:\n", (void*)vm);

    woort_VMRuntime_TraceCallstack_Iter trace_iter;
    woort_VMRuntime_TraceCallstack trace;
    size_t logged_count = 0;

    woort_VMRuntime_trace_begin(vm, &trace_iter);
    while (logged_count < WOORT_GC_SHUTDOWN_TRACE_FRAMES_PER_VM
        && woort_VMRuntime_trace_next(&trace_iter, &trace))
    {
        woort_VMRuntime_log_trace(&trace);
        ++logged_count;
    }

    if (logged_count == 0)
        woort_log("        (no trace available)\n");

    /* Count remaining frames beyond the WOORT_GC_SHUTDOWN_TRACE_FRAMES_PER_VM logged. */
    size_t extra_frames = 0;
    while (woort_VMRuntime_trace_next(&trace_iter, &trace))
        ++extra_frames;

    if (extra_frames > 0)
        woort_log("        ... (%zu more frames not shown)\n", extra_frames);

    return true;
}

void woort_GC_shutdown(void)
{
    /*
    * 等待所有 VM 关闭，并排空 GC 待释放单元。
    * 参见旧实现 wo_finish 中的同类流程。
    */
    woort_CodeEnv_drop_all();

    time_t last_warning_time = 0;
    size_t last_warning_vm_count = 0;

    for (;;)
    {
        woort_rwspinlock_write_lock(&g_gc_context.m_root_vms_to_mark_mx);
        const bool already_no_vm_exists = g_gc_context.m_root_vms_to_mark.m_size == 0;
        {
            if (!already_no_vm_exists)
            {
                /* 向所有存活的 VM 发送 ABORT 请求 */
                (void)woort_hashmap_foreach(
                    &g_gc_context.m_root_vms_to_mark,
                    &_woort_GC_shutdown_abort_vm_callback,
                    NULL);

                /* 限速：每秒最多一次警告，且仅在数量变化时输出 */
                const time_t now = time(NULL);
                if ((last_warning_time == 0 || now != last_warning_time)
                    && g_gc_context.m_root_vms_to_mark.m_size != last_warning_vm_count)
                {
                    last_warning_time = now;
                    last_warning_vm_count = g_gc_context.m_root_vms_to_mark.m_size;

                    woort_log(
                        "WOORT: %zu VM(s) have not been closed during shutdown.\n",
                        g_gc_context.m_root_vms_to_mark.m_size);

                    _woort_GC_shutdown_dump_traces_context dump_ctx;
                    dump_ctx.m_remaining_quota = WOORT_GC_SHUTDOWN_TRACE_VM_QUOTA;
                    dump_ctx.m_total_vm_count = g_gc_context.m_root_vms_to_mark.m_size;

                    (void)woort_hashmap_foreach(
                        &g_gc_context.m_root_vms_to_mark,
                        &_woort_GC_shutdown_dump_vm_trace_callback,
                        &dump_ctx);

                    if (dump_ctx.m_total_vm_count > 3)
                        woort_log(
                            "    ... %zu more VM(s) not shown\n",
                            dump_ctx.m_total_vm_count - 3);
                }
            }
        }
        woort_rwspinlock_write_unlock(&g_gc_context.m_root_vms_to_mark_mx);

        /* 触发一次完整的 GC 回收（标记 → 终结 → 清扫） */
        woort_mem_trigger_gc(false);

        if (already_no_vm_exists
            && woort_mem_gc_memory_size_after_last_round_sweep == 0)
            break;
    }

    woort_mem_shutdown();

    woort_rwspinlock_deinit(&g_gc_context.m_gc_stage_switch_lock);
    woort_rwspinlock_deinit(&g_gc_context.m_root_vms_to_mark_mx);
    woort_spinlock_deinit(&g_gc_context.m_not_been_marked_weak_vm_mx);
    woort_hashmap_deinit(&g_gc_context.m_root_vms_to_mark);
    woort_hashmap_deinit(&g_gc_context.m_not_been_marked_weak_vm);
}

WOORT_NODISCARD bool woort_GC_register_root_vm(struct woort_VMRuntime* vmruntime)
{
    bool result = true;
    woort_rwspinlock_write_lock(&g_gc_context.m_root_vms_to_mark_mx);
    {
        switch (woort_hashmap_insert(&g_gc_context.m_root_vms_to_mark, &vmruntime, NULL))
        {
        case WOORT_HASHMAP_RESULT_OK:
            break;
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            /* Failed. */
            result = false;
            break;
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        default:
            WOORT_DEBUG("Unexpected status, vm %p was already registered as root vm.", vmruntime);
            abort();
        }
    }
    woort_rwspinlock_write_unlock(&g_gc_context.m_root_vms_to_mark_mx);
    return result;
}
void woort_GC_unregister_root_vm(struct woort_VMRuntime* vmruntime)
{
    woort_rwspinlock_write_lock(&g_gc_context.m_root_vms_to_mark_mx);
    {
        if (vmruntime->m_is_weak)
        {
            woort_spinlock_lock(&g_gc_context.m_not_been_marked_weak_vm_mx);
            (void)woort_hashmap_remove(&g_gc_context.m_not_been_marked_weak_vm, &vmruntime);
            woort_spinlock_unlock(&g_gc_context.m_not_been_marked_weak_vm_mx);
        }

        if (!woort_hashmap_remove(&g_gc_context.m_root_vms_to_mark, &vmruntime))
        {
            WOORT_DEBUG("Unexpected status, vm %p not been registered as root vm.", vmruntime);
            abort();
        }
    }
    woort_rwspinlock_write_unlock(&g_gc_context.m_root_vms_to_mark_mx);
}

typedef struct _woort_GC_ForeachContext
{
    woort_GC_ForeachRootVMCallback m_callback;
    void* m_user_data;
} _woort_GC_ForeachContext;

static bool _woort_GC_foreach_root_vm_callback_adapter(
    const void* key,
    void* value,
    void* user_data)
{
    (void)value;

    _woort_GC_ForeachContext* ctx =
        (_woort_GC_ForeachContext*)user_data;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    return ctx->m_callback(vm, ctx->m_user_data);
}

void woort_GC_foreach_root_vm(
    woort_GC_ForeachRootVMCallback callback,
    void* user_data)
{
    _woort_GC_ForeachContext ctx;
    ctx.m_callback = callback;
    ctx.m_user_data = user_data;

    woort_rwspinlock_read_lock(&g_gc_context.m_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_foreach_root_vm_callback_adapter,
            &ctx);
    }
    woort_rwspinlock_read_unlock(&g_gc_context.m_root_vms_to_mark_mx);
}

void woort_GC_mark_weak_vm_manually(woort_VMRuntime* vm)
{
    assert(vm->m_is_weak);

    /* NOTE: 此处使用 _woort_GC_mark_vm_proxy 是为了确保：

            * 如果 VM 此时恰好进入 dispatch，并且正在自行标记；

        想象此时：

            * 如果 GC 线程此时完成最后一个灰色单元的 Fullmark

        自行标记期间的单元可能发生 Missing Mark；因此，为了保证 GC
        的 Gray2Black 标记流程正确收尾，需要确保此处的标记操作 VM 的
        自行标记总是早于 woort_vm_mark_weak_manually 返回，做一个同步。
    */
    _woort_GC_mark_vm_proxy(vm, false);
}

void woort_GC_mark_droped_env_manually(
    const woort_CodeEnv* env)
{
    woort_mem_mark_unit_head((void*)env);
}

void woort_GC_mark_internal_value_manually(
    const woort_Value* val)
{
    woort_mem_mark_fuzzy_unit_head(val->m_gcinstance);
}

void woort_GC_set_internal_value_with_mixed_write_barrier(
    woort_Value* dst, const woort_Value* val)
{
    woort_GC_mixed_write_barrier_value(dst, *val);
}

void woort_GC_internal_value_delete_barrier(
    const woort_Value* dst)
{
    woort_GC_delete_barrier_value(*dst);
}

WOORT_NODISCARD void* woort_GC_allocate(size_t sz, int attribute)
{
    void* const p = woort_GCUnit_alloc_delay_init(sz);

    if (attribute & WOORT_GCALLOCATE_FLAG_AUTO_MARK)
        woort_GCUnit_init_delay_alloc(A, p);
    else
        woort_GCUnit_init_delay_alloc(O, p);

    return p;
}

WOORT_NODISCARD void* woort_GC_allocate_as_root(size_t sz, int attribute)
{
    void* const p = woort_GCUnit_alloc_delay_init(sz);

    if (attribute & WOORT_GCALLOCATE_FLAG_AUTO_MARK)
        woort_mem_allocate_end_as_root(p, WOORT_MEM_ATTRIB_NEED_SWEEP | WOORT_MEM_ATTRIB_AUTO_MARK);
    else
        woort_mem_allocate_end_as_root(p, WOORT_MEM_ATTRIB_NEED_SWEEP);

    return p;
}

void woort_GC_unregister_root(void* p)
{
    woort_mem_remove_from_root_set(p);
}

void woort_GC_mark_addr_manually(/* OPTIONAL */ void* p)
{
    woort_mem_mark_fuzzy_unit(p);
}

void woort_GC_set_addr_with_mixed_write_barrier(void** dst, void* p)
{
    woort_GC_mixed_write_barrier_gcaddr(dst, p);
}

void woort_GC_addr_delete_barrier(const void* p)
{
    woort_GC_delete_barrier_gcaddr((void*)p);
}

WOORT_NODISCARD bool woort_GC_sync_marking_lock(void)
{
    if (WOORT_t_this_thread_vm == NULL && !WOORT_t_in_gc_external_marking_sync_guard)
    {
        woort_rwspinlock_read_lock(&g_gc_context.m_gc_stage_switch_lock);

        WOORT_t_in_gc_external_marking_sync_guard = true;
        return true;
    }
    return false;
}

void woort_GC_sync_marking_unlock(void)
{
    assert(WOORT_t_this_thread_vm == NULL && WOORT_t_in_gc_external_marking_sync_guard);

    WOORT_t_in_gc_external_marking_sync_guard = false;
    woort_rwspinlock_read_unlock(&g_gc_context.m_gc_stage_switch_lock);
}

static bool _woort_GC_walk_through_to_send_suspend(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_SUSPEND);

    return true;
}

static bool _woort_GC_walk_through_to_sync_suspend(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    do
    {
        if (woort_VMRuntime_request_check(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE))
        {
            break;
        }

        woort_thread_yield();

    } while (woort_VMRuntime_request_check(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_SUSPEND));

    return true;
}

static bool _woort_GC_walk_through_to_send_resume(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm =
        *(woort_VMRuntime* const*)key;

    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_RESUME);

    return true;
}

void woort_GC_suspend_all_vm_to_do_sth(
    woort_GC_SuspendVMJobCallback callback,
    void* user_data)
{
    woort_VMRuntime* const last = woort_VMRuntime_swap(NULL);

    woort_rwspinlock_read_lock(&g_gc_context.m_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_walk_through_to_send_suspend,
            NULL);

        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_walk_through_to_sync_suspend,
            NULL);

        callback(user_data);

        (void)woort_hashmap_foreach(
            &g_gc_context.m_root_vms_to_mark,
            &_woort_GC_walk_through_to_send_resume,
            NULL);
    }
    woort_rwspinlock_read_unlock(&g_gc_context.m_root_vms_to_mark_mx);

    (void)woort_VMRuntime_swap(last);
}

void woort_GC_raise_debug_request_in_next_round(void)
{
    woort_atomic_flag_clear_explicit(
        &g_gc_context.m_raise_debug_request_in_next_round_gc_flag,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
}
