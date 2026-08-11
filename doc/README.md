# WooRT 技术文档

WooRT（Woolang Runtime）是 Woolang 脚本语言的 C11 运行时，包含字节码解释器、基于 GC 的内存管理器（内置的 `woort_mem` 分配层）、IR 编译器与调试支持。唯一公开 API 头文件是 [`include/woort.h`](../include/woort.h)。

本目录是 WooRT 的架构与技术参考文档。所有文档与代码同步，权威定义以头文件与源码为准。

## 文档索引

| 文档 | 内容 | 建议读者 |
|------|------|----------|
| [runtime.md](./runtime.md) | **运行时使用指南**：初始化、CodeEnv、VM 生命周期、调用约定、原生函数编写 | 所有使用者（先读） |
| [values.md](./values.md) | **值与装箱**：`woort_Value`/`woort_DynBox` 表示、Float63/Int62/Bool 内联装箱、容器类型、union/option/result | 使用者 / 前端实现者 |
| [ir.md](./ir.md) | **IR（中间表示）**：无限虚拟寄存器 + Label 的字节码构建 API、`finish()` 流程、源码位置 | 前端实现者 |
| [opcodes.md](./opcodes.md) | **指令集参考**：全部字节码指令、编码格式、操作数语义 | 前端实现者 / 调试 |
| [gc.md](./gc.md) | **垃圾回收**：woort_mem 分层、`woort_GCUnit` 对象模型、两阶段分配、写屏障、root set | 内部实现者 / 扩展 GC 类型 |
| [subsystems.md](./subsystems.md) | **子系统**：二进制序列化、VFS、Dylib、WAIPO 调试器与陷阱、线程与协程 | 进阶使用者 |

## 推荐阅读路径

* **只想用 WooRT 跑脚本**：`runtime.md` → `values.md`。
* **实现 Woolang 前端（生成字节码）**：`runtime.md` → `ir.md` → `opcodes.md` → `values.md`。
* **扩展 GC 类型 / 深入内存管理**：`gc.md` → `values.md`。
* **了解序列化 / 调试 / FFI**：`subsystems.md`。

## 快速上手

```c
#include "woort.h"

woort_api my_print(void) {
    printf("%lld\n", (long long)woort_int(3));   /* 读第 0 个参数 */
    woort_ret_void();
}

int main(int argc, char** argv) {
    woort_init(argc, argv);
    /* ... 用 IRCompiler 构建 CodeEnv（见 ir.md）... */
    woort_VMRuntime* vm;
    woort_VMRuntime_create(&vm);
    woort_VMRuntime_swap(vm);
    woort_bootup(WOORT_IGNORE, cenv, false);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);
    woort_shutdown(NULL, NULL);
    return 0;
}
```

## 代码地图

```
include/woort.h             单一公开 API 头（extern "C" 保护）
src/
├─ woort.c                   顶层：init/shutdown/VM 调度
├─ woort_vm.{h,c}            VM 运行时与解释器
├─ woort_value.{h,c}         值表示与装箱
├─ woort_codeenv.{h,c}       CodeEnv（代码环境）
├─ woort_codeenv_bin.c       CodeEnv 二进制序列化
├─ woort_ir_*.{h,c}          IR 编译器（compiler/function/block/op/value/srcloc）
├─ woort_opcode*.h           字节码定义与编码格式
├─ woort_gc*.{h,c}           GC 对象类型（string/vec/map/struct/closure/gchandle/pin）
├─ woort_mem*.{h,c}          内存分配层（GC 堆 / mark-sweep，原 woomem，已内置）
├─ woort_serialize.{h,c}     DynBox 文本序列化
├─ woort_vfs.{h,c}           虚拟文件系统
├─ woort_dylib.{h,c}         动态库（原生 + 伪库）
├─ woort_waipo_debugger*.{h,c}  WAIPO 调试器
├─ woort_threads.{h,c}       OS 线程原语（内部）
├─ woort_hashmap/ordermap    内部非 GC 容器原语
└─ woort_utf8/path/env/...   工具
3rd/asmjit/                  asmjit JIT 后端（git 子模块，可选，由 WOORT_BUILD_WITH_ASMJIT 控制）
test/                        每个测试 .c 编译为独立可执行 + ctest 条目
```

## 构建与测试

见根目录 [AGENTS.md](../AGENTS.md) 与 [CMakeLists.txt](../CMakeLists.txt)。简要：

```bash
git submodule sync --recursive && git submodule update --init --recursive
cmake -B build -G "Visual Studio 17 2022" -A x64   # Windows
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
