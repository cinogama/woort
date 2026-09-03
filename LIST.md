# woort.h brief 注解与实现不符清单

> 审查范围：`include/woort.h` 全部公开 API 的 doc 注解，逐条与 `src/` 实现比对。
> 结论：共 **30 处确定不符**、**18 处高度疑似不符**，另有若干文档缺口与 1 个顺带发现的实现 bug。
> 行号以当前版本（WOORT_VERSION 1.0.8.0）为准。

---

## 一、确定不符（certain）

### 字符串 / UTF-8

1. **`woort_str_get_char` / `woort_strn_get_char`**（`include/woort.h:5357`, `:5367`）
   - 注解：`index` 是 "**byte offset**"。
   - 实际：走 `woort_u8stridx` → `woort_u8substr`，按 **UTF-8 字符数**前进（`src/woort_utf8.c:60`）。
     `"éA"` 传 `index=2` 返回 false 而非 `'A'`。仅纯 ASCII 输入时两者巧合一致。

2. **`woort_u8enstring`**（`include/woort.h:5768`）
   - 注解：force_unicode 非零时 "**always**" 对非 ASCII 发 `\uXXXX`。
   - 实际：仅当字符 `iswprint()` 为假时才查 `force_unicode`（`src/woort_utf8.c:265,285`），
     可打印的非 ASCII（如中文）始终原样输出。

3. **`woort_u16exractu8`**（`include/woort.h:5753`）
   - 注解：返回值 "(1 or 2)"。
   - 实际：`charcount == 0` 时返回 **0**（`src/woort_utf8.c:209`）。

### VM / 栈值

4. **`woort_internal_value`**（`include/woort.h:2920`）
   - 注解："positive for **absolute**, negative for frame-relative"。
   - 实际：所有索引一律 frame-relative：`m_sb[3+N]`（`src/woort.c:465`），
     正数索引的是本帧参数，不存在绝对索引模式。

5. **`woort_bootup`**（`include/woort.h:2953`）
   - 注解："supersedes the deprecated `woort_bootup_codeenv()`"。
   - 实际：该函数在全仓库已不存在，失效引用。

6. **`woort_set_struct`**（`include/woort.h:3086`）
   - 注解："Set a stack slot to an **empty** struct with the given capacity"。
   - 实际：`GCStruct_new(cap)` 直接 `m_size = cap`（`src/woort_gc_struct.c:20`），
     结构体立刻拥有 cap 个字段，`woort_struct_len` 返回 cap，并非空。
     姊妹函数 `woort_set_union_struct` 的描述（"with @p cap fields"）反而是对的。

7. **`woort_ret_panic`**（`include/woort.h:3631`）
   - 注解：用户选项 "1) abort 2) treat as Abort 3) attach debugger"。
   - 实际：默认菜单为四项：**1) Abort process 2) Ignore（忽略并继续）
     3) Terminate current vm 4) Attach debuggee**（`src/woort_diagnosis.c:210`）——
     漏了 "Ignore"，编号错位。

### 容器 / 序列化

8. **`woort_map_iter`**（`include/woort.h:4096`）
   - 注解："false if … the slot is empty (**tombstone**)"。
   - 实际：稠密数组，erase 用 swap-with-last 压缩，无空洞；
     `index < m_size` 恒返回 true（`src/woort_gc_map.c:572`）。tombstone 情形不可能发生。

9. **`woort_serialize_dynbox` / `_map` / `_vec`**（`include/woort.h:4147`, `:4159`, `:4171`）
   - 注解："@return NULL on failure (**unsupported type, cycle**, OOM)"。
   - 实际：默认 flags 下环输出 `"[...]"/"{...}"` 占位符、不支持类型输出
     `<struct>/<gchandle>/<function>` 占位符并**成功返回**；仅 `STRICT` 下才失败
     （`src/woort_serialize.c:172`, `:284`）。
   - 连带：`WOORT_SERIALIZE_FLAG_STRICT`（`include/woort.h:4129`）的说明只提
     "non-deserializeable types"，漏了它对环同样生效。

### CodeEnv

10. **`woort_CodeEnv_drop`**（`include/woort.h:911`）
    - 注解："Release a CodeEnv and **free all associated resources**"。
    - 实际：函数体只有 `code_env->m_hold = false;`（`src/woort_codeenv.c:448`），
      资源要等 GC 后续回收时才释放。

11. **`WOORT_CODEENV_RESTORE_FAIL_INVALID_STRPOOL`**（`include/woort.h:888`）
    - 注解："String pool size invalid"。
    - 实际：`restore_binary` 全程**从不返回**该值（strpool 读取失败走
      FAIL_READ / TRUNCATED），只在 `restore_failed_desc` 里出现。

12. **`woort_CodeEnv_restore_binary` 头部短读**（`include/woort.h:878`）
    - 注解：FAIL_READ = "Cannot read from vfile"。
    - 实际：文件头读取不足 32 字节这一典型读失败返回
      `FAIL_MAGIC_DOESNT_MATCH`（`src/woort_codeenv_bin.c:1250`）。

13. **`woort_CodeEnv_foreach_offset_by_srcloc`**（`include/woort.h:991`）
    - 注解："every entry whose source location **covers** the given line"。
    - 实际：只匹配 `m_begin_line == line` 精确相等（`src/woort_ir_srcloc.c:268`），
      跨行区间覆盖该行但起始行更早的条目不会被访问。

14. **`woort_CodeEnv_set_const_box_int` / `_real` / `_bool`**（`include/woort.h:2740`, `:2754`, `:2768`）
    - 注解："A DynBox object **is allocated on the GC heap**"。
    - 实际：bool 永不分配（纯位模式立即数，`src/woort_value.c:76`）；
      int/real 仅超出 int62/float63 表示范围时才堆分配，常规小值均为立即数。

15. **`woort_CodeEnv_set_const_extern_closure`**（`include/woort.h:2718`）
    - 注解："can be called from script code via **CALLNFP/CALLNWO**"。
    - 实际：闭包常量由 `CALL/CALLC` 派发（`src/woort_vm.c:1095`）；
      CALLNFP/CALLNWO 会把常量槽里的 `GCClosure*` 当函数地址重解释，是错误用法。
      该注解描述的其实是 `set_const_extern_function` 的行为。

### IR 编译

16. **IR 发射组总注解**（`include/woort.h:1356`）
    - 注解："All `woort_IR_*` functions **append** an IROp… false on **out-of-memory**"。
    - 实际两个例外：`woort_IR_POPR(f, 0)` 不追加直接返回 true（`src/woort_ir_block.c:287`）；
      `woort_IR_PACKARG` 在 `named_param_count > 1023` 时返回 false——范围错误而非 OOM
      （`src/woort_ir_block.c:672`）。

17. **`woort_IRFunction_get_argument`**（`include/woort.h:1242`）
    - 注解："@return … NULL **if out of range**"。
    - 实际：越界触发 `assert`（debug 中止，release 下静默新建一个值），
      NULL 仅在 OOM 时返回（`src/woort_ir_function.c:311`）。
      同族 `get_captured` 写的 "NULL on out-of-memory" 才是正确描述。

18. **`woort_IR_LDIDVECX`**（`include/woort.h:1945`）
    - 注解："(**unchecked**)"。
    - 实际：解释器与 JIT 都做边界检查，越界抛 index_out_of_range（`src/woort_vm.c:2130`）；
      它与 LDIDVEC 的真正区别是结果**装箱**不卸箱。

19. **`woort_IR_MKVEC` / `woort_IR_MKMAP`**（`include/woort.h:1546`, `:1557`）
    - 注解："new Vec(**capacity** = elem_count)" / "new Map(**capacity** = kvpair_count)"。
    - 实际：栈上的元素/键值对直接成为**内容**：vec 长度即为 elem_count、
      map 实际插入 kvpair_count 个条目（`src/woort_vm.c:1503`, `:1522`）。

20. **`woort_IR_CALLNWO`**（`include/woort.h:1471`）
    - 注解："(**native**, without overflow check)"。
    - 实际：调用的是常量池中的**脚本（字节码）函数**，且同样做 2 槽调用帧的
      栈溢出检查（`src/woort_vm.c:999`）；doc/opcodes.md 亦写 "call script function (NEAR)"。

21. **`woort_IR_CALLNFP`**（`include/woort.h:1484`）
    - 注解："Call a **Woolang** function (with frame pointer setup)"。
    - 实际：读取常量槽 `m_native_function` 直接调用**原生 C 函数指针**
      （`src/woort_vm.c:1036`）——CALLNWO/CALLNFP 两条注解的语义互相写反了半截。

### 调试器

22. **`woort_WAIPO_Debugger_attach`**（`include/woort.h:4842`）
    - 注解："registers it with **the current VM runtime**"。
    - 实际：注册到进程级全局单例 `g_debugger`（`src/woort_vm_debugger_api.c:25`），
      所有 VM 共用，与"当前 VM"无关。

23. **Debugger 句柄注解**（`include/woort.h:4815`）
    - 注解："released automatically when the debugger is closed or **replaced by a new attach**"。
    - 实际：新 attach 从不替换旧实例——直接释放**新**实例并返回 ALREADY_ATTACHED，
      旧调试器继续存活（`src/woort_vm_debugger_api.c:116`）。

24. **`BreakpointInfo.m_filename`**（`include/woort.h:4925`）
    - 注解："stays valid until the breakpoint is deleted or the debugger detached"。
    - 实际：指向断点记录内嵌 `m_desc[256]`，记录按值存于 vector，
      **新增第 9/17/…个断点触发 realloc** 时所有已返回的 m_filename 指针全部悬空
      （`src/woort_waipo_debugger.c:341` + `src/woort_vector.c:44`）。

25. **`woort_VMRuntime_Debugger_serialize_value`**（`include/woort.h:5280`）
    - 注解："Returns 0 on failure (**NULL @p value** …)"。
    - 实际：对 NULL 只有一句 `assert`，release 下直接解引用崩溃，永远不会返回 0
      （`src/woort_vm_debugger_api.c:356,377`）。

26. **`woort_ctrlc_setup`**（`include/woort.h:5300`）
    - 注解："after **4 hits** … calls abort()"。
    - 实际：判断在计数自增**之前**（`if (hit_count >= 4)` → `++hit_count`，
      `src/woort_ctrlc.c:49`），第 **5** 次按下才 abort；2 秒窗口本身无误。

### VFS / dylib

27. **`woort_vfile_open_reader`**（`include/woort.h:4601`）
    - 注解："buf may be NULL, **yielding a zero-length file**"。
    - 实际：`m_size = buflen` 与 buf 无关（`src/woort_vfs.c:545`）：
      `buf=NULL, buflen>0` 时文件长度仍是 buflen，读取会执行
      `memcpy(dst, NULL+pos, n)`——未定义行为。

28. **`woort_vfile_read`**（`include/woort.h:4616`）
    - 注解："@param buffer may be NULL to **skip/advance**"。
    - 实际：该跳过语义只对虚拟文件 / reader 句柄成立；**磁盘文件**句柄传 NULL buffer
      是彻底 no-op——不读也不前进，返回 0（`src/woort_vfs.c:623`）。

29. **`woort_get_builtin_lib`**（`include/woort.h:4773`）
    - 注解："contains the core runtime native functions (return_it_self, bad_function, panic, print)"。
    - 实际：内置 "woolang" 库含 **234 个**函数，且全部带 `woostd_` 前缀
      （`{"woostd_" #name, …}`，`src/woort_builtin.c:3709`）；
      按文档裸名 `"print"` 去 `dylib_load_func` 会得到 NULL。

30. **`woort_dylib_load`**（`include/woort.h:4697`）
    - 注解："A platform-specific extension (.dll/.so/.dylib) is appended in steps 1-3"。
    - 实际：追加的是 `WOORT_DYLIB_SUFFIX WOORT_DYLIB_EXT`，debug/32 位构建下还带
      `_debug`/`32` 后缀（`src/woort_dylib.c:79`），如 `"foo_debug.dll"`。

---

## 二、高度疑似不符（likely，语义/措辞与代码有出入）

1. **`woort_console_getc`**（`include/woort.h:172`）
   "does not wait for a full line"：库自身从不设置 raw mode（全树唯一 `SetConsoleMode`
   是输出句柄），默认控制台模式下照样等整行；需调用方自行配置终端。

2. **`woort_GCPin_set_dup_boxed` / `_internal`**（`include/woort.h:683`, `:740`）
   "deep duplicate / independent copy"：只复制最外层容器，嵌套 box 共享引用。

3. **`woort_GC_addr_delete_barrier`**（`include/woort.h:622`）
   p 被直接标记、从不解引用，效果等同 `woort_GC_mark_addr_manually`，
   与 "pointer slot" 的语义不符。

4. **`woort_GC_allocate`**（`include/woort.h:577`）
   "allocated and **initialized**"：载荷实为回收的脏内存；AUTO_MARK 标志
   （含糊不清）开启后整个载荷按指针模糊扫描。

5. **`woort_GC_allocate_as_root`**（`include/woort.h:585`）
   "keeping all objects **reachable from it** alive" 仅在 AUTO_MARK 下成立，
   默认标志只保活自身，内部裸指向的对象仍可被回收。

6. **`woort_VMRuntime_weaken`**（`include/woort.h:495`）
   "must be called while … current thread's active VM" 前置条件实现中无任何检查
   （函数体仅两行，`src/woort_vm.c:168`）；"GC will skip automatic marking" 也只在
   VM 离开 dispatch 后生效，执行中仍在检查点自标记。

7. **`WOORT_VM_CALL_STATUS_ABORTED`**（`include/woort.h:239`）
   "**only** returned when interpreted execution receives an interrupt request"：
   硬错误 panic 标签、bootup/spawn/invoke 的失败路径都直接返回 ABORTED
   （`src/woort_vm.c:3995` 等）。

8. **`woort_spawn`**（`include/woort.h:2974`）
   "Spawn a new **coroutine**"：并未创建协程对象，是当前线程 VM 上的直接调用，
   非 NORMAL 时落入 `woort_resume` 重新派发（`src/woort.c:1137`）。

9. **`woort_CodeEnv_find_srcloc_by_offset`**（`include/woort.h:944`）
   "**closest** to a given bytecode offset"：实为二分取 `offset ≤ query` 的最后一项，
   不会返回偏移之后的更近项。

10. **`woort_CodeEnv_set_trap`**（`include/woort.h:1041`）
    返回 false 的情形还包括指令**原本就是** DEBUGTRAP（编译期
    `woort_IR_debugtrap` 产物，无 trap 记录），不在注解所列两种之内。

11. **`woort_CodeEnv_query_function`**（`include/woort.h:922`）
    "true if the function was **found**"：不查任何函数表，纯 `m_code_offset`
    落在代码区间的范围检查（`src/woort_codeenv.c:477`）。

12. **restore 错误码挪用**（`include/woort.h:893`, `:879`）
    `FAIL_INVALID_OFFSET` 被用于 "extern 常量缺 lib/func 名"、"局部变量表越界"
    等非偏移场景；`FAIL_ALLOC` 被用于 "重名 extern 常量"、"重复 trap offset"
    等非 OOM 场景（`src/woort_codeenv_bin.c:1554`, `:1731`, `:1857`）。

13. **`woort_deserialize_dynbox`**（`include/woort.h:4188`）
    "false on **parse error**"：解析完一个值后不检查尾随内容，`"123abc"` 返回 true
    （map/vec 版本则严格拒绝尾随内容，行为不一致）。

14. **`woort_fs_is_file_readable`**（`include/woort.h:4546`）
    "readable **file**"：POSIX 上 `fopen(dir,"rb")` 成功，目录也返回 true
    （`src/woort_vfs.c:328`）。

15. **`WOORT_UTF8MAXLEN = 6`**（`include/woort.h:5707`）
    "Maximum byte length of a single UTF-8 code point"：解码器最多识别 5 字节序列、
    编码器最多产出 4 字节，6 只是保守的缓冲区上界常数。

16. **`woort_u8strnchar`**（`include/woort.h:5720`）
    "true if the character is **well-formed**"：过长编码（overlong，如 `C0 80`）
    也返回 true。

17. **panic handler 枚举**（`include/woort.h:5320`, `:5324`）
    `ACTION_ABORT` 的 "(abort)" 实际只中止**当前 VM**不终止进程；
    `USE_DEFAULT_HANDLER` 的 "(print error, trace, and abort)" 实际默认进入
    交互菜单（可忽略/中止 VM/附加调试器），abort 只是选项之一。

18. **`woort_IRCompiler_finish`**（`include/woort.h:1218`）
    "the compiler is **consumed**"：finish 并不释放编译器，仍必须调用
    `woort_IRCompiler_close()`，否则泄漏——措辞容易诱导跳过 close。

---

## 三、附带发现

### 文档缺口

- `woort_init` 函数式宏（`include/woort.h:109`）隐式追加 `setlocale`，
  且与真实 `woort_init` 内部的 `_woort_env_bootup` 行为重复，注解未提。
- `woort_set_string_fmt` / `woort_set_union_string_fmt`（`include/woort.h:3065`, `:3186`）
  完全无注解。
- `woort_REPLPrinter_create` 的 `param` 参数及回调第三参（`void*`）无 `@param`。

### 顺带发现的实现 bug（非注解问题）

- `woort_u8destring` 输入以孤立反斜杠结尾时会读过 NUL 终止符越界
  （`src/woort_utf8.c:336`, `:469`）。

---

## 其余验证结果

其余约 240 项注解经逐条比对与实现一致，包括：

- 全部 12 个 bounded-fill 字符串转换函数的完整契约；
- `WOORT_VM_CALL_STATUS_RESYNC` 的四象限状态机描述；
- 四个整除 guard（CHKDIVIL/IR/IRZ/ILR）的触发条件；
- 断点 id 单调不复用、QueryBreakpointCallback 的锁与死锁说明；
- `try_breakdown_any_vm`（无调试器时请求被消费）与 `try_breakdown_vm`
  （请求驻留重试）的刻意不对称；
- WOORT_IGNORE / WOORT_RETURN_SLOT 的全部声称用法；
- vec/map/struct 访问器的返回值与 miss 时 dst 不变语义；
- 路径 API（exe_path/work_path/get_file_loc/normalize_path）的 bounded-fill 契约。
