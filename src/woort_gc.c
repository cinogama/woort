#include "woort_gc.h"
#include "woort_vm.h"
#include "woort_hashmap.h"
#include "woort_util.h"
#include "woort_spin.h"
#include "woort_diagnosis.h"
#include "woort_log.h"

#include <stdbool.h>

woort_RWSpinlock g_root_vms_to_mark_mx;
woort_HashMap /* struct woort_VMRuntime* */ g_root_vms_to_mark;

void woort_GC_bootup(void)
{
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
    return reult;
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

void _woort_GC_walk_through_to_sync_gc_vm()
{

}

void woort_GC_start_gc_callback(void* /* useless */)
{
    woort_rwspinlock_read_lock(&g_root_vms_to_mark_mx);
    {
        (void)woort_hashmap_foreach(&g_root_vms_to_mark, )
    }
    woort_rwspinlock_read_unlock(&g_root_vms_to_mark_mx);
}
void woort_GC_end_gc_callback(void* /* useless */)
{

}
