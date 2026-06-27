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

extern WOORT_THREAD_LOCAL woort_VMRuntime* WOORT_t_this_thread_vm;

typedef enum woort_VMRuntime_CheckRequestMask
{
    /*
    ABORT
    虚拟机状态发生错误而无法继续，此请求在 Panic 发出后决定。
    设置此请求时，**必须** 在当前虚拟机的 m_sp 处立即写入一个
    GCString 用以描述 panic 信息。
        * JIT 运行时：
            JIT 运行时无法处理此请求，执行正同步之后以 WOORT_VM_CALL_STATUS_RESYNC 
            向上抛出到解释执行
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

    /*
    YIELD
    请求虚拟以 WOORT_VM_CALL_STATUS_YIELD 状态结束执行，执行完整的正向同步以将 RT 状态
    保存到 VM 状态，后续可以继续 dispatch 执行。

        * JIT 运行时：
            JIT 运行时无法处理此请求，执行正同步之后以 WOORT_VM_CALL_STATUS_RESYNC 
            向上抛出到解释执行
        * 解释执行运行时：
            考虑到：
                1）如果是 Native-call 返回 RESYNC 导致检查点请求：
                2) 如果是 JIT-call 返回 RESYNV 导致检查点请求；
                2）如果是外部直接发起的请求中断；
            以上两种情况，都保证产生正确的正同步代码；接受请求之后暂离虚拟机，不执行
            其他同步
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_YIELD = 1 << 6,

    /*
    TERMINATE
    虚拟机被外部请求立即终止
    接受此请求时，当前 VM 会设置 ABORT, 然后按照约定向 m_sp 写入
    GCString。
        * JIT 运行时：
            JIT 运行时无法处理此请求，执行正同步之后以 WOORT_VM_CALL_STATUS_RESYNC 
            向上抛出到解释执行
        * 解释执行运行时：
            按照约定描述终止原因，然后设置 ABORT
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE = 1 << 7,

    WOORT_VMRUNTIME_CHECK_REQUEST_SHRINK_STACK = 1 << 8,

    WOORT_VMRUNTIME_CHECK_REQUEST_GC_MARK_FINISHED = 1 << 9,

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

    uint32_t                m_jit_call_depth;

    /*
    NOTE: m_env 的设计用意如下：
        考虑到：
            1) 大多数情况下，far-call 并不总是发生的（仅限于 CALLC/CALLS）
               在遇到
            2) 大多数情况下，native-function 调用的脚本函数亦是当前 env 的
            3) 查询脚本函数所属的 env 开销相对较大
            4) JIT 不能调用纯的 SIM 函数
            5) JIT 不需要使用 far-call 机制同步常量表
        因此：
            1) SIM 保有自己的 rt_env 局部状态，不关心 native-call 导致的 env
                变动（因为无论 native-call 中执行了任意 env 中的代码，其返回
                后总要回到 SIM 的 env 中继续执行）；SIM 的局部 rt_env 仅在 
                far-call 发生时更新，此时也会更新 m_env。
            2) m_env 始终保持最后一次同步 env 的结果，这意味着：
                1. 存在一些情况下，far-call 发生在 native-function，native-
                    function 返回后，回到之前的 env 上执行时，此时 VM 实际
                    执行的代码与 m_env 不一致，即：允许 rt_env 与 m_env 不一
                    致的情况
                2. 在 GC 和 VM 例外情况处理时，rt_env 需要同步到 m_env
                3. 从外部，在没有其他机制保证的情况下观测，可能读取到与调用栈
                    状态不一致的 m_env，所以包括调试器在内的实现，要避免使用
                    m_env；考虑到调试器等场景性能不敏感，可以读取调用栈后，直
                    接查询 env
            3) 可以使用 m_env 快速判别目标函数（在 native-function 中尝试调
                用 VM 函数时）

    */

    /* OPTIONAL */ woort_CodeEnv*    m_env;

    woort_AtomicUInt32          m_check_request_mask;

    bool                        m_is_weak;

    int8_t                      m_hangup_c;
    woort_Mutex*                m_hangup_mx;
    woort_ConditionVariable*    m_hangup_cv;

    uint8_t                     m_shrink_stack_count;
    uint8_t                     m_shrink_stack_edge;

};

/////////////////////////////////////////////////////////

void woort_VMRuntime_mark_vm_after_sync(woort_VMRuntime* vm);
void woort_VMRuntime_mark_weak_vm_after_sync(woort_VMRuntime* vm);

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

WOORT_NODISCARD bool woort_VMRuntime_advise_shrink_stack(woort_VMRuntime* vm);

void woort_VMRuntime_reset_shrink_stack_count(woort_VMRuntime* vm);

WOORT_NODISCARD woort_VmCallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm);
