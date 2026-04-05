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

WOORT_THREAD_LOCAL woort_VMRuntime* WOORT_t_this_thread_vm = NULL;

typedef enum woort_VMRuntime_CheckRequestMask
{
    /*
    ABORT
    虚拟机状态发生错误而无法继续
        * JIT 运行时：
        * 解释执行运行时：
            以 WOORT_VM_CALL_STATUS_ABORTED 结束调用
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_ABORT = 1 << 0,

    /*
    STACK_OCCUPYING
    虚拟机栈正在被内部重新分配或外部读取，在正式地执行操作期间，此请求
    必须被设置，如果设置失败，应当自旋地重试，直到设置成功
        * JIT 运行时：
        * 解释执行运行时：
            接收，然后挂起，直到 STACK_OCCUPYING 结束，外部占用方负责拉
            起虚拟机。
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_STACK_OCCUPYING = 1 << 1,

    /*
    GC_CHECK
    GC 工作线程将向所有正在运行中的 RootVM 发起此请求
        * JIT 运行时：
        * 解释执行运行时：
            接受此请求，如果成功接受，执行自我标记（标记栈起始地址和全局
            区）。
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK = 1 << 2,

    /*
    GC_PROCESSING
    当前 VM 正在被执行标记；如果是 VM 自己发起的标记，GC 线程应当在稍后跳
    过此 VM，否则应当代理标记。如果 VM 在被代理标记期间发起运行，应当暂停
    执行。
        * JIT 运行时：
        * 解释执行运行时：
            接收，然后挂起，直到 GC_PROCESSING 结束，外部占用方负责拉
            起虚拟机。
        * 如果是 GC 工作线程设置此标记失败，此时说明 VM 已经接收 GC_CHECK
        请求，等待 VM 完成标记。
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING = 1 << 3,

    /*
    GC_LEAVE
    如果虚拟机暂时脱离 GC 作用域，该标记将被设置，如果 GC 工作线程尝试标
    记不在作用域内的的 VM，将通过 STACK_OCCUPYING 请求，然后执行代理的标
    记操作。

    任一线程同时只能有一个 VM 处于运行状态，切换此标记只能通过旋转操作执
    行。
        * JIT 运行时：
        * 解释执行运行时：
            不应当出现此情况，PANIC 终止
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE = 1 << 4,

    /*
    DEBUG_CALLBACK
    请求虚拟机执行调试回调，调试回调函数和上下文应当在之前被设定并尚未被清除

    如果收到此请求时调试上下文未设定，无视此请求。

        * JIT 运行时：
            JIT 运行时无法处理此请求，执行正同步之后以 WOORT_VM_CALL_STATUS_RESYNC 
            向上抛出到解释执行
        * 解释执行运行时：
            执行调试回调机制
    
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK = 1 << 5,

}woort_VMRuntime_CheckRequestMask;

struct woort_VMRuntime
{
    // VM Runtime status.
    uint32_t                m_stack_realloc_version;
    woort_Value*            m_stack;
    // NOTE: m_stack_end 指向栈空间的尾后位置，不可访问其中的内容
    woort_Value*            m_stack_end; 
    woort_Value*            m_sb;
    woort_Value*            m_sp;
    const woort_Bytecode*   m_ip;

    woort_CodeEnv*    m_env;

    woort_AtomicUInt32      m_check_request_mask;

    int8_t                      m_hangup_c;
    woort_Mutex*                m_hangup_mx;
    woort_ConditionVariable*    m_hangup_cv;

};

/////////////////////////////////////////////////////////

void woort_VMRuntime_mark_vm_after_sync(woort_VMRuntime* vm);

void woort_VMRuntime_handle_gc_check_request_and_mark(woort_VMRuntime* vm);

void woort_VMRuntime_gc_checkpoint(woort_VMRuntime* vm);

/////////////////////////////////////////////////////////

WOORT_NODISCARD bool woort_VMRuntime_request_set(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask);

WOORT_NODISCARD bool woort_VMRuntime_request_check(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask);

WOORT_NODISCARD bool woort_VMRuntime_request_accept(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask);

void woort_VMRuntime_hangup(woort_VMRuntime* vm);

void woort_VMRuntime_wakeup(woort_VMRuntime* vm);

WOORT_NODISCARD bool _woort_VMRuntime_extern_stack(woort_VMRuntime* vm);
