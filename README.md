<div align="center">

<img src="./icon/woort.svg" alt="WooRT" width="220">

# WooRT

**Woolang Runtime · Woolang 运行时**

[简体中文](#简体中文) · [English](#english)

</div>

---

## 简体中文

WooRT 是 [Woolang](https://git.cinogama.net/cinogamaproject/woolang) 脚本语言的 C11 运行时。

### 功能特性

- 一个高效且通用的字节码解释执行器
- 一个适配 GC 支持的内存管理器（通过 `woomem` 子模块）
- 用于生成可执行 CodeEnv 的 IR 编译器接口
- 原生函数与 JIT 调用支持（x64 / ARM64，基于 asmjit）
- 内置调试与介入支持（WAIPO 调试器、陷阱、反汇编）
- 二进制序列化、虚拟文件系统（VFS）与动态库（dylib）加载

### 快速上手

```c
#include "woort.h"

woort_api my_print(void) {
    printf("%lld\n", (long long)woort_int(3));   /* 读取第 0 个参数 */
    woort_ret_void();
}

int main(int argc, char** argv) {
    woort_init(argc, argv);
    /* ... 通过 IR 编译器构建 CodeEnv（见 doc/ir.md）... */
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

### 构建

```bash
git submodule sync --recursive
git submodule update --init --recursive
cmake -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### 文档

[`doc/`](./doc) 目录包含完整技术参考，建议从 [`doc/runtime.md`](./doc/runtime.md) 开始阅读。权威 API 定义见 [`include/woort.h`](./include/woort.h)。

---

## English

WooRT is the C11 runtime for the [Woolang](https://git.cinogama.net/cinogamaproject/woolang) scripting language.

### Features

- An efficient and general-purpose bytecode interpreter
- A GC-aware memory manager (via the `woomem` submodule)
- An IR compiler API for generating executable CodeEnvs
- Native (C) and JIT function call support (x64 / ARM64 via asmjit)
- Built-in debugging and inspection (WAIPO debugger, traps, disassembly)
- Binary serialization, virtual file system (VFS), and dynamic library (dylib) loading

### Quick Start

```c
#include "woort.h"

woort_api my_print(void) {
    printf("%lld\n", (long long)woort_int(3));   /* read 1st argument */
    woort_ret_void();
}

int main(int argc, char** argv) {
    woort_init(argc, argv);
    /* ... build a CodeEnv via the IR compiler (see doc/ir.md) ... */
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

### Build

```bash
git submodule sync --recursive
git submodule update --init --recursive
cmake -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Documentation

The [`doc/`](./doc) directory contains the full technical reference. A recommended starting point is [`doc/runtime.md`](./doc/runtime.md). The authoritative API definition is [`include/woort.h`](./include/woort.h).
