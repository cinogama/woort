# WooRT 代码审查报告（正确性 / 逻辑错误）

> 审查范围：`include/woort.h` + `src/` 全部 133 个源文件（约 49,400 行）
> 审查重点：正确性 / 逻辑错误（UB、内存安全、边界、溢出、错误处理、并发、生命周期）
> 产出形式：分级问题清单，含 `file:line` 定位与修复建议
>
> 说明：行号由审查代理从源码中定位，建议复核时以实际文件为准；明显的重复项（如 GC struct/closure 分配溢出被两个代理分别报告）已合并。

---

## 概览

| 严重度 | 数量 | 说明 |
|---|---|---|
| :red_circle: 严重 | ~45 | UB、内存破坏、崩溃、UAF、数据竞争 |
| :yellow_circle: 中等 | ~95 | 错误路径、边界、溢出、缺校验、并发 |
| :green_circle: 轻微 | ~75 | 可维护性、死代码、契约不一致 |

### 最值得优先处理的系统性模式（跨模块反复出现）

1. **柔性数组成员的 `count * sizeof(...)` 整数溢出** — GC string/struct/closure/map/vec/pin、codeenv 全套都没做 `count <= (SIZE_MAX - HDR) / ELEM` 前置校验，溢出后分配过小、随后 `memcpy`/索引写越界。这是本仓库**最普遍的高危缺陷**。
2. **二进制反序列化普遍缺边界检查** — `codeenv_bin` 的 strpool、偏移、count 字段几乎都直接信任文件，可被构造输入触发 OOB 读/写。
3. **解释器完全信任字节码** — 跳转不校验 `code_end`、双字指令不校验 `rt_ip+1`、DIVI/MODI 不在 handler 内重检除数。
4. **有符号整数运算 UB** — `ADDI/SUBI/MULI/NEGI`、移位、`INT64_MIN` 取负、`val << 2` 负值左移。
5. **`assert`-only 校验在 release 失效** — 大量索引/计数仅靠 `assert`，release 下静默越界。
6. **GC 对象初始化顺序竞争** — `init_delay_alloc` 先把对象挂入可达链再让调用者填字段，并发 marker 会扫到未初始化的 `woort_Value`。
7. **信号处理与全局上下文生命周期的数据竞争** — ctrlc 处理器调用非 async-signal-safe 函数；`m_globalcontext_alive` 非原子读写。

---

## :red_circle: 严重问题（Critical）

### 公共 API (`include/woort.h`)

1. **`woort.h:4`** — `WOORT_VERSION` 展开为 `WOORT_VERSION_WRAP(1,0,6,13)`，但 `WOORT_VERSION_WRAP` 在头文件中**从未定义**（只在 `woort.c`/`woort_builtin.c`/`resources.rc` 内部定义）。任何外部消费者引用 `WOORT_VERSION` 都会编译失败。
   - **修复**：在头文件第 4 行之前定义 `WOORT_VERSION_WRAP`。

### 运行时核心

2. **`woort_ctrlc.c:20-73`** — SIGINT 处理器调用了非 async-signal-safe 函数（`woort_log`→`vfprintf`、`time()`、`woort_WAIPO_Debugger_attach`、`woort_VMRuntime_Debugger_breakdown_all_vm`），POSIX 下为 UB，可能死锁/破坏 stdio/malloc 内部状态。
   - **修复**：处理器只置 `volatile sig_atomic_t` 标志，实际工作交给普通线程。

### 内存管理器 (`woort_mem*`)

3. **`woort_mem_chunk.c:161-167`** — `woort_mem_chunk_allocate_pages` 在 `commit_page` 失败时直接返回 NULL，未回滚已修改的元数据（已从 freelist 摘除、count_arr 标记 ALLOCATED），永久泄漏块并破坏分配器状态。
   - **修复**：失败路径回滚（重新插入 freelist、清 ALLOCATED_FLAG、decommit 已提交子页）。
4. **`woort_mem_gc.c:489-501` + `woort_mpsc.h:48-74`** — GC worker 的灰队列终止竞争：worker 在最后一次 drain 与置 `is_draining=false` 之间，生产者成功 enqueue 的单元被孤立在队列里，保持 `SELF_MARKED` 不被 trace → debug 下 `assert(life!=SELF_MARKED)`（gc.c:340）abort，release 下子对象被回收 → UAF。
   - **修复**：置 `is_draining=false` 后在自旋锁下再 drain 一次，非空则继续处理。
5. **`woort_mem.c:82-86`** — `woort_mem_allocate_begin` 计算 `sizeof(PageHead)+sizeof(UnitHead)+size` 无溢出检查，`size` 接近 `SIZE_MAX` 时回绕为很小的值，随后 payload 写越界。
   - **修复**：前置校验 `size > SIZE_MAX - HDR`。

### GC 对象（合并重复）

6. **`woort_gc_string.c:43`** — `sizeof(woort_GCString)+len+1` 溢出 → tiny 分配 + `memcpy(...,len)` 越界写。
7. **`woort_gc_string.c:82,85`** — 字符串拼接 `a->m_length+b->m_length+1` 溢出 → 同上。
8. **`woort_gc_struct.c:15-17,30-31`** — `struct_size*sizeof(woort_Value)` 溢出 → 调用方 `m_datas[]` 越界写。
9. **`woort_gc_closure.c:19-21`** — `captured_count*sizeof(woort_Value)` 溢出 → `m_datas[]` 越界写。
10. **`woort_gc_map.c:123-124`** — `capacity*(sizeof(Bucket)+sizeof(uint32_t))` 溢出 → bucket/entry 越界。
11. **`woort_gc_vec.c:46,51,128`** — `new_space*sizeof(woort_DynBox)` 溢出 → 后续 push/insert 越界。
12. **`woort_gc_pin.c:49`** — `count*sizeof(woort_Value)` 溢出 → `GCPin_set` 越界写。

> 6–12 **统一修复**：每次 `alloc_delay_init` 前校验 `count <= (SIZE_MAX - sizeof(Header)) / sizeof(Elem)`。

### IR 编译器

13. **`woort_ir_function.c:379-410`** — `_phase0_jump_chaining` 终止条件只识别直接 2-环；更长环（如 L2→L3→L2）会死循环（`final_target` 每轮被覆盖）。
    - **修复**：用 visited 集合，或对比前后两次 `final_target`。
14. **`woort_ir_function.c:542-554`** — Phase 1 在 `instr_to_block` 的 `malloc` 失败时，`woort_vector_resize` 已把 `m_size` 设为 `block_count` 但未初始化元素，`deinit` 时 `free(garbage_ptr)`。
    - **修复**：先 resize 为 0 或先 zero-init，失败时不增大 `m_size`。
15. **`woort_ir_srcloc.c:123-127`** — `pop` 仅 `assert` 保护空栈，release 下 `m_size--` 下溢到 `SIZE_MAX`，下次 `push_back` 越界写。
    - **修复**：加运行期检查并提前返回。

### VM / 字节码 / 值层

16. **`woort_vm.c:3817-3831`** — `CHKDIVILR` 的被除数/除数条件**逻辑写反**：触发于 `dividend==0` 与 `dividend==INT64_MIN && divisor==-1`，永远拦不住真正的 UB（`divisor==0`、`dividend==INT64_MIN && divisor==-1`），反而误杀 `0/5`。
    - **修复**：交换操作数语义为 `if (0 != divisor && (-1 != divisor || INT64_MIN != dividend)) break;`。
17. **`woort_vm.c:1703-1740`** — `ADDI/SUBI/MULI` 直接做有符号 `int64` 运算，溢出即 UB，且无 `CHKDIVI` 类前置守卫。
    - **修复**：用无符号运算再 cast 回。
18. **`woort_vm.c:1727-1747,1838-1842,1990-2005`** — `DIVI/MODI/CDIVI/CMODI` 完全依赖字节码已发 `CHKDIVI`；handler 内无除数重检（JIT 或畸形字节码路径可绕过）。
    - **修复**：handler 内加廉价除数检查。
19. **`woort_vm.c:1743-1747,1838-1842,1990-2005`** — `NEGI`（`-m_integer`）、`CADDI/CSUBI/CMULI` 的 `+=/-=/*=` 在 `INT64_MIN`/溢出时为有符号 UB。
    - **修复**：`0u - (uint64_t)x` 等。
20. **`woort_value.c:65-75`** — `_woort_try_box_int62` 对有符号 `woort_Int` 做 `(val << 2)`，负值左移为 UB（C11 §6.5.7p4）。
    - **修复**：先 `(uint64_t)val << 2`。
21. **`woort_vm.c:1309-1500`** — `JFWD/JBCK` 及全部条件跳转、`JIFINITED` 设置 `rt_ip = code+offset` / `rt_ip +=/-= off` **从不校验** `code_end`；畸形/JIT 字节码可在下一轮 OOB 取指。
    - **修复**：fetch 前 `[code_begin, code_end)` 校验（含 2-word 尾）。
22. **`woort_vm.c`（双字指令多处，如 481-653）** — 读 `rt_ip[1]` 前不校验 `rt_ip+1 < code_end`，边界处 OOB 读。
    - **修复**：读扩展字前 bounds-check。
23. **`woort_vm.c:3667-3752`** — `ASTORE/ALOAD/CAS/JIFINITED` 把 `&rt_env_data[i].m_integer`（普通 `int64_t`）cast 为 `woort_AtomicInt64*`（`_Atomic int64_t*`）再做原子操作，违反 strict-aliasing 且为数据竞争 UB。
    - **修复**：这些槽从一开始就用 `_Atomic` 类型存储。

### 内建库 (`woort_builtin.c`)

24. **`woort_builtin.c:559`** — `take_string` 用 `sscanf(input,"%s%zn", string_buf[1024], ...)`，token > 1023 字节即栈缓冲溢出。
    - **修复**：`%1023s` 或动态扩容。
25. **`woort_builtin.c:544`** — `take_token` 把用户可控的 `matching_format` 直接传给 `sscanf`，格式串含 `%d/%s/%n` 时读取未提供的 varargs → 随机栈槽读写。
    - **修复**：禁止转发用户格式，自己实现匹配器。
26. **`woort_builtin.c:3376,3393`** — `int_to_hex/oct` 对 `INT64_MIN` 做 `-val` → 有符号溢出 UB。
    - **修复**：无符号域取负 `-(uint64_t)val`。
27. **`woort_builtin.c:282`** — `random_i` 计算 `(uint64_t)(to-from)+1`，`from==INT64_MIN && to==INT64_MAX` 时 `to-from` 为有符号溢出 UB，且 `range` 可为 0 → `%0` 除零 UB。
    - **修复**：`(uint64_t)to-(uint64_t)from+1`，结果 0 时直接返回 `from`。

### CodeEnv / 序列化

28. **`woort_codeenv_bin.c:1245`** — `data_count-constant_count` 未先校验 `data_count>=constant_count`，无符号下溢到接近 `SIZE_MAX` 作为 `static_storage_count`。
    - **修复**：提前拒绝。
29. **`woort_codeenv_bin.c:1217`** — `(size_t)code_size*sizeof(woort_Bytecode)` 可溢出，通过后续大小检查后欠分配。
    - **修复**：先比 `code_size > total_size/sizeof(Bytecode)`。
30. **`woort_codeenv.c:286,296,300`** — `CodeEnv_create` 对 `bytecodes_count*sizeof`、`total_data_count*sizeof`、`const+static` 求和均无溢出检查，仅 `assert`。
    - **修复**：加运行期检查返回 false。
31. **`woort_codeenv_bin.c:262-277`** — `_bin_strpool_get` 读 `data+offset` 4 字节并返回 `data+offset+4`，**无 strpool 大小校验**，攻击者可控 offset → 堆 OOB 读。
    - **修复**：传入 `strpool_size` 校验 `offset+4 <= strpool_size`。
32. **`woort_codeenv_bin.c:1097-1125`** — `_rst_make_cstr` 调用未校验的 `_bin_strpool_get`，再用文件给的 `len` 做 `memcpy`，且 `malloc(len+1)` 在 `len==UINT32_MAX` 时回绕为 0 后做多 GB `memcpy`。
    - **修复**：校验 `off+4+len<=strpool_size`，拒绝 `len==UINT32_MAX`。
33. **`woort_codeenv_bin.c:1463-1465,1533-1535`** — `SCRIPT_FUNC`/`SCRIPT_CLOSURE` 的恢复 offset `off` 未校验 `< code_size`，直接 `code_begin+off` 存为常量 → 悬挂/OOB 指针（trap 恢复 1788 行有正确校验，可对照）。
    - **修复**：加 `off < code_size` 守卫。
34. **`woort_codeenv_bin.c:723-731`** — `malloc(sizeof(SourceMap_Entry)*sm_count)` 可溢出 → 后续循环写 `sm_count` 项越界。
    - **修复**：cap 到 `SIZE_MAX/sizeof(...)`。

### 工具库

35. **`woort_linklist.c:134`** — `linklist_index` 在越界/空表时 `assert(current!=NULL)` 在 release 被剥除，随后解引用 NULL。
    - **修复**：改为运行期 `return false`。
36. **`woort_utf8.c:257`** — `woort_u8enstring` 每轮把**总** `bytelen` 传给 `u8combineu16`，而非剩余 `p_end-p`，`i>=bytelen` 守卫在尾部永不触发，多字节首字节接近缓冲尾时读越界。
    - **修复**：传 `p_end-p`。
37. **`woort_vector.c:47`** — `realloc(capacity*element_size)` 无溢出检查，realloc 得到 tiny 缓冲，后续 `emplace_back` 越界写。
    - **修复**：先校验 `capacity > SIZE_MAX/element_size`。
38. **`woort_vector.c:38-44`** — 倍增循环 `m_capacity*=2` 无溢出守卫，回绕到 0 后第 40-41 行重置回 8 → 死循环。
    - **修复**：`m_capacity > SIZE_MAX/2` 时失败。

### I/O / 调试器

39. **`woort_dylib.c:345 + 403-407`** — `woort_dylib_fake` 对依赖的 `m_use_count` **加了两次**（345 行 `dylib_keep` + 405 行直接 `atomic_fetch_add`），但 `dylib_unload`(720) 只减一次 → 永久泄漏一个引用。
    - **修复**：删除 403-407 的冗余加。
40. **`woort_waipo_debugger_cmd.c:1060-1075`** — `CALL` 的 next-IP 计算中 `target=sb[...].m_closure` 后**未判空**即访问 `target->m_script_function`(1071)，闭包为空时 NULL 解引用。
    - **修复**：`if(target==NULL){*out=ip+1;return true;}`。
41. **`woort_waipo_debugger_cmd.c:1094-1102`** — `RET` 的 next-IP 计算先读 `trace_sb[1]`(1095)、`trace_sb[2]`(1101) 再校验（1098 在循环内），初始帧靠近栈底时 OOB 读。
    - **修复**：把 `stack_end-trace_sb<3` 检查提到首次读之前。

---

## :yellow_circle: 中等问题（Medium，精选；每模块附计数）

### 公共 API (5)

- `woort.h:769` `woort_VMRuntime_current()` 用 K&R `()` 而非 `(void)`，C 中非原型。
- `woort.h:4524/1135/3012/3026/3127/3143/4768/1288/620` 多处 doxygen `@param` 缺漏/顺序错/类型错（`reason` 应为 `woort_PanicReason`、`idx` 误称 "Must not be NULL" 等）。
- `woort.h:2964` `woort_set_pointer` 直接 `(woort_Int)(src)` 而非经 `(intptr_t)`，与同族不一致（实现定义）。
- `woort.h:417-431` `char16_t/char32_t` C 模式 typedef 块缺 `#else` 兜底，非 MSVC/GCC/Clang 的 C 编译器编译失败。

### 运行时核心 (10)

- `woort.c:39-46` `version_int` 各字段按**十六进制**解析而 `version_str` 按**十进制**输出 → 两者不一致（tweak `0x13`=19 vs "13"）。
- `woort.c:122` `max_reserved_memory_in_mb*1024*1024` 可溢出 `size_t`，负 `atoi` 变成巨大值。
- `woort.c:99/101` 用 `atoi` 解析命令行，无范围/错误校验；`(_HaltPanicVMMode)atoi(...)` 把未校验整数 cast 到枚举。
- `woort.c:164-185` 关闭顺序非 init 逆序：JIT(init#8)最后拆(#13)、GCPin(init#4)在 CodeEnv(init#5)之前拆。
- `woort.c:1015-1028` `woort_push_reserve` 先 `m_sp -= count` 再 bounds-check，巨大 `count` 使指针回绕，绕过 `sp<stack` 守卫，`out_stack` 落到栈外。
- `woort.c:1067` `_woort_pre_invoke` 用 `m_sp-2-target->m_size < m_stack` 形成越界指针（UB），巨大 `m_size` 回绕 → `m_sp -= size; memcpy` 下溢栈。
- `woort.c:1230` 用了错误的 panic 码：`STACK_OVERFLOW` 而非 `CODE_ENV_NOT_FOUND`。
- `woort_threads.c:646,778,803` `WaitForSingleObject(...,INFINITE)` 返回值被忽略，`WAIT_FAILED` 时仍当已加锁。
- `woort_threads.c:54,50` `pthread_self()`/`pthread_threadid_np` 假定整型/忽略返回。
- `woort_env.c:103-114` `(*plen+u8len+1)*2` 可溢出 `size_t`。

### 内存管理器 (8)

- `woort_mem_chunk.c:200-203,276-278,284` `reserved_size`/`required_pages` 多处 size 计算无溢出保护，`required_pages` 截断到 `uint32_t`。
- `woort_mem_chunk.c:231` `count_arr[0]=(uint32_t)total_pages` 在 `total_pages>UINT32_MAX` 时截断，破坏 buddy 元数据。
- `woort_mem.c:189-194` `woort_mem_validate_addr` 未校验 `unit_index<unit_count`，页内 padding 地址会越界读 `m_life`。
- `woort_atomic.h:268-279`（及多处 `_Generic` 表）**缺 `woort_AtomicSize/IntPtr/UIntPtr`** 分支 → `__STDC_NO_ATOMICS__` 回退路径下 `mpsc.h`/`gc.c`/`spin.c` 用的 size_t 原子**编译失败**。
- `woort_atomic.h:282-286`（多处）回退路径 `_explicit` 宏**丢弃 memory_order**（MSVC 内建硬接全屏障），`mpsc.h`/`gc.c` 精心写的 acquire/release 在这些构建上不生效。
- `woort_mem_gc.c:381-384 vs 408-437` `current_running_out` 只 ACQUIRE 取一次，并发 mutator 期间可变陈旧 → 无存活页被错误重链。
- `woort_mem_thread_context.c:26-27` + `global_context.c:178` `m_globalcontext_alive/inited` 为普通 `bool` 无同步读写 → 数据竞争（UB）。
- `woort_mem.c:153` `reallocate` 拷贝整 size-class 而非用户请求大小，huge 单元拷入页对齐 padding 的未初始化字节。

### GC 核心 / 对象 (14)

- `woort_gc_pin.c:48-84` `m_datas[]` 未清零即 `init_delay_alloc`，并发 marker 扫到未初始化 `m_gcinstance`（UB + 误留）。
- `woort_gc_vec.c:145-153` `emplace_back` 扩 `m_length` 后返回未初始化 slot，AUTO_MARK 按容量全扫。
- `woort_gc_map.c:575-592` emplace API 文档要求调用方直接写 `bucket->m_key/val`，已 FULL_MARK 的 backing 不会重 trace → key/val 被过早回收。
- `woort_gc_string.c:61-67,121,131` `vsnprintf/snprintf` 返回 -1 时 `assert` 被剥，`(size_t)len=SIZE_MAX` → 分配回绕。
- `woort_gc_map.c:36-52` `_woort_next_power_of_two(SIZE_MAX)` 返回 0 → `capacity=0`、`m_mask=(size_t)-1`。
- `woort_gc_pin.c:88-129` `GCPin_destroy` 摘链后未清 `m_prev/m_next`，二次 destroy 写 `m_prev->m_next` 破坏链。
- `woort_gc.h:82` `mixed_write_barrier_dynbox` 对 `src` 直接 `mark_unit_head`，若为非 GC 但 8 字节对齐的指针 → `ptr-sizeof(UnitHead)` 解引用非法内存（应用 `mark_fuzzy_unit`）。
- `woort_gc_struct.c:13-25`、`woort_gc_closure.c:19-30`、`woort_gc_map.c:132-141/422/451/480/539`、`gc_vec.c:107-135/174-183` 多处对象先挂入 GC 再由调用方填字段，或 realloc 后高位 bucket 为旧 `m_entries` 字节 → 保守扫描误判。
- `woort_gc_map.c:155,416,445,474,541,585` `m_size` 截断到 `uint32_t`，超 `UINT32_MAX` 后索引回绕撞链。
- `woort_gc_vec.c:127-131` `shrink` 的 `mem_reallocate` 在更小时为 no-op，`m_space` 更新但底层仍大、含陈旧 DynBox。
- `woort_gc_units.c:62-77` `_woort_GCUnit_alloc_failed` 把 `m_sp=m_stack` 跑 GC 再恢复，`[m_stack,osp)` 区间对 marker 不可见，若这些值无其它 root 可能被回收 → 调用方悬空引用。
- `woort_gc_map.c:594-607` `GCMap_copy` 在 `dst==src` 时先 clear 再拷贝 → 静默清空。
- `woort_gc_string.c:136-139` `to_integer` 不查 `errno==ERANGE`、不查 end 指针。
- `woort_gc_map.c:361` bool key hash 只返回 0/1，全部撞到两个桶。

### IR 编译器 (9)

- `woort_ir_value.c:19-25` `init_captured` 对 `captured_idx=0` 得偏移 0，与 SB+0..2 保留槽冲突。
- `woort_ir_function.c:1415` 支配深度循环 `while(cur!=idom[cur] && cur>=0)` 先读 `idom[cur]` 再判 `cur>=0`，`cur=-1` 时越界读。
- `woort_ir_function.c:377,381,398` `_phase0_jump_chaining` 仅 `assert` 检查 `m_bound`，release 下未绑定跳转静默链到指令 0。
- `woort_ir_function.c:148-155` `LiveInterval` qsort 对相等起点返回 0 → 槽分配/字节码不确定。
- `woort_ir_compiler.c:2379-2392` `finish` 失败时不回滚 `m_committed_codes`。
- `woort_ir_function.c:452-479` Phase 2b 删 noop 跳转但留陈旧 CFG 边。
- `woort_ir_function.c:1716-1747` const 放置可能选到不可达的公共支配者（block 0）。
- `woort_ir_srcloc.c:135-136` `top` cast away const 调非 const `vector_at`（技术 UB）。
- `woort_ir_function.c:1484-1519` 每条回边重 `calloc/malloc` O(block_count)，patological IR 抖动。

### VM / opcode (13)

- `woort_vm.c:1290-1297` `POPRS` `rt_sp += (size_t)rt_sb[...].m_integer`，负值变巨大 size_t 立即破坏 sp（仅 assert）。
- `woort_vm.c:1502-1625` `MKVEC/MKMAP/MKSTRUCT/...EXT` 读 `rt_sp[1..size]` 后 `rt_sp+=size` 不校验 `rt_sp+size<=rt_sb`，EXT 的 `size` 是全 32 位。
- `woort_vm.c:1196-1261` `RET/RETVS/RETVC` 用 `rt_sp[-1].m_ret_bp.m_bp_offset` 算 `rt_sb` 无校验。
- `woort_vm.c:1230-1231` `RETVS` 读 `rt_sp[(int16_t)BC16-2]`（±32K 偏移）无边界检查。
- `woort_vm.c:3641-3665` `PACKARG` `pack_argc = sb[3].m_integer - skip_count`，`<skip_count` 时无符号下溢 → 巨大 `memcpy`。
- `woort_vm.c:545-595` `PUSHRCHK/ASSURESSZ` 用 `rt_sp-reserve` 做 overflow 检查，32-bit 上 `reserve` 可达 2^24-1，减法回绕绕过检查（且减过 `rt_stack` 本身是 UB）。
- `woort_vm.c:623-637` `POPR/POPS/POPC` 仅 assert `rt_sp<=rt_sb`。
- `woort_vm.c:3783-3791` `CHKDIVIL` 对被除数 `INT64_MIN` 无条件 panic，误杀 `INT64_MIN/2` 等合法除法。
- `woort_vm.c:3767-3774` `PANICS` 不判 nil/类型即 `msg->m_content` 用 `%s`。
- `woort_opcode_dispatcher.c:767-788` `LDIDVECEXT/LDIDVECXEXT` 把 `a=BC16`(实际是 vec) 和 `b=c[1]>>16`(实际是 idx) 的语义与 VM/`opcode_builder.h:658` 相反 → 反汇编/JIT 看到交换的操作数。
- `woort_opcode_dispatcher.c:1171-1175` 未知 opcode `default:` 假定 1 字宽返回 `c+1`，多字未知码会错推进 PC。
- `woort_opcode_dispatcher.c:1113-1133` `ATOMIC` mode 3（保留）fallthrough 到 `c+1`，而其它 mode 是 `c+2`。
- `woort_vm.c:597-621` PUSH mode 1/2/3 仅 `assert(rt_sp>rt_stack)`，release 无溢出检查。

### 内建库 (12)

- `woort_builtin.c:316` `sleep` `(uint32_t)(tm*1000.0)` 对大 `tm` 为越界转换 UB。
- `woort_builtin.c:3425/3432/3438` `bit_shl/shr/ashr` 移位数无校验（<0 或 ≥64 为 UB）。
- `woort_builtin.c:1686` `create_str_by_ascii` `len==0` 时 `malloc(0)` 可能返回 NULL，`memcpy(_,NULL,0)` 为 UB。
- `woort_builtin.c:1411` `string_split` 迭代器只 root slot 0，slot 1 的分隔符指针悬空（UAF）。
- `woort_builtin.c:1932` `array_sub_to` `begin+count>src_len` 两个负参数求和可回绕绕过检查，随后巨大 `vec_resize`。
- `woort_builtin.c:3470/3473` `tuple_nthcdr` 截断到 `uint16_t`，元组 >65535 时 size/索引静默错。
- `woort_builtin.c:1184/1247/1303/1345` `u8strtou32` 返回 NULL 统一报 "Out of memory"，与 `string_find:1122` 的 `&& aim_len>0` 守卫不一致。
- `woort_builtin.c:455/463/849/875` `isalpha/isalnum` 对补充平面（非 BMP）字符一律返回 true。
- `woort_builtin.c:512` `take_token` `buf_size=format_len*2+4` 无溢出检查 → tiny 分配 + 堆溢出。
- `woort_builtin.c:1877/1908/1496/1080` 多处 `len1+len2`/`len+u8len`/`out+repl_len` 加法无溢出守卫。
- `woort_builtin.c:3671` `repl_print_normal` 仅查 `argn>=2` 即 `woort_pointer(1)` 解引用，无类型校验。
- `woort_builtin.c:67` `print` 按自读的 `argn` 索引栈槽，无内部上限。

### CodeEnv / 序列化 (12)

- `woort_codeenv.c:639-649` `total_entries` 为 `uint32_t` 累加可溢出 → 欠分配 + 越界写。
- `woort_codeenv_bin.c:1602-1609/1653-1664` struct member `midx`、extern `cidx` 未校验 `< constant_count`。
- `woort_codeenv_bin.c:211,243,254` strpool offset 存 `uint32_t`，>4GiB 截断破坏后续引用。
- `woort_codeenv.c:294-302` `alloc_delay_init` 返回 NULL 仅 `assert`，release 解引用 NULL。
- `woort_codeenv_bin.c:1277-1303/1318` `strpool_size`/各 count 为 uint64 无上限合理性检查。
- `woort_codeenv_bin.c:376` `_bin_read_raw` `r->m_pos+len > r->m_size` 加法可回绕，改为 `len > m_size-m_pos`。
- `woort_codeenv_bin.c:1593-1594` `member_count*sizeof(IRConstantIndex)` 32-bit 可溢出。
- `woort_codeenv_bin.c:1444-1445` `CONST_TYPE_STRING` 恢复不校验 `off+4+slen<=strpool_size`。
- `woort_codeenv_bin.c:1182-1186` 32 字节头短读被报成 magic 不匹配。
- `woort_codeenv.c:1028/1068` `cidx_for_script_function` 未找到时 `abort()` 而非优雅失败。
- `woort_codeenv.c:461-465` `query_function` 先做 `code_begin+offset` 指针运算再查范围，巨大 offset 为 UB。
- `woort_codeenv_bin.c:1-83` 二进制格式按主机字节序裸写，无端序转换（仅靠 magic 偶然拦截）。

### JIT (10，无 critical)

- `woort_jit_x64_impl.cpp:2287-2291` / `arm64:2287-2290` 闭包调用 JIT 分支只在 `status==RESYNC` 返回，YIELD/ABORTED 等非 NORMAL 状态 fallthrough（与 `CALLNJIT` 不一致）。
- `x64:2382/2401/2412` + `arm64:2382` `RETVS/RETVC` 先把返回值写到 `sb[2]`（ret_addr 槽）再 `emit_ret`，`FROM_NATIVE` 路径把 ret_ip 读成返回值 → `vm->ip` 错乱（解释器 `vm.c:1226` 是先读后写，顺序相反）。
- `woort_jit_bridge.c:105-106` `make_closure` 写 upvalue 跳过 init write barrier（兄弟 `make_vec/struct/union` 都用）。
- `x64:2313-2314` / `arm64:2314-2315` 原生调用分支 `vm->sb=new_sp` 而非 ret-frame base `new_sb`，`captured_count>0` 的原生闭包所有 sb 相对寻址偏 `size_bytes`（标准原生闭包 `captured_count=0` 掩盖了它）。
- `arm64:3410/3424` ARM64 `sdiv` 除 0 静默返回 0，完全依赖前置 `CHKDIVIR`；x86 `idiv` 会 trap，两平台行为不一致。
- `x64:4343-...` / arm64 等 `LDIDSTRUCT/STIDSTRUCT/PUSHIDST*` 字段索引 `idx*8` 无溢出/越界校验。
- `bridge.c:331` `GCString_to_bool` 对 `str->m_content` 做 `strcmp` 无 NULL 检查。
- `x64:664/arm64:697` epilogue `assert(m_sync_runtime_status_site_count>0)` 依赖 prologue 必发 checkpoint 的隐式不变量。
- `x64:462/arm64:493` `scan_jump_targets` 把 BC16 cast `uint16_t` 当无符号前向偏移。
- `x64:2424/arm64:2427` `POPRS` 用整槽 64 位当 pop 计数无校验。

### 工具库 (10)

- `woort_utf8.c:32` `u8charnlen` 对 6 字节首字节总返回 1，`u8combineu32` 的 `case 6` 为死代码，6 字节序列被误计。
- `woort_utf8.c:103-133` `u8combineu32` 不拒绝 overlong/代理/`>U+10FFFF`。
- `woort_utf8.c:151-157/228` `u32exractu8/u16exractu8` 对代理码点产出 CESU-8/孤立 surrogate。
- `woort_utf8.c:352` 八进制转义上界写成 `'8'`，应 `'7'`。
- `woort_utf8.c:240/478/520/543` 多处 `bytelen*6+3`/`*sizeof(char32_t)`/`*sizeof(char16_t)` 无溢出检查。
- `woort_hashmap.c:52-77` `deinit` 不清 `m_buckets`/count → 二次 deinit double-free。
- `woort_hashmap.c:123-125` `bucket_count*2` 无溢出检查。
- `woort_hashmap.c:232` `size*4 >= bucket_count*3` 双侧可溢出 → rehash 被跳过/误触。
- `woort_linklist.c:18-27` `deinit` 不重置 head/tail → 二次 deinit double-free。
- `woort_bitset.c:13` `bit_count` 接近 `SIZE_MAX` 时 word_count 计算溢出。

### I/O / 调试器 (12)

- `woort_vfs.c:286-291` `_get_all_paths_foreach` 用 NULL 哨兵扫描，单个 path malloc 失败丢后续条目但 `get_all_paths` 仍返回全量 size → 调用方解引用 NULL。
- `woort_vfs.c:35-47` `_shutdown_foreach_callback` 无视 `m_refcount` 直接 free，借用中的 VFile UAF。
- `woort_vfs.c:591-628` `vfile_read` `buffer==NULL` 时 virtual/reader 分支前进位置当 "skip"，real 分支不前进 → 行为不一致。
- `woort_vfs.c:344-514` + `woort_path.c:300-319` `normalize_path` 不折叠 `..`，脚本路径 `../../etc/passwd` 可逃出搜索根。
- `woort_vm_debugger_api.c:35-42` `Debugger_shutdown` 在 `detach` 后立即 `rwspinlock_deinit/mutex_destroy`，未等 `try_trap` 引用排空 → 锁被占用时销毁 UB。
- `woort_diagnosis.c:56-59` `make_string` 可能返回 NULL 直接存入 `m_sp->m_string`，后续解引用 NULL。
- `woort_dylib.c:635-664` `load_func` POSIX 下不调 `dlerror` 区分"符号缺失"与"符号合法为 NULL"。
- `woort_dylib.c:782-800` `find_by_resolved_func` 返回 dylib 不增引用，并发 unload → UAF。
- `woort_waipo_debugger_cmd.c:1047-1051` `CALLNWO` next-IP 用 26 位码值索引 `m_data_begin` 无 `m_data_count` 校验。
- `woort_waipo_debugger_cmd.c:1299-1300/1356/1376-1384` `m_callstack_offset_of_base`/`m_stack_offset`/`global_index` 多处未校验即索引栈/数据表。
- `woort_dylib.c:465/488/511` `work_path(...)+1` 在返回 `SIZE_MAX` 时回绕为 0。
- `woort_disassembly.c:752-770` `dump_codes` 未处理 `m_code_begin==NULL`。

---

## :green_circle: 轻微问题（Minor，计数 + 精选）

各模块计数：API 9 · 运行时 5 · 内存 8 · GC 7 · IR 13 · VM 19 · 内建 14 · CodeEnv 13 · JIT 5 · 工具 9 · I/O 10。代表性项：

- `woort.h:3980` `map_contains_*` 命名缺 `_by_`，与同族 `get_by_/set_by_/erase_by_` 不一致。
- `woort.h:2820` `woort_panic` 宏用非标准 `__FUNCTION__` 而非 `__func__`（`woort_log.h:16` 同样）。
- `woort.c:1082-1083` `(uint32_t)(m_sb-m_sp-2)` 把 bp 偏移截到 32 位（>4GiB 帧潜在损坏）。
- `woort_vm.c:2155/2380/...` `LDIDSTRUCT` 等仅 `assert(index<size)`，release 越界。
- `woort_value.c:102-105` `((woort_Int)val)>>2` 负值右移为实现定义。
- `woort_codeenv_bin.c:1782-1793` trap 用 `&cenv->m_code_begin[off]`（const）作 key，`set/clear_trap` 用 `woort_Bytecode**`，const-ness 不一致。
- `woort_waipo_debugger.c:53-71` hashmap 结果 switch 无 `default`。
- `woort_builtin.c:285` `random_r` 大区间灾难性抵消，质量缺陷。
- `woort_utf8.c:240/312-313` 分配/守卫两套 `bytelen*6` 计算不一致。
- `woort_atomic.h` 多处 `_Generic` 表缺 `woort_AtomicSize/IntPtr/UIntPtr`（无 `stdatomic` 回退路径编译失败）。
- `woort_ir_function.c:1194` `(int32_t)max_slots` 在 `max_slots>INT32_MAX` 时为实现定义。
- `woort_mem_chunk.c:290-306` `free_page` 对非 ALLOCATED 块静默返回，掩盖 double-free。
- `woort_waipo_debugger_cmd.c:1903-1906` 上一条命令重放只存命令名，丢参数。
- `woort_hashmap.c:184` `insert` 不校验 `value!=NULL`（当 `m_value_size!=0`）。

---

## 建议修复优先级

1. **第一波（修复成本最低、收益最高）**：
   - 所有柔性数组分配前置加 `(SIZE_MAX-HDR)/ELEM` 溢出校验（一次到位盖掉 critical #6–12 与多个 medium）。
   - `codeenv_bin` 全面补边界检查（critical #28–34）。
   - `woort.h:4` 补 `WOORT_VERSION_WRAP` 定义。

2. **第二波（正确性核心）**：
   - VM `CHKDIVILR` 操作数反转（#16）、`ADDI/SUBI/MULI/NEGI` 改无符号运算（#17–19）、`val<<2` 改 `(uint64_t)`（#20）。
   - 跳转/双字指令补 `code_end` 校验（#21–22）、原子槽改 `_Atomic` 存储（#23）。
   - IR 跳转链环检测（#13）、Phase1 失败回滚（#14）。

3. **第三波（并发与生命周期）**：
   - GC worker 灰队列终止竞争（#4）、ctrlc 信号处理器重写（#2）。
   - `m_globalcontext_alive` 原子化、GC 对象 `init_delay_alloc` 前清零、dylib 双重引用（#39）。

4. **第四波（健壮性）**：
   - `assert`-only 校验逐个升级为运行期检查。
   - `atomic.h` `_Generic` 表补 `Size/IntPtr`。
   - 二进制格式端序与版本化。
