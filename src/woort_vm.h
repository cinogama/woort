#pragma once

/*
woort_vm.h
*/

#include "woort.h"

#include "woort_diagnosis.h"
#include "woort_value.h"
#include "woort_opcode_formal.h"
#include "woort_codeenv.h"
#include "woort_threads.h"

#include <stdbool.h>

typedef enum woort_VMRuntime_CheckRequestMask
{
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_SYNC = 1 << 0,
    WOORT_VMRUNTIME_CHECK_REQUEST_OCCUPYING = 1 << 1,
    WOORT_VMRUNTIME_CHECK_REQUEST_ABORT = 1 << 2,

}woort_VMRuntime_CheckRequestMask;

typedef struct woort_VMRuntime
{
    // VM Runtime status.
    uint32_t                m_stack_realloc_version;
    woort_Value*            m_stack;
    // NOTE: m_stack_end 指向栈空间的尾后位置，不可访问其中的内容
    woort_Value*            m_stack_end; 
    woort_Value*            m_sb;
    woort_Value*            m_sp;
    const woort_Bytecode*   m_ip;

    const woort_CodeEnv*    m_env;

    woort_AtomicUInt32      m_check_request_mask;

    int8_t                      m_hangup_c;
    woort_Mutex*                m_hangup_mx;
    woort_ConditionVariable*    m_hangup_cv;

} woort_VMRuntime;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm);
void woort_VMRuntime_deinit(woort_VMRuntime* vm);

WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func);

/////////////////////////////////////////////////////////

WOORT_NODISCARD bool woort_VMRuntime_request_set(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask);

WOORT_NODISCARD bool woort_VMRuntime_request_check(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask);

WOORT_NODISCARD bool woort_VMRuntime_request_accept(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask);

void woort_VMRuntime_wakeup(woort_VMRuntime* vm);

