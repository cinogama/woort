#include "woort_vm.h"
#include "woort_threads.h"
#include "woort_value.h"
#include "woort_log.h"
#include "woort_codeenv.h"

#include <assert.h>

WOORT_THREAD_LOCAL woort_VMRuntime* t_this_thread_vm = NULL;

const size_t WOORT_VM_DEFAULT_STACK_BEGIN_SIZE = 32;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm)
{
    // Init stack state.
    vm->m_stack = malloc(
        WOORT_VM_DEFAULT_STACK_BEGIN_SIZE * sizeof(woort_Value));
    
    if (vm->m_stack == NULL)
    {
        WOORT_DEBUG("Out of memory");
        return false;
    }

    vm->m_stack_end = vm->m_stack + WOORT_VM_DEFAULT_STACK_BEGIN_SIZE;
    vm->m_sb = vm->m_sp = vm->m_stack_end - 1;

    // Init runtime state.
    vm->m_ip = NULL;
    vm->m_env = NULL;

    return true;
}
void woort_VMRuntime_deinit(woort_VMRuntime* vm)
{
    if (vm->m_stack != NULL)
    {
        free(vm->m_stack);
    }
}

WOORT_NODISCARD woort_VMRuntime_CallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm);

WOORT_NODISCARD woort_VMRuntime_CallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func)
{
    if (!woort_CodeEnv_find(func, &vm->m_env))
        return WOORT_VM_CALL_STATUS_TBD_BAD_STATUS;
    
    // Push call stack info here.
    /*
        [  SP AFTER CALL ]
        [  CALL CONTEXT  ] ==> {
        [ CLOSUER UNPACK ]          [   RETURN ADDRESS    ]
        [   ARGUMENTS    ]          [ CALLSTACK TYPE & BP ]
                                }
    */

    // Reserve sp
    vm->m_sp -= 3;

    // Set call way and bp offset.
    vm->m_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_FROM_NATIVE;
    vm->m_sp[1].m_ret_bp.m_bp_offset = 
        (uint32_t)(vm->m_stack_end - vm->m_sb);

    // Set ret addr (Only for trace).
    vm->m_sp[2].m_ret_addr = vm->m_ip /* trace from current. */;

    // Sync bp to sp.
    vm->m_sb = vm->m_sp;

    // Set target ip.
    vm->m_ip = func;
    
    return _woort_VMRuntime_dispatch(vm);
}

WOORT_NODISCARD woort_VMRuntime_CallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm)
{
    assert(vm->m_ip != NULL);

    for (;;)
    {
        const woort_Bytecode* const this_bytecode = vm->m_ip;
        switch (this_bytecode->m_op6m2)
        {
        default:
            // Unknown bytecode command.
            woort_panic(
                WOORT_PANIC_BAD_BYTE_CODE, 
                "Bad command(%x).", 
                *(uint32_t*)this_bytecode);

            return WOORT_VM_CALL_STATUS_TBD_BAD_STATUS;
        }
    }
}
