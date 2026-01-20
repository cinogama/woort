#include "woort_vm.h"
#include "woort_threads.h"
#include "woort_value.h"
#include "woort_log.h"

WOORT_THREAD_LOCAL woort_VMRuntime* t_this_thread_vm = NULL;

const size_t WOORT_VM_DEFAULT_STACK_BEGIN_SIZE = 32;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm)
{
    // Init stack state.
    vm->m_stack = malloc(
        WOORT_VM_DEFAULT_STACK_BEGIN_SIZE * sizeof(woort_Value));
    
    if (vm->m_stack == NULL)
    {
        TODO;
    }

    vm->m_sb = vm->m_sp =
        vm->m_stack + (WOORT_VM_DEFAULT_STACK_BEGIN_SIZE - 1);
}
void woort_VMRuntime_deinit(woort_VMRuntime* vm)
{
}