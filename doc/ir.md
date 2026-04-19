# IR

由于 WooRT 的字节码需要考虑操作数范围、跳转范围等一系列制约因素，我们提供了一个类似 AsmJit 风格的 IR 接口——无限可变虚拟寄存器 + Label 显式跳转，开发者通过调用这些 IR API，生成最终的 WooRT 字节码。

## 概念

* **CodeEnv**：`woort_CodeEnv`，概念上类似 Module，包含一系列函数的字节码和它们所需的常量/静态存储区域。
* **ByteCode**：字节码，由 `woort_VMRuntime` 直接执行的指令码，大部分字节码都是 4 字节的，有一部分字节码指令带有额外的 4 字节拓展操作数。
* **IR**：结构上类似字节码，但是编写时不考虑操作数的寻址/跳转范围，由字节码构建流程负责整理和优化最终代码生成。
* **虚拟寄存器 (`woort_IRValue*`)**：可变的无限虚拟寄存器，可被多次读写。在 `finish()` 阶段通过活跃性分析分配栈槽。
* **Label (`woort_IRLabel*`)**：跳转目标标记。用户通过 `woort_IR_bind` 绑定 Label 到指令流中的某个位置，然后用 `woort_IR_jmp` / `woort_IR_jcc_*` 跳转。

## 设计理念

* **无 SSA/PHI**：虚拟寄存器是可变的，同一个寄存器可以在不同位置被赋不同值。循环变量的合流通过显式 `woort_IR_MOV` 实现。
* **Label + Block 混合模型**：用户接口是线性指令流 + Label + 显式跳转；框架在 `finish()` 时根据 Label 和跳转指令自动切分基本块，用于内部分析。
* **无类型信息**：WooRT 是 Woolang 的运行时，Woolang 的编译器前端负责执行类型检查，保证类型正确。
* **自动指令选择**：compound 指令（如 `CADDI`）、操作数编码宽度、跳转方向/编码自动处理。
* **常量加载放置**：框架在 `finish()` 阶段通过 dominator 分析将常量加载提升到合适位置，避免重复加载。

## IR 接口风格

### 核心 API

```c
/* 编译器 */
woort_IRCompiler irc;
woort_IRCompiler_init(&irc);

woort_IRConstantIndex ci = woort_IRCompiler_add_constant(&irc);
woort_IRStaticIndex si = woort_IRCompiler_add_static(&irc);

woort_IRFunction* f;
woort_IRCompiler_add_function(&irc, param_count, &f);

/* 虚拟寄存器 */
woort_IRValue* v = woort_IRFunction_new_vreg(f);
woort_IRValue* arg = woort_IRFunction_get_argument(f, 0);

/* 常量值（返回绑定到 G[idx] 的 IRValue*，同一 idx 返回同一指针） */
const woort_IRValue* c = woort_IRFunction_fetch_const(f, ci);

/* Label */
woort_IRLabel* L = woort_IRFunction_new_label(f);

/* 指令发射（全部返回 bool，false 表示 OOM） */
woort_IR_MOV(f, dst, src);                 /* dst = src */
woort_IR_ADDI(f, dst, a, b);              /* dst = a + b */
woort_IR_SUBI(f, dst, a, b);              /* dst = a - b */
woort_IR_PUSHCHK(f, src);                 /* 压栈（带栈检查） */
woort_IR_CALLNWO(f, target, argc, dst);   /* 调用脚本函数 */
woort_IR_CALLNFP(f, target, argc, dst);   /* 调用原生函数 */
woort_IR_POPR(f, count);                  /* 弹栈 n 个 */

/* 控制流 */
woort_IR_bind(f, L);                       /* 绑定 Label */
woort_IR_jmp(f, L);                        /* 无条件跳转 */
woort_IR_jcc(f, cond, L);                 /* if (cond != 0) goto L */
woort_IR_jcc_lt(f, a, b, L);             /* if (a < b) goto L */
woort_IR_jcc_le(f, a, b, L);             /* if (a <= b) goto L */
woort_IR_jcc_gt(f, a, b, L);             /* if (a > b) goto L */
woort_IR_jcc_ge(f, a, b, L);             /* if (a >= b) goto L */
woort_IR_jcc_eq(f, a, b, L);             /* if (a == b) goto L */
woort_IR_jcc_ne(f, a, b, L);             /* if (a != b) goto L */
woort_IR_ret(f, val);                      /* return val */
woort_IR_ret_void(f);                      /* return void */

/* 编译完成 */
woort_CodeEnv* cenv;
woort_IRCompiler_finish(&irc, &cenv);
```

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
woort_IRCompiler_add_function(&irc, 1, &f_fib);
{
    woort_IRValue* n_arg = woort_IRFunction_get_argument(f_fib, 0);
    const woort_IRValue* v2 = woort_IRFunction_fetch_const(f_fib, c2);
    const woort_IRValue* v1 = woort_IRFunction_fetch_const(f_fib, c1);
    woort_IRValue* tmp1  = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* tmp2  = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* r1    = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* r2    = woort_IRFunction_new_vreg(f_fib);
    woort_IRValue* sum   = woort_IRFunction_new_vreg(f_fib);

    woort_IRLabel* L_base = woort_IRFunction_new_label(f_fib);

    woort_IR_jcc_lt(f_fib, n_arg, v2, L_base);  /* if (n < 2) goto base */

    /* 递归分支 */
    woort_IR_SUBI(f_fib, tmp1, n_arg, v1);      /* tmp1 = n - 1 */
    woort_IR_SUBI(f_fib, tmp2, n_arg, v2);      /* tmp2 = n - 2 */

    woort_IR_PUSHCHK(f_fib, tmp1);
    woort_IR_CALLNWO(f_fib, cfib, 1, r1);       /* r1 = fib(n-1) */

    woort_IR_PUSHCHK(f_fib, tmp2);
    woort_IR_CALLNWO(f_fib, cfib, 1, r2);       /* r2 = fib(n-2) */

    woort_IR_ADDI(f_fib, sum, r1, r2);          /* sum = r1 + r2 */
    woort_IR_ret(f_fib, sum);

    /* 基本情况：return n */
    woort_IR_bind(f_fib, L_base);
    woort_IR_ret(f_fib, n_arg);
}

/* ====== func main() => void ====== */
woort_IRFunction* f_main;
woort_IRCompiler_add_function(&irc, 0, &f_main);
{
    const woort_IRValue* vn = woort_IRFunction_fetch_const(f_main, cn);
    woort_IRValue* result = woort_IRFunction_new_vreg(f_main);

    woort_IR_PUSHCHK(f_main, vn);
    woort_IR_CALLNWO(f_main, cfib, 1, result);
    woort_IR_ret(f_main, result);
}

woort_CodeEnv* cenv;
woort_IRCompiler_finish(&irc, &cenv);

cenv->m_data_begin[c2].m_integer = 2;
cenv->m_data_begin[c1].m_integer = 1;
cenv->m_data_begin[cfib].m_script_function = cenv->m_code_begin + 0;
cenv->m_data_begin[cn].m_integer = 10;

/* 调用 main */
woort_VMRuntime* vm;
woort_VMRuntime_create(&vm);
woort_VMRuntime_invoke(vm, cenv->m_code_begin + f_fib->m_code_length);
/* vm->m_sp[0].m_integer == 55 */
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
woort_IRCompiler_add_function(&irc, 0, &f);
{
    const woort_IRValue* vn = woort_IRFunction_fetch_const(f, cn);  /* 常量 n=10 */
    const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);  /* 常量 0 */
    const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);  /* 常量 1 */
    woort_IRValue* i    = woort_IRFunction_new_vreg(f);        /* 可变：循环计数器 */
    woort_IRValue* acc  = woort_IRFunction_new_vreg(f);        /* 可变：累加器 */

    woort_IRLabel* L_header = woort_IRFunction_new_label(f);
    woort_IRLabel* L_exit   = woort_IRFunction_new_label(f);

    /* 初始化循环变量 */
    woort_IR_MOV(f, i, v1);           /* i = 1 */
    woort_IR_MOV(f, acc, v0);         /* acc = 0 */

    /* 循环头 */
    woort_IR_bind(f, L_header);
    woort_IR_jcc_gt(f, i, vn, L_exit);  /* if (i > n) goto exit */

    /* 循环体 */
    woort_IR_ADDI(f, acc, acc, i);       /* acc += i */
    woort_IR_ADDI(f, i, i, v1);         /* i += 1 */
    woort_IR_jmp(f, L_header);

    /* 退出 */
    woort_IR_bind(f, L_exit);
    woort_IR_ret(f, acc);
}
```

## finish() 内部处理流程

`woort_IRCompiler_finish()` 对每个函数执行以下分析和代码生成：

1. **Label → Block 切分**：根据 Label 绑定点和跳转指令自动切分基本块，构建 CFG
2. **活跃性分析**：标准的迭代数据流分析（USE/DEF → LIVE_IN/LIVE_OUT）。CONST 源的 vreg 不参与 bitset 活跃性。
3. **常量直连标记**：检查 CONST vreg 是否仅被一条 PUSHCHK/RET 使用，标记为 const_direct（发射 PUSHCCHK/RETVC）
4. **栈槽分配**：线性扫描算法，活跃区间不重叠的普通 vreg 共享栈槽。CONST vreg 跳过。
5. **Dominator 分析 + 循环检测**：Cooper-Harvey-Kennedy 算法构建支配树，检测自然循环
6. **常量加载放置**：为非 const_direct 的 CONST vreg 分配独立栈槽，将 LOAD 放置到使用点的公共支配者处，循环内的常量加载提升到循环外
7. **字节码发射**：逐指令翻译 IR → 字节码，自动选择 compound 指令和最优编码宽度。CONST vreg 自动产生 PUSHCCHK/RETVC/LOAD。
8. **跳转修补**：解析 Label 地址，选择 JFWD/JBCK 编码，处理 offset overflow 扩展

## 注意

* `woort_IRFunction_fetch_const` 返回的 const IRValue* 代表不可变的常量值，不应作为指令的 `dst` 参数。可变变量应使用 `woort_IRFunction_new_vreg` 创建，通过 `woort_IR_MOV` 从常量值初始化。
* 同一 `const_index` 多次调用 `load_const` 返回相同的 IRValue*（天然去重）
* 常量值的加载时机和方式由框架自动决定：单次使用于 PUSHCHK/RET 时直接使用特化指令（PUSHCCHK/RETVC）；多次使用时在最优位置放置 LOAD 指令
* 指令发射函数名保持大写（如 `woort_IR_ADDI`），控制流函数使用小写（如 `woort_IR_jmp`）
* `woort_IR_CALLNWO/CALLNFP/CALLNJIT/CALL` 的 `argc` 参数用于生成 `RESULT`/`POPR` 指令时指示所需弹出清理栈上空间的数量
* 如果调用结果的 `dst` 参数为 `NULL`，则不使用 `RESULT` 指令，直接 `POPR`
* 所有指令发射函数返回 `bool`，`false` 表示内存分配失败
* 每个函数的 `m_code_length` 字段在 `finish()` 后可用，用于计算函数在 CodeEnv 中的偏移
