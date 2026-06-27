#include "woort.h"

#include "woort_gc_units.h"
#include "woort_vm.h"
#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_vector.h"
#include "woort_threads.h"

#include <assert.h>

woort_Mutex* _woort_gc_allocate_failed_log_mx;

WOORT_NODISCARD bool woort_GCUnit_bootup(void)
{
    return woort_mutex_create(&_woort_gc_allocate_failed_log_mx);
}

void woort_GCUnit_shutdown(void)
{
    woort_mutex_destroy(_woort_gc_allocate_failed_log_mx);
}

void _woort_GCUnit_alloc_failed(void)
{
    woort_Value* osp = NULL;
    woort_VMRuntime* current_vm = NULL;

    if (WOORT_t_this_thread_vm != NULL)
    {
        current_vm = WOORT_t_this_thread_vm;

        woort_mutex_lock(_woort_gc_allocate_failed_log_mx);
        {
            woort_log(WOORT_ANSI_HIY "GC Allocate failed " WOORT_ANSI_RST ", ");
            woort_VMRuntime_print_backtrace(current_vm, 8);
        }
        woort_mutex_unlock(_woort_gc_allocate_failed_log_mx);

        osp = current_vm->m_sp;
        current_vm->m_sp = current_vm->m_stack;

        (void)woort_vm_swap(NULL);
    }

    woomem_trigger_gc(false);

    if (current_vm != NULL)
    {
        assert(osp != NULL);

        (void)woort_vm_swap(current_vm);
        current_vm->m_sp = osp;
    }
}