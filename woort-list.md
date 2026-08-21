# woort.h API 清单

整理自 `include/woort.h`（共 5304 行，版本 `1.0.6.17`）。

目录：

- [1. 枚举](#1-枚举)
- [2. 函数指针类型（typedef）](#2-函数指针类型typedef)
- [3. 常量](#3-常量)
- [4. 宏](#4-宏)
- [5. 函数](#5-函数)
- [附录：其他类型别名与不透明句柄](#附录其他类型别名与不透明句柄)

> 约定：`woort_api` 是 `woort_VmCallStatus` 的类型别名（见 2.1）。
> 标注 `OPTIONAL` 的参数允许传 NULL / `WOORT_IGNORE`（按各函数注释）。

---

## 1. 枚举

### 1.1 `woort_VmCallStatus`（别名 `woort_api`）— VM 调用派发状态码

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_VM_CALL_STATUS_NORMAL` | 0 | 正常返回 |
| `WOORT_VM_CALL_STATUS_YIELD` | 1 | 请求挂起（可恢复） |
| `WOORT_VM_CALL_STATUS_ABORTED` | 2 | 程序被终止，VM 拒绝继续执行 |
| `WOORT_VM_CALL_STATUS_RESYNC` | 3 | 下一层调用栈需要反向同步 / 检查点 |

### 1.2 `woort_BoxValueType` — 装箱动态值类型标签

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_BOX_VALUE_TYPE_GCUNIT` | 0（0b000） | GC 管理单元 |
| `WOORT_BOX_VALUE_TYPE_REAL` | 1（0b001） | 装箱 double |
| `WOORT_BOX_VALUE_TYPE_INT` | 2（0b010） | 装箱 64 位有符号整数 |
| `WOORT_BOX_VALUE_TYPE_BOOL` | 4（0b100） | 装箱布尔 |
| `WOORT_BOX_VALUE_TYPE_NIL` | 8（0b1000） | nil 值 |
| `WOORT_BOX_VALUE_TYPE_STRING` | 9 | 字符串 |
| `WOORT_BOX_VALUE_TYPE_VEC` | 10 | 向量（动态数组） |
| `WOORT_BOX_VALUE_TYPE_MAP` | 11 | map（哈希表） |
| `WOORT_BOX_VALUE_TYPE_STRUCT` | 12 | struct |
| `WOORT_BOX_VALUE_TYPE_GCHANDLE` | 13 | GC 句柄（外部资源） |
| `WOORT_BOX_VALUE_TYPE_CLOSURE` | 14 | 闭包 |

### 1.3 `woort_DylibUnloadMethod` — 动态库卸载方式标志

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_DYLIB_NONE` | 0 | 无 |
| `WOORT_DYLIB_UNREF` | 1（1<<0） | 引用计数减一，减到 0 时释放 |
| `WOORT_DYLIB_BURY` | 2（1<<1） | 从命名库注册表中移除 |
| `WOORT_DYLIB_UNREF_AND_BURY` | 3（UNREF \| BURY） | 组合：unref + bury |

### 1.4 `woort_GCAllocate_Flag` — GC 分配行为标志

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_GCALLOCATE_FLAG_NONE` | 0 | 无 |
| `WOORT_GCALLOCATE_FLAG_AUTO_MARK` | 1 | 自动标记 |

### 1.5 `woort_CodeEnv_RestoreResult` — CodeEnv 反序列化结果码

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_CODEENV_RESTORE_OK` | 0 | 成功 |
| `WOORT_CODEENV_RESTORE_FAIL_READ` | 1 | 无法从 vfile 读取 |
| `WOORT_CODEENV_RESTORE_FAIL_ALLOC` | 2 | 内存分配失败 |
| `WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH` | 3 | 魔数不匹配 |
| `WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH` | 4 | 版本不匹配 |
| `WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE` | 5 | 代码大小超限 |
| `WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV` | 6 | 创建 CodeEnv 失败 |
| `WOORT_CODEENV_RESTORE_FAIL_INVALID_STRPOOL` | 7 | 字符串池大小非法 |
| `WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA` | 8 | 数据截断 / 未终结 |
| `WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE` | 9 | 未知常量类型标签 |
| `WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET` | 10 | 池内偏移非法 |
| `WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE` | 11 | 无法解析外部函数 / 库 |

### 1.6 `woort_PanicReason` — panic 原因码

| 项 | 值 |
|---|---|
| `WOORT_PANIC_BAD_BYTE_CODE` | 0xD001 |
| `WOORT_PANIC_STACK_OVERFLOW` | 0xD002 |
| `WOORT_PANIC_CODE_NOT_FOUND` | 0xD003 |
| `WOORT_PANIC_BAD_CALLSTACK` | 0xD004 |
| `WOORT_PANIC_BAD_TYPE` | 0xD005 |
| `WOORT_PANIC_BAD_VM_REQUEST` | 0xD006 |
| `WOORT_PANIC_ABORTED` | 0xD007 |
| `WOORT_PANIC_INDEX_OUT_OF_RANGE` | 0xD008 |
| `WOORT_PANIC_USER` | 0xD009 |
| `WOORT_PANIC_INTEGER_DIV_FAIL` | 0xD00A |
| `WOORT_PANIC_OUT_OF_MEMORY` | 0xD00B |
| `WOORT_PANIC_ALREADY_CLOSED` | 0xD00C |

### 1.7 `woort_SerializeFlag` — 序列化行为标志（按位 OR 组合）

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_SERIALIZE_FLAG_NONE` | 0 | 默认：紧凑输出，递归结构输出占位字面量 |
| `WOORT_SERIALIZE_FLAG_PRETTY` | 1（1<<0） | 带缩进换行的美化输出 |
| `WOORT_SERIALIZE_FLAG_STRICT` | 2（1<<1） | 不可反序列化类型报错而非输出占位符 |
| `WOORT_SERIALIZE_FLAG_USE_NULL` | 4（1<<2） | NIL 输出为 "null" 而非 "nil" |

### 1.8 `woort_DebuggerAttachResult` — 调试器附加结果

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_DEBUGGER_ATTACH_RESULT_FAILED` | 0 | OOM 或其他失败 |
| `WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED` | 1 | 已有调试器附加 |
| `WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS` | 2 | 附加成功 |

### 1.9 `woort_PanicHandler_Action` — panic 处理器返回的动作

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_PANIC_HANDLER_ACTION_ABORT` | 0 | 继续终止程序（abort） |
| `WOORT_PANIC_HANDLER_ACTION_CONTINUE` | 1 | 抑制 panic 并继续执行 |
| `WOORT_PANIC_HANDLER_ACTION_USE_DEFAULT_HANDLER` | 2 | 交给默认处理器（打印错误、trace 并 abort） |

### 1.10 `woort_REPLPrinter_FlushResult` — REPL 打印机刷新结果

| 项 | 值 | 说明 |
|---|---|---|
| `WOORT_REPL_PRINTER_FLUSH_OK` | 0 | 刷新成功 |
| `WOORT_REPL_PRINTER_FLUSH_NOTHING` | 1 | 缓冲为空，无内容可刷 |
| `WOORT_REPL_PRINTER_FLUSH_FAILED` | 2 | 刷新失败（如分配失败） |

---

## 2. 函数指针类型（typedef）

| 类型名 | 定义 | 用途 |
|---|---|---|
| `woort_api` | `woort_VmCallStatus` 的别名 | 原生/JIT 函数的返回状态类型 |
| `woort_ShutdownPostCallback` | `void (*)(void*)` | woort_shutdown 之后回调 |
| `woort_NativeFunction` | `woort_api (*)(void)` | 可被 Woolang 调用的原生（C）函数签名 |
| `woort_JitFunction` | `woort_api (*)(woort_VMRuntime*, const woort_Value*, const woort_Value*)` | JIT（C）函数签名（vm, bp, sp） |
| `woort_DylibEntryFunc` | `void (*)(woort_Dylib*)` | 动态库入口函数 |
| `woort_DylibLeaveFunc` | `void (*)(void)` | 动态库离开函数 |
| `woort_GCHandle_UserMarkFunction` | `void (*)(void*)` | GC 句柄 mark 回调（标记可达引用） |
| `woort_GCHandle_UserDestructFunction` | `void (*)(void*)` | GC 句柄析构回调 |
| `woort_PanicHandlerFunction` | `woort_PanicHandler_Action (*)(woort_VMRuntime*, const char* funcname, const char* location, int line, int reason, const char* message)` | 用户自定义 panic 处理器 |
| `woort_REPLPrinter_ResultCallback` | `void (*)(const char*, size_t, void*)` | REPL 打印机刷新回调（缓冲区、长度、用户参数） |

---

## 3. 常量

| 常量 | 值 | 说明 |
|---|---|---|
| `WOORT_VERSION` | `WOORT_VERSION_WRAP(1, 0, 6, 17)` | 版本号（major, minor, patch, tweak）；`WOORT_VERSION_WRAP` 定义于别处 |
| `WOORT_IGNORE` | `((woort_StackValue)-2)` | 忽略输出参数的哨兵值（dst / hold / out_key_boxed 等） |
| `WOORT_RETURN_SLOT` | `((woort_StackValue)-1)` | 函数返回值专用栈槽（供 woort_set_* 写返回值） |
| `WOORT_DEFAULT_ENTRY` | `"@entry"` | 默认入口函数名 |
| `WOORT_EXTERN_LIB_FUNC_END` | `{ NULL, NULL }` | `woort_ExternLibFunc` 数组结尾哨兵 |
| `WOORT_VFS_SCHEME` | `"woovf://"` | 虚拟文件系统 URI 前缀 |
| `WOORT_VFS_SCHEME_LEN` | `8` | 前缀长度 |
| `WOORT_UTF8MAXLEN` | `6` | 单个 UTF-8 码点最大字节数 |
| `WOORT_UTF16MAXLEN` | `2` | 单个 UTF-16 码点最大单元数 |

---

## 4. 宏

### 4.1 编译 / 链接控制宏（按编译器自适应）

| 宏 | 定义（随环境变化） | 说明 |
|---|---|---|
| `WOORT_NODISCARD` | `[[nodiscard]]` / `_Check_return_` / `__attribute__((warn_unused_result))` / 空 | 修饰返回值不可忽略 |
| `WOORT_DEPRECATED` | `[[deprecated]]` / `__declspec(deprecated)` / `__attribute__((deprecated))` / 空 | 弃用标记 |
| `WOORT_IMPORT` | Windows: `__declspec(dllimport)`；其他: `extern` | 导入声明 |
| `WOORT_EXPORT` | Windows: `__declspec(dllexport)`；其他: `extern` | 导出声明 |
| `WOORT_API` | 定义 `WOORT_STATIC_LIB` 时为空；定义 `WOORT_IMPL` 时为 `WOORT_EXPORT`；否则为 `WOORT_IMPORT`。文件末尾会重定义为 dllexport 形式供实现源文件使用 | API 可见性 |

### 4.2 别名宏（对象式，直接指向另一符号）

| 宏 | 指向 |
|---|---|
| `woort_vm_close` | `woort_VMRuntime_destroy` |
| `woort_vm_swap` | `woort_VMRuntime_swap` |
| `woort_vm_get_runtime_error` | `woort_VMRuntime_get_runtime_error_msg` |
| `woort_codeenv_drop` | `woort_CodeEnv_drop` |
| `woort_set_void` | `woort_set_nil` |
| `woort_set_union_void` | `woort_set_union_nil` |
| `woort_ret_union_void` | `woort_ret_union_nil` |
| `woort_ret_void()` | `woort_ret()` |

### 4.3 包装 / 转换类函数宏

| 宏 | 展开 |
|---|---|
| `woort_init(argc, argv)` | `woort_init(argc, argv); setlocale(LC_CTYPE, woort_env_locale_name());`（do-while 包裹） |
| `woort_panic(REASON, MSGFMT, ...)` | `woort_raise_panic((woort_PanicReason)(REASON), __FUNCTION__, __FILE__, __LINE__, MSGFMT, ##__VA_ARGS__)` |
| `woort_set_pointer(dst, src)` | `woort_set_int(dst, (woort_Int)(intptr_t)(src))` |
| `woort_set_box_pointer(dst, src)` | `woort_set_box_int(dst, (woort_Int)(intptr_t)(src))` |
| `woort_set_box_float(DST, SRC)` | `woort_set_box_real(DST, (woort_Real)SRC)` |
| `woort_set_union_pointer(dst, id, src)` | `woort_set_union_int(dst, id, (woort_Int)(intptr_t)(src))` |
| `woort_set_union_box_pointer(dst, id, src)` | `woort_set_union_box_int(dst, id, (woort_Int)(intptr_t)(src))` |
| `woort_pointer(src)` | `((void*)woort_int(src))` |
| `woort_unbox_float(SRC)` | `((float)woort_unbox_real(SRC))` |
| `woort_unbox_pointer(SRC)` | `((void*)woort_unbox_int(SRC))` |
| `woort_option_get(dst, src)` | `(0 == woort_union_get(dst, src))`（id==0 即 value 变体） |
| `woort_result_get(dst, src)` | `(0 == woort_union_get(dst, src))`（id==0 即 Ok 变体） |
| `woort_map_set_by_pointer(src, ptr, val_boxed)` | `woort_map_set_by_int((src), (woort_Int)(intptr_t)(ptr), (val_boxed))` |
| `woort_struct_get_float(src, index)` | `((float)woort_struct_get_real((src), (index)))` |
| `woort_struct_set_float(src, index, val)` | `woort_struct_set_real((src), (index), (woort_Real)(val))` |
| `woort_struct_get_pointer(src, index)` | `((void*)woort_struct_get_int((src), (index)))` |
| `woort_struct_set_pointer(src, index, val)` | `woort_struct_set_int((src), (index), (woort_Int)(val))` |

### 4.4 Option / Result 写入宏

约定：Union `id=0` 为 value/Ok 变体，`id=1` 为 none/Err 变体。

**Option 写入**（`woort_set_option_*`，展开为对应的 `woort_set_union_*(dst, 0, ...)`；none 为 `id=1`）：

`woort_set_option_none`、`woort_set_option_value`、`woort_set_option_nil`、`woort_set_option_void`、`woort_set_option_int`、`woort_set_option_pointer`、`woort_set_option_real`、`woort_set_option_float`、`woort_set_option_bool`、`woort_set_option_string`、`woort_set_option_string_fmt`、`woort_set_option_buffer`、`woort_set_option_box_int`、`woort_set_option_box_pointer`、`woort_set_option_box_real`、`woort_set_option_box_bool`、`woort_set_option_gchandle`、`woort_set_option_gcstruct`

**Result::Ok 写入**（`woort_set_result_ok_*`，全部是同名 `woort_set_option_*` 的别名宏）：

`woort_set_result_ok_value`、`_nil`、`_void`、`_int`、`_pointer`、`_real`、`_float`、`_bool`、`_string`、`_string_fmt`、`_buffer`、`_box_int`、`_box_pointer`、`_box_real`、`_box_bool`、`_gchandle`、`_gcstruct`

**Result::Err 写入**（`woort_set_result_err_*`，展开为对应的 `woort_set_union_*(dst, 1, ...)`）：

`woort_set_result_err_value`、`_nil`、`_void`、`_int`、`_pointer`、`_real`、`_float`、`_bool`、`_string`、`_string_fmt`、`_buffer`、`_box_int`、`_box_pointer`、`_box_real`、`_box_bool`、`_gchandle`、`_gcstruct`

### 4.5 返回值宏（原生函数内使用）

统一模式：`(woort_set_X(WOORT_RETURN_SLOT, ...), woort_ret())`，其中 `woort_ret()` 即 `WOORT_VM_CALL_STATUS_NORMAL`。

- **普通返回**：`woort_ret()`、`woort_ret_value(src)`、`woort_ret_nil()`、`woort_ret_int(src)`、`woort_ret_pointer(src)`、`woort_ret_real(src)`、`woort_ret_float(src)`、`woort_ret_bool(src)`、`woort_ret_string(src)`、`woort_ret_string_fmt(fmt, ...)`、`woort_ret_buffer(src, len)`、`woort_ret_box_int(src)`、`woort_ret_box_pointer(src)`、`woort_ret_box_real(src)`、`woort_ret_box_bool(src)`、`woort_ret_gchandle(addr, hold, close, dylib, ...)`、`woort_ret_gcstruct(addr, mark, close, dylib, ...)`
- **Union 返回**：`woort_ret_union_without_value(id)`、`woort_ret_union_value(id, src)`、`woort_ret_union_nil(id)`、`woort_ret_union_void(id)`、`woort_ret_union_int(id, src)`、`woort_ret_union_pointer(id, src)`、`woort_ret_union_real(id, src)`、`woort_ret_union_float(id, src)`、`woort_ret_union_bool(id, src)`、`woort_ret_union_string(id, src)`、`woort_ret_union_string_fmt(id, fmt, ...)`、`woort_ret_union_buffer(id, src, len)`、`woort_ret_union_box_int(id, src)`、`woort_ret_union_box_pointer(id, src)`、`woort_ret_union_box_real(id, src)`、`woort_ret_union_box_bool(id, src)`、`woort_ret_union_gchandle(id, addr, hold, close, dylib, ...)`、`woort_ret_union_gcstruct(id, addr, mark, close, dylib, ...)`
- **Option 返回**：`woort_ret_option_none()`、`woort_ret_option_value(src)`、`woort_ret_option_nil()`、`woort_ret_option_void()`、`woort_ret_option_int(src)`、`woort_ret_option_pointer(src)`、`woort_ret_option_real(src)`、`woort_ret_option_float(src)`、`woort_ret_option_bool(src)`、`woort_ret_option_string(src)`、`woort_ret_option_string_fmt(fmt, ...)`、`woort_ret_option_buffer(src, len)`、`woort_ret_option_box_int(src)`、`woort_ret_option_box_pointer(src)`、`woort_ret_option_box_real(src)`、`woort_ret_option_box_bool(src)`、`woort_ret_option_gchandle(...)`、`woort_ret_option_gcstruct(...)`
- **Result::Ok 返回**：`woort_ret_result_ok_value`、`_nil`、`_void`、`_int`、`_pointer`、`_real`、`_float`、`_bool`、`_string`、`_string_fmt`、`_buffer`、`_box_int`、`_box_pointer`、`_box_real`、`_box_bool`、`_gchandle`、`_gcstruct`
- **Result::Err 返回**：`woort_ret_result_err_value`、`_nil`、`_void`、`_int`、`_pointer`、`_real`、`_float`、`_bool`、`_string`、`_string_fmt`、`_buffer`、`_box_int`、`_box_pointer`、`_box_real`、`_box_bool`、`_gchandle`、`_gcstruct`

### 4.6 ANSI 转义码宏

前缀 `WOORT_ANSI_ESC` = `"\033["`，结尾 `WOORT_ANSI_END` = `"m"`。

| 类别 | 宏（值） |
|---|---|
| 属性控制 | `WOORT_ANSI_RST`(`0m`)、`WOORT_ANSI_HIL`(`1m` 加粗)、`WOORT_ANSI_FAINT`(`2m`)、`WOORT_ANSI_ITALIC`(`3m`)、`WOORT_ANSI_UNDERLNE`(`4m`)、`WOORT_ANSI_NUNDERLNE`(`24m`)、`WOORT_ANSI_SLOW_BLINK`(`5m`)、`WOORT_ANSI_FAST_BLINK`(`6m`)、`WOORT_ANSI_INV`(`7m` 反显)、`WOORT_ANSI_FADE`(`8m` 隐藏) |
| 前景色 | `WOORT_ANSI_BLK`(30)、`WOORT_ANSI_GRY`(1;30)、`WOORT_ANSI_RED`(31)、`WOORT_ANSI_HIR`(1;31)、`WOORT_ANSI_GRE`(32)、`WOORT_ANSI_HIG`(1;32)、`WOORT_ANSI_YEL`(33)、`WOORT_ANSI_HIY`(1;33)、`WOORT_ANSI_BLU`(34)、`WOORT_ANSI_HIB`(1;34)、`WOORT_ANSI_MAG`(35)、`WOORT_ANSI_HIM`(1;35)、`WOORT_ANSI_CLY`(36)、`WOORT_ANSI_HIC`(1;36)、`WOORT_ANSI_WHI`(37)、`WOORT_ANSI_HIW`(1;37) |
| 背景色 | `WOORT_ANSI_BBLK`(40)、`WOORT_ANSI_BGRY`(1;40)、`WOORT_ANSI_BRED`(41)、`WOORT_ANSI_BHIR`(1;41)、`WOORT_ANSI_BGRE`(42)、`WOORT_ANSI_BHIG`(1;42)、`WOORT_ANSI_BYEL`(43)、`WOORT_ANSI_BHIY`(1;43)、`WOORT_ANSI_BBLU`(44)、`WOORT_ANSI_BHIB`(1;44)、`WOORT_ANSI_BMAG`(45)、`WOORT_ANSI_BHIM`(1;45)、`WOORT_ANSI_BCLY`(46)、`WOORT_ANSI_BHIC`(1;46)、`WOORT_ANSI_BWHI`(47)、`WOORT_ANSI_BHIW`(1;47) |

**向后兼容别名**：以上每个 `WOORT_ANSI_*` 都有去掉前缀的旧名别名 `ANSI_*`（如 `ANSI_ESC`、`ANSI_RST`、`ANSI_BLK`、`ANSI_BHIW` 等共 45 个）。

---

## 5. 函数

> 每个函数给出：函数名、函数指针类型（即可用于声明同签名函数指针的类型写法）、简述。

### 5.1 版本与运行时生命周期

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_version` | `const char* (*)(void)` | 版本字符串 "major.minor.patch.tweak" |
| `woort_version_int` | `uint64_t (*)(void)` | 打包为 64 位整数的版本号 |
| `woort_init` | `void (*)(int, char**)` | 初始化运行时（头文件中有同名宏会追加 setlocale） |
| `woort_shutdown` | `void (*)(woort_ShutdownPostCallback, void*)` | 关闭运行时，可带 shutdown 后回调 |
| `woort_print_runtime_help` | `void (*)(void)` | 打印 --woort-* 命令行选项帮助 |

### 5.2 控制台 I/O

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_console_readline` | `char* (*)(void)` | 从控制台读一行 UTF-8（需 woort_free 释放） |
| `woort_free` | `void (*)(void*)` | 释放 woort 分配的缓冲 |
| `woort_stdin_isatty` | `bool (*)(void)` | stdin 是否交互式终端 |
| `woort_console_getc` | `int (*)(void)` | 读一个原始 UTF-8 字节（Ctrl+C 返回 0x03） |
| `woort_console_ungetc` | `int (*)(int)` | 回退一个字节（1 深度） |
| `woort_env_locale_name` | `const char* (*)(void)` | 平台默认 UTF-8 locale 名（静态字符串） |

### 5.3 VM 运行时

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_VMRuntime_create` | `bool (*)(woort_VMRuntime**)` | 创建 VM 实例 |
| `woort_VMRuntime_destroy` | `void (*)(woort_VMRuntime*)` | 销毁 VM 实例 |
| `woort_VMRuntime_weaken` | `void (*)(woort_VMRuntime*)` | 将 VM 设为弱引用（GC 不再视其为根） |
| `woort_VMRuntime_swap` | `woort_VMRuntime* (*)(woort_VMRuntime*)` | 切换线程局部 VM，返回旧 VM |
| `woort_VMRuntime_current` | `woort_VMRuntime* (*)(void)` | 获取当前线程局部 VM |
| `woort_VMRuntime_get_runtime_error_msg` | `const char* (*)(woort_VMRuntime*)` | VM 中止后的错误消息 |
| `woort_vm_create` | `woort_VMRuntime* (*)(void)` | 便捷创建（失败返回 NULL） |

### 5.4 栈追踪 / 回溯

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_VMRuntime_trace_begin` | `void (*)(woort_VMRuntime*, woort_VMRuntime_TraceCallstack_Iter*)` | 开始遍历调用栈 |
| `woort_VMRuntime_trace_next` | `bool (*)(woort_VMRuntime_TraceCallstack_Iter*, woort_VMRuntime_TraceCallstack*)` | 迭代下一帧 |
| `woort_VMRuntime_log_trace` | `void (*)(woort_VMRuntime_TraceCallstack*)` | 记录一条 trace 日志 |
| `woort_VMRuntime_print_backtrace` | `void (*)(woort_VMRuntime*, size_t)` | 打印完整回溯（0 = 不限深度） |

### 5.5 GC（手动标记 / 屏障 / 分配）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_GC_mark_weak_vm_manually` | `void (*)(woort_VMRuntime*)` | 标记弱 VM 的栈与 env |
| `woort_GC_mark_droped_env_manually` | `void (*)(const woort_CodeEnv*)` | 标记已丢弃的 CodeEnv |
| `woort_GC_mark_internal_value_manually` | `void (*)(const woort_Value*)` | 标记一个值为可达 |
| `woort_GC_set_internal_value_with_mixed_write_barrier` | `void (*)(woort_Value*, const woort_Value*)` | 带混合写屏障的值写入 |
| `woort_GC_internal_value_delete_barrier` | `void (*)(const woort_Value*)` | 覆盖 / 移除值时的删除屏障 |
| `woort_GC_allocate` | `void* (*)(size_t, int)` | 分配 GC 内存 |
| `woort_GC_allocate_as_root` | `void* (*)(size_t, int)` | 分配并注册为 GC 根 |
| `woort_GC_unregister_root` | `void (*)(void*)` | 注销 GC 根 |
| `woort_GC_mark_addr_manually` | `void (*)(void*)` | 手动标记裸指针可达 |
| `woort_GC_set_addr_with_mixed_write_barrier` | `void (*)(void**, void*)` | 裸指针写入（混合写屏障） |
| `woort_GC_addr_delete_barrier` | `void (*)(const void*)` | 裸指针删除屏障 |
| `woort_GC_sync_marking_lock` | `bool (*)(void)` | 获取 GC 标记锁（无线程 VM 时） |
| `woort_GC_sync_marking_unlock` | `void (*)(void)` | 释放上述锁 |

### 5.6 GCPin（值钉住，作为 GC 根）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_GCPin_create` | `woort_GCPin* (*)(size_t)` | 创建含 count 个槽的 pin |
| `woort_GCPin_destroy` | `void (*)(woort_GCPin*)` | 销毁 pin |
| `woort_GCPin_set` | `void (*)(woort_GCPin*, size_t, woort_StackValue)` | 写入 StackValue（需活动 VM） |
| `woort_GCPin_get` | `void (*)(woort_StackValue, woort_GCPin*, size_t)` | 读出到 StackValue |
| `woort_GCPin_set_dup_boxed` | `void (*)(woort_GCPin*, size_t, woort_StackValue)` | 深拷贝装箱值后写入 |
| `woort_GCPin_set_internal` | `void (*)(woort_GCPin*, size_t, const woort_Value*)` | 从裸 woort_Value 写入 |
| `woort_GCPin_get_internal` | `void (*)(woort_Value*, woort_GCPin*, size_t)` | 读出到裸 woort_Value（带屏障） |
| `woort_GCPin_get_internal_without_barrier` | `void (*)(woort_Value*, woort_GCPin*, size_t)` | 读出（无屏障，仅限栈局部目标） |
| `woort_GCPin_set_dup_boxed_internal` | `void (*)(woort_GCPin*, size_t, const woort_Value*)` | 从裸值深拷贝写入 |

### 5.7 CodeEnv — 序列化 / 查询 / 断点 / 锁

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_CodeEnv_save_binary` | `bool (*)(woort_CodeEnv*, void**, size_t*)` | 序列化为二进制缓冲 |
| `woort_CodeEnv_restore_binary` | `woort_CodeEnv_RestoreResult (*)(woort_VFile*, woort_CodeEnv**)` | 从流反序列化 |
| `woort_CodeEnv_restore_failed_desc` | `const char* (*)(woort_CodeEnv_RestoreResult)` | 结果码的可读描述 |
| `woort_CodeEnv_drop` | `void (*)(woort_CodeEnv*)` | 释放 CodeEnv |
| `woort_CodeEnv_query_function` | `bool (*)(woort_CodeEnv*, woort_IRFunction*, const woort_Bytecode**)` | 查询函数字节码地址 |
| `woort_CodeEnv_lock` | `void (*)(woort_CodeEnv*)` | 加互斥锁 |
| `woort_CodeEnv_unlock` | `void (*)(woort_CodeEnv*)` | 解锁 |
| `woort_CodeEnv_find_srcloc_by_offset` | `bool (*)(const woort_CodeEnv*, uint32_t, woort_SourceLocation*)` | 按字节码偏移查源码位置 |
| `woort_CodeEnv_find_offset_by_srcloc` | `bool (*)(const woort_CodeEnv*, const char*, uint32_t, uint32_t*)` | 按源码位置查字节码偏移 |
| `woort_CodeEnv_find_function_name_by_offset` | `const char* (*)(const woort_CodeEnv*, uint32_t)` | 按偏移查函数名 |
| `woort_CodeEnv_set_trap` | `bool (*)(woort_CodeEnv*, woort_Bytecode*)` | 设置断点（DEBUGTRAP） |
| `woort_CodeEnv_clear_trap` | `bool (*)(woort_CodeEnv*, woort_Bytecode*)` | 清除断点 |
| `woort_CodeEnv_raw_trap` | `woort_Bytecode (*)(woort_CodeEnv*, const woort_Bytecode*)` | 读取断点处的原始指令 |
| `woort_CodeEnv_dumps` | `void (*)(const woort_CodeEnv*)` | 反汇编打印到 stdout |
| `woort_CodeEnv_register_extern_constant` | `bool (*)(woort_CodeEnv*, const char*, woort_IRConstantIndex)` | 注册 extern 常量名 → 常量索引 |
| `woort_CodeEnv_find_extern_constant` | `bool (*)(const woort_CodeEnv*, const char*, woort_IRConstantIndex*)` | 按名查 extern 常量索引 |
| `woort_CodeEnv_add_extern_lib` | `bool (*)(woort_CodeEnv*, woort_Dylib*)` | 关联动态库（引用计数 +1） |
| `woort_CodeEnv_jit` | `void (*)(woort_CodeEnv*)` | JIT 编译环境内全部函数 |

### 5.8 CodeEnv — 常量池写入（须在 lock/unlock 之间调用）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_CodeEnv_set_const_int` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, woort_Int)` | 设为整数 |
| `woort_CodeEnv_set_const_real` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, woort_Real)` | 设为实数 |
| `woort_CodeEnv_set_const_buffer` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, const void*, size_t)` | 设为缓冲（复制为 GCString） |
| `woort_CodeEnv_set_const_script_function` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, const woort_Bytecode*)` | 设为脚本函数入口 |
| `woort_CodeEnv_set_const_extern_function` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, woort_NativeFunction)` | 设为原生函数 |
| `woort_CodeEnv_set_const_script_closure` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, const woort_Bytecode*)` | 设为脚本闭包（GCClosure） |
| `woort_CodeEnv_set_const_extern_closure` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, woort_NativeFunction)` | 设为原生闭包 |
| `woort_CodeEnv_set_const_box_int` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, woort_Int)` | 设为装箱整数（DynBox） |
| `woort_CodeEnv_set_const_box_real` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, woort_Real)` | 设为装箱实数 |
| `woort_CodeEnv_set_const_box_bool` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, bool)` | 设为装箱布尔 |
| `woort_CodeEnv_set_const_struct` | `void (*)(woort_CodeEnv*, woort_IRConstantIndex, const woort_IRConstantIndex*, size_t)` | 由成员常量组成 struct |

### 5.9 CodeEnv — 静态存储槽

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_CodeEnv_set_static_value` | `void (*)(woort_CodeEnv*, woort_IRStaticIndex, const woort_Value*)` | 写静态槽（需持锁） |
| `woort_CodeEnv_get_static_value` | `void (*)(woort_CodeEnv*, woort_IRStaticIndex, woort_Value*)` | 读静态槽（需持锁） |
| `woort_CodeEnv_get_static_storage_count` | `size_t (*)(const woort_CodeEnv*)` | 静态槽数量 |

### 5.10 IR 编译器（woort_IRCompiler）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_IRCompiler_create` | `woort_IRCompiler* (*)(void)` | 创建编译器实例 |
| `woort_IRCompiler_close` | `void (*)(woort_IRCompiler*)` | 关闭并销毁编译器 |
| `woort_IRCompiler_intern_string` | `const char* (*)(woort_IRCompiler*, const char*)` | 驻留字符串（稳定指针） |
| `woort_IRCompiler_add_function` | `bool (*)(woort_IRCompiler*, uint32_t, uint32_t, woort_IRFunction**)` | 添加函数（参数数 / 捕获数） |
| `woort_IRCompiler_add_constant` | `woort_IRConstantIndex (*)(woort_IRCompiler*)` | 分配常量池槽 |
| `woort_IRCompiler_add_static` | `woort_IRStaticIndex (*)(woort_IRCompiler*)` | 分配静态数据槽 |
| `woort_IRCompiler_finish` | `bool (*)(woort_IRCompiler*, woort_CodeEnv**)` | 完成编译产出 CodeEnv（编译器被消耗） |
| `woort_IRCompiler_record_static_var` | `void (*)(woort_IRCompiler*, const char*, woort_IRStaticIndex)` | 记录静态变量调试信息 |

### 5.11 IR 函数（woort_IRFunction）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_IRFunction_new_vreg` | `woort_IRValue* (*)(woort_IRFunction*)` | 新建虚拟寄存器 |
| `woort_IRFunction_get_argument` | `woort_IRValue* (*)(woort_IRFunction*, uint32_t)` | 取参数寄存器 |
| `woort_IRFunction_get_captured` | `woort_IRValue* (*)(woort_IRFunction*, uint32_t)` | 取捕获变量寄存器 |
| `woort_IRFunction_new_label` | `woort_IRLabel* (*)(woort_IRFunction*)` | 新建标签 |
| `woort_IRFunction_fetch_const` | `const woort_IRValue* (*)(woort_IRFunction*, woort_IRConstantIndex)` | 取常量池 G[idx] 的 IR 值（自然去重） |
| `woort_IRFunction_push_srcloc` | `bool (*)(woort_IRFunction*, const char*, uint32_t, uint32_t, uint32_t, uint32_t)` | 压入源码位置栈 |
| `woort_IRFunction_pop_srcloc` | `void (*)(woort_IRFunction*)` | 弹出源码位置 |
| `woort_IRFunction_set_name` | `void (*)(woort_IRFunction*, const char*)` | 设置函数名 |
| `woort_IRFunction_record_local_var` | `void (*)(woort_IRFunction*, const char*, woort_IRValue*)` | 记录局部变量调试信息 |

### 5.12 IR 指令发射 — 数据传送 / 栈操作 / 类型转换

| 函数名 | 函数指针类型 | 指令语义 |
|---|---|---|
| `woort_IR_NOP` | `bool (*)(woort_IRFunction*)` | 空操作 |
| `woort_IR_MOV` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | dst = src |
| `woort_IR_LOAD` | `bool (*)(woort_IRFunction*, woort_IRValue*, woort_IRStaticIndex)` | dst = Static[idx] |
| `woort_IR_STORE` | `bool (*)(woort_IRFunction*, woort_IRStaticIndex, const woort_IRValue*)` | Static[idx] = src |
| `woort_IR_LOADPVALUE` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | dst = *ptr |
| `woort_IR_STOREPVALUE` | `bool (*)(woort_IRFunction*, const woort_IRValue*, const woort_IRValue*)` | *ptr = src（带写屏障） |
| `woort_IR_MKPVALUE` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | dst = new box(src) |
| `woort_IR_PUSHCHK` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | 带溢出检查的压栈 |
| `woort_IR_PUSHSTATICCHK` | `bool (*)(woort_IRFunction*, woort_IRStaticIndex)` | 静态值压栈（LOAD+PUSHCHK） |
| `woort_IR_POP` | `bool (*)(woort_IRFunction*, woort_IRValue*)` | 弹栈到 dst |
| `woort_IR_POPR` | `bool (*)(woort_IRFunction*, uint32_t)` | 弹 count 个（丢弃） |
| `woort_IR_POPRS` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | 按寄存器值弹栈 |
| `woort_IR_ITOR` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | int → real |
| `woort_IR_ITOS` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | int → string |
| `woort_IR_RTOI` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | real → int |
| `woort_IR_RTOS` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)` | real → string |

### 5.13 IR 指令发射 — 调用 / 闭包 / 容器构造

| 函数名 | 函数指针类型 | 指令语义 |
|---|---|---|
| `woort_IR_CALLNWO` | `bool (*)(woort_IRFunction*, woort_IRConstantIndex, uint32_t, woort_IRValue*)` | 调用 Woolang 函数（原生，无溢出检查） |
| `woort_IR_CALLNFP` | `bool (*)(woort_IRFunction*, woort_IRConstantIndex, uint32_t, woort_IRValue*)` | 调用（带帧指针设置） |
| `woort_IR_CALLNJIT` | `bool (*)(woort_IRFunction*, woort_IRConstantIndex, uint32_t, woort_IRValue*)` | 调用 JIT 函数 |
| `woort_IR_CALL` | `bool (*)(woort_IRFunction*, const woort_IRValue*, uint32_t, woort_IRValue*)` | 经函数值间接调用（闭包） |
| `woort_IR_MKCLOSURE` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint32_t, woort_IRConstantIndex)` | 创建闭包 |
| `woort_IR_MKVEC` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint32_t)` | 创建 vec |
| `woort_IR_MKMAP` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint32_t)` | 创建 map |
| `woort_IR_MKSTRUCT` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint32_t)` | 创建 struct |
| `woort_IR_MKUNION` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*, uint32_t)` | 创建 tagged union |

### 5.14 IR 指令发射 — 动态类型（装箱 / 拆箱 / 字符串转换）

| 函数名 | 函数指针类型 | 指令语义 |
|---|---|---|
| `woort_IR_BOXDYN` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | 装箱：dst = Box(typ, src) |
| `woort_IR_UNBOXDYN` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | 拆箱（类型不符 panic） |
| `woort_IR_CHECKDYN` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | 动态类型检查 |
| `woort_IR_PUSHBOXDYN` | `bool (*)(woort_IRFunction*, uint8_t, const woort_IRValue*)` | 装箱并压栈 |
| `woort_IR_CASTSTO` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | string → T8 类型 |
| `woort_IR_CASTSFROM` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | T8 类型 → string |
| `woort_IR_CASTDYN` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | 装箱值 → T8 类型 |
| `woort_IR_ASSERTDYN` | `bool (*)(woort_IRFunction*, uint8_t, const woort_IRValue*)` | 断言装箱值类型 |

### 5.15 IR 指令发射 — 整数 / 实数 / 字符串运算与比较

二元算术 / 比较（`dst = a OP b`）均为 `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*, const woort_IRValue*)`：

- 整数算术：`woort_IR_ADDI`、`woort_IR_SUBI`、`woort_IR_MULI`、`woort_IR_DIVI`、`woort_IR_MODI`
- 实数算术：`woort_IR_ADDR`、`woort_IR_SUBR`、`woort_IR_MULR`、`woort_IR_DIVR`、`woort_IR_MODR`
- 字符串：`woort_IR_ADDS`（连接）、比较 `woort_IR_LTS`、`woort_IR_GTS`、`woort_IR_LES`、`woort_IR_GES`、`woort_IR_EQS`、`woort_IR_NES`
- 整数比较：`woort_IR_LTI`、`woort_IR_GTI`、`woort_IR_LEI`、`woort_IR_GEI`、`woort_IR_EQI`、`woort_IR_NEI`
- 实数比较：`woort_IR_LTR`、`woort_IR_GTR`、`woort_IR_LER`、`woort_IR_GER`、`woort_IR_EQR`、`woort_IR_NER`
- 逻辑：`woort_IR_LAND`、`woort_IR_LOR`

一元运算（`dst = OP src`）均为 `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*)`：

- `woort_IR_NEGI`（整数取负）、`woort_IR_NEGR`（实数取负）、`woort_IR_LNOT`（逻辑非）

除法安全检查（panic 型 guard）：

| 函数名 | 函数指针类型 | 语义 |
|---|---|---|
| `woort_IR_CHKDIVIL` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | a == INT64_MIN 时 panic |
| `woort_IR_CHKDIVIR` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | a == 0 或 -1 时 panic |
| `woort_IR_CHKDIVIRZ` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | a == 0 时 panic |
| `woort_IR_CHKDIVILR` | `bool (*)(woort_IRFunction*, const woort_IRValue*, const woort_IRValue*)` | 完整除零 / 溢出检查 |

### 5.16 IR 指令发射 — 索引读

`dst = container[idx]` 型（idx 为寄存器），签名 `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*, const woort_IRValue*)`：

`woort_IR_LDIDVEC`（越界检查）、`woort_IR_LDIDVECX`（不检查）、`woort_IR_LDIDSTRING`、`woort_IR_LDIDDICTI`、`woort_IR_LDIDDICTR`、`woort_IR_LDIDDICTB`、`woort_IR_LDIDDICTX`、`woort_IR_LDIDDICTIX`、`woort_IR_LDIDDICTRX`、`woort_IR_LDIDDICTBX`、`woort_IR_LDIDDICTXX`

| 函数名 | 函数指针类型 | 语义 |
|---|---|---|
| `woort_IR_LDIDSTRUCT` | `bool (*)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*, uint32_t)` | 读 struct 字段（常量下标） |

### 5.17 IR 指令发射 — 索引写

`container[idx] = val` 型（c、idx、val 均为寄存器），签名 `bool (*)(woort_IRFunction*, const woort_IRValue*, const woort_IRValue*, const woort_IRValue*)`：

- Vector：`woort_IR_STIDVECI`、`woort_IR_STIDVECR`、`woort_IR_STIDVECB`、`woort_IR_STIDVECX`
- Dict（键×值类型 I=int / R=real / B=bool / X=boxed，共 16 个）：`woort_IR_STIDDICTII`、`_IR`、`_IB`、`_IX`、`_RI`、`_RR`、`_RB`、`_RX`、`_BI`、`_BR`、`_BB`、`_BX`、`_XI`、`_XR`、`_XB`、`_XX`
- Map（同上 16 个）：`woort_IR_STIDMAPII`、`_IR`、`_IB`、`_IX`、`_RI`、`_RR`、`_RB`、`_RX`、`_BI`、`_BR`、`_BB`、`_BX`、`_XI`、`_XR`、`_XB`、`_XX`

| 函数名 | 函数指针类型 | 语义 |
|---|---|---|
| `woort_IR_STIDSTRUCT` | `bool (*)(woort_IRFunction*, const woort_IRValue*, uint32_t, const woort_IRValue*)` | 写 struct 字段（常量下标） |

### 5.18 IR 指令发射 — 解包 / struct 字段压栈 / 原子 / 变参

| 函数名 | 函数指针类型 | 语义 |
|---|---|---|
| `woort_IR_UNPACKVEC` | `bool (*)(woort_IRFunction*, uint8_t, const woort_IRValue*)` | 解包 vec 并拆箱（mode=0） |
| `woort_IR_UNPACKVECX` | `bool (*)(woort_IRFunction*, uint8_t, const woort_IRValue*)` | 解包不拆箱（mode=1） |
| `woort_IR_UNPACKVECALL` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | 全解包+拆箱，计数写 dst（mode=2） |
| `woort_IR_UNPACKVECXALL` | `bool (*)(woort_IRFunction*, woort_IRValue*, uint8_t, const woort_IRValue*)` | 全解包不拆箱，计数写 dst（mode=3） |
| `woort_IR_PUSHIDSTRUCT` | `bool (*)(woort_IRFunction*, const woort_IRValue*, uint32_t)` | struct 字段压栈 |
| `woort_IR_PUSHIDSTBOXI` | `bool (*)(woort_IRFunction*, const woort_IRValue*, uint32_t)` | 装箱 int 字段压栈 |
| `woort_IR_PUSHIDSTBOXR` | `bool (*)(woort_IRFunction*, const woort_IRValue*, uint32_t)` | 装箱 real 字段压栈 |
| `woort_IR_PUSHIDSTBOXB` | `bool (*)(woort_IRFunction*, const woort_IRValue*, uint32_t)` | 装箱 bool 字段压栈 |
| `woort_IR_ASTORE` | `bool (*)(woort_IRFunction*, woort_IRStaticIndex, const woort_IRValue*)` | 原子存（release） |
| `woort_IR_ALOAD` | `bool (*)(woort_IRFunction*, woort_IRValue*, woort_IRStaticIndex)` | 原子读（acquire） |
| `woort_IR_CAS` | `bool (*)(woort_IRFunction*, woort_IRStaticIndex, woort_IRValue*, const woort_IRValue*)` | 比较并交换 |
| `woort_IR_PACKARG` | `bool (*)(woort_IRFunction*, uint16_t, woort_IRValue*)` | 变参打包为数组 |

### 5.19 IR 指令发射 — 控制流 / 陷阱 / 返回

| 函数名 | 函数指针类型 | 语义 |
|---|---|---|
| `woort_IR_bind` | `bool (*)(woort_IRFunction*, woort_IRLabel*)` | 绑定标签到当前位置 |
| `woort_IR_jmp` | `bool (*)(woort_IRFunction*, woort_IRLabel*)` | 无条件跳转 |
| `woort_IR_jifinited` | `bool (*)(woort_IRFunction*, woort_IRStaticIndex, woort_IRLabel*)` | 线程安全 once 初始化守卫 |
| `woort_IR_jcc` | `bool (*)(woort_IRFunction*, const woort_IRValue*, woort_IRLabel*)` | cond != 0 跳转 |
| `woort_IR_jccz` | `bool (*)(woort_IRFunction*, const woort_IRValue*, woort_IRLabel*)` | cond == 0 跳转 |
| `woort_IR_jcc_lt` | `bool (*)(woort_IRFunction*, const woort_IRValue*, const woort_IRValue*, woort_IRLabel*)` | a < b 跳转 |
| `woort_IR_jcc_le` | 同上 | a <= b 跳转 |
| `woort_IR_jcc_eq` | 同上 | a == b 跳转 |
| `woort_IR_jcc_gt` | 同上 | a > b 跳转 |
| `woort_IR_jcc_ge` | 同上 | a >= b 跳转 |
| `woort_IR_jcc_ne` | 同上 | a != b 跳转 |
| `woort_IR_debugtrap` | `bool (*)(woort_IRFunction*)` | 发出调试陷阱（断点） |
| `woort_IR_panic` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | 发出带字符串消息的 panic |
| `woort_IR_ret` | `bool (*)(woort_IRFunction*, const woort_IRValue*)` | 返回值 |
| `woort_IR_ret_void` | `bool (*)(woort_IRFunction*)` | 返回 void |

### 5.20 运行时调用与栈操作

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_raise_panic` | `void (*)(woort_PanicReason, const char*, const char*, int, const char*, ...)` | 触发 panic（变参格式） |
| `woort_push_reserve` | `bool (*)(size_t, woort_StackValue*)` | 预留求值栈空间 |
| `woort_pop` | `void (*)(size_t)` | 丢弃栈顶 count 个值 |
| `woort_internal_value` | `woort_Value* (*)(woort_StackValue)` | 取栈槽裸指针（内部 API） |
| `woort_import_value` | `void (*)(woort_StackValue, woort_VMRuntime*, woort_StackValue)` | 跨 VM 拷贝值 |
| `woort_bootup` | `woort_VmCallStatus (*)(woort_StackValue, woort_CodeEnv*, bool)` | 加载默认入口并调用（可选 JIT） |
| `woort_invoke` | `woort_VmCallStatus (*)(woort_StackValue, woort_StackValue)` | 调用函数值并等待完成 |
| `woort_spawn` | `woort_VmCallStatus (*)(woort_StackValue, woort_StackValue)` | 从函数值派生协程 |
| `woort_resume` | `woort_VmCallStatus (*)(woort_StackValue)` | 恢复挂起的协程 |
| `woort_load_const` | `void (*)(woort_StackValue, const woort_CodeEnv*, woort_IRConstantIndex)` | 加载常量到栈槽 |
| `woort_load_extern_const` | `bool (*)(woort_StackValue, const woort_CodeEnv*, const char*)` | 按名加载 extern 常量 |

### 5.21 栈值写入（`woort_set_*`）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_set_value` | `void (*)(woort_StackValue, woort_StackValue)` | 栈槽拷贝 dst = src |
| `woort_set_dup_boxed` | `void (*)(woort_StackValue, woort_StackValue)` | 浅拷贝装箱值 |
| `woort_set_nil` | `void (*)(woort_StackValue)` | 置 nil |
| `woort_set_int` | `void (*)(woort_StackValue, woort_Int)` | 置整数 |
| `woort_set_real` | `void (*)(woort_StackValue, woort_Real)` | 置实数 |
| `woort_set_float` | `void (*)(woort_StackValue, float)` | 置单精度浮点 |
| `woort_set_bool` | `void (*)(woort_StackValue, bool)` | 置布尔 |
| `woort_set_string` | `void (*)(woort_StackValue, woort_U8CString)` | 置 UTF-8 字符串 |
| `woort_set_string_fmt` | `void (*)(woort_StackValue, woort_U8CString, ...)` | 置格式化字符串 |
| `woort_set_buffer` | `void (*)(woort_StackValue, const void*, size_t)` | 置字节缓冲（复制） |
| `woort_set_vec` | `void (*)(woort_StackValue)` | 置空 vec |
| `woort_set_map` | `void (*)(woort_StackValue)` | 置空 map |
| `woort_set_struct` | `void (*)(woort_StackValue, size_t)` | 置指定容量的空 struct |
| `woort_set_gchandle` | `void (*)(woort_StackValue, void*, woort_StackValue, woort_GCHandle_UserDestructFunction, woort_Dylib*)` | 置 GC 句柄 |
| `woort_set_gcstruct` | `void (*)(woort_StackValue, void*, woort_GCHandle_UserMarkFunction, woort_GCHandle_UserDestructFunction, woort_Dylib*)` | 置带 mark 回调的 GC struct |
| `woort_set_box_int` | `void (*)(woort_StackValue, woort_Int)` | 置装箱整数 |
| `woort_set_box_real` | `void (*)(woort_StackValue, woort_Real)` | 置装箱实数 |
| `woort_set_box_bool` | `void (*)(woort_StackValue, bool)` | 置装箱布尔 |

（另有指针 / float / void 等变体为宏，见 4.3。）

### 5.22 Union 写入（`woort_set_union_*`）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_set_union_without_value` | `void (*)(woort_StackValue, woort_Int)` | 无内联负载的变体 |
| `woort_set_union_value` | `void (*)(woort_StackValue, woort_Int, woort_StackValue)` | 携带栈值的变体 |
| `woort_set_union_nil` | `void (*)(woort_StackValue, woort_Int)` | nil 变体 |
| `woort_set_union_int` | `void (*)(woort_StackValue, woort_Int, woort_Int)` | int 变体 |
| `woort_set_union_real` | `void (*)(woort_StackValue, woort_Int, woort_Real)` | real 变体 |
| `woort_set_union_float` | `void (*)(woort_StackValue, woort_Int, float)` | float 变体 |
| `woort_set_union_bool` | `void (*)(woort_StackValue, woort_Int, bool)` | bool 变体 |
| `woort_set_union_string` | `void (*)(woort_StackValue, woort_Int, woort_U8CString)` | string 变体 |
| `woort_set_union_string_fmt` | `void (*)(woort_StackValue, woort_Int, woort_U8CString, ...)` | 格式化 string 变体 |
| `woort_set_union_buffer` | `void (*)(woort_StackValue, woort_Int, const void*, size_t)` | buffer 变体 |
| `woort_set_union_vec` | `void (*)(woort_StackValue, woort_Int, size_t)` | vec 变体（预置容量） |
| `woort_set_union_map` | `void (*)(woort_StackValue, woort_Int, size_t)` | map 变体（预保留） |
| `woort_set_union_struct` | `void (*)(woort_StackValue, woort_Int, size_t)` | struct 变体（预置容量） |
| `woort_set_union_gchandle` | `void (*)(woort_StackValue, woort_Int, void*, woort_StackValue, woort_GCHandle_UserDestructFunction, woort_Dylib*)` | GC 句柄变体 |
| `woort_set_union_gcstruct` | `void (*)(woort_StackValue, woort_Int, void*, woort_GCHandle_UserMarkFunction, woort_GCHandle_UserDestructFunction, woort_Dylib*)` | GC struct 变体 |
| `woort_set_union_box_int` | `void (*)(woort_StackValue, woort_Int, woort_Int)` | 装箱 int 变体 |
| `woort_set_union_box_real` | `void (*)(woort_StackValue, woort_Int, woort_Real)` | 装箱 real 变体 |
| `woort_set_union_box_bool` | `void (*)(woort_StackValue, woort_Int, bool)` | 装箱 bool 变体 |

（Option / Result 写入宏见 4.4。）

### 5.23 特殊返回（原生函数内）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_ret_panic` | `woort_api (*)(const char*, ...)` | 触发 panic 并阻塞等待用户处置 |
| `woort_ret_yield` | `woort_api (*)(void)` | 请求挂起 VM（之后可 woort_resume） |

### 5.24 栈值读取

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_int` | `woort_Int (*)(woort_StackValue)` | 读整数 |
| `woort_real` | `woort_Real (*)(woort_StackValue)` | 读实数 |
| `woort_float` | `float (*)(woort_StackValue)` | 读单精度浮点 |
| `woort_bool` | `bool (*)(woort_StackValue)` | 读布尔 |
| `woort_string` | `woort_U8CString (*)(woort_StackValue)` | 读字符串指针 |
| `woort_buffer` | `const void* (*)(woort_StackValue, size_t*)` | 读缓冲指针与长度 |
| `woort_gcpointer` | `void* (*)(woort_StackValue)` | 读裸 GC 指针 |
| `woort_unbox_int` | `woort_Int (*)(woort_StackValue)` | 拆箱读整数 |
| `woort_unbox_real` | `woort_Real (*)(woort_StackValue)` | 拆箱读实数 |
| `woort_unbox_bool` | `bool (*)(woort_StackValue)` | 拆箱读布尔 |
| `woort_unbox_type` | `woort_BoxValueType (*)(woort_StackValue)` | 查询装箱类型标签 |
| `woort_unbox` | `woort_BoxValueType (*)(woort_StackValue, woort_StackValue)` | 拆箱并将内部值写到 dst |
| `woort_union_get` | `woort_Int (*)(woort_StackValue, woort_StackValue)` | 取 union 判别 id 并拷贝负载到 dst |

### 5.25 Vector

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_vec_len` | `size_t (*)(woort_StackValue)` | 元素数 |
| `woort_vec_resize` | `void (*)(woort_StackValue, size_t)` | 调整大小 |
| `woort_vec_resize_with` | `void (*)(woort_StackValue, size_t, woort_StackValue)` | 调整大小并以 init_val 填充 |
| `woort_vec_shrink` | `bool (*)(woort_StackValue, size_t)` | 收缩（不可超过当前大小） |
| `woort_vec_get` | `bool (*)(woort_StackValue, woort_StackValue, size_t)` | 读元素（装箱）到 dst |
| `woort_vec_set` | `bool (*)(woort_StackValue, size_t, woort_StackValue)` | 写元素 |
| `woort_vec_push` | `void (*)(woort_StackValue, woort_StackValue)` | 尾部追加 |
| `woort_vec_pop` | `bool (*)(woort_StackValue)` | 弹出末尾元素 |
| `woort_vec_insert` | `bool (*)(woort_StackValue, size_t, woort_StackValue)` | 插入（后移） |
| `woort_vec_erase` | `bool (*)(woort_StackValue, size_t)` | 删除（前移） |
| `woort_vec_clear` | `void (*)(woort_StackValue)` | 清空 |
| `woort_vec_copy` | `void (*)(woort_StackValue, woort_StackValue)` | 拷贝全部元素 |
| `woort_vec_swap` | `void (*)(woort_StackValue, woort_StackValue)` | 交换内容 |

### 5.26 Map

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_map_len` | `size_t (*)(woort_StackValue)` | 键值对数量 |
| `woort_map_reserve` | `void (*)(woort_StackValue, size_t)` | 预留容量 |
| `woort_map_get` | `bool (*)(woort_StackValue, woort_StackValue, woort_StackValue)` | 按装箱键查找 |
| `woort_map_get_by_int` | `bool (*)(woort_StackValue, woort_StackValue, woort_Int)` | 按 int 键查找 |
| `woort_map_get_by_real` | `bool (*)(woort_StackValue, woort_StackValue, woort_Real)` | 按 real 键查找 |
| `woort_map_get_by_bool` | `bool (*)(woort_StackValue, woort_StackValue, bool)` | 按 bool 键查找 |
| `woort_map_get_by_string` | `bool (*)(woort_StackValue, woort_StackValue, woort_U8CString)` | 按字符串键查找 |
| `woort_map_set` | `bool (*)(woort_StackValue, woort_StackValue, woort_StackValue)` | 插入 / 更新（装箱键值） |
| `woort_map_set_by_int` | `bool (*)(woort_StackValue, woort_Int, woort_StackValue)` | 按 int 键插入 / 更新 |
| `woort_map_set_by_real` | `bool (*)(woort_StackValue, woort_Real, woort_StackValue)` | 按 real 键 |
| `woort_map_set_by_bool` | `bool (*)(woort_StackValue, bool, woort_StackValue)` | 按 bool 键 |
| `woort_map_set_by_string` | `bool (*)(woort_StackValue, woort_U8CString, woort_StackValue)` | 按字符串键 |
| `woort_map_erase` | `bool (*)(woort_StackValue, woort_StackValue)` | 按装箱键删除 |
| `woort_map_erase_by_int` | `bool (*)(woort_StackValue, woort_Int)` | 按 int 键删除 |
| `woort_map_erase_by_real` | `bool (*)(woort_StackValue, woort_Real)` | 按 real 键删除 |
| `woort_map_erase_by_bool` | `bool (*)(woort_StackValue, bool)` | 按 bool 键删除 |
| `woort_map_erase_by_string` | `bool (*)(woort_StackValue, woort_U8CString)` | 按字符串键删除 |
| `woort_map_clear` | `void (*)(woort_StackValue)` | 清空 |
| `woort_map_copy` | `void (*)(woort_StackValue, woort_StackValue)` | 拷贝全部条目 |
| `woort_map_swap` | `void (*)(woort_StackValue, woort_StackValue)` | 交换内容 |
| `woort_map_contains` | `bool (*)(woort_StackValue, woort_StackValue)` | 按装箱键判断存在 |
| `woort_map_contains_int` | `bool (*)(woort_StackValue, woort_Int)` | int 键存在 |
| `woort_map_contains_real` | `bool (*)(woort_StackValue, woort_Real)` | real 键存在 |
| `woort_map_contains_bool` | `bool (*)(woort_StackValue, bool)` | bool 键存在 |
| `woort_map_contains_string` | `bool (*)(woort_StackValue, woort_U8CString)` | 字符串键存在 |
| `woort_map_iter` | `bool (*)(woort_StackValue, size_t, woort_StackValue, woort_StackValue)` | 按迭代索引取键值（可传 WOORT_IGNORE） |

### 5.27 序列化 / 反序列化

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_serialize_dynbox` | `char* (*)(woort_StackValue, uint32_t)` | 装箱值 → Woolang 字面量字符串 |
| `woort_serialize_map` | `char* (*)(woort_StackValue, uint32_t)` | map → 字面量字符串 |
| `woort_serialize_vec` | `char* (*)(woort_StackValue, uint32_t)` | vec → 字面量字符串 |
| `woort_deserialize_dynbox` | `bool (*)(woort_StackValue, const char*)` | 字面量 → 装箱值 |
| `woort_deserialize_map` | `bool (*)(woort_StackValue, const char*)` | `{...}` 字面量 → map |
| `woort_deserialize_vec` | `bool (*)(woort_StackValue, const char*)` | `[...]` 字面量 → vec |

### 5.28 Struct

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_struct_len` | `size_t (*)(woort_StackValue)` | 字段数 |
| `woort_struct_get` | `void (*)(woort_StackValue, woort_StackValue, size_t)` | 读字段到栈槽 |
| `woort_struct_set` | `void (*)(woort_StackValue, size_t, woort_StackValue)` | 写字段 |
| `woort_struct_get_int` | `woort_Int (*)(woort_StackValue, size_t)` | 读 int 字段 |
| `woort_struct_get_real` | `woort_Real (*)(woort_StackValue, size_t)` | 读 real 字段 |
| `woort_struct_get_string` | `woort_U8CString (*)(woort_StackValue, size_t)` | 读 string 字段 |
| `woort_struct_get_bool` | `bool (*)(woort_StackValue, size_t)` | 读 bool 字段 |
| `woort_struct_set_int` | `void (*)(woort_StackValue, size_t, woort_Int)` | 写 int 字段 |
| `woort_struct_set_real` | `void (*)(woort_StackValue, size_t, woort_Real)` | 写 real 字段 |
| `woort_struct_set_string` | `void (*)(woort_StackValue, size_t, woort_U8CString)` | 写 string 字段 |
| `woort_struct_set_bool` | `void (*)(woort_StackValue, size_t, bool)` | 写 bool 字段 |

### 5.29 路径工具

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_exe_path` | `size_t (*)(char*, size_t)` | 可执行文件所在目录（snprintf 语义） |
| `woort_set_exe_path` | `bool (*)(const char*)` | 覆盖自动探测的可执行目录 |
| `woort_work_path` | `size_t (*)(char*, size_t)` | 当前工作目录 |
| `woort_set_work_path` | `bool (*)(const char*)` | 设置工作目录 |
| `woort_get_file_loc` | `size_t (*)(const char*, char*, size_t)` | 取路径的目录部分（可原地） |
| `woort_normalize_path` | `void (*)(char*)` | 原地规范化分隔符（Windows '\\'→'/'） |

### 5.30 虚拟文件系统（VFS）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_vfs_create` | `bool (*)(const char*, const void*, size_t, bool)` | 创建 / 覆盖虚拟文件 |
| `woort_vfs_remove` | `bool (*)(const char*)` | 删除虚拟文件（须 enable_modify） |
| `woort_vfs_is_virtual_uri` | `bool (*)(const char*)` | URI 是否 "woovf://" 前缀 |
| `woort_vfs_exists` | `bool (*)(const char*)` | 虚拟文件是否存在 |
| `woort_vfs_get_all_paths` | `size_t (*)(char***)` | 列出全部虚拟文件路径（malloc 数组） |
| `woort_fs_is_file_readable` | `bool (*)(const char*)` | 磁盘文件是否可读 |
| `woort_vfs_resolve_path` | `bool (*)(const char*, const char* const*, size_t, char**)` | 按搜索顺序解析路径（虚拟 + 真实） |

### 5.31 流式文件（woort_VFile）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_vfile_open` | `bool (*)(const char*, woort_VFile**)` | 打开文件（虚拟或磁盘） |
| `woort_vfile_open_reader` | `bool (*)(const void*, size_t, woort_VFile**)` | 将内存缓冲包装为只读 VFile |
| `woort_vfile_read` | `size_t (*)(woort_VFile*, void*, size_t)` | 读取至多 size 字节 |
| `woort_vfile_seek` | `bool (*)(woort_VFile*, int64_t, int)` | 定位（SEEK_SET/CUR/END） |
| `woort_vfile_tell` | `int64_t (*)(woort_VFile*)` | 当前读位置 |
| `woort_vfile_size` | `int64_t (*)(woort_VFile*)` | 文件总大小 |
| `woort_vfile_close` | `void (*)(woort_VFile*)` | 关闭并释放 |

### 5.32 动态库

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_dylib_fake` | `woort_Dylib* (*)(const char*, const woort_ExternLibFunc*, woort_Dylib*)` | 注册函数表支撑的"伪"库 |
| `woort_dylib_load` | `woort_Dylib* (*)(const char*, const char*, const char*, bool)` | 按搜索顺序加载原生库 |
| `woort_dylib_load_func` | `void* (*)(woort_Dylib*, const char*)` | 按名查函数（GetProcAddress/dlsym） |
| `woort_dylib_get_func_name` | `const char* (*)(woort_Dylib*, void*)` | 按地址反查函数名 |
| `woort_dylib_unload` | `void (*)(woort_Dylib*, woort_DylibUnloadMethod)` | 卸载（UNREF / BURY 标志） |
| `woort_dylib_keep` | `void (*)(woort_Dylib*)` | 引用计数 +1 |
| `woort_get_builtin_lib` | `woort_Dylib* (*)(void)` | 内置 "woolang" 伪库句柄 |

### 5.33 调试器 / Ctrl+C / panic 回调

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_WAIPO_Debugger_attach` | `woort_DebuggerAttachResult (*)(void)` | 向当前 VM 附加 WAIPO 调试器 |
| `woort_VMRuntime_Debugger_breakdown_all_vm` | `void (*)(void)` | 向所有根 VM 发调试回调请求 |
| `woort_ctrlc_setup` | `void (*)(void)` | 注册 Ctrl+C（SIGINT）处理（首按附加调试器；2 秒内 4 次则 abort） |
| `woort_ctrlc_teardown` | `void (*)(void)` | 恢复默认 SIGINT 处置 |
| `woort_set_panic_callback` | `woort_PanicHandlerFunction (*)(woort_PanicHandlerFunction)` | 安装自定义 panic 处理器，返回旧处理器 |

### 5.34 字符串 / Unicode 转换（缓冲区版）

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_str_get_char` | `bool (*)(const char*, size_t, char32_t*)` | 取字节偏移处码点 |
| `woort_strn_get_char` | `bool (*)(const char*, size_t, size_t, char32_t*)` | 同上（显式长度） |
| `woort_str_to_wstr` | `size_t (*)(const char*, wchar_t*, size_t)` | UTF-8 → wchar_t |
| `woort_strn_to_wstr` | `size_t (*)(const char*, size_t, wchar_t*, size_t)` | 同上（显式长度） |
| `woort_wstr_to_str` | `size_t (*)(const wchar_t*, char*, size_t)` | wchar_t → UTF-8 |
| `woort_wstrn_to_str` | `size_t (*)(const wchar_t*, size_t, char*, size_t)` | 同上（显式长度） |
| `woort_str_to_u16str` | `size_t (*)(const char*, char16_t*, size_t)` | UTF-8 → UTF-16 |
| `woort_strn_to_u16str` | `size_t (*)(const char*, size_t, char16_t*, size_t)` | 同上（显式长度） |
| `woort_u16str_to_str` | `size_t (*)(const char16_t*, char*, size_t)` | UTF-16 → UTF-8 |
| `woort_u16strn_to_str` | `size_t (*)(const char16_t*, size_t, char*, size_t)` | 同上（显式长度） |
| `woort_str_to_u32str` | `size_t (*)(const char*, char32_t*, size_t)` | UTF-8 → UTF-32 |
| `woort_strn_to_u32str` | `size_t (*)(const char*, size_t, char32_t*, size_t)` | 同上（显式长度） |
| `woort_u32str_to_str` | `size_t (*)(const char32_t*, char*, size_t)` | UTF-32 → UTF-8 |
| `woort_u32strn_to_str` | `size_t (*)(const char32_t*, size_t, char*, size_t)` | 同上（显式长度） |

### 5.35 原始 UTF-8 / UTF-16 / UTF-32 工具

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_u8charnlen` | `size_t (*)(const char*, size_t)` | UTF-8 字符的字节长度 |
| `woort_u8strnlen` | `size_t (*)(const char*, size_t)` | 字节区间内 UTF-8 字符数 |
| `woort_u8strnchar` | `bool (*)(const char*, size_t, size_t*)` | 检查首字符是否合法 |
| `woort_u8substr` | `const char* (*)(const char*, size_t, size_t, size_t*)` | 前进 from 个字符的子串 |
| `woort_u8substrr` | `const char* (*)(const char*, size_t, size_t, size_t, size_t*)` | 子串 [from, tail] |
| `woort_u8substrn` | `const char* (*)(const char*, size_t, size_t, size_t, size_t*)` | 从 from 起 length 个字符 |
| `woort_u8combineu32` | `size_t (*)(const char*, size_t, char32_t*)` | 解码 UTF-8 字符为码点 |
| `woort_u32exlactu8` | `void (*)(char32_t, char[WOORT_UTF8MAXLEN], size_t*)` | 码点编码为 UTF-8 |
| `woort_u8combineu16` | `size_t (*)(const char*, size_t, char16_t[WOORT_UTF16MAXLEN], size_t*)` | 解码 UTF-8 字符为 UTF-16 |
| `woort_u16exlactu8` | `size_t (*)(const char16_t*, size_t, char[WOORT_UTF8MAXLEN], size_t*)` | UTF-16 序列编码为 UTF-8 |
| `woort_u16hisurrogate` | `bool (*)(char16_t)` | 是否高位（前导）代理项 |
| `woort_u16losurrogate` | `bool (*)(char16_t)` | 是否低位（尾随）代理项 |
| `woort_u8enstring` | `char* (*)(const char*, size_t, int)` | 转义为带引号的 Woolang 字面量（malloc，需 woort_free） |
| `woort_u8destring` | `char* (*)(const char*, size_t*)` | 反转义字面量（malloc，需 woort_free） |
| `woort_u8strtou32` | `char32_t* (*)(const char*, size_t, size_t*)` | UTF-8 → UTF-32（malloc） |
| `woort_u32strtou8` | `char* (*)(const char32_t*, size_t, size_t*)` | UTF-32 → UTF-8（malloc） |
| `woort_u8strtou16` | `char16_t* (*)(const char*, size_t, size_t*)` | UTF-8 → UTF-16（malloc） |
| `woort_u16strtou8` | `char* (*)(const char16_t*, size_t, size_t*)` | UTF-16 → UTF-8（malloc） |
| `woort_u16strcount` | `size_t (*)(const char16_t*)` | NUL 结尾 UTF-16 串单元数 |
| `woort_u32strcount` | `size_t (*)(const char32_t*)` | NUL 结尾 UTF-32 串单元数 |
| `woort_u32isu16` | `bool (*)(char32_t)` | 码点是否落在 BMP（单 UTF-16 单元） |
| `woort_u8stridx` | `bool (*)(const char*, size_t, size_t, char32_t*)` | 按字符索引取码点 |

### 5.36 REPL 打印机

| 函数名 | 函数指针类型 | 说明 |
|---|---|---|
| `woort_REPLPrinter_create` | `bool (*)(woort_REPLPrinter_ResultCallback, void*, woort_REPLPrinter**)` | 创建打印机（回调为 NULL 时写 stdout） |
| `woort_REPLPrinter_destroy` | `void (*)(woort_REPLPrinter*)` | 销毁打印机 |
| `woort_REPLPrinter_flush` | `woort_REPLPrinter_FlushResult (*)(woort_REPLPrinter*)` | 刷新缓冲的 UTF-8 文本 |

---

## 附录：其他类型别名与不透明句柄

**基础类型别名**：

| 别名 | 实际类型 | 说明 |
|---|---|---|
| `woort_Value` | 8 字节 union（实现内部布局） | VM 值 |
| `woort_StackValue` | `int32_t` | 求值栈索引（负值相对帧基） |
| `woort_IRConstantIndex` | `uint32_t` | CodeEnv 常量池索引 |
| `woort_IRStaticIndex` | `uint32_t` | CodeEnv 静态数据区索引 |
| `woort_Int` | `int64_t` | Woolang 整数 |
| `woort_Handle` | `uint64_t` | Woolang 句柄 |
| `woort_Real` | `double` | Woolang 实数 |
| `woort_Bytecode` | `uint32_t` | 字节码指令字 |
| `woort_U8CString` / `woort_string_t` | `const char*` | UTF-8 C 字符串 |
| `woort_Char` | `char32_t` | Unicode 字符 |

**便捷别名**（Runtime API 一节）：`woort_vm` = `woort_VMRuntime`、`woort_value` = `woort_StackValue`、`woort_codeenv` = `woort_CodeEnv`、`woort_constidx` = `woort_IRConstantIndex`、`woort_callstatus` = `woort_VmCallStatus`。

**不透明句柄（前置声明 struct）**：`woort_VMRuntime`、`woort_CodeEnv`、`woort_Dylib`、`woort_IRCompiler`、`woort_IRFunction`、`woort_IRValue`、`woort_IRLabel`、`woort_VFile`、`woort_GCPin`、`woort_REPLPrinter`。

**公开的 POD 结构**：

- `woort_ExternLibFunc` — 伪库函数表项：`m_name`（const char*，NULL 表结尾）、`m_func_addr`（void*）
- `woort_SourceLocation` — 源码位置：`m_filepath`（可 NULL）、`m_begin_line/Column`、`m_end_line/Column`（uint32，1 起）
- `woort_VMRuntime_TraceCallstack_Iter` — 栈追踪迭代器：`m_vm`、`m_next_tracing_depth`、`m_next_tracing_offset_of_base`
- `woort_VMRuntime_TraceCallstack` — 栈帧信息：`m_callstack_depth`、`m_has_location`、`m_function_name`、`m_file_or_lib_name`、`m_location_begin[2]`、`m_location_end[2]`、`m_code_addr`、`m_callstack_offset_of_base`
