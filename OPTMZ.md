# WooRT JIT / IR 生成性能 Review

## 架构概览（先说清楚做对的地方）

- **栈机模型**：所有操作数以 `[SB + slot*8]` 寻址，VM 指针 `m_vm/m_sb/m_sp/m_stack/m_stack_end` 作为持久虚寄存器横跨整个 asmjit 函数，分配器将其固定在 callee-saved 寄存器中——这是正确的。
- **检查点（request polling）只发在后向跳转与原生调用之后**，并非逐指令（`woort_jit.c:148-155` 的 pre/post_dispatch 只绑 label，不插检查）——正确。
- **慢路径（栈溢出/重同步/检查点）共用并下沉到 epilogue**，经 `JumpAnnotation` + 间接跳转返回——热路径极轻。
- **IR 侧**已具备：跳转合并（Phase 0）、死块消除（Phase 1b）、noop 跳转删除（Phase 2b）、常量直连 `PUSHCCHK/RETVC`（Phase 3b）、支配树 + 自然循环检测（Phase 4a/4b）、**常量加载按支配者放置并循环外提**（Phase 4c）、线性扫描栈槽复用（Phase 3）。这套 IR pipeline 对解释器是相当扎实的。
- **GC 写屏障是条件化的**（查 `woomem_gc_marking_state_flag`），不是无条件调用。
- **CAS 用 `lock cmpxchg`**；整数转浮点（`ITOR/RTOI`）内联 `cvtsi2sd/cvttsd2si`；实数算术内联 SSE2。

下面是**可以优化运行时性能**的点，按影响排序。

---

## Part A — JIT 生成代码（影响最大）

### A1【高】所有操作数在指令间经 SB 栈帧做内存往返，无跨指令寄存器缓存
`woort_jit_x64_impl.cpp:387-400`

```cpp
Gp load_stack_gp(...) { Gp reg = c->new_gp64(); mov(reg, sb_slot(src)); return reg; }  // 每次新建虚寄存器并从内存载入
void store_stack(...) { mov(sb_slot(dst), v); }                                          // 每次写回内存
```

`ADDI t,a,b ; ADDI u,t,c` 会生成 `load a; add [b]; store t; load t; add [c]; store u`。`t` 刚算出就被存回内存、下条指令又立刻重载。因为每条 opcode 都 `new_gp64()` 申请新虚寄存器，asmjit 分配器看不到跨指令的虚寄存器连续性，无法做 load elimination。`SUBI/MULI/NEGI/所有比较/MOV/LOADPVALUE/ITOR` 全是此模式。

**优化**：引入 TOS（栈顶）缓存或局部位值编号——让上一条指令的 `dst` 直接以虚寄存器形式流入下一条的 `src`。即使只缓存栈顶一个槽，也能消掉绝大部分冗余 mov。**这是单项收益最大的改动。**

### A2【高】比较产生者与条件分支消费者未融合
`woort_jit_x64_impl.cpp:3475-3557`（LTI 等）对比 `2464-2540`（JFWDNZ 等）

`LTI r,a,b; JFWDNZ r,off` 当前发出：`xor; cmp; setl; mov[r]`（物化布尔到栈）→ `cmp[r],0; jnz`（重载再测）。本应是 2 条 `cmp; jl`。注意 JIT **已有**融合形式 `JFWDLT/...`（`2580-2702`），但它们仍各自 `load_stack_gp`，且不与前置比较指令的 `cmp` 标志复用。

**优化**：JIT 层做 `CMPcc; Jcc-on-result` 超指令窥孔；或确保 IR/前端优先发射融合的 `JCC_LT` 形式（见 B1）。

### A3【高】`LDIDVEC`/`UNPACKVEC` 每个元素都调用 `woort_JIT_unbox_dyn_no_check`
`woort_jit_x64_impl.cpp:4294-4304`、`5158-5174`

非 X 变体对每个元素 `invoke` 一次外联函数（asmjit 会发完整 caller-save spill/reload）。`UNPACKVEC n` 就是 n 次调用。而 X 变体是直接拷贝位，不走调用。

**优化**：把 `woort_DynBox_unbox_no_check`（一个小的 tag 解码：移位/掩码）内联进 JIT。集合密集代码收益明显。

### A4【中】GC 写屏障每次重料化标志地址
`woort_jit_x64_impl.cpp:760-795`（STORE）、`805-835`（STOREPVALUE）、`5074-5115`（STIDSTRUCT）

每次 store 都 `mov flag_ptr, imm64(&woomem_gc_marking_state_flag)`（10 字节立即数）再 `cmp byte[flag_ptr],0`。屏障本身条件化是对的，但地址每次重料化。

**优化**：函数入口把标志地址载入一个常驻虚寄存器，各处引用 `byte_ptr(flag_reg)`；更进一步可快照标志值并在 GC 标记开启时重定位 patch（许多 JIT 的做法）。

### A5【中】`DIVI/MODI` 用内存操作数的 `idiv`，且 `CHKDIVI*` 重复载入除数
`woort_jit_x64_impl.cpp:3435-3461`（DIVI/MODI）、`5760-5835`（CHKDIVIR 等）

`idiv r64, m64` 比寄存器形式 uops 多得多；且前置 `CHKDIVIR` 先 `load_stack_gp(src)`，DIVI 又再从 `[sb+src*8]` 载一次——除数被载两次。

**优化**：除数载入寄存器一次 `idiv r,r`；把 `CHKDIVI*` 守卫折进同一发射序列。

### A6【中】`NEGR` 每次料化 64 位符号掩码
`woort_jit_x64_impl.cpp:3628-3643`

每次 `mov r64, imm64(INT64_MIN)`（10 字节）。

**优化**：prologue 预置常量到专用虚寄存器；或用 XMM 常量 `pxor`。

### A7【中】`MODR/CMODR` 无快速路径，总是 `invoke fmod`
`woort_jit_x64_impl.cpp:3605-3626`、`4157-4179`

**优化**：常见情形内联（`roundsd` 截断 → `a - trunc(a/b)*b`），仅在超大商/Inf/NaN 时 fallback。

### A8【低-中】逐 push 栈溢出检查未在 JIT 内合并
`woort_jit_x64_impl.cpp:847-948`（PUSHRCHK/PUSHSCHK/PUSHCCHK/ASSURESSZ）

字节码层的 `PUSHRCHK n`/`ASSURESSZ n` 已批量（好），但连续多个 `PUSH*CHK` JIT 不合并。

**优化**：在 JIT 线性流上识别连续 `PUSH*CHK` run，按总增量合并为一次 `cmp; jae`。

### A9【低】`EQS/NES` 即使指针相等也无条件调用 `GCString_compare`
`woort_jit_x64_impl.cpp:3875-3942`

**优化**：先 `cmp a,b; je L_true` 指针短路，再 fallback 到 compare。

### A10【低】`MOV/LOADPVALUE/RESULT` 未做 src==dst 消除
`woort_jit_x64_impl.cpp:837-843` 等

**优化**：发射时若 `dst==src`（编译期已知）直接跳过。

### A11【低】静态存储访问每次料化 64 位绝对地址
`woort_jit_x64_impl.cpp:960-984`（PUSHC）、`748-758`（LOAD）、`5651-5733`（ALOAD/CAS）

**优化**：prologue 把 `m_cenv_static_storage` 基址载入常驻寄存器，引用变为 `[base + idx*8]`。

---

## Part B — IR 生成（同时影响解释器与 JIT，因为 JIT 编译的是 IR 产出的字节码）

### B1【高】缺少"比较 + 条件跳转"融合窥孔
字节码层**已存在**融合形式 `JFWDLT/GT/EL/EG`、`JFWDEQ/NEQ`（`woort_opcode_dispatcher.c:343-371`、`300-311`）。IR 也提供了 `JCC_LT/LE/EQ/NE`（`woort_ir_op.h:197-202`）。但 IR 编译器的分析 pipeline（`woort_ir_function.c:1846-1908` 的 Phase 0/1/1b/2b/3b/4a/4b/4c/2/3）**没有任何 pass** 把 `LTI r,a,b ; JCC r,L` 折成 `JCC_LT a,b,L`。

这完全依赖 Woolang 前端是否调用了 `woort_IR_jcc_lt`。若前端走"算比较→JCC"两步，则：
- 解释器多执行 1 条字节码 + 多一次栈物化/重载；
- JIT 触发 A2 问题。

**优化**：在 IR 加一个轻量窥孔 pass，识别 `比较op(dst) ; JCC/JCCZ(dst==比较结果, dst此后不再使用)` → 改写为对应 `JCC_LT/LE/EQ/NE`（比较为 EQ/NE 时改 EQ/NE；LT/LE 时需判类型族 int/real/string）。同时删掉被吸收的比较指令。这对两条执行路径都受益。

### B2【中】无拷贝传播 / 局部 move 消除
线性扫描已能让非重叠区间共享槽（`woort_ir_function.c:1172-1212`），且 `MOV` 发射在 `src_f==dst_f` 时跳过（`woort_ir_compiler.c:496-497`）。但没有拷贝传播：`MOV d,s; ...use d; d死`（s 仍活）仍会发 mov，本可把后续对 d 的使用改指 s。

**优化**：在活到死的简单情形做局部拷贝传播/合并，消除更多 mov（对解释器尤其有用，每条 mov 是一条字节码）。

### B3【中】比较/分支融合后比较结果槽的常量直连未覆盖
`Phase 3b`（`woort_ir_function.c:876-933`）的 const_direct 只认 `MOV/PANIC/PUSHCHK/RET`。若 `JCC_LT a,b,L` 中 a 或 b 是常量且仅此一处用，目前不会走"直连"。不过当前 `JCC_LT` 操作数是栈槽，需要先 `LOAD`——可在发射层让 `JCC_*` 的常量操作数走内联立即数比较（若字节码支持），减少一次 LOAD。

### B4【低】`_get_fact_offset` 的 -126 陷阱偏移
`woort_ir_compiler.c:79-84`：偏移 ≤ -126 时额外 -3 以避开临时槽 -126/-127/-128。当栈槽数逼近 126 时，会把这些槽推到 S16 区，使后续寻址退化为更宽编码（解释器略慢、JIT 也可能需 `MOVLD` 中转）。

**优化**：线性扫描分配时尽量把"高频"槽留在 S8 范围内（按使用频次启发式赋低槽号），而非纯按首次定义顺序。属锦上添花。

### B5【低】活跃区间用半指令粒度（5 点/指令）+ 逆序 use 点排序
`woort_ir_function.c:1000-1023`：DEF=`5i+4`，USE=`5i+(3-s)`（逆序）。注释说"期待优先复用首个读操作数以便计算指令能用更快寻址特化版本"——意图是好的，但这只是顺序启发式，并未结合实际复合指令选择（`_EMIT_BINOP_COMMUTATIVE` 里 `dst==src` 判断的是 `m_assigned_stack_offset` 相等，而复用同槽主要靠线性扫描）。可考虑在分配后做一轮"把 dst 尽量分配到与某个 src 同槽"的合并，直接喂给复合形式（CADDI 等），减少发射的三地址形式。

---

## 优先级总表

| 优先级 | 项 | 位置 | 预估热循环收益 | 改动量 |
|---|---|---|---|---|
| 1 | **A1** TOS/跨指令值缓存，消除 SB 内存往返 | `jit_x64_impl.cpp:387-400` | 非常高（几乎所有算术/比较/mov） | 中 |
| 2 | **B1** IR 窥孔融合 比较+JCC→JCC_LT | `ir_function.c`（新增 pass） | 高（解释器+JIT 双赢） | 低-中 |
| 3 | **A3** 内联 `unbox_dyn_no_check` | `jit_x64_impl.cpp:4294-4304` 等 | 高（集合密集） | 低 |
| 4 | **A2** JIT 层 cmp+setcc→jcc 融合（与 B1 互补） | `jit_x64_impl.cpp:3475-3557` | 高 | 低 |
| 5 | **A4** 缓存 GC 标志地址/快照 | `jit_x64_impl.cpp:760-795` | 中（结构体/字典写） | 低 |
| 6 | **A5** `idiv` 寄存器操作数 + 除数单次载入 | `jit_x64_impl.cpp:3435-3461` | 中（整除密集） | 低 |
| 7 | **A7** `fmod` 快速路径 | `jit_x64_impl.cpp:3605-3626` | 中（实数模） | 中 |
| 8 | **A6** NEGR 符号掩码常量外提 | `jit_x64_impl.cpp:3628-3643` | 低-中 | 低 |
| 9 | **A8** 合并连续 push 栈检查 | `jit_x64_impl.cpp:847-948` | 低-中 | 低 |
| 10 | **A11** 缓存 static_storage 基址 | `jit_x64_impl.cpp:748+` | 低 | 低 |
| 11 | **B2/B5** IR 拷贝传播 + dst-src 合槽 | `ir_function.c` | 低-中（解释器） | 中 |

---

## 结论

**单项最高收益是 A1**：当前 JIT 把每个操作数都钉在 `[SB+off]` 上做 load/store 往返，asmjit 的寄存器分配被"每条 opcode 新建短命虚寄存器"完全架空。引入哪怕一个最小的 TOS/局部值编号缓存，让结果经寄存器流向下一条指令，就能消掉绝大多数冗余内存操作。

**第二高是 B1**：它对解释器（少一条字节码）和 JIT（少一次物化+重测）同时有益，且改动小、风险低——一个识别"比较 + 单用 JCC"的 IR 窥孔即可。
