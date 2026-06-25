#pragma once

/*
woort_jit.h

================================================================================
WooRT JIT 代码生成框架
================================================================================

本模块提供 JIT 代码生成的「流程框架」。它本身**不**包含任何具体后端
（x64 / ARM64 的机器码发射）；框架负责整个编译流水线的编排，而把
「如何为每条字节码生成原生代码」委托给一个通过 woort_JIT_install_backend()
注册的后端实现。

未来实现一个 JIT，只需按本文件末尾「后端契约」实现一个 const
woort_JIT_Backend 并 woort_JIT_install_backend() 即可——无需改动框架、
解释器或字节码格式。

框架的编译流程（woort_JIT_compile_function）：
    1. woort_CodeEnv_find(entry)            反查所属 CodeEnv
    2. 在 m_function_boundaries 二分定位     取得 [entry, code_end)
    3. 查编译缓存                            命中则直接安装并返回
    4. backend->m_emitter_begin()           后端自管可执行内存（RW）
    5. backend->m_emit_prologue()           按 woort_JitFunction 约定建帧
    6. 线性遍历：pc=entry; while pc<code_end:
           pc = woort_OpcodeDispatcher_decode(
                   pc, backend->m_dispatchers, emitter);
       —— 每条字节码触发后端的一个降级回调，在其中发射原生代码
    7. backend->m_emit_epilogue()           默认正常返回路径
    8. backend->m_emitter_finalize()        重定位 + 翻转 RX，产出入口
    9. 写入常量池对应 SCRIPT_FUNC 槽的 m_jit_function
   10. 登记到编译缓存

任意一步失败则回滚：不修改常量池、释放已发射内存、该函数回退解释执行。

集成点（已贯穿整个 WooRT 流水线，框架直接复用）：
    - woort_JitFunction 调用约定：
        woort_api (*)(woort_VMRuntime* vm, const woort_Value* bp)
                                          -> woort_VmCallStatus
    - CALLNJIT 字节码与 woort_spawn 入口都从常量池/闭包读 m_jit_function 调用
    - RESYNC 协议：JIT 无法处理时（GC/调试/abort/yield）先正向同步
      (写回 m_ip/m_sp/m_sb) 再返回 WOORT_VM_CALL_STATUS_RESYNC
================================================================================
*/

#include "woort.h"                 /* WOORT_NODISCARD, woort_Bytecode */
#include "woort_value.h"          /* woort_JitFunction */
#include "woort_codeenv.h"        /* woort_CodeEnv, woort_FunctionBoundary */
#include "woort_opcode_dispatcher.h" /* woort_OpcodeDispatchers */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* 后端发射器句柄                                                            */
/* ======================================================================== */

/*
 * 后端自定义的不透明句柄。后端在 m_emitter_begin 中分配、在 m_emitter_finalize
 * 中释放（或失败回滚时由后端自行释放）。框架只以指针形式透传它，在
 * woort_OpcodeDispatcher_decode 驱动时作为每个 opcode 回调的 userctx 传入。
 */
typedef struct woort_JIT_Emitter woort_JIT_Emitter;

/* ======================================================================== */
/* 编译请求                                                                  */
/* ======================================================================== */

/*
 * 框架对单个函数的完整描述，传给后端的 m_emitter_begin。
 * 后端据此了解要编译的函数的代码范围与元信息。
 */
typedef struct woort_JIT_CompileRequest
{
    woort_CodeEnv*        m_env;      /**< 所属 CodeEnv（常量池来源）。 */
    /* OPTIONAL */ const char* m_name; /**< 函数名（来自 m_function_boundaries，可能为 NULL）。 */
    const woort_Bytecode* m_entry;    /**< 函数首指令 = env->m_code_begin + m_offset_begin。 */
    const woort_Bytecode* m_code_end; /**< 函数尾后 = m_entry + m_code_length（不可解引用）。 */
} woort_JIT_CompileRequest;

/* ======================================================================== */
/* 后端接口                                                                  */
/* ======================================================================== */

/*
 * const 后端 vtable，由每个后端定义一个静态全局实例并注册。
 * 所有函数指针均为必需（非常规的 OPTIONAL），框架假定注册的后端是完整的。
 */
typedef struct woort_JIT_Backend
{
    const char* m_name;   /**< 后端名（"x64"/"arm64"/...），仅诊断用。 */

    /* 自检：当前平台/CPU 是否被本后端支持。install_backend 据此决定是否激活。 */
    bool (*m_query_support)(void);

    /*
     * 发射器生命周期开始。后端在此自行申请可执行内存（VirtualAlloc/mmap），
     * 发射期间保持可写（RW）。框架不参与内存管理。
     * 返回 NULL 表示分配失败。
     */
    woort_JIT_Emitter* (*m_emitter_begin)(const woort_JIT_CompileRequest* req);

    /*
     * 函数序言：按 woort_JitFunction 约定接收 (vm, bp)，建立帧。
     *   - vm 是 woort_VMRuntime*，bp 是新帧基（const woort_Value*）
     *   - 读操作数即 bp[signed_offset]（8B 槽）
     * 返回 false 表示发射失败（框架中止本函数编译、回退解释）。
     */
    bool (*m_emit_prologue)(woort_JIT_Emitter* e);

    /*
     * 函数尾声：默认正常返回路径（返回 WOORT_VM_CALL_STATUS_NORMAL，
     * 返回值约定写入 *vm->m_sp）。
     */
    bool (*m_emit_epilogue)(woort_JIT_Emitter* e);

    /*
     * 终结：完成重定位、翻转内存为可执行只读（RX），产出可调用入口。
     *   out_entry = 生成的 woort_JitFunction。
     * 内存所有权归属后端，由框架在 m_free_func 中回调释放。
     * 返回 false 表示失败（框架会调用 m_free_func(*out_entry) 已是有效入口时，
     * 或假定后端已在内部清理未完成的发射）。
     */
    bool (*m_emitter_finalize)(woort_JIT_Emitter* e, woort_JitFunction* out_entry);

    /*
     * 逐 opcode 降级回调表。框架用 woort_OpcodeDispatcher_decode 驱动，
     * 把已解码的操作数原样传给对应回调。每个回调签名与既有 dispatcher
     * 完全一致：
     *     void cb(void* userctx, 已解码的操作数...)
     * （userctx 即 Emitter 句柄。）后端在每个回调内发射对应原生代码。
     * 未实现的 opcode 可在该回调内发射「返回 RESYNC」的桩，从而优雅回退
     * 到解释执行。
     */
    const woort_OpcodeDispatchers* m_dispatchers;

    /*
     * 释放 finalize 产出的入口及其内存（后端自行 munmap/VirtualFree）。
     * 框架在 CodeEnv 销毁、或编译失败回滚时回调。
     * 允许传 NULL 入口（此时为无操作）。
     */
    void (*m_free_func)(woort_JitFunction entry);

} woort_JIT_Backend;

/* ======================================================================== */
/* 框架入口                                                                   */
/* ======================================================================== */

/*
 * 注册一个后端。若 backend->m_query_support() 返回 false，则不激活（返回 false）。
 * 同一时刻只能有一个活跃后端；重复注册会覆盖前一个（先释放旧后端名下已编译代码）。
 *
 * @return true 若后端被激活；false 若平台不支持或参数非法。
 */
WOORT_NODISCARD bool woort_JIT_install_backend(const woort_JIT_Backend* backend);

/*
 * 卸载当前后端并释放所有已编译的 JIT 代码（调用旧后端的 m_free_func）。
 * 通常仅在关闭运行时前调用。无活跃后端时为无操作。
 */
void woort_JIT_uninstall_backend(void);

/*
 * 是否已激活可用后端。无后端注册时整条 JIT 路径为空操作，零行为变化。
 */
WOORT_NODISCARD bool woort_JIT_is_enabled(void);

/*
 * 编译单个函数（按其首指令地址定位）。完整流程见文件头注释。
 * 若该 entry 已编译过（缓存命中），直接安装并返回 true。
 * 失败（任何一步返回 false 或无可用后端）则回滚并返回 false，
 * 该函数将保持解释执行。
 *
 * @param function_entry 函数首指令地址（必须在某 CodeEnv 代码范围内）。
 * @return true 编译并安装成功（或命中缓存）。
 */
WOORT_NODISCARD bool woort_JIT_compile_function(const woort_Bytecode* function_entry);

/*
 * 预编译：遍历 CodeEnv 全部 m_function_boundaries，对每个函数调
 * woort_JIT_compile_function。单项失败仅记日志，不影响其余函数与整体返回。
 * 通常在 woort_bootup_codeenv 之前由运行时调用。
 *
 * @return true 若至少尝试过编译（即有活跃后端）；false 若无后端（空操作）。
 */
WOORT_NODISCARD bool woort_JIT_compile_env(woort_CodeEnv* env);

/*
 * 释放某 CodeEnv 名下所有已编译函数（遍历缓存，调 m_free_func）。
 * 在 CodeEnv 被 GC 销毁前由运行时回调。无活跃后端时为无操作。
 */
void woort_JIT_release_env(woort_CodeEnv* env);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ======================================================================== */
/* 后端契约（实现一个 JIT 后端只需照此填空）                                  */
/* ======================================================================== */
/*
 * 1. 定义一个 const woort_JIT_Backend 的静态全局实例（如 g_woort_jit_x64）。
 *
 * 2. m_query_support()：检测本机架构（可用 woort_platform.h 的
 *    WOORT_PLATFORM_X64 / WOORT_PLATFORM_ARM64 等宏）。
 *
 * 3. m_emitter_begin / m_emitter_finalize：自管可执行内存。
 *    建议流程：emitter_begin 时 VirtualAlloc/MEM_COMMIT PAGE_READWRITE
 *    （POSIX: mmap PROT_READ|PROT_WRITE）；finalize 时 VirtualProtect 翻成
 *    PAGE_EXECUTE_READ（POSIX: mprotect PROT_READ|PROT_EXEC）。
 *    释放走 m_free_func（VirtualFree/MEM_DECOMMIT 或 munmap）。
 *    注意：woomem 分配器只产 RW，没有 EXEC，必须自管。
 *
 * 4. m_emit_prologue / m_emit_epilogue：遵守 woort_JitFunction 约定：
 *      入口签名 woort_api f(woort_VMRuntime* vm, const woort_Value* bp)
 *      bp = 新帧基（CALLNJIT/CALLC 设置好的 new_sp / new_sb）。
 *      读操作数即 bp[signed_offset]（8B 的 woort_Value 槽）。
 *      返回 woort_VmCallStatus。
 *
 * 5. m_dispatchers：填一张 woort_OpcodeDispatchers（每 opcode 一个回调，
 *    签名见 woort_opcode_dispatcher.h），每个回调内发射对应原生代码。
 *    未实现降级的 opcode，可在回调内发射「正向同步 + 返回 RESYNC」的桩，
 *    让解释器接管，从而先支持部分 opcode 再逐步扩展。
 *
 * 6. 必须遵守的运行时不变量：
 *    (a) 调用之后（CALLNJIT/CALLS/CALLC 目标返回），若 vm->m_stack_realloc_version
 *        发生变化，必须重新派生栈指针或返回 RESYNC（对应解释器宏
 *        WOORT_VM_CHECK_STACK_VERSION_AND_RESYNC_STACK_STATE）。
 *    (b) 写 GC 引用到堆槽（STORE / STIDX* / ASTORE 等）须调用
 *        woort_GC_mixed_write_barrier_*（woort_gc.h）。
 *    (c) 在安全点检查 vm->m_check_request_mask（woort_vm.h 的
 *        WOORT_VMRUNTIME_CHECK_REQUEST_* 各位）：
 *          ABORT / DEBUG_CALLBACK / YIELD / TERMINATE / GC_LEAVE / (栈占用等)
 *        JIT 无法处理的请求：先正向同步（写回 vm->m_ip / m_sp / m_sb），
 *        再返回 WOORT_VM_CALL_STATUS_RESYNC。
 *    (d) GC_CHECK / GC_PROCESSING 等可由 JIT 自行处理或走 RESYNC，视后端能力。
 */
