# WooRT 运行时使用指南

本文档介绍如何使用 WooRT 运行时：初始化、CodeEnv、VM 生命周期、调用约定以及原生（C）函数编写。权威 API 见 [`include/woort.h`](../include/woort.h)。

> 阅读顺序建议：本文（运行时） → [values.md](./values.md)（值与装箱） → [ir.md](./ir.md)（IR 编译） → [opcodes.md](./opcodes.md)（指令集） → [gc.md](./gc.md)（GC） → [subsystems.md](./subsystems.md)（序列化/VFS/Dylib/调试器）。

## 概述

WooRT 运行时采用字节码解释执行模式，核心组件：

- **woort_CodeEnv**：代码环境，管理字节码、常量池、静态存储区与调试信息（PDB）。
- **woort_VMRuntime**：虚拟机运行时，管理执行栈与状态。每个线程同一时刻只有一个 VM 处于「当前 VM」状态。
- **woort_StackValue**：栈槽索引，所有公开 API 通过它读写值，而非直接操作裸指针。

## 栈槽模型（StackValue）

公开 API 不暴露 `woort_Value*`，而是通过 `woort_StackValue`（`int32_t`）间接寻址：

```c
typedef int32_t woort_StackValue;
#define WOORT_IGNORE       ((woort_StackValue)-2)  /* 丢弃返回值 */
#define WOORT_RETURN_SLOT  ((woort_StackValue)-1)  /* 当前函数的返回值槽 */
```

* **负值**：相对于当前栈帧基址 `m_sb` 的偏移。`WOORT_RETURN_SLOT (-1)` 即 `SB+2`，是约定写返回值的位置。
* **正值**：绝对栈索引（由 `woort_push_reserve` 返回）。
* 参数位于 `SB+3+idx`，即从 `m_sb` 看是 `3+idx`（正值）。原生函数通过相对偏移读取参数（见下）。

## 初始化与关闭

```c
#include "woort.h"

int main(int argc, char** argv) {
    woort_init(argc, argv);        /* 必须最先调用；会同时 setlocale */
    /* ... 使用运行时 ... */
    woort_shutdown(NULL, NULL);    /* 必须最后调用 */
    return 0;
}
```

> **签名变更**：`woort_init` 现在接收 `(int argc, char** argv)`（通常直接传 `main` 的参数）。`woort_shutdown` 接收一个可选的 post-callback 及其自定义数据。

`woort_init` 会：

1. 初始化 woomem（GC 堆）；
2. 注册内置伪库 `"woolang"`（含 `print`、`panic` 等）；
3. 设置平台 locale（UTF-8）。

## 完整使用流程

下面展示从 IR 编译到执行的完整流程（IR 部分详见 [ir.md](./ir.md)）：

```c
#include "woort.h"

woort_api my_print(void) {
    /* 读取第 0 个参数（整数），原生函数参数从 SB+3 开始，相对偏移为 3 */
    woort_Int x = woort_int(3);
    printf("%lld\n", (long long)x);
    woort_ret_void();             /* 写返回槽并返回 NORMAL */
}

int main(int argc, char** argv) {
    woort_init(argc, argv);

    /* 1. 用 IR 编译器构造 CodeEnv */
    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);
    /* ... woort_IRCompiler_add_function / woort_IR_* 发射指令 ... */
    woort_IRConstantIndex c_print = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_val   = woort_IRCompiler_add_constant(&irc);
    /* ... */
    woort_CodeEnv* cenv;
    woort_IRCompiler_finish(&irc, &cenv);

    /* 2. 填充常量池（必须在 lock/unlock 之间） */
    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_extern_function(cenv, c_print, my_print);
    woort_CodeEnv_set_const_int(cenv, c_val, 100);
    woort_CodeEnv_unlock(cenv);

    /* 3. 创建 VM 并切换为当前线程的 active VM */
    woort_VMRuntime* vm;
    woort_VMRuntime_create(&vm);
    woort_VMRuntime_swap(vm);

    /* 4. 调用入口函数 */
    woort_VmCallStatus st = woort_bootup_codeenv(WOORT_IGNORE, cenv);

    /* 5. 释放 CodeEnv（GC 会回收其字节码/常量内存） */
    woort_CodeEnv_drop(cenv);

    /* 6. 清理 */
    woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);
    woort_shutdown(NULL, NULL);
    return 0;
}
```

---

## woort_CodeEnv

### 结构

`woort_CodeEnv` 是 GC 对象（以 `woort_GCUnit` 为首成员），定义见 `src/woort_codeenv.h`。公开 API 中它是**不透明句柄**，关键字段：

| 字段（内部） | 说明 |
|--------------|------|
| `m_code_begin` / `m_code_end` | 字节码区间 |
| `m_data_begin[]` | 常量池 + 静态存储区（柔性数组，`woort_Value`） |
| `m_constant_count` / `m_data_count` | 常量数 / 总数据槽数 |
| `m_pdb` | 程序调试数据库（源码映射、函数边界、局部变量名） |
| `m_trap_records` | 断点（trap）记录表 |
| `m_extern_constants` | 外部常量名 → 索引 注册表 |
| `m_extern_libs` | 关联的外部库句柄列表 |
| `m_const_records` | 常量池元数据（用于二进制序列化） |
| `m_mutex` | 保护并发访问的互斥锁 |

### 主要函数

| 函数 | 说明 |
|------|------|
| `woort_CodeEnv_create(bytecodes, count, const_count, static_count, &out)` | 从裸字节数组创建（低层 API） |
| `woort_CodeEnv_drop(env)` | 释放 CodeEnv 及其全部资源 |
| `woort_CodeEnv_lock(env)` / `unlock(env)` | 加/解锁（写常量池、装/卸 trap 时必须持锁） |
| `woort_CodeEnv_query_function(env, f, &out_addr)` | 查询某 IR 函数的字节码入口地址 |
| `woort_CodeEnv_find(env_addr, &out)` | 按字节码地址反查所属 CodeEnv |
| `woort_CodeEnv_set_const_*(env, cidx, val)` | 写常量池（int/real/string/extern_function/...） |
| `woort_CodeEnv_find_srcloc_by_offset(...)` | 字节码偏移 → 源码位置 |
| `woort_CodeEnv_find_function_name_by_offset(...)` | 字节码偏移 → 函数名 |
| `woort_CodeEnv_register_extern_constant(env, name, &cidx)` | 注册具名外部常量 |
| `woort_CodeEnv_save_binary / restore_binary` | 二进制序列化（见 [subsystems.md](./subsystems.md)） |

### 常量池填充约定

常量池索引（`woort_IRConstantIndex`）必须在 `woort_IRCompiler_finish()` **之前**通过 `woort_IRCompiler_add_constant()` 预留；**之后**再在 `woort_CodeEnv_lock()/unlock()` 之间用 `woort_CodeEnv_set_const_*` 写入实际值：

```c
woort_CodeEnv_lock(cenv);
woort_CodeEnv_set_const_int(cenv, cidx_int, 42);
woort_CodeEnv_set_const_real(cenv, cidx_real, 3.14);
woort_CodeEnv_set_const_buffer(cenv, cidx_str, "hello", 5);   /* 字符串经 buffer 写入 */
woort_CodeEnv_set_const_extern_function(cenv, cidx_fn, my_native_fn);
woort_CodeEnv_unlock(cenv);
```

### 执行入口约定

默认入口是常量池中名为 `WOORT_DEFAULT_ENTRY`（`"@entry"`）的具名外部常量。`woort_bootup_codeenv` 会加载该常量并调用。也可以用 `woort_load_const` / `woort_load_extern_const` 自行加载任意函数值后用 `woort_invoke` 调用。

---

## woort_VMRuntime

`woort_VMRuntime` 是不透明句柄（内部结构见 `src/woort_vm.h`）。

### 生命周期

| 函数 | 说明 |
|------|------|
| `woort_VMRuntime_create(&out)` | 创建 VM 实例 |
| `woort_VMRuntime_destroy(vm)` | 销毁 VM 实例 |
| `woort_VMRuntime_swap(vm)` | 把当前线程的 active VM 切换为 `vm`，返回前一个 |
| `woort_VMRuntime_current()` | 获取当前线程的 active VM |
| `woort_VMRuntime_weaken(vm)` | 把 VM 标记为弱引用（GC 不再自动当作 root） |

**多线程模型**：每个线程同一时刻只有一个 VM 处于「当前 VM」。所有依赖栈的 API（`woort_set_*`、`woort_int`、`woort_invoke` 等）都作用于 `woort_VMRuntime_current()`。切换 VM 用 `woort_VMRuntime_swap`，传 `NULL` 则脱离当前线程的 VM 作用域。

### 调用入口

```c
/* 加载默认入口 @entry 并调用 */
woort_VmCallStatus woort_bootup_codeenv(woort_StackValue dst, woort_CodeEnv* cenv);

/* 调用栈上已就位的函数值 */
woort_VmCallStatus woort_invoke(woort_StackValue dst, woort_StackValue f);

/* 启动协程 / 恢复协程 */
woort_VmCallStatus woort_spawn(woort_StackValue dst, woort_StackValue f);
woort_VmCallStatus woort_resume(woort_StackValue dst);
```

* `dst`：返回值写入的栈槽，传 `WOORT_IGNORE` 丢弃。
* `f`：持有可调用值（脚本函数/JIT/原生/闭包）的栈槽。

### 调用状态（woort_VmCallStatus）

```c
typedef enum woort_VmCallStatus {
    WOORT_VM_CALL_STATUS_NORMAL,   /* 正常返回 */
    WOORT_VM_CALL_STATUS_YIELD,    /* 协程主动让出（仅 spawn/resume 可返回）*/
    WOORT_VM_CALL_STATUS_ABORTED,  /* 程序终止（panic/abort），VM 不可继续 */
    WOORT_VM_CALL_STATUS_RESYNC,   /* JIT/解释执行切换需要重新同步 */
} woort_VmCallStatus;
```

| 状态 | 说明 |
|------|------|
| `NORMAL` | 函数正常返回 |
| `YIELD` | 协程请求暂停，可稍后 `woort_resume` 继续。`invoke` 若返回 `YIELD` 会 panic |
| `ABORTED` | 程序被终止，VM 被标记为 aborted，拒绝继续执行 |
| `RESYNC` | JIT 与解释执行切换时的内部状态，外部 dispatch 需再次调度 |

---

## 栈布局

VM 栈用于存储局部变量、临时值、函数参数与调用帧信息。

```
sp      |                        | < 下一个值压入位置
        |~~~~~~~~~~~~~~~~~~~~~~~|
...     |   局部变量 / 临时值     |
        |_______________________|
sb      |_______________________| < 闭包捕获解包位置（如果是闭包）
sb + 1  |__ CALLWAY & BPOFFSET __| < 调用方式 + 调用者帧偏移
sb + 2  |____ RETURN ADDRESS ____| < * 返回值存储位置（WOORT_RETURN_SLOT）
sb + 3  |_____ ARGUMENT 0 ______| < 变长参数数量（如果是变长参数函数）
sb + 4  |_____ ARGUMENT 1 ______|
```

### 关键指针（内部，定义于 src/woort_vm.h）

| 指针 | 说明 |
|------|------|
| `m_stack` | 栈空间起始地址 |
| `m_stack_end` | 栈空间尾后位置（不可访问） |
| `m_sb` | 当前栈帧基址（Stack Base） |
| `m_sp` | 栈顶（下一个压入位置） |
| `m_ip` | 指令指针 |
| `m_env` | 最近一次同步的 CodeEnv（调试器等应通过调用栈查询而非依赖此字段） |

### 栈预留/释放（公开 API）

```c
bool woort_push_reserve(size_t count, woort_StackValue* out_stack);  /* 预留 count 个槽，返回基址索引 */
void woort_pop(size_t count);                                         /* 弹出并丢弃 count 个值 */
woort_Value* woort_internal_value(woort_StackValue src);             /* 直接读栈槽指针（内部 API）*/
```

---

## 原生（C）函数编写

### 函数签名

原生函数是一个**不接收 C 参数**、返回 `woort_VmCallStatus`（`woort_api`）的函数：

```c
typedef woort_api(*woort_NativeFunction)(void);
```

参数通过 VM 栈传递，用栈读取函数访问。函数指针通过 `woort_CodeEnv_set_const_extern_function` 写入常量池，或包装为闭包用 `woort_CodeEnv_set_const_extern_closure`。

### 读取参数

参数位于 `SB+3+idx`。在原生函数内，第 0 个参数的相对偏移是 `3`，第 1 个是 `4`，依此类推：

```c
woort_api add(void) {
    woort_Int a = woort_int(3);       /* 第 0 个参数 */
    woort_Int b = woort_int(4);       /* 第 1 个参数 */
    woort_ret_int(a + b);             /* 写返回槽并返回 NORMAL */
}
```

读取族函数（`woort_<type>`）：

| 函数 | 说明 |
|------|------|
| `woort_int(src)` | 读整数 |
| `woort_real(src)` / `woort_float(src)` | 读实数/单精度 |
| `woort_bool(src)` | 读布尔 |
| `woort_string(src)` | 读 UTF-8 C 字符串指针 |
| `woort_buffer(src, &len)` | 读缓冲区指针与长度 |
| `woort_pointer(src)` | 读指针（整数强转） |
| `woort_gcpointer(src)` | 读裸 GC 指针 |
| `woort_unbox_int/real/bool(src)` | 从 DynBox 拆箱读标量 |
| `woort_unbox(dst, src)` | 拆箱并返回类型标签 |
| `woort_unbox_type(src)` | 查询 DynBox 类型标签 |

### 写返回值

返回值通过写 `WOORT_RETURN_SLOT` 槽实现。便捷宏 `woort_ret_*` 会同时写槽并返回 `NORMAL`：

```c
woort_ret_void();            /* 返回 void */
woort_ret_int(42);
woort_ret_real(3.14);
woort_ret_bool(true);
woort_ret_string("hello");
woort_ret_value(some_slot);  /* 返回栈上某槽的值 */
woort_ret_nil();
woort_ret_box_int(42);       /* 返回装箱整数 */
```

### 非正常返回

```c
woort_api woort_ret_panic(const char* fmt, ...);   /* 触发 panic，返回 ABORTED */
woort_api woort_ret_yield(void);                    /* 协程让出（仅协程函数可用）*/
```

### 写栈槽（woort_set_*）

`woort_set_*` 族用于向任意栈槽写值（参数传递、构建容器等）：

```c
woort_set_int(dst, 42);
woort_set_real(dst, 3.14);
woort_set_string(dst, "hello");
woort_set_string_fmt(dst, "x=%d", x);
woort_set_buffer(dst, buf, len);
woort_set_value(dst, src);            /* 槽间复制 */
woort_set_dup_boxed(dst, src);        /* 深拷贝 vec/map/struct */
woort_set_nil(dst);

/* 创建容器 */
woort_set_vec(dst);                   /* 空向量 */
woort_set_map(dst);                   /* 空映射 */
woort_set_struct(dst, cap);           /* 空结构体（指定容量）*/

/* 装箱值 */
woort_set_box_int(dst, 42);
woort_set_box_real(dst, 3.14);
woort_set_box_bool(dst, true);
```

### GC 安全：GCPin

原生函数中若需要在调用其他会触发 GC 的 API 时保住某些值，使用 `woort_GCPin`：

```c
woort_GCPin* pin = woort_GCPin_create(2);
woort_GCPin_set(pin, 0, some_slot);
/* ... 执行可能触发 GC 的操作 ... */
woort_GCPin_get(dst, pin, 0);
woort_GCPin_destroy(pin);
```

详见 [gc.md](./gc.md)。

### 示例：原生打印字符串

```c
woort_api print_string(void) {
    woort_U8CString s = woort_string(3);   /* 第 0 个参数 */
    fputs(s, stdout);
    fputc('\n', stdout);
    woort_ret_void();
}
```

### 示例：访问与修改容器

```c
woort_api vec_sum(void) {
    woort_StackValue vec = 3;              /* 第 0 个参数是向量 */
    size_t n = woort_vec_len(vec);
    woort_Int sum = 0;
    woort_StackValue tmp = woort_push_reserve(1, &(woort_StackValue){0});
    /* 注意：push_reserve 需要传出索引；这里简化展示 */
    for (size_t i = 0; i < n; ++i) {
        if (!woort_vec_get(tmp, vec, i)) { /* 取出第 i 个（装箱值）到 tmp */
            woort_ret_panic("index out of range");
        }
        sum += woort_unbox_int(tmp);       /* 拆箱为整数累加 */
    }
    woort_pop(1);
    woort_ret_int(sum);
}
```

> 容器读写、union/option/result 构造等 API 的完整列表见 [values.md](./values.md)。

---

## 异步请求机制（CheckRequest）

VM 支持异步请求处理，通过 `m_check_request_mask`（原子）协调 GC、调试器、终止等事件。请求在**检查点**（`JBCK` 回跳、native-call 返回等）被处理。主要请求位：

| 请求位 | 说明 |
|--------|------|
| `ABORT` | 程序终止（panic 后设置） |
| `STACK_OCCUPYING` | 栈正被重分配或外部读取，VM 挂起 |
| `GC_CHECK` | GC 工作线程请求 VM 自标记 |
| `GC_PROCESSING` | VM 正被代理标记 |
| `GC_LEAVE` | VM 暂时脱离 GC 作用域 |
| `DEBUG_CALLBACK` | 请求 VM 执行调试回调 |
| `YIELD` | 请求以 YIELD 结束执行 |
| `TERMINATE` | 外部请求立即终止 |
| `SHRINK_STACK` | 请求收缩栈 |
| `GC_MARK_FINISHED` | 标记阶段完成 |

相关内部函数：`woort_VMRuntime_request_set/check/accept`、`woort_VMRuntime_hangup/wakeup`、`woort_VMRuntime_gc_checkpoint`。

---

## 调用栈追踪

```c
woort_VMRuntime_TraceCallstack_Iter iter;
woort_VMRuntime_trace_begin(vm, &iter);

woort_VMRuntime_TraceCallstack frame;
while (woort_VMRuntime_trace_next(&iter, &frame)) {
    /* frame 包含每一层的函数地址、源码位置等 */
}
```

便捷函数：`woort_VMRuntime_print_backtrace(vm, stream)`、`woort_VMRuntime_log_trace(&trace)`。

---

## 注意事项

1. **初始化顺序**：先 `woort_init(argc, argv)`，最后 `woort_shutdown(NULL, NULL)`。
2. **当前 VM**：所有栈操作 API 作用于 `woort_VMRuntime_current()`。线程内切换用 `woort_VMRuntime_swap`。
3. **常量池写入**：必须在 `woort_CodeEnv_lock()/unlock()` 之间，且索引需在 `finish()` 前预留。
4. **GC 对象**：字符串、向量、映射、结构体、闭包由 GC 管理，无需手动释放。
5. **原生函数返回值**：必须用 `woort_ret_*` 宏写返回槽并返回 `WOORT_VM_CALL_STATUS_NORMAL`（或 `ret_yield`/`ret_panic`）。
6. **线程安全**：每个线程同时只能有一个 VM 处于运行状态。跨线程访问 GC 对象需用 `woort_GC_sync_marking_lock`（见 [gc.md](./gc.md)）。
7. **CodeEnv 释放**：`woort_CodeEnv_drop` 释放全部资源；执行完成后即可调用（字节码在执行期间已被 VM 引用）。
