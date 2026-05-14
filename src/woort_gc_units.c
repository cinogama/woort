#include "woort.h"

#include "woort_gc_units.h"
#include "woort_vm.h"
#include "woort_diagnosis.h"

void _woort_GCUnit_alloc_failed(void)
{
    if (WOORT_t_this_thread_vm == NULL)
        // WTF? should not happend.
        woort_panic(WOORT_PANIC_OUT_OF_MEMORY, "Out of memory.");
    else
    {
        woort_Value * const osp = WOORT_t_this_thread_vm->m_sp;

        // Make sure all stack would be marked.
        WOORT_t_this_thread_vm->m_sp = WOORT_t_this_thread_vm->m_stack;

        woort_VMRuntime* const last_vm = woort_vm_swap(NULL);
        {
            woomem_gc_collect();
        }
        (void)woort_vm_swap(last_vm);

        WOORT_t_this_thread_vm->m_sp = osp;
    }
}