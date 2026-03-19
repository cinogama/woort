# WooRT 运行时使用指南

本文档介绍 `woort_CodeEnv` 和 `woort_VMRuntime` 的基本工作方式。

## 概述

WooRT 运行时采用字节码解释执行模式，主要由两个核心组件构成：

- **woort_CodeEnv**：代码环境，管理字节码和常量数据
- **woort_VMRuntime**：虚拟机运行时，管理执行栈和状态

## 基本使用流程

```c
#include "woort.h"
#include "woort_codeenv.h"
#include "woort_vm.h"

int main(void) {
    /* 1. 初始化运行时 */
    woort_init();

    /* 2. 创建代码环境 */
    woort_CodeEnv* codeenv;
    woort_CodeEnv_create(bytecodes, count, data_count, &codeenv);

    /* 3. 初始化常量数据 */
    codeenv->m_data_begin[0].m_integer = 42;
    codeenv->m_data_begin[1].m_native_or_jit_function = &my_func;
    /* ... */

    /* 4. 创建虚拟机 */
    woort_VMRuntime* vm;
    woort_VMRuntime_create(&vm);

    /* 5. 执行代码 */
    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

    /* 6. 清理资源 */
    woort_VMRuntime_destroy(vm);
    woort_shutdown();
    return 0;
}
```

---

## woort_CodeEnv 详解

### 结构定义

```c
typedef struct woort_CodeEnv {
    woort_GCUnit m_gc_unit;          /* GC 单元头，支持垃圾回收 */
    bool m_hold;                      /* 持有标记 */
    const woort_Bytecode* m_code_begin;  /* 字节码起始地址 */
    const woort_Bytecode* m_code_end;    /* 字节码结束地址 */
    woort_Value m_data_begin[];       /* 常量/静态数据区（柔性数组） */
} woort_CodeEnv;
```

### 主要函数

| 函数 | 说明 |
|------|------|
| `woort_CodeEnv_bootup()` | 启动代码环境子系统（内部使用） |
| `woort_CodeEnv_shutdown()` | 关闭代码环境子系统（内部使用） |
| `woort_CodeEnv_create(bytecodes, count, data_count, &out)` | 创建代码环境 |
| `woort_CodeEnv_drop(code_env)` | 释放代码环境（执行前必须调用） |
| `woort_CodeEnv_find(addr, &out)` | 根据地址查找所属代码环境 |

### 创建代码环境

```c
const woort_Bytecode bcs[] = {
    /* 字节码指令 */
};

woort_CodeEnv* codeenv;
woort_CodeEnv_create(
    bcs,                    /* 字节码数组 */
    sizeof(bcs) / sizeof(woort_Bytecode),  /* 字节码数量 */
    9,                      /* 常量/静态数据区大小 */
    &codeenv);              /* 输出参数 */
```

### 常量数据初始化

`m_data_begin` 是一个 `woort_Value` 数组，用于存储：

- 整数常量 (`m_integer`)
- 实数常量 (`m_real`)
- 字符串常量 (`m_string`)
- 原生函数指针 (`m_native_or_jit_function`)
- 其他 GC 对象

```c
codeenv->m_data_begin[0].m_integer = 0;
codeenv->m_data_begin[1].m_integer = 3000000000;
codeenv->m_data_begin[2].m_native_or_jit_function = &print_int;
codeenv->m_data_begin[3].m_string = woort_GCString_make_string("hello", 5);
```

### 执行前释放

**重要**：在调用 `woort_VMRuntime_invoke` 之前，必须调用 `woort_CodeEnv_drop`：

```c
woort_CodeEnv_drop(codeenv);
woort_VMRuntime_invoke(vm, codeenv->m_code_begin);
```

---

## woort_VMRuntime 详解

### 结构定义

```c
typedef struct woort_VMRuntime {
    /* 栈管理 */
    uint32_t m_stack_realloc_version;  /* 栈重分配版本号 */
    woort_Value* m_stack;              /* 栈底指针 */
    woort_Value* m_stack_end;           /* 栈尾后位置（不可访问） */
    woort_Value* m_sb;                 /* 栈基指针（Stack Base） */
    woort_Value* m_sp;                 /* 栈指针（Stack Pointer） */
    const woort_Bytecode* m_ip;        /* 指令指针（Instruction Pointer） */

    /* 代码环境 */
    woort_CodeEnv* m_env;              /* 当前代码环境 */

    /* 检查请求 */
    woort_AtomicUInt32 m_check_request_mask;

    /* 挂起机制 */
    int8_t m_hangup_c;
    woort_Mutex* m_hangup_mx;
    woort_ConditionVariable* m_hangup_cv;
} woort_VMRuntime;
```

### 主要函数

| 函数 | 说明 |
|------|------|
| `woort_VMRuntime_create(&out)` | 创建虚拟机实例 |
| `woort_VMRuntime_destroy(vm)` | 销毁虚拟机实例 |
| `woort_VMRuntime_invoke(vm, func)` | 调用函数执行字节码 |

### 执行函数

```c
woort_VmCallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm,
    const woort_Bytecode* func);
```

参数：
- `vm`：虚拟机实例
- `func`：要执行的函数入口地址（通常是 `codeenv->m_code_begin`）

返回值：`woort_VmCallStatus`

---

## 调用状态 (woort_VmCallStatus)

```c
typedef enum woort_VmCallStatus {
    WOORT_VM_CALL_STATUS_NORMAL,   /* 正常返回 */
    WOORT_VM_CALL_STATUS_YIELD,    /* 请求暂停（yield） */
    WOORT_VM_CALL_STATUS_ABORTED,  /* 执行被中止 */
    WOORT_VM_CALL_STATUS_RESYNC,   /* 需要重新同步 */
} woort_VmCallStatus;
```

| 状态 | 说明 |
|------|------|
| `NORMAL` | 函数正常返回，无特殊情况 |
| `YIELD` | 虚拟机请求暂停执行，可稍后继续 |
| `ABORTED` | 程序被终止，虚拟机不可继续执行 |
| `RESYNC` | JIT/解释执行切换时需要同步状态 |

---

## 原生函数编写

### 函数签名

```c
woort_api my_native_function(woort_vm vm, woort_value* args) {
    /* 实现 */
    return WOORT_VM_CALL_STATUS_NORMAL;
}
```

### 示例：打印整数

```c
woort_api print_int(woort_vm vm, woort_value* args) {
    woort_Value* val = (woort_Value*)args;
    printf("%lld\n", val->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}
```

### 示例：打印字符串

```c
woort_api print_string(woort_vm vm, woort_value* args) {
    const woort_GCString* gcstr = ((woort_Value*)args)->m_string;
    for (size_t i = 0; i < gcstr->m_length; ++i)
        putchar(gcstr->m_content[i]);
    putchar('\n');
    return WOORT_VM_CALL_STATUS_NORMAL;
}
```

### 示例：访问虚拟机栈

```c
woort_api get_current_time(woort_vm vm, woort_value* args) {
    /* 通过 vm->m_sb 访问栈基指针 */
    vm->m_sb[2].m_integer = clock();
    return WOORT_VM_CALL_STATUS_NORMAL;
}
```

---

## 栈布局

虚拟机栈用于存储：

- 局部变量
- 临时计算结果
- 函数参数和返回值
- 调用帧信息

### 栈指针说明

| 指针 | 说明 |
|------|------|
| `m_stack` | 栈空间起始地址 |
| `m_stack_end` | 栈空间结束地址（不可访问） |
| `m_sb` | 当前栈帧基址（Stack Base） |
| `m_sp` | 栈顶指针（Stack Pointer） |

### 栈偏移寻址

字节码中的栈偏移是相对于 `m_sb` 的有符号偏移：

- `SB-0`：`m_sb[0]`
- `SB-1`：`m_sb[-1]`
- `SB+2`：`m_sb[2]`

---

## 字节码生成

### 指令编码

使用 `woort_OpCodeFormal_cons` 宏构造字节码：

```c
const woort_Bytecode bcs[] = {
    /* LOAD G[0], [SB-0] */
    woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 0, 0),

    /* CALL G[4] - 调用常量区索引4处的原生函数 */
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 4),

    /* RET - 返回 */
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0),
};
```

详见 [opcodes.md](./opcodes.md) 了解完整指令集。

---

## 检查请求机制

虚拟机支持异步请求处理：

```c
typedef enum woort_VMRuntime_CheckRequestMask {
    WOORT_VMRUNTIME_CHECK_REQUEST_ABORT = 1 << 0,           /* 中止请求 */
    WOORT_VMRUNTIME_CHECK_REQUEST_STACK_OCCUPYING = 1 << 1, /* 栈占用 */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK = 1 << 2,        /* GC检查 */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING = 1 << 3,   /* GC处理中 */
    WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE = 1 << 4,        /* GC脱离 */
} woort_VMRuntime_CheckRequestMask;
```

### 相关函数

```c
bool woort_VMRuntime_request_set(vm, mask);     /* 设置请求 */
bool woort_VMRuntime_request_check(vm, mask);   /* 检查请求 */
bool woort_VMRuntime_request_accept(vm, mask);  /* 接受请求 */
void woort_VMRuntime_hangup(vm);                 /* 挂起虚拟机 */
void woort_VMRuntime_wakeup(vm);                 /* 唤醒虚拟机 */
```

---

## 完整示例

```c
#include "woort.h"
#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_gc_string.h"
#include <stdio.h>

woort_api my_print(woort_vm vm, woort_value* args) {
    printf("%lld\n", ((woort_Value*)args)->m_integer);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

int main(void) {
    woort_init();

    /* 定义字节码 */
    const woort_Bytecode bcs[] = {
        /* 压栈 5 个槽位 */
        woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 0, 5),
        /* 加载常量 G[0]=100 到 [SB-0] */
        woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 0, 0),
        /* 调用原生函数 G[1] */
        woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 1),
        /* 返回 */
        woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0),
    };

    /* 创建代码环境 */
    woort_CodeEnv* codeenv;
    woort_CodeEnv_create(bcs, sizeof(bcs)/sizeof(woort_Bytecode), 2, &codeenv);

    /* 初始化常量 */
    codeenv->m_data_begin[0].m_integer = 100;
    codeenv->m_data_begin[1].m_native_or_jit_function = &my_print;

    /* 创建虚拟机并执行 */
    woort_VMRuntime* vm;
    woort_VMRuntime_create(&vm);

    woort_CodeEnv_drop(codeenv);
    woort_VMRuntime_invoke(vm, codeenv->m_code_begin);

    /* 清理 */
    woort_VMRuntime_destroy(vm);
    woort_shutdown();
    return 0;
}
```

---

## 注意事项

1. **必须调用 `woort_CodeEnv_drop`**：在执行前释放代码环境的内部管理结构
2. **初始化顺序**：先 `woort_init()`，最后 `woort_shutdown()`
3. **GC 对象**：字符串、向量、映射等对象由 GC 管理，无需手动释放
4. **原生函数返回值**：原生函数必须返回 `WOORT_VM_CALL_STATUS_NORMAL`
5. **线程安全**：同一时间每个线程只能有一个 VM 处于运行状态