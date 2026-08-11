# IR（中间表示）

WooRT 的字节码受操作数范围、跳转范围等一系列制约因素约束。为此我们提供一个类似 AsmJit 风格的 IR 接口——无限可变虚拟寄存器 + Label 显式跳转，开发者通过调用这些 IR API，生成最终的 WooRT 字节码。

IR 接口的声明集中在 [`include/woort.h`](../include/woort.h) 的 `IR Compiler` / `IR Instruction Emission` 两节，内部实现位于 `src/woort_ir_*.c`。

## 概念

* **IRCompiler (`woort_IRCompiler`)**：顶层编译器，管理一系列函数、常量池（constant pool）、静态存储区（static storage），编排 `finish` 流程。
* **IRFunction (`woort_IRFunction`)**：一个待编译的函数，持有虚拟寄存器、Label、线性指令列表以及源码位置栈。
* **CodeEnv (`woort_CodeEnv`)**：概念上类似 Module，由 `woort_IRCompiler_finish()` 产出，包含一系列函数的字节码和它们所需的常量/静态存储区域。详见 [runtime.md](./runtime.md)。
* **ByteCode**：字节码，由 VM 直接执行的指令码，每条 32 位，部分指令带额外 32 位拓展操作数。详见 [opcodes.md](./opcodes.md)。
* **IR**：结构上类似字节码，但是编写时不考虑操作数的寻址/跳转范围，由 `finish` 流程负责整理、优化并生成最终字节码。
* **虚拟寄存器 (`woort_IRValue*`)**：可变的无限虚拟寄存器，可被多次读写。在 `finish()` 阶段通过活跃性分析分配栈槽。
* **Label (`woort_IRLabel*`)**：跳转目标标记。用户通过 `woort_IR_bind` 绑定 Label 到指令流中的某个位置，然后用 `woort_IR_jmp` / `woort_IR_jcc_*` 跳转。

## 设计理念

* **无 SSA/PHI**：虚拟寄存器是可变的，同一个寄存器可以在不同位置被赋不同值。循环变量的合流通过显式 `woort_IR_MOV` 实现。
* **Label + Block 混合模型**：用户接口是线性指令流 + Label + 显式跳转；框架在 `finish()` 时根据 Label 和跳转指令自动切分基本块，用于内部分析。
* **无类型信息**：WooRT 是 Woolang 的运行时，Woolang 的编译器前端负责执行类型检查，保证类型正确。因此 IR 操作不带类型标注，整数/实数/字符串/动态类型有各自独立的指令族。
* **自动指令选择**：compound 指令（如 `CADDI`）、操作数编码宽度（8/16/24/32 位）、跳转方向（`JFWD`/`JBCK`）与编码均由发射层自动处理。
* **常量加载放置**：框架在 `finish()` 阶段通过 dominator 分析将常量加载提升到合适位置，避免重复加载。

## 编译流程总览

```text
woort_IRCompiler_init(&irc)
        │
        │  （可选）woort_IRCompiler_intern_string()  预注册源码路径
        │  （可选）woort_IRCompiler_add_static()      预留静态存储槽
        ▼
woort_IRCompiler_add_function(irc, param_count, captured_count, &f)
        │
        │  woort_IRFunction_new_vreg / get_argument / get_captured
        │  woort_IRFunction_fetch_const(f, cidx)        常量值引用
        │  woort_IRFunction_new_label / push_srcloc / record_local_var
        │  woort_IR_*(f, ...)                           发射指令
        ▼
woort_IRCompiler_add_constant(&irc)                      预留常量槽（每个常量）
        ▼
woort_IRCompiler_finish(&irc, &cenv)                     生成 CodeEnv
        │
        ▼
woort_CodeEnv_lock(cenv) → woort_CodeEnv_set_const_*(...) → woort_CodeEnv_unlock(cenv)
        填充常量池的实际值
```

## 核心 API

### 编译器与函数

```c
woort_IRCompiler irc;
woort_IRCompiler_init(&irc);

/* 在 add_function 之前先预留常量/静态槽（按需，顺序无关） */
woort_IRConstantIndex ci = woort_IRCompiler_add_constant(&irc);
woort_IRStaticIndex   si = woort_IRCompiler_add_static(&irc);

/* 新增函数：param_count = 参数个数，captured_count = 闭包捕获个数 */
woort_IRFunction* f;
woort_IRCompiler_add_function(&irc, param_count, captured_count, &f);

/* 源码路径字符串需先 intern，再用于 push_srcloc */
const char* path = woort_IRCompiler_intern_string(&irc, "foo.wo");
```

> **签名变更**：`woort_IRCompiler_add_function` 现在需要 **三个** 参数 `(c, param_count, captured_count, &out_f)`。闭包函数需要通过 `captured_count` 声明捕获数量，随后用 `woort_IRFunction_get_captured(f, idx)` 获取每个 upvalue 对应的虚拟寄存器。

### 虚拟寄存器与常量

```c
woort_IRValue* v   = woort_IRFunction_new_vreg(f);          /* 可变虚拟寄存器 */
woort_IRValue* arg = woort_IRFunction_get_argument(f, 0);    /* 参数，预分配到 SB+3+idx */
woort_IRValue* upv = woort_IRFunction_get_captured(f, 0);    /* 闭包捕获，预分配到 SB-(1+idx) */

/* 常量值：返回绑定到 G[idx] 的 const IRValue*，同一 idx 返回同一指针（天然去重）。
 * 它代表不可变常量，不应作为指令的 dst。 */
const woort_IRValue* c = woort_IRFunction_fetch_const(f, ci);
```

虚拟寄存器有三种来源（`woort_IRValue_Source`）：

| 来源 | 创建方式 | 栈位置 | 可变性 |
|------|----------|--------|--------|
| `VREG` | `new_vreg` | `finish()` 阶段分配 | 可变 |
| `ARGUMENT` | `get_argument` | `SB+3+idx`（固定） | 只读 |
| `CONST` | `fetch_const` | 绑定 `G[idx]` | 不可变 |

### 指令发射

所有 `woort_IR_*` 发射函数将一条 `woort_IROp` 追加到函数的线性指令列表，返回 `bool`，`false` 表示 OOM。命名约定：**指令族用大写**（`woort_IR_ADDI`、`woort_IR_CALLNWO`），**控制流用小写**（`woort_IR_jmp`、`woort_IR_jcc_*`、`woort_IR_bind`）。

```c
/* 数据移动 */
woort_IR_MOV(f, dst, src);            /* dst = src              */
woort_IR_LOAD(f, dst, si);            /* dst = Static[si]       */
woort_IR_STORE(f, si, src);           /* Static[si] = src       */

/* pvalue 指针（见 opcodes.md §2.2 / §15.4）*/
woort_IR_LOADPVALUE(f, dst, ptr);     /* dst = *ptr.m_pvalue    */
woort_IR_STOREPVALUE(f, ptr, src);    /* *ptr.m_pvalue = src（带写屏障）*/
woort_IR_MKPVALUE(f, dst, src);       /* 分配 GC 盒：dst.m_pvalue -> new box; *box = src */

/* 栈操作 */
woort_IR_PUSHCHK(f, src);             /* 压栈（带溢出检查）       */
woort_IR_PUSHSTATICCHK(f, si);        /* 压入静态值（带检查）     */
woort_IR_POP(f, dst);                 /* dst = pop              */
woort_IR_POPR(f, count);              /* 弹出并丢弃 count 个     */
woort_IR_POPRS(f, count_src);         /* 弹出数量由寄存器决定     */

/* 算术（整数族 / 实数族 / 字符串族） */
woort_IR_ADDI(f, dst, a, b);          /* dst = a + b            */
woort_IR_SUBI / MULI / DIVI / MODI / NEGI(f, ...)
woort_IR_ADDR / SUBR / MULR / DIVR / MODR / NEGR(f, ...)
woort_IR_ADDS(f, dst, a, b);          /* 字符串连接              */

/* 比较（结果为 0/1 整数） */
woort_IR_LTI / GTI / LEI / GEI / EQI / NEI(f, dst, a, b)
woort_IR_LTR / GTR / LER / GER / EQR / NER(f, dst, a, b)
woort_IR_LTS / GTS / LES / GES / EQS / NES(f, dst, a, b)

/* 逻辑 */
woort_IR_LAND / LOR(f, dst, a, b)
woort_IR_LNOT(f, dst, src);

/* 类型转换 */
woort_IR_ITOR / ITOS(f, dst, src);    /* int → real / string   */
woort_IR_RTOI / RTOS(f, dst, src);    /* real → int / string   */

/* 除法安全检查（与 DIVI/MODI 配合使用） */
woort_IR_CHKDIVIL(f, src);            /* 检查左操作数为 INT64_MIN */
woort_IR_CHKDIVIR(f, src);            /* 检查右操作数为 0 或 -1  */
woort_IR_CHKDIVIRZ(f, src);           /* 检查右操作数为 0        */
woort_IR_CHKDIVILR(f, l, r);          /* 同时检查左右           */

/* 函数调用 */
woort_IR_PUSHCHK(f, arg0);
woort_IR_PUSHCHK(f, arg1);
woort_IR_CALLNWO(f, cidx_target, argc, dst);  /* 调用脚本函数 */
woort_IR_CALLNFP(f, cidx_target, argc, dst);  /* 调用原生函数 */
woort_IR_CALLNJIT(f, cidx_target, argc, dst); /* 调用 JIT 函数 */
woort_IR_CALL(f, callee_vreg, argc, dst);     /* 间接调用（vreg 持有函数值） */

/* 控制流 */
woort_IR_bind(f, L);                  /* 绑定 Label             */
woort_IR_jmp(f, L);                   /* 无条件跳转             */
woort_IR_jcc(f, cond, L);             /* if (cond != 0) goto L */
woort_IR_jccz(f, cond, L);            /* if (cond == 0) goto L */
woort_IR_jcc_lt(f, a, b, L);          /* if (a <  b) goto L    */
woort_IR_jcc_le(f, a, b, L);          /* if (a <= b) goto L    */
woort_IR_jcc_eq(f, a, b, L);          /* if (a == b) goto L    */
woort_IR_jcc_gt(f, a, b, L);          /* if (a >  b) goto L    （a/b 交换的 lt）*/
woort_IR_jcc_ge(f, a, b, L);          /* if (a >= b) goto L    （a/b 交换的 le）*/
woort_IR_jcc_ne(f, a, b, L);          /* if (a != b) goto L    （参数交换 + 取反）*/
woort_IR_ret(f, val);                 /* return val            */
woort_IR_ret_void(f);                 /* return void           */
```

> **关于 `jcc_gt/ge/ne`**：它们是语法糖。`finish` 阶段会把 `gt(a,b)` 改写为 `lt(b,a)`、`ge(a,b)` 改写为 `le(b,a)`、`ne(a,b)` 改写为参数交换并取反的形式，最终复用硬件友好的 `JFWD*`/`JBCK*` 比较跳转指令。

### 动态类型 / 装箱

```c
/* 装箱：把标量值装箱为 woort_DynBox（T = INT/REAL/BOOL/GCUNIT） */
woort_IR_BOXDYN(f, dst, type, src);
woort_IR_PUSHBOXDYN(f, type, src);         /* 装箱并压栈 */

/* 拆箱：类型不匹配时抛异常 */
woort_IR_UNBOXDYN(f, dst, type, src);

/* 检查：结果为 0/1 整数 */
woort_IR_CHECKDYN(f, dst, type, src);

/* 装箱类型的转换（CASTX 指令族，详见 opcodes.md §10） */
woort_IR_CASTSTO(f, dst, type, src);       /* 字符串 → 标量 */
woort_IR_CASTSFROM(f, dst, type, src);     /* 标量 → 字符串 */
woort_IR_CASTDYN(f, dst, type, src);       /* 动态类型间转换 */
woort_IR_ASSERTDYN(f, type, src);          /* 断言动态类型，否则 panic */
```

### 容器与闭包

```c
/* 创建向量/映射/结构体/联合：
 * 先用 PUSHCHK 依次压入元素，再调用对应的 MK* 指令。
 * - MKVEC:   count 个元素
 * - MKMAP:   count*2 个值（键值交替）
 * - MKSTRUCT: count 个字段
 * - MKUNION: id(整数) + 1 个负载值 */
woort_IR_MKVEC(f, dst, count);
woort_IR_MKMAP(f, dst, count);
woort_IR_MKSTRUCT(f, dst, count);
woort_IR_MKUNION(f, dst, id, src);

/* 创建闭包：先压入捕获值，再调用。
 * cidx_target 指向常量池中的脚本函数字节码入口。*/
woort_IR_MKCLOSURE(f, dst, cidx_target, capture_count);
```

### 索引读写

WooRT 为不同的「容器 × 键类型 × 值是否装箱」组合提供独立的 IR 指令，命名规律为：

* `LDID<容器>` —— 加载（读）：`LDIDVEC`、`LDIDVECX`（读到 DynBox）、`LDIDSTRUCT`、`LDIDSTRING`、`LDIDDICT{I,R,B,X}`、`LDIDDICT{I,R,B,X}X`。
* `STID<容器><键类型><值类型>` —— 存储（写）：`STIDVEC{I,R,B,X}`、`STIDDICT<键><值>`、`STIDMAP<键><值>`（不存在则插入）、`SDIDSTRUCT`。

其中 `I/R/B/X` 分别表示整数/实数/布尔/动态（DynBox）键或值。完整列表见 [opcodes.md §16–§17](./opcodes.md)。

```c
woort_IR_LDIDVEC(f, dst, idx, vec);
woort_IR_STIDVECI(f, vec, idx, val);   /* 写整数到向量 */
woort_IR_STIDMAPII(f, map, ikey, ival); /* 写整数到映射（键为整数，不存在则插入） */
```

### 解包与字段压栈

```c
/* 解包向量：把前 count 个元素按逆序压栈 */
woort_IR_UNPACKVEC(f, count, vec);
woort_IR_UNPACKVECX(f, count, vec);       /* 解包为 DynBox */
woort_IR_UNPACKVECALL(f, dst_count, vec); /* 全部解包，元素数写入 dst_count */
woort_IR_UNPACKVECXALL(f, dst_count, vec);

/* 读取结构体字段并装箱/压栈 */
woort_IR_PUSHIDSTRUCT(f, field_idx, st);      /* 原样压栈（woort_Value） */
woort_IR_PUSHIDSTBOXI(f, field_idx, st);      /* 装箱为整数 */
woort_IR_PUSHIDSTBOXR(f, field_idx, st);      /* 装箱为实数 */
woort_IR_PUSHIDSTBOXB(f, field_idx, st);      /* 装箱为布尔 */
```

### 变长参数收集

变长参数函数把多余的实参打包成向量：

```c
/* 从 SB+3+skip 开始收集剩余参数，打包为向量写入 dst。
 * skip = 固定参数个数；实际变长参数数存在 SB+3 处。*/
woort_IR_PACKARG(f, dst, skip);
```

### 原子操作与一次性初始化

静态存储区（`Static[idx]`）可用作原子变量，支持 release-store / acquire-load / CAS：

```c
woort_IR_ASTORE(f, si, src);    /* atomic store（release） */
woort_IR_ALOAD(f, dst, si);     /* atomic load（acquire）  */
woort_IR_CAS(f, si, expected, desired);  /* CAS：si==expected 则写 desired */
```

线程安全的「一次性初始化」可用 `woort_IR_jifinited`。它把 `Static[cond_idx]` 当作三态标志（0=未初始化、1=初始化中、2=已完成）：

* `flag == 2`：跳转到 `target`；
* `flag == 0`：CAS 0→1 成功则顺序执行（调用方随后发射初始化代码）；
* `flag == 1` 或 CAS 失败：自旋（带 GC 检查点）直到 `flag == 2`，然后跳转。

```c
woort_IR_jifinited(f, cond_idx, target);
```

### 陷阱与 Panic

```c
woort_IR_debugtrap(f);          /* 发射 DEBUGTRAP，进入调试器 */
woort_IR_panic(f, msg);         /* 带字符串消息 panic，触发 ABORT */
```

## 源码位置（srcloc）

IR 支持把源码位置关联到每条指令，用于调试与错误定位。编译期维护一个**源码位置栈**：

```c
const char* path = woort_IRCompiler_intern_string(&irc, "fib.wo");

woort_IRFunction_push_srcloc(f, path,
    /*begin_line=*/10, /*begin_column=*/5,
    /*end_line=*/10,   /*end_column=*/20);
    woort_IR_ADDI(f, sum, r1, r2);   /* 关联到上面的 srcloc */
woort_IRFunction_pop_srcloc(f);
```

* 栈为空时发射的指令无源码信息（`m_srcloc_index = WOORT_SRCLOC_INVALID_INDEX`）。
* `finish()` 阶段会把每个函数的源码映射合并到 `CodeEnv` 的 PDB 中，可通过 `woort_CodeEnv_find_srcloc_by_offset()` 查询。

局部变量名可通过 `woort_IRFunction_record_local_var(f, name, ir_value)` 记录，`finish` 后转移到 PDB 供调试器显示。

## 示例代码：递归 Fibonacci

```c
woort_IRCompiler irc;
woort_IRCompiler_init(&irc);

woort_IRConstantIndex c2   = woort_IRCompiler_add_constant(&irc);
woort_IRConstantIndex c1   = woort_IRCompiler_add_constant(&irc);
woort_IRConstantIndex cfib = woort_IRCompiler_add_constant(&irc);
woort_IRConstantIndex cn   = woort_IRCompiler_add_constant(&irc);

/* ====== func fib(n: int) => int ====== */
woort_IRFunction* f_fib;
woort_IRCompiler_add_function(&irc, /*param_count=*/1, /*captured_count=*/0, &f_fib);
{
    woort_IRValue* n_arg = woort_IRFunction_get_argument(f_fib, 0);
    const woort_IRValue* v2 = woort_IRFunction_fetch_const(f_fib, c2);
    const woort_IRValue* v1 = woort_IRFunction_fetch_const(f_fib, c1);
    woort_IRValue* tmp1 = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* tmp2 = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* r1   = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* r2   = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* sum  = woort_IRFunction_new_vreg(f_fib);

    woort_IRLabel* L_base = woort_IRFunction_new_label(f_fib);

    woort_IR_jcc_lt(f_fib, n_arg, v2, L_base);  /* if (n < 2) goto base */

    /* 递归分支 */
    woort_IR_SUBI(f_fib, tmp1, n_arg, v1);       /* tmp1 = n - 1 */
    woort_IR_SUBI(f_fib, tmp2, n_arg, v2);       /* tmp2 = n - 2 */

    woort_IR_PUSHCHK(f_fib, tmp1);
    woort_IR_CALLNWO(f_fib, cfib, 1, r1);        /* r1 = fib(n-1) */

    woort_IR_PUSHCHK(f_fib, tmp2);
    woort_IR_CALLNWO(f_fib, cfib, 1, r2);        /* r2 = fib(n-2) */

    woort_IR_ADDI(f_fib, sum, r1, r2);           /* sum = r1 + r2 */
    woort_IR_ret(f_fib, sum);

    /* 基本情况：return n */
    woort_IR_bind(f_fib, L_base);
    woort_IR_ret(f_fib, n_arg);
}

/* ====== func main() => void ====== */
woort_IRFunction* f_main;
woort_IRCompiler_add_function(&irc, /*param_count=*/0, /*captured_count=*/0, &f_main);
{
    const woort_IRValue* vn = woort_IRFunction_fetch_const(f_main, cn);
    woort_IRValue* result = woort_IRFunction_new_vreg(f_main);

    woort_IR_PUSHCHK(f_main, vn);
    woort_IR_CALLNWO(f_main, cfib, 1, result);
    woort_IR_ret(f_main, result);
}

woort_CodeEnv* cenv;
woort_IRCompiler_finish(&irc, &cenv);

/* 填充常量池（必须在 lock/unlock 之间） */
woort_CodeEnv_lock(cenv);
woort_CodeEnv_set_const_int(cenv, c2, 2);
woort_CodeEnv_set_const_int(cenv, c1, 1);
woort_CodeEnv_set_const_script_function(cenv, cfib, cenv->m_code_begin + f_fib->m_code_offset);
woort_CodeEnv_set_const_int(cenv, cn, 10);
woort_CodeEnv_unlock(cenv);

/* 执行（详见 runtime.md） */
woort_VMRuntime* vm;
woort_VMRuntime_create(&vm);
woort_VMRuntime_swap(vm);
/* ... 通过 woort_bootup / woort_invoke 调用 main ... */
woort_VMRuntime_swap(NULL);
woort_VMRuntime_destroy(vm);
```

## 示例代码：循环求和（MOV 替代 PHI）

```c
/*
 * func sum_1_to_n(n: int) => int {
 *     i = 1; acc = 0;
 *     while (i <= n) { acc += i; i += 1; }
 *     return acc;
 * }
 */
woort_IRFunction* f;
woort_IRCompiler_add_function(&irc, /*param_count=*/1, /*captured_count=*/0, &f);
{
    woort_IRValue* n_arg = woort_IRFunction_get_argument(f, 0);
    const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);  /* 常量 0 */
    const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);  /* 常量 1 */
    woort_IRValue* i   = woort_IRFunction_new_vreg(f);   /* 可变：循环计数器 */
    woort_IRValue* acc = woort_IRFunction_new_vreg(f);   /* 可变：累加器 */

    woort_IRLabel* L_header = woort_IRFunction_new_label(f);
    woort_IRLabel* L_exit   = woort_IRFunction_new_label(f);

    /* 初始化循环变量 */
    woort_IR_MOV(f, i, v1);          /* i = 1   */
    woort_IR_MOV(f, acc, v0);        /* acc = 0 */

    /* 循环头 */
    woort_IR_bind(f, L_header);
    woort_IR_jcc_gt(f, i, n_arg, L_exit);  /* if (i > n) goto exit */

    /* 循环体 */
    woort_IR_ADDI(f, acc, acc, i);        /* acc += i */
    woort_IR_ADDI(f, i, i, v1);           /* i += 1   */
    woort_IR_jmp(f, L_header);

    /* 退出 */
    woort_IR_bind(f, L_exit);
    woort_IR_ret(f, acc);
}
```

## `finish()` 内部处理流程

`woort_IRCompiler_finish()` 对每个函数执行以下分析与代码生成：

1. **源码位置去重**：把编译期 `push_srcloc` 产生的 `woort_SourceLocation` 去重合并到 `m_source_locations`。
2. **Label → Block 切分**：根据 Label 绑定点和跳转指令自动切分基本块，构建 CFG。
3. **活跃性分析**：标准的迭代数据流分析（USE/DEF → LIVE_IN/LIVE_OUT）。`CONST` 源的 vreg 不参与 bitset 活跃性。
4. **常量直连标记**：检查 `CONST` vreg 是否仅被一条支持直连的指令（`PUSHCHK`/`RET`/`CALL`）使用，标记为 `m_is_const_direct`（发射 `PUSHCCHK`/`RETVC`/`CALLC`）。
5. **栈槽分配**：线性扫描算法，活跃区间不重叠的普通 vreg 共享栈槽。`CONST` vreg 跳过。
6. **Dominator 分析 + 循环检测**：Cooper-Harvey-Kennedy 算法构建支配树，检测自然循环。
7. **常量加载放置**：为非 const_direct 的 `CONST` vreg 分配独立栈槽，将 `LOAD` 放置到使用点的公共支配者处；循环内的常量加载提升到循环外。
8. **字节码发射**：逐指令翻译 IR → 字节码，自动选择 compound 指令和最优编码宽度。`CONST` vreg 自动产生 `PUSHCCHK`/`RETVC`/`LOAD`。
9. **跳转修补**：解析 Label 地址，选择 `JFWD`/`JBCK` 编码，处理 offset overflow 扩展。

## 注意事项

* `fetch_const` 返回的 const IRValue* 代表不可变常量，**不应**作为指令的 `dst`。可变变量用 `new_vreg` 创建，通过 `woort_IR_MOV` 从常量初始化。
* 同一 `const_index` 多次调用 `fetch_const` 返回相同指针（天然去重）。
* 常量值的加载时机由框架自动决定：单次用于 `PUSHCHK`/`RET`/`CALL` 时直接特化指令；多次使用时在最优位置放置 `LOAD`。
* `CALLNWO/CALLNFP/CALLNJIT/CALL` 的 `argc` 表示调用前已压入栈的实参数量，用于生成 `RESULT`/`POPR` 清理。
* 如果调用的 `dst` 为 `NULL`，则不生成 `RESULT`，直接 `POPR`。
* 所有指令发射函数返回 `bool`，`false` 表示 OOM。
* `finish()` 后，每个函数的 `m_code_offset` / `m_code_length` 可用，用于定位它在 `CodeEnv->m_code_begin` 中的位置。
* 常量池的实际值要在 `finish()` 之后、在 `woort_CodeEnv_lock()/unlock()` 之间通过 `woort_CodeEnv_set_const_*` 填充。
