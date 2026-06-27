#pragma once

#include "woort_value.h"
#include "woort_opcode_dispatcher.h"

#include <stdbool.h>

typedef bool (*woort_JIT_Backend_EmitPrologue)(
    const woort_CodeEnv*,
    const woort_Bytecode**,
    void** out_emmiter);

typedef bool (*woort_JIT_Backend_EmitEpilogue)(
    void*,
    woort_JitFunction* out_code);

typedef bool (*woort_JIT_Backend_PreDispatch)(
    void*);

typedef bool (*woort_JIT_Backend_PostDispatch)(
    void*);

typedef void (*woort_JIT_Backend_DropCode)(
    woort_JitFunction*);

typedef struct woort_JIT_Backend
{
    /*
    序言：JIT 尝试编译一个函数时的起始接口，JIT 实现应当为这个函数创建上下文
        并生成必要的序言代码。
    */
    woort_JIT_Backend_EmitPrologue m_emit_prologue;
    /*
    尾声：完成 JIT，生成尾声处理代码，并产生最终的 JIT 代码。
        通常而言，尾声接口还需要负责清理上下文。
    */
    woort_JIT_Backend_EmitEpilogue m_emit_epilogue;
    /*
    预派发钩子：对于每一条指令，在 JIT 调用 m_dispatchers 中对应的指令生成派发
        接口之前都会通过此接口通知后端；通常用于在当前字节码偏移处记录信息（例如
        字节码偏移到 JIT 代码偏移的映射、为跳转目标建立标签等）。
        当其返回失败时，剩余的指令提交和尾声提交都将被跳过——因此，此接口需要在
        返回失败时，负责清理上下文。
    */
    woort_JIT_Backend_PreDispatch m_pre_dispatch;

    /*
    派发后检查：对于每一条指令，JIT 在调用 m_dispatchers 中提供的指令生成派发接口
        之后都会通过此接口检查提交过程是否发生错误；
        一旦有任何一条指令发生错误，剩余的指令提交和尾声提交都将被跳过——因此，此接口
        需要在返回失败时，负责清理上下文。
    */
    woort_JIT_Backend_PostDispatch m_post_dispatch;

    /*
    指令分发接口，对应具体的指令提交
    */
    const woort_OpcodeDispatchers* m_dispatchers;

    /*
    JIT 函数释放接口，负责释放已经产生的 JIT 函数实例。
    */
    woort_JIT_Backend_DropCode m_drop_code;

} woort_JIT_Backend;

WOORT_NODISCARD bool woort_JIT_bootup(void);
void woort_JIT_shutdown(void);

void woort_JIT_set_backend(const woort_JIT_Backend* backend);

void woort_JIT_compile_env(woort_CodeEnv* cenv);
