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
    The VM state has errored and cannot continue; this request is
    decided after a panic is raised. When setting this request, a
    GCString describing the panic message **must** be written at the
    current m_sp.
        * JIT runtime:
            Cannot be handled in JIT; after a forward-sync it is thrown
            up to the interpreter via WOORT_VM_CALL_STATUS_RESYNC.
        * Interpreter runtime:
            Ends the call with WOORT_VM_CALL_STATUS_ABORTED.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_ABORT = 1 << 0,

    /*
    STACK_OCCUPYING
    The VM stack is being internally reallocated or externally read.
    This request must be set for the duration of such an operation; if
    setting fails it must be retried spin-style until it succeeds.
        * JIT runtime:
            (no special handling)
        * Interpreter runtime:
            Accepts the request, then hangs up until STACK_OCCUPYING is
            cleared; the external owner is responsible for waking the
            VM back up.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_STACK_OCCUPYING = 1 << 1,

    /*
    GC_CHECK
    The GC worker thread issues this request to every running root VM.
        * JIT runtime:
            (no special handling)
        * Interpreter runtime:
            Accepts the request and, on success, performs self-marking
            (marks the stack starting address and the global area).
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK = 1 << 2,

    /*
    GC_PROCESSING
    The VM is currently being marked. If the VM initiated the mark
    itself, the GC thread should skip this VM later; otherwise the GC
    thread should mark on its behalf. If the VM starts running while
    being marked by proxy, it should suspend execution.
        * JIT runtime:
            (no special handling)
        * Interpreter runtime:
            Accepts the request, then hangs up until GC_PROCESSING is
            cleared; the external owner is responsible for waking the
            VM back up.
        * If the GC worker thread fails to set this flag, the VM has
          already accepted GC_CHECK and the GC thread waits for the VM
          to finish marking.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING = 1 << 3,

    /*
    GC_LEAVE
    Set when the VM temporarily leaves the GC's scope. If the GC worker
    thread tries to mark a VM that is out of scope, it issues a
    STACK_OCCUPYING request and then performs the mark by proxy.

    Only one VM per thread may be running at a time; switching this
    flag must be done via a spin operation.
        * JIT runtime:
        * Interpreter runtime:
            Should never occur; terminated with a PANIC.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE = 1 << 4,

    /*
    DEBUG_CALLBACK
    Requests the VM to invoke the debug callback; the debug callback
    function and its context must have been set previously and not yet
    cleared.

    If the debug context is unset when this request is received, it is
    ignored.

        * JIT runtime:
            Cannot be handled in JIT; after a forward-sync it is thrown
            up to the interpreter via WOORT_VM_CALL_STATUS_RESYNC.
        * Interpreter runtime:
            Runs the debug-callback mechanism.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK = 1 << 5,

    /*
    YIELD
    Requests the VM to end execution with WOORT_VM_CALL_STATUS_YIELD,
    performing a full forward-sync to save RT state into VM state so
    that dispatch can resume later.

        * JIT runtime:
            Cannot be handled in JIT; after a forward-sync it is thrown
            up to the interpreter via WOORT_VM_CALL_STATUS_RESYNC.
        * Interpreter runtime:
            Considering:
                1) checkpoint triggered by a native-call returning RESYNC;
                2) checkpoint triggered by a JIT-call returning RESYNC;
                3) checkpoint triggered by an external interrupt;
            all of the above guarantee correct forward-sync code. After
            accepting the request the VM is temporarily detached; no
            further sync is performed.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_YIELD = 1 << 6,

    /*
    TERMINATE
    The VM is requested to terminate immediately by an external source.
    On accepting this request the VM sets ABORT and, by convention,
    writes a GCString at m_sp.
        * JIT runtime:
            Cannot be handled in JIT; after a forward-sync it is thrown
            up to the interpreter via WOORT_VM_CALL_STATUS_RESYNC.
        * Interpreter runtime:
            Writes a GCString describing the termination reason by
            convention, then sets ABORT.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE = 1 << 7,

    /*
    SHRINK_STACK
    Set by the VM itself at the tail of mark_vm_after_sync /
    mark_weak_vm_after_sync when actual stack usage drops below 1/4 of
    the current capacity and the current capacity is at least
    2 * WOORT_VM_DEFAULT_STACK_BEGIN_SIZE, advising the stack be
    shrunk to a tighter capacity.
        * JIT runtime:
            Shares the same handler as the interpreter.
        * Interpreter runtime:
            After accepting the request, calls
            _woort_VMRuntime_shrink_stack, which locks the stack via
            STACK_OCCUPYING, mallocs a new buffer, copies the used
            portion tail-aligned, and frees the old stack; on success
            m_shrink_stack_edge is recomputed from the new capacity.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_SHRINK_STACK = 1 << 8,

    /*
    GC_MARK_FINISHED
    Set by the GC worker thread during the stop-mark phase for every
    root VM to signal that marking for this round has finished; the GC
    thread then spin-waits for the bit to be cleared by the VM.
        * JIT runtime:
            Shares the same handler as the interpreter.
        * Interpreter runtime:
            request_accept clears this bit either at the end of
            handle_gc_check_request_and_mark or in the standalone
            checkpoint branch, waking the waiting GC worker thread.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_MARK_FINISHED = 1 << 9,

    /*
    SUSPEND
    Issued by the GC worker thread via
    woort_GC_suspend_all_vm_to_do_sth to stop-the-world for a global
    operation (e.g. JIT un-jit of every codeenv). After setting it the
    GC thread spin-waits for the bit to be consumed, confirming the VM
    has truly parked.
        * JIT runtime:
            Cannot block in place; after a forward-sync it is thrown up
            to the interpreter via WOORT_VM_CALL_STATUS_RESYNC.
        * Interpreter runtime:
            After accepting the request, detaches from the current
            thread via woort_VMRuntime_swap(NULL), then spin-yields
            (woort_thread_yield) until RESUME can be successfully
            accepted, after which it swaps the VM back in.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_SUSPEND = 1 << 10,

    /*
    RESUME
    Issued by the GC worker thread after the callback in
    woort_GC_suspend_all_vm_to_do_sth finishes, releasing VMs parked
    by SUSPEND.
        * Not handled by the request dispatch chain:
            Consumed only by the wait loop inside the SUSPEND handler
            (request_accept(RESUME)) to break the spin and resume
            execution.
    */
    WOORT_VMRUNTIME_CHECK_REQUEST_RESUME = 1 << 11,

}woort_VMRuntime_CheckRequestMask;

struct woort_VMRuntime
{
    /* VM Runtime status. */
    woort_Value*            m_stack;
    /* NOTE: m_stack_end 指向栈空间的尾后位置，不可访问其中的内容 */
    woort_Value*            m_stack_end;

    uint32_t                m_jit_call_depth;

    woort_Value*            m_sb;
    woort_Value*            m_sp;
    const woort_Bytecode*   m_ip;

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

/* ====================================================== */

void woort_VMRuntime_mark_vm_after_sync(woort_VMRuntime* vm);
void woort_VMRuntime_mark_weak_vm_after_sync(woort_VMRuntime* vm);

void woort_VMRuntime_handle_gc_check_request_and_mark(woort_VMRuntime* vm);

void woort_VMRuntime_gc_checkpoint(woort_VMRuntime* vm);

/* ====================================================== */

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

void woort_VMRuntime_decay_shrink_stack_count(woort_VMRuntime* vm);

WOORT_NODISCARD woort_VmCallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm);
