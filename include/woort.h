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

    /*
    调用（Invoke 或 Dispatch）一个函数时，函数可能以下述状态为结果：
    */
    typedef enum woort_VmCallStatus
    {
        WOORT_VM_CALL_STATUS_NORMAL,
        /*
        WOORT_VM_CALL_STATUS_NORMAL
        调用的目标函数以正常预期结果返回，没有特殊情况需要处理。
        */

        WOORT_VM_CALL_STATUS_YIELD,
        /*
        WOORT_VM_CALL_STATUS_YIELD
        虚拟机正在请求以当前状态暂停执行，如果情况允许，可以在稍后继续执
        行。

        * YIELD 仅由解释执行收到中断请求后返回

        * 外部执行的 Dispatch 操作可能以此状态结束
        * 如果一个 Invoke 操作被试图以 YIELD 结束，则 PANIC
        */

        WOORT_VM_CALL_STATUS_ABORTED,
        /*
        WOORT_VM_CALL_STATUS_ABORTED
        程序被终止，虚拟机不能继续执行当前的调用栈，虚拟机同时将被以 Abort
        标记并拒绝执行其他任何操作。

        * ABORTED 仅由解释执行收到中断请求后返回
        */

        WOORT_VM_CALL_STATUS_RESYNC,
        /*
        WOORT_VM_CALL_STATUS_RESYNC
        下一层调用栈涉及到了一些新的虚拟机状态变化，下一层调用栈请求上一层
        调用栈执行一些反向同步或检查点检查以恢复到正常状态

        * 解释执行绝不以 RESYNC 结束

        * JIT 调用 Native 函数发生 RESYNC 时：
            恢复到调用后状态，然后执行一次检查点
        * JIT 调用 JIT 函数发生 RESYNC 时：
            不执行同步，立即以 RESYNC 结束当前 JIT 函数
        * 解释执行调用 Native 函数发生 RESYNC 时：
            恢复到调用后状态，然后执行一次检查点
        * 解释执行调用 JIT 函数发生 RESYNC 时：
            执行一次反同步，然后执行一次检查点

        外部执行的 Invoke 或 Dispatch 操作可能以外部函数或者 JIT 函数为目标，
        如果出现此情况，需要再执行 _woort_VMRuntime_dispatch 以获取准确结果
        */
    } woort_VmCallStatus, woort_api;

    typedef struct woort_VMRuntime* woort_vm;
    typedef struct woort_value { char _[8]; } woort_value;

    typedef woort_api(*woort_NativeFunction)(woort_vm vm, woort_value* args);

#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
