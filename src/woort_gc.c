#include "woomem.h"

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

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

static woort_RWSpinlock g_root_vms_to_mark_mx;
static woort_HashMap /* struct woort_VMRuntime* */ g_root_vms_to_mark;

void _woort_GC_marker_callback(
    woomem_UserContext /* useless */_useless, void* unit)
{
    (void)_useless;

    woort_GCUnit* gcunit = unit;
    gcunit->m_proxy->m_marker(gcunit);
}
void _woort_GC_destroier_callback(
    woomem_UserContext /* useless */_useless, void* unit)
{
    (void)_useless;

    woort_GCUnit* gcunit = unit;
    gcunit->m_proxy->m_destructor(gcunit);
}

bool _woort_GC_walk_through_to_start_gc_vm_mark(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm_to_request_gc_mark =
        *(woort_VMRuntime* const*)key;

    const bool r = woort_VMRuntime_request_set(
        vm_to_request_gc_mark,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK);

    (void)r;
    assert(r);

    return true;
}

bool _woort_GC_walk_through_to_sync_vm_mark(
    const void* key,
    void* value,
    void* user_data)
{
    (void)user_data;
    (void)value;

    woort_VMRuntime* const vm_to_request_gc_mark =
        *(woort_VMRuntime* const*)key;

    while (woort_VMRuntime_request_check(
        vm_to_request_gc_mark,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK))
    {
        if (woort_VMRuntime_request_check(
            vm_to_request_gc_mark,
            WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE))
        {
            // This vm has leaved, we will mark it here.
            if (!woort_VMRuntime_request_set(
                vm_to_request_gc_mark,
                WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
            {
                // 就在刚刚检查的一瞬间，VM 已经开始处于标记工作中，跳出循环，等待 VM 标记工作完成。
                break;
            }

            if (woort_VMRuntime_request_accept(
                vm_to_request_gc_mark,
                WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK))
            {
                woort_VMRuntime_mark_vm_after_sync(vm_to_request_gc_mark);
            }
            // else: 否则，注意，如果发现此情况，说明 VM 就在刚刚的一瞬间，全都标记完成了（因为 
            // WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 能被成功设置，说明 VM 已经不在处理流程
            // 中），立即取消 WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 流程。

            if (!woort_VMRuntime_request_accept(
                vm_to_request_gc_mark,
                WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
            {
                // WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING 已经被 VM 接收，拉起
                woort_VMRuntime_wakeup(vm_to_request_gc_mark);
            }

            // 代理标记结束（或者 VM 已经完成标记，不需要等待）
            return true;
        }

        // Wait for some time.
        woort_thread_sleep_ms(10);
    }

    // 执行到此处，说明 VM 正在执行自标记，等待自标记完成
    if (woort_VMRuntime_request_accept(
        vm_to_request_gc_mark,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
    {
        // VM 正在标记流程中，等待直到标记完成
        woort_VMRuntime_hangup(vm_to_request_gc_mark);
    }

    return true;
}

void _woort_GC_start_gc_callback(void* /* useless */_useless)
{
    (void)_useless;

    woort_rwspinlock_read_lock(&g_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_root_vms_to_mark,
            &_woort_GC_walk_through_to_start_gc_vm_mark,
            NULL);

        (void)woort_hashmap_foreach(
            &g_root_vms_to_mark,
            &_woort_GC_walk_through_to_sync_vm_mark,
            NULL);
    }
    woort_rwspinlock_read_unlock(&g_root_vms_to_mark_mx);

    woort_CodeEnv_GC_mark_all_envs();
}
void woort_GC_bootup(void)
{
    woomem_init(
        NULL,
        &_woort_GC_marker_callback,
        &_woort_GC_destroier_callback,
        &_woort_GC_start_gc_callback,
        NULL);

    woort_rwspinlock_init(&g_root_vms_to_mark_mx);
    woort_hashmap_init(
        &g_root_vms_to_mark,
        sizeof(struct woort_VMRuntime*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);
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
    while (logged_count < 4
        && woort_VMRuntime_trace_next(&trace_iter, &trace))
    {
        woort_VMRuntime_log_trace(&trace);
        ++logged_count;
    }

    if (logged_count == 0)
        woort_log("        (no trace available)\n");

    /* Count remaining frames beyond the 32 logged. */
    size_t extra_frames = 0;
    while (woort_VMRuntime_trace_next(&trace_iter, &trace))
        ++extra_frames;

    if (extra_frames > 0)
        woort_log("        ... (%zu more frames not shown)\n", extra_frames);

    return true;
}

static bool _woort_GC_debug_callback_vm_walk(
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
        WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK);

    return true;
}

void _woort_GC_debug_callback_all_vm(void)
{
    woort_rwspinlock_read_lock(&g_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_root_vms_to_mark,
            &_woort_GC_debug_callback_vm_walk,
            NULL);
    }
    woort_rwspinlock_read_unlock(&g_root_vms_to_mark_mx);
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
        woort_rwspinlock_write_lock(&g_root_vms_to_mark_mx);
        const bool already_no_vm_exists = g_root_vms_to_mark.m_size == 0;
        {
            if (!already_no_vm_exists)
            {

                /* 向所有存活的 VM 发送 ABORT 请求 */
                (void)woort_hashmap_foreach(
                    &g_root_vms_to_mark,
                    &_woort_GC_shutdown_abort_vm_callback,
                    NULL);

                /* 限速：每秒最多一次警告，且仅在数量变化时输出 */
                const time_t now = time(NULL);
                if ((last_warning_time == 0 || now != last_warning_time)
                    && g_root_vms_to_mark.m_size != last_warning_vm_count)
                {
                    last_warning_time = now;
                    last_warning_vm_count = g_root_vms_to_mark.m_size;

                    woort_log(
                        "WOORT: %zu VM(s) have not been closed during shutdown.\n",
                        g_root_vms_to_mark.m_size);

                    _woort_GC_shutdown_dump_traces_context dump_ctx;
                    dump_ctx.m_remaining_quota = 3;
                    dump_ctx.m_total_vm_count = g_root_vms_to_mark.m_size;

                    (void)woort_hashmap_foreach(
                        &g_root_vms_to_mark,
                        &_woort_GC_shutdown_dump_vm_trace_callback,
                        &dump_ctx);

                    if (dump_ctx.m_total_vm_count > 3)
                        woort_log(
                            "    ... %zu more VM(s) not shown\n",
                            dump_ctx.m_total_vm_count - 3);
                }
            }
        }
        woort_rwspinlock_write_unlock(&g_root_vms_to_mark_mx);

        /* 触发一次完整的 GC 回收（标记 → 终结 → 清扫） */
        woomem_gc_collect();

        if (already_no_vm_exists
            && g_alive_unit_size_after_gc_scan == 0)
            break;
    }

    woomem_shutdown();

    woort_rwspinlock_deinit(&g_root_vms_to_mark_mx);
    woort_hashmap_deinit(&g_root_vms_to_mark);
}

WOORT_NODISCARD bool woort_GC_register_root_vm(struct woort_VMRuntime* vmruntime)
{
    bool result = true;
    woort_rwspinlock_write_lock(&g_root_vms_to_mark_mx);
    {
        switch (woort_hashmap_insert(&g_root_vms_to_mark, &vmruntime, NULL))
        {
        case WOORT_HASHMAP_RESULT_OK:
            break;
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            // Failed.
            result = false;
            break;
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        default:
            WOORT_DEBUG("Unexpected status, vm %p was already registered as root vm.", vmruntime);
            abort();
        }
    }
    woort_rwspinlock_write_unlock(&g_root_vms_to_mark_mx);
    return result;
}
void woort_GC_unregister_root_vm(struct woort_VMRuntime* vmruntime)
{
    woort_rwspinlock_write_lock(&g_root_vms_to_mark_mx);
    {
        if (!woort_hashmap_remove(&g_root_vms_to_mark, &vmruntime))
        {
            WOORT_DEBUG("Unexpected status, vm %p not been registered as root vm.", vmruntime);
            abort();
        }
    }
    woort_rwspinlock_write_unlock(&g_root_vms_to_mark_mx);
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

    woort_rwspinlock_read_lock(&g_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(
            &g_root_vms_to_mark,
            &_woort_GC_foreach_root_vm_callback_adapter,
            &ctx);
    }
    woort_rwspinlock_read_unlock(&g_root_vms_to_mark_mx);
}
