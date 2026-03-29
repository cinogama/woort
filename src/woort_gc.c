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

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

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
void woort_GC_shutdown(void)
{
    woort_rwspinlock_deinit(&g_root_vms_to_mark_mx);
    woort_hashmap_deinit(&g_root_vms_to_mark);

    woomem_shutdown();
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
