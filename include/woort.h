#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifdef _WIN32
#   define WOORT_IMPORT __declspec(dllimport)
#   define WOORT_EXPORT __declspec(dllexport)
#else
#   define WOORT_IMPORT extern
#   define WOORT_EXPORT extern
#endif

#ifdef WOORT_AS_DYLIB
#   ifdef WOORT_IMPL
#       define WOORT_API WOORT_EXPORT
#   else
#       define WOORT_API WOORT_IMPORT
#   endif
#else
#   define WOORT_API
#endif

    WOORT_API void woort_init(void);
    WOORT_API void woort_shutdown(void);

    typedef enum woort_VmCallStatus
    {
        WOORT_VM_CALL_STATUS_NORMAL,
        /*
        WOORT_VM_CALL_STATUS_NORMAL
        调用的目标函数以正常预期结果返回，没有特殊情况需要处理。
        */

        WOORT_VM_CALL_STATUS_ABORTED,
        /*
        WOORT_VM_CALL_STATUS_ABORTED
        程序被终止，虚拟机不能继续执行当前的调用栈。

        初次返回 WOORT_VM_CALL_STATUS_ABORTED 之前，应当执行一次正同步操
        作，以确保可以获取到正确的调试信息。

            + 如果是解释执行收到此状态：
                以相同状态返回上一层，不执行 RT 状态同步
            + 如果是 JIT 收到此状态：
                以相同状态返回上一层，不执行 RT 状态同步
            + 如果是外部调用方收到此状态：
                以失败为结果，调用结束，回滚到调用发生之前的调用栈。
        */

        WOORT_VM_CALL_STATUS_YIELD,
        /*
        WOORT_VM_CALL_STATUS_YIELD
        虚拟机正在请求以当前状态暂停执行，如果情况允许，可以在稍后继续执
        行。

        初次返回 WOORT_VM_CALL_STATUS_YIELD 之前，应当执行一次正同步操作，
        以确保稍后可以正确恢复执行。

            + 如果是解释执行收到此状态：
                以相同状态返回上一层，不执行 RT 状态同步
            + 如果是 JIT 收到此状态：
                以相同状态返回上一层，不执行 RT 状态同步
            + 如果是外部调用方收到此状态：
                如果是 dispatch 调用，以中断为结果，调用结束。
                如果是 invoke 调用，以失败为结果，调用结束，回滚到调用发
                生之前的调用栈。
        */

        WOORT_VM_CALL_STATUS_RESYNC,
        /*
        WOORT_VM_CALL_STATUS_RESYNC
        在 JIT 调用期间发生了一些状态改变，这些改变无法在 JIT 执行期间处理，
        因此需要回滚到解释执行进行。

        JIT 在以下事件发生时，会返回 WOORT_VM_CALL_STATUS_RESYNC：
            + 在执行 PUSHCHK 等指令时，栈空间不足
            + 在外部函数返回之后（如果此期间，发生了栈空间的重新申请）
            + 即将调用一个脚本函数
            + JIT 调用深度达到最大值

        初次返回 WOORT_VM_CALL_STATUS_RESYNC 之前，应当执行一次正同步操作，
        以确保稍后可以正确恢复执行。

            + 如果是解释执行收到此状态：
                执行一次反向的 RT 状态同步，然后继续执行
            + 如果是 JIT 收到此状态：
                以相同状态返回上一层，不执行 RT 状态同步
            + 如果是外部调用方收到此状态：
                以当前 RT 状态，解释执行继续执行
        */
    } woort_VmCallStatus, woort_api;

    typedef struct woort_VMRuntime* woort_vm;
    typedef struct woort_value { char _storage[8]; } woort_value;

    typedef woort_api(*woort_NativeFunction)(woort_vm vm, woort_value* args);

#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
