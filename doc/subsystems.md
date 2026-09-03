# 子系统

本文档介绍 WooRT 的几个独立子系统：二进制序列化、虚拟文件系统（VFS）、动态库（Dylib）、调试器与陷阱（Debugger/Trap）、线程与协程。所有公开 API 见 [`include/woort.h`](../include/woort.h)。

---

## 1. 二进制序列化（CodeEnv 存档）

CodeEnv 可序列化为二进制缓冲区，以便缓存编译产物、跨进程加载等。

### 公开 API

```c
/* 序列化到 malloc 缓冲区（调用方负责用 woort_free 释放）*/
bool woort_CodeEnv_save_binary(woort_CodeEnv* code_env, void** out_buffer, size_t* out_len);

/* 从 VFile 流反序列化 */
woort_CodeEnv_RestoreResult woort_CodeEnv_restore_binary(woort_VFile* f, woort_CodeEnv** out_code_env);

/* 错误码描述 */
const char* woort_CodeEnv_restore_failed_desc(woort_CodeEnv_RestoreResult rt);
```

### Restore 结果码

```c
typedef enum woort_CodeEnv_RestoreResult {
    WOORT_CODEENV_RESTORE_OK = 0,
    /* I/O 错误 */
    WOORT_CODEENV_RESTORE_FAIL_READ = 1,        /* 无法读取 */
    WOORT_CODEENV_RESTORE_FAIL_ALLOC = 2,       /* 内存分配失败 */
    /* 头部校验 */
    WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH = 3,
    WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH = 4,
    /* 结构错误 */
    WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE = 5,
    WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV = 6,
    /* 常量数据错误 */
    WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA = 7,
    WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE = 8,
    WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET = 9,
    WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE = 10,   /* 无法解析外部函数/库 */
} woort_CodeEnv_RestoreResult;
```

### 二进制格式（`src/woort_codeenv_bin.c`）

* **Magic**：`0x54524f57`（ASCII `"WORT"`）
* **Version**：`7`（与 v6 同字段、同顺序、同编码；仅版本号变更并修正 struct 成员索引回退 bug——对状态正常的 CodeEnv 产生相同字节）
* **布局**：32 字节头部（magic、version、code_size、data_count、constant_count）→ 代码段 → 字符串池（长度前缀，二进制安全）→ 外部库表 → 常量数据 → 外部常量映射 → 函数边界 / 源码映射 / trap / 局部变量 / 静态变量调试信息。详见 `src/woort_codeenv_bin.c` 文件顶部的布局注释。
* **外部函数恢复**：常量池中的 extern 函数/闭包记录了库名与函数名（见 `woort_ConstRecord`），反序列化时通过 Dylib 子系统重新解析符号。

> 序列化要求常量池的每个槽都通过 `woort_CodeEnv_set_const_record()` 记录了类型元数据（`woort_ConstRecordType`），因为 `woort_Value` 是无标签联合体，无法从其二进制表示反推类型。

---

## 2. 虚拟文件系统（VFS）

VFS 是一个全局注册表，用于在运行时嵌入「虚拟文件」，使 `import`/`require` 等路径解析能命中内存中的内容而非磁盘。

### 虚拟文件 URI

虚拟文件以 `woovf://` 为协议前缀：

```c
#define WOORT_VFS_SCHEME     "woovf://"
#define WOORT_VFS_SCHEME_LEN 8
```

### 注册与查询

```c
bool woort_vfs_create(const char* filepath, const void* data, size_t length, bool enable_modify);
bool woort_vfs_remove(const char* filepath);
bool woort_vfs_is_virtual_uri(const char* uri);
bool woort_vfs_exists(const char* filepath);
size_t woort_vfs_get_all_paths(char*** out_paths);
```

* `woort_vfs_create`：注册一个虚拟文件（创建或覆盖）。`enable_modify=true` 才允许后续 `woort_vfs_remove`。
* `woort_vfs_get_all_paths`：返回所有已注册路径（malloc 分配的 NULL 终止数组，用 `woort_free` 释放）。

### 路径解析

```c
bool woort_fs_is_file_readable(const char* path);
bool woort_vfs_resolve_path(filepath, search_dirs, search_dir_count, &out_resolved_path);
```

`woort_vfs_resolve_path` 按以下顺序解析路径：

1. 若是 `woovf://` URI → 直接命中虚拟文件；
2. 搜索 `search_dirs` 中的目录；
3. 当前工作目录；
4. 可执行文件所在目录；
5. 原样使用；
6. 尝试 `woovf://` + path。

### 流式文件（VFile）

```c
typedef struct woort_VFile woort_VFile;

bool    woort_vfile_open(const char* filepath, woort_VFile** out_file);
bool    woort_vfile_open_reader(const void* buf, size_t buflen, woort_VFile** out_file);
size_t  woort_vfile_read(woort_VFile* file, void* buffer, size_t size);
bool    woort_vfile_seek(woort_VFile* file, int64_t offset, int whence);
int64_t woort_vfile_tell(woort_VFile* file);
int64_t woort_vfile_size(woort_VFile* file);
void    woort_vfile_close(woort_VFile* file);
```

* `woort_vfile_open`：`woovf://` 开头走 VFS，否则 `fopen` 磁盘文件。
* `woort_vfile_open_reader`：把外部内存缓冲包装为只读 VFile（**不拷贝、不持有、不释放**）。

VFile 有三种内部类型（`src/woort_vfs.h`）：`REAL`（磁盘 `FILE*`）、`VIRTUAL`（注册表条目 + 读位置）、`READER`（外部缓冲）。虚拟文件条目带引用计数，`open/close` 配对。

---

## 3. 动态库（Dylib）

Dylib 子系统统一管理**原生库**（`.dll`/`.so`/`.dylib`）与**伪库**（进程内函数表），提供一致的符号查找接口。

### 库句柄与卸载标志

```c
typedef struct woort_Dylib woort_Dylib;

typedef enum woort_DylibUnloadMethod {
    WOORT_DYLIB_NONE = 0,
    WOORT_DYLIB_UNREF          = 1 << 0,  /* 引用计数减一，归零释放 */
    WOORT_DYLIB_BURY           = 1 << 1,  /* 从具名注册表移除 */
    WOORT_DYLIB_UNREF_AND_BURY = WOORT_DYLIB_UNREF | WOORT_DYLIB_BURY,
} woort_DylibUnloadMethod;
```

### 伪库（fake library）

伪库由用户提供的函数表支撑，无需磁盘文件：

```c
typedef struct woort_ExternLibFunc {
    const char* m_name;          /* 函数名（NULL = 表结束）*/
    void* m_func_addr;           /* 函数指针（NULL = 表结束）*/
} woort_ExternLibFunc;

#define WOORT_EXTERN_LIB_FUNC_END { NULL, NULL }

woort_Dylib* woort_dylib_fake(const char* libname,
                              const woort_ExternLibFunc* funcs,
                              woort_Dylib* dependence_dylib);
```

`woort_init` 会自动注册内置伪库 `"woolang"`（含 `print`、`panic` 等），可通过 `woort_get_builtin_lib()` 获取。

### 加载与符号查找

```c
woort_Dylib* woort_dylib_load(const char* libname, const char* path,
                              const char* script_path, bool panic_when_fail);
void*        woort_dylib_load_func(woort_Dylib* lib, const char* funcname);
const char*  woort_dylib_get_func_name(woort_Dylib* lib, void* func_addr);  /* 反查，func_addr 可为 NULL */
void         woort_dylib_unload(woort_Dylib* lib, woort_DylibUnloadMethod method);
void         woort_dylib_keep(woort_Dylib* lib);                            /* 引用计数加一 */
```

* `woort_dylib_load` 的搜索顺序：`script_path` → 当前目录 → 可执行文件目录 → path 原样 → OS 默认。前 3 步会自动追加 `.dll`/`.so`/`.dylib`。
* `woort_dylib_load_func`：原生库用 `GetProcAddress`/`dlsym`；伪库线性查表。
* `woort_dylib_get_func_name`：通过内部「已解析函数」表（addr→name 缓存）反查，用于调用栈追溯。

### 与 CodeEnv 关联

```c
bool woort_CodeEnv_add_extern_lib(woort_CodeEnv* env, woort_Dylib* lib);
```

关联后，CodeEnv 被 GC 回收时会自动对每个关联库调用 `woort_dylib_unload(WOORT_DYLIB_UNREF)`。`add_extern_lib` 会增加库的引用计数。

> **线程性**：`woort_dylib_get_func_name` 使用内部 RWSpinlock 保护 addr→name 缓存，但调用栈追溯场景仍需注意并发（见 `src/woort_dylib.h` 注释）。

---

## 4. 调试器（WAIPO）与陷阱（Trap）

### WAIPO 调试器

WAIPO（Watch And Inspect Program Operation）是 WooRT 的交互式、多 VM 调试器，支持断点、单步、值检查。

```c
typedef enum woort_DebuggerAttachResult {
    WOORT_DEBUGGER_ATTACH_RESULT_FAILED,            /* OOM 或其他失败            */
    WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED,  /* 已有调试器挂载            */
    WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS,           /* 新挂载成功                */
} woort_DebuggerAttachResult;

typedef struct woort_WAIPO_Debugger woort_WAIPO_Debugger;  /* 不透明调试器句柄 */

typedef void (*woort_WAIPO_Debugger_TrapCallback)(woort_WAIPO_Debugger*, woort_VMRuntime*);

woort_DebuggerAttachResult woort_WAIPO_Debugger_attach(
    /* OPTIONAL */ woort_WAIPO_Debugger_TrapCallback breakdown_callback,  /* NULL = 命令行 REPL */
    /* OPTIONAL */ woort_WAIPO_Debugger** out_debugger_handle);           /* 取回调试器实例 */

void woort_VMRuntime_Debugger_try_breakdown_any_vm(void);  /* 请求任一运行中的 VM 中断进调试器 */

size_t woort_WAIPO_Debugger_query_vms(
    /* OPTIONAL */ woort_VMRuntime** out_vms,  /* NULL = 只取数量（此时 count 须为 0） */
    size_t vms_buffer_count);                  /* 返回当前时刻的存活 VM 数量 */
```

* `attach`：引擎侧逻辑（断点命中、step/next/return 完成、调试中断请求）始终由内部回调执行，与传入的回调无关；VM 真正停下（陷阱）时才调用陷阱回调——`breakdown_callback` 非 NULL 时用之，否则进入内置命令行 REPL。`out_debugger_handle` 用于取回调试器实例，例如在自定义回调里驱动会话。若已有调试器挂载，`attach` 会释放新实例并返回 `ALREADY_ATTACHED`，原调试器保持不变。
* `query_vms`：遍历 GC 的 root VM 集合（`woort_GC_foreach_root_vm`），向 `out_vms` 最多写入 `vms_buffer_count` 个 VM，返回遍历到的存活 VM 总数（可能大于缓冲区容量，调用方可据此重试更大的缓冲）。无论是否挂载调试器均可调用。
* `try_breakdown_any_vm`：异步且尽力而为。布防一次性标志并唤醒后台 GC 线程后立即返回（可用于信号处理函数）；GC 开始时向所有 root VM 设置 `WOORT_VMRUNTIME_CHECK_REQUEST_EXTERNAL_DEBUG_BREAK`，最先到达检查点的 VM 赢得 race、取消其余 VM 的请求并中断进调试器——每次调用**至多一个** VM 停下。无调试器挂载时请求被消费，VM 照常运行。

### Ctrl+C 信号处理

```c
void woort_ctrlc_setup(void);     /* 注册 SIGINT 处理器 */
void woort_ctrlc_teardown(void);  /* 恢复默认 SIGINT 处置 */
```

`ctrlc_setup` 的行为：

1. 首次 SIGINT：自动挂载 WAIPO 调试器（默认命令行 REPL），并请求任一运行中的 VM 中断进调试器；
2. 2 秒内连续 SIGINT 计数；
3. 累计 4 次 → 调用 `abort()`。

### 陷阱（Trap = 运行时断点）

陷阱是调试断点的底层机制：在某个字节码地址上把指令替换为 `DEBUGTRAP`，原指令存入查表，命中时进入调试器。

```c
bool woort_CodeEnv_set_trap(woort_CodeEnv* env, woort_Bytecode* code);        /* 设断点 */
bool woort_CodeEnv_clear_trap(woort_CodeEnv* env, woort_Bytecode* code);      /* 清断点 */
woort_Bytecode woort_CodeEnv_raw_trap(woort_CodeEnv* env, const woort_Bytecode* code); /* 读原始指令 */
```

* `set_trap`：把 `code` 处的字节码替换为 `DEBUGTRAP`，原指令存入 `m_trap_records`（`woort_HashMap`）。线程安全（持 `m_mutex`）。返回 `false` 表示该地址已有陷阱或 OOM。
* `clear_trap`：恢复原指令并移除记录。
* `raw_trap`：透明读取「陷阱前」的原始指令（用于反汇编），无陷阱则原样返回。

陷阱记录存储在 `woort_CodeEnv::m_trap_records`（`src/woort_codeenv.h`），由 `m_mutex` 保护。

### 编译期调试陷阱

IR 层可用 `woort_IR_debugtrap(f)` 发射 `DEBUGTRAP` 指令（见 [ir.md](./ir.md)）。

### 分层结构

* **`src/woort_vm_debugger_api.h`**：底层 VM 调试回调管道。`woort_VMRuntime_Debugger_attach(callback, context, destroy_callback)` 注册通用回调（调试器对象带引用计数，回调在全局执行互斥下运行，同一时刻只有一个调试器回调在执行），VM 在检查点调用；`try_breakdown_any_vm` 的 GC 传播与 `EXTERNAL_DEBUG_BREAK` race 仲裁也在这里实现。
* **`src/woort_waipo_debugger.h/.c`**：WAIPO 调试器本体（引擎与状态）。断点集中在 `woort_WAIPO_BreakpointCollection`：
  * `m_breakpoints`：普通断点（ip → 计数）；
  * `m_debug_breakpoints`：无条件断点（无视 focus，总中断）；
  * `m_user_breakpoints`：用户通过 break 命令设置的断点（一条源码行可对应多个指令地址，用于列表/删除）。每个断点带创建时分配的稳定编号（`m_id`，由 `m_next_breakpoint_id` 单调递增供给、永不复用），删除某个断点不会使其余断点编号移位，`delete` 命令按编号查找。

  另有 `m_focusing_vms`（各受关注 VM 的局部上下文：单步断点、步进目标源码位置等）与 `m_trap_callback`（陷阱回调）。支持单步（step）、源码单步（step source）、next（step over）、step out（run until return）、focus 切换等。
* **`src/woort_waipo_debugger_cmd.c`**：命令行交互层——命令解析、命令表、REPL 循环与 printf 输出，只做 CLI 交互/展示，不含调试状态逻辑。

---

## 5. 线程与协程

### OS 线程原语（内部）

`src/woort_threads.h` 提供 OS 线程抽象，**不**通过 `woort.h` 公开：

* `woort_Thread` + `woort_thread_start/join/sleep_ms/yield`
* 互斥锁族：`woort_Mutex`、`woort_TimeMutex`、`woort_RecursiveMutex`、`woort_TimeRecursiveMutex`
* 条件变量：`woort_ConditionVariable`（wait/timed_wait/signal/broadcast）
* `WOORT_THREAD_LOCAL` 宏

后端选择：C11 线程 / Win32 / pthread。

### 公开的线程相关 API

公开层只暴露：

```c
woort_VMRuntime* woort_VMRuntime_swap(woort_VMRuntime* vm);  /* 切换线程局部 active VM */
woort_VMRuntime* woort_VMRuntime_current(void);              /* 获取当前 active VM */

bool woort_GC_sync_marking_lock(void);                       /* 获取临时 GC 作用域 */
void woort_GC_sync_marking_unlock(void);
```

### 协程（cooperative）

WooRT 支持**协作式协程**（非 OS 线程）：

```c
woort_VmCallStatus woort_spawn(woort_StackValue dst, woort_StackValue f);  /* 启动协程 */
woort_VmCallStatus woort_resume(woort_StackValue dst);                     /* 恢复协程 */
```

* `woort_spawn`：从函数值 `f` 启动新协程。返回 `NORMAL`（协程执行完毕）、`YIELD`（协程主动让出）或 `ABORTED`。
* `woort_resume`：恢复最近让出的协程。
* 协程通过原生函数中的 `woort_ret_yield()` 主动让出（见 [runtime.md](./runtime.md)）。

> **注意区分**：`woort_spawn` 是协程 API（返回 `woort_VmCallStatus`，含 `YIELD`），与 OS 线程无关。每个 OS 线程同时只能有一个 active VM。

---

## 6. 其他设施

### 控制台 I/O

```c
char* woort_console_readline(void);   /* 从控制台读一行 UTF-8（malloc 分配，用 woort_free 释放）*/
void  woort_free(void* buf);          /* 释放 woort 分配的缓冲 */
const char* woort_env_locale_name(void); /* 平台 UTF-8 locale 名（静态分配）*/
```

### UTF-8 工具

`src/woort_utf8.c` 实现 UTF-8 / char16_t / char32_t 转换工具，其接口通过 `woort.h` 的 "Raw UTF-8 Helpers" 区段导出（不再使用独立的 `woort_utf8.h`）。`woort_Char` 即 `char32_t`。

### 诊断与日志

```c
/* woort_panic 是 include/woort.h 中的宏，触发 ABORT */
/* woort_ret_panic(fmt, ...) 在原生函数中触发 panic */

/* 自定义 panic 处理器（覆盖默认的「打印 + abort」行为）*/
typedef enum woort_PanicHandler_Action {
    WOORT_PANIC_HANDLER_ACTION_ABORT,                /* 终止程序（默认）*/
    WOORT_PANIC_HANDLER_ACTION_CONTINUE,             /* 吞掉 panic 继续执行 */
    WOORT_PANIC_HANDLER_ACTION_USE_DEFAULT_HANDLER,  /* 交给默认处理器 */
} woort_PanicHandler_Action;

typedef woort_PanicHandler_Action(*woort_PanicHandlerFunction)(
    /* OPTIONAL */ woort_VMRuntime* vm,
    const char* funcname, const char* location, int line,
    int reason, const char* message);

woort_PanicHandlerFunction woort_set_panic_callback(
    /* OPTIONAL */ woort_PanicHandlerFunction callback);   /* 返回前一个处理器 */
```

### 版本信息

```c
const char* woort_version(void);     /* "major.minor.patch.tweak" 形式的版本字符串 */
uint64_t    woort_version_int(void); /* 64 位打包版本（每 16 位一个字段）*/
```

`include/woort.h` 顶部的 `WOORT_VERSION` 宏定义为 `WOORT_VERSION_WRAP(major, minor, patch, tweak)`，可用于编译期断言。

### 设置

`src/woort_setting.h` 提供运行时配置项（内部）。`woort_init(argc, argv)` 可从命令行参数读取配置。

### 反汇编

`src/woort_disassembly.h` 提供字节码反汇编。`woort_CodeEnv_dumps(env)` 输出 CodeEnv 的反汇编。
