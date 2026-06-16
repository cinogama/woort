# WooRT 指令集参考

本文档整理 WooRT 虚拟机的所有指令，按功能分组。权威定义见 [`src/woort_opcode.h`](../src/woort_opcode.h)（主指令枚举）与 [`src/woort_opcode_formal.h`](../src/woort_opcode_formal.h)（编码格式宏）。

## 栈模型

```
sp      |                        | < 下一个值压入位置
        |~~~~~~~~~~~~~~~~~~~~~~~|
...     |                        |
        |_______________________|
sb      |_______________________| < 闭包捕获解包位置（如果是闭包）
sb + 1  |__ CALLWAY & BPOFFSET __|
sb + 2  |____ RETURN ADDRESS ____| < * 返回值存储位置
sb + 3  |_____ ARGUMENT 0 ______| < 变长参数数量（如果是变长参数函数）
sb + 4  |_____ ARGUMENT 1 ______|
```

* 所有字节码中的栈偏移都是相对 `m_sb` 的有符号偏移。`sb+3+idx` 是第 idx 个参数。
* 闭包函数的捕获变量从 `sb-1` 开始向下排布：`sb-(1+idx)` 是第 idx 个 upvalue。

## 操作数符号

| 符号 | 含义 |
|------|------|
| `S8`/`S16`/`S32` | 有符号栈偏移（8/16/32 位） |
| `U8`/`U16` | 无符号偏移量（用于条件跳转距离） |
| `C16`/`C18`/`C24`/`C26`/`C32` | 常量区索引 |
| `N8`/`N10`/`N24`/`N32` | 计数/数量（无符号） |
| `T4`/`T8` | 类型标识（见下） |
| `BA26` | 绝对跳转地址（26 位） |
| `R_M_S8`/`R_M_S16` | 读操作数，对应容器会被修改 |

### 操作数属性

| 属性 | 说明 |
|------|------|
| `R_ONLY` | 只读操作数 |
| `W_ONLY` | 只写操作数 |
| `R_M` | 读操作数，对应的容器值将被修改 |
| `R_W` | 既读又写（复合赋值目标） |

**重要**：所有指令的写操作总是发生在读操作之前。

### 类型标识 T 的取值

`T8`/`T4` 用于动态类型相关的指令，取值与 `woort_BoxValueType` 一致：

| 值 | 含义 |
|----|------|
| `0` (`0b000`) | GCUNIT（GC 对象：string/vec/map/struct/closure/gchandle/扩展装箱） |
| `1` (`0b001`) | REAL（内联 Float63，或扩展装箱实数） |
| `2` (`0b010`) | INT（内联 Int62，或扩展装箱整数） |
| `4` (`0b100`) | BOOL（内联 Bool） |

> 装箱方案详见 [values.md](./values.md)。

---

## 字节码格式

每条字节码为 32 位，结构如下：

```
|__Main_Command(6bits)___|__Mode_(2bits)__|__A_(8bits)__ _|__B_(8bits)____|__C_(8bits)____|__EX_(32bits)__|
```

* `OP6`：主指令（6 位，最多 64 条主指令）。
* `M2`：模式选择（2 位，区分同一主指令下的 4 个变体）。
* `A8`/`B8`/`C8`：三个 8 位操作数字段。
* `EX`：可选的 32 位扩展操作数（紧跟主指令字之后的第二个 32 位字）。带 `EX` 的指令在 `opcode.h` 中以 `|_______X_______|` 标注。

下文的「格式」列给出该指令使用的字段组合（如 `MAB18_C8` 表示 mode+A+B 共 18 位作一个操作数，C 占 8 位）。

### 格式构造宏

使用 `woort_OpCodeFormal_cons(FORMAL, ...)` 宏构造字节码（定义见 `woort_opcode_formal.h`）。`FORMAL` 取下列值之一：

| 格式名 | 字段布局 | 典型用途 |
|--------|----------|----------|
| `OP6` | OP6 | 无操作数（NOP） |
| `OP6_M2` | OP6 + M2 | 仅模式选择（RET mode=0） |
| `OP6_MABC26` | OP6 + M2+A8+B8+C8（26 位） | 常量索引/绝对跳转地址 |
| `OP6_MAB18_C8` | OP6 + (M2+A8+B8) + C8 | 18 位常量索引 + 8 位偏移 |
| `OP6_M2_ABC24` | OP6 + M2 + (A8+B8+C8) | 模式 + 24 位计数/偏移 |
| `OP6_M2_BC16` | OP6 + M2 + (B8+C8) | 模式 + 16 位偏移 |
| `OP6_M2_A8_BC16` | OP6 + M2 + A8 + (B8+C8) | 模式 + 8 位 + 16 位偏移 |
| `OP6_M2_A8_B8_C8` | OP6 + M2 + A8 + B8 + C8 | 三个独立 8 位偏移 |
| `OP6_M2_B8_C8` | OP6 + M2 + B8 + C8 | 模式 + 两个 8 位偏移 |
| `OP6_MA10_BC16` | OP6 + (M2+A8=10 位) + (B8+C8) | 10 位计数 + 16 位偏移 |
| `OP6_MA10_B8_C8` | OP6 + (M2+A8=10 位) + B8 + C8 | 10 位计数 + 两个 8 位偏移 |

示例：

```c
woort_OpCodeFormal_cons(OP6, WOORT_OPCODE_NOP)
woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0)              /* RET（无返回值） */
woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, 4)      /* 调用 G[4] */
woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, 0, 0)    /* LOAD G[0] → [SB+0] */
woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 0, 5) /* PUSHRCHK 5 */
woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 0, -4, -3, -4) /* ADDI */
```

> 便捷封装宏（`woort_OpCode_LOAD`、`woort_OpCode_PUSHRCHK` 等）定义在 `src/woort_opcode_builder.h` 中。

---

## 1. 空操作

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `NOP` | `OP6` | — | 空操作，不执行任何操作 |

---

## 2. 数据加载/存储

### 2.1 常量区加载/存储（LOAD / STORE）

| 变体 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `LOAD` | `OP6_MAB18_C8` | R_ONLY C18 → W_ONLY S8 | `G[c18] → [SB+s8]` |
| `STORE` | `OP6_MAB18_C8` | W_ONLY C18 ← R_ONLY S8 | `[SB+s8] → G[c18]` |

### 2.2 扩展加载/存储（LDSTEX，带 EX）

支持 32 位常量索引与 16 位栈偏移。

| 变体 | Mode | 字段 | 说明 |
|------|------|------|------|
| `LOADEX` | 0 | R_ONLY C32 (EX), W_ONLY S16 | `G[c32] → [SB+s16]` |
| `STOREEX` | 1 | R_ONLY C32 (EX), W_ONLY S16 | `[SB+s16] → G[c32]` |
| `<保留>` | 2 | — | — |
| `<保留>` | 3 | — | — |

### 2.3 静态区加载/存储（LOAD/STORE，STATIC 段）

静态存储区（Static）与常量区共用 `G[idx]` 寻址空间，但由 `IR_LOAD`/`IR_STORE`（绑定 `woort_IRStaticIndex`）发射，运行时语义为读/写可变全局。

### 2.4 栈间移动（MOV）

在栈位置之间复制值。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `MOVLD` | 0 | W_ONLY S8 ← R_ONLY S16 | 16 位偏移 → 8 位偏移 |
| `MOVST` | 1 | R_ONLY S8 → W_ONLY S16 | 8 位偏移 → 16 位偏移 |
| `MOVLDEXT` | 2 | W_ONLY S16 ← R_ONLY S32 (EX) | 32 位偏移 → 16 位偏移 |
| `MOVSTEXT` | 3 | R_ONLY S16 → W_ONLY S32 (EX) | 16 位偏移 → 32 位偏移 |

---

## 3. 栈操作

### 3.1 压栈并检查溢出（PUSHCHK）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `PUSHRCHK` | 0 | N24 | 预留 N 个栈槽（栈检查） |
| `PUSHSCHK` | 1 | R_ONLY S16 | 从栈偏移压入（栈检查） |
| `PUSHCCHK` | 2 | R_ONLY C24 | 从常量区压入（栈检查） |
| `PUSHCCHKEXT` | 3 | R_ONLY C32 (EX) | 从常量区压入（32 位索引，栈检查） |

### 3.2 压栈-断言版本（PUSH）

不检查溢出的压栈操作（调用方已通过 `PUSHRCHK` 保证空间）。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `ASSURESSZ` | 0 | N24 | 确保栈空间足够 |
| `PUSHS` | 1 | R_ONLY S16 | 从栈偏移压入 |
| `PUSHC` | 2 | R_ONLY C24 | 从常量区压入 |
| `PUSHCEXT` | 3 | R_ONLY C32 (EX) | 从常量区压入（32 位索引） |

### 3.3 弹栈（POP）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `POPR` | 0 | N24 | 弹出 N 个值，释放空间 |
| `POPS` | 1 | R_ONLY S16 | 弹出到栈偏移 |
| `POPC` | 2 | R_ONLY C24 | 弹出到常量区 |
| `POPCEXT` | 3 | R_ONLY C32 (EX) | 弹出到常量区（32 位索引） |

---

## 4. 整数除法安全检查（CHKDIVI）

> **新增指令组**。配合 `DIVI`/`MODI` 使用，在除法前检查会导致未定义行为的输入。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CHKDIVIL` | 0 | R_ONLY S16 | 检查被除数是否为 `INT64_MIN`（溢出） |
| `CHKDIVIR` | 1 | R_ONLY S16 | 检查除数是否为 `0` 或 `-1` |
| `CHKDIVIRZ` | 2 | R_ONLY S16 | 检查除数是否为 `0` |
| `CHKDIVILR` | 3 | R_ONLY S8, R_ONLY S16 | 同时检查除数 S8 与被除数 S16 |

检查失败时触发 panic（`ABORT`）。

---

## 5. 类型转换

### 5.1 整数转换（CASTI）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `ITORST` | 0 | R_ONLY S8 → W_ONLY S16 | 整数 → 实数 |
| `ITORLD` | 1 | R_ONLY S16 → W_ONLY S8 | 整数 → 实数 |
| `ITOSST` | 2 | R_ONLY S8 → W_ONLY S16 | 整数 → 字符串 |
| `ITOSLD` | 3 | R_ONLY S16 → W_ONLY S8 | 整数 → 字符串 |

### 5.2 实数转换（CASTR）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `RTOIST` | 0 | R_ONLY S8 → W_ONLY S16 | 实数 → 整数 |
| `RTOILD` | 1 | R_ONLY S16 → W_ONLY S8 | 实数 → 整数 |
| `RTOSST` | 2 | R_ONLY S8 → W_ONLY S16 | 实数 → 字符串 |
| `RTOSLD` | 3 | R_ONLY S16 → W_ONLY S8 | 实数 → 字符串 |

### 5.3 动态类型转换（CASTX）

> **新增指令组**。处理装箱值与标量/字符串之间的转换。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CASTSTO` | 0 | T8, R_ONLY S8 → W_ONLY S8 | 字符串 → 标量（类型 T） |
| `CASTSFROM` | 1 | T8, R_ONLY S8 → W_ONLY S8 | 标量（类型 T） → 字符串 |
| `CASTDYN` | 2 | T8, R_ONLY S8 → W_ONLY S8 | 动态类型间转换 |
| `ASSERTDYN` | 3 | T8, R_ONLY S16 | 断言动态类型为 T，否则 panic |

---

## 6. 函数调用

### 6.1 直接调用

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `CALLNWO` | `OP6_MABC26` | R_ONLY C26 | 调用脚本函数（NEAR，不发生 FAR_CALL） |
| `CALLNFP` | `OP6_MABC26` | R_ONLY C26 | 调用原生函数指针 |
| `CALLNJIT` | `OP6_MABC26` | R_ONLY C26 | 调用 JIT 编译函数 |

### 6.2 间接调用（CALL）

通过栈或常量区中持有的函数值（脚本/JIT/原生/闭包）调用。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CALLS` | 0 | R_ONLY S16 | 从栈偏移调用 |
| `CALLC` | 1 | R_ONLY C24 | 从常量区调用 |
| `<保留>` | 2 | — | — |
| `<保留>` | 3 | — | — |

---

## 7. 返回指令（RET）

### 7.1 返回

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `RET` | 0 | — | 无返回值返回 |
| `RETVS` | 1 | R_ONLY S16 | 从栈偏移取返回值 |
| `RETVC` | 2 | R_ONLY C24 | 从常量区取返回值 |
| `POPRS` | 3 | R_ONLY S16 | **动态弹栈**（见 §7.2） |

### 7.2 动态弹栈（POPRS）

> 此变体借放在 RET 指令组中，但**不是**返回指令。它从栈偏移读取一个整数值作为数量 N，弹出 N 个值。用于变长参数处理等「弹栈数量运行时才确定」的场景。

### 7.3 结果处理（RESULT）

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `RESULT` | `OP6_MA10_BC16` | N10, W_ONLY S16 | 将栈顶值复制到 `[SB+s16]` 并弹出 N 个值 |

---

## 8. 跳转指令

### 8.1 无条件跳转

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `JFWD` | `OP6_MABC26` | BA26 | 向前无条件跳转 |
| `JBCK` | `OP6_MABC26` | BA26 | 向后跳转并触发检查点 |

> `JFWD`/`JBCK` 是 IR `jmp` 在发射阶段根据方向自动选择的结果。`JBCK`（回跳）同时充当 GC/中断检查点；向前跳转 `JFWD` 在需要时也会插入检查点。

### 8.2 条件跳转（JFWDCND / JBCKCND）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `JFWDNZ` / `JBCKNZ` | 0 | R_ONLY S8, U16 | 非零则跳转 |
| `JFWDZ` / `JBCKZ` | 1 | R_ONLY S8, U16 | 为零则跳转 |
| `JFWDEQ` / `JBCKEQ` | 2 | R_ONLY S8, R_ONLY S8, U8 | 相等则跳转 |
| `JFWDNEQ` / `JBCKNEQ` | 3 | R_ONLY S8, R_ONLY S8, U8 | 不等则跳转 |

`JFWD*` 向前跳转；`JBCK*` 向后跳转并带检查点。

### 8.3 整数比较跳转（JFDCMP / JBCKCMP）

比较后跳转，操作数顺序为 `a, b, distance`。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `JFWDLT` / `JBCKLT` | 0 | R_ONLY S8, R_ONLY S8, U8 | 小于则跳转 |
| `JFWDGT` / `JBCKGT` | 1 | R_ONLY S8, R_ONLY S8, U8 | 大于则跳转 |
| `JFWDEL` / `JBCKEL` | 2 | R_ONLY S8, R_ONLY S8, U8 | 小于等于则跳转 |
| `JFWDEG` / `JBCKEG` | 3 | R_ONLY S8, R_ONLY S8, U8 | 大于等于则跳转 |

---

## 9. 容器创建

### 9.1 创建容器（CONS）

从栈顶弹出元素创建容器。元素按调用约定预先压栈。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `MKVEC` | 0 | N8, W_ONLY S16 | 创建向量，弹出 N 个元素 |
| `MKMAP` | 1 | N8, W_ONLY S16 | 创建映射，弹出 N×2 个键值对 |
| `MKSTRUCT` | 2 | N8, W_ONLY S16 | 创建结构体，弹出 N 个字段 |
| `MKUNION` | 3 | N8, R_ONLY S8, W_ONLY S8 | 创建联合，N 为 id，S8 为负载 |

### 9.2 扩展创建容器（CONSEX）

支持更大数量（32 位计数）。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `MKVECEXT` | 0 | W_ONLY S16, N32 | 创建向量（32 位计数） |
| `MKMAPEXT` | 1 | W_ONLY S16, N32 | 创建映射（32 位计数） |
| `MKSTRUCTEXT` | 2 | W_ONLY S16, N32 | 创建结构体（32 位计数） |
| `MKUNIONEXT` | 3 | R_ONLY S8, W_ONLY S16, N32 | 创建联合（32 位 id） |

### 9.3 创建闭包（MKCLOSURE）

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `MKCLOSURE` | `OP6_MA10_BC16` + EX | N10, W_ONLY S16, R_ONLY C32 | 创建闭包，捕获 N 个变量，函数索引 C32 |

---

## 10. 动态类型操作（DYN）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `BOXDYN` | 0 | T8, R_ONLY S8 → W_ONLY S8 | 将标量值装箱为类型 T 的 DynBox |
| `UNBOXDYN` | 1 | T8, R_ONLY S8 → W_ONLY S8 | 将 DynBox 拆箱（类型不匹配抛异常） |
| `CHECKDYN` | 2 | T8, R_ONLY S8 → W_ONLY S8 | 检查动态类型是否为 T（结果 1/0） |
| `PUSHBOXDYN` | 3 | T8, R_ONLY S16 | 装箱并压入栈顶 |

类型 T 取值见「操作数符号」表（0=GCUNIT，1=REAL，2=INT，4=BOOL）。

---

## 11. 整数算术运算

### 11.1 加减乘除（OPIASMD）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `ADDI` | 0 | R S8, R S8 → W S8 | 整数加法 |
| `SUBI` | 1 | R S8, R S8 → W S8 | 整数减法 |
| `MULI` | 2 | R S8, R S8 → W S8 | 整数乘法 |
| `DIVI` | 3 | R S8, R S8 → W S8 | 整数除法 |

### 11.2 取模与比较（OPIONLG）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `MODI` | 0 | R S8, R S8 → W S8 | 整数取模 |
| `NEGI` | 1 | R S8 → W S16 | 整数取负 |
| `LTI` | 2 | R S8, R S8 → W S8 | 小于比较（结果 1/0） |
| `GTI` | 3 | R S8, R S8 → W S8 | 大于比较（结果 1/0） |

### 11.3 比较运算（OPISREN）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LEI` | 0 | R S8, R S8 → W S8 | 小于等于 |
| `GEI` | 1 | R S8, R S8 → W S8 | 大于等于 |
| `EQI` | 2 | R S8, R S8 → W S8 | 相等 |
| `NEI` | 3 | R S8, R S8 → W S8 | 不等 |

---

## 12. 实数算术运算

### 12.1 加减乘除（OPRASMD）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `ADDR` | 0 | R S8, R S8 → W S8 | 实数加法 |
| `SUBR` | 1 | R S8, R S8 → W S8 | 实数减法 |
| `MULR` | 2 | R S8, R S8 → W S8 | 实数乘法 |
| `DIVR` | 3 | R S8, R S8 → W S8 | 实数除法 |

### 12.2 取模与比较（OPRONLG）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `MODR` | 0 | R S8, R S8 → W S8 | 实数取模（`fmod`） |
| `NEGR` | 1 | R S8 → W S16 | 实数取负 |
| `LTR` | 2 | R S8, R S8 → W S8 | 小于比较 |
| `GTR` | 3 | R S8, R S8 → W S8 | 大于比较 |

### 12.3 比较运算（OPRSREN）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LER` | 0 | R S8, R S8 → W S8 | 小于等于 |
| `GER` | 1 | R S8, R S8 → W S8 | 大于等于 |
| `EQR` | 2 | R S8, R S8 → W S8 | 相等 |
| `NER` | 3 | R S8, R S8 → W S8 | 不等 |

---

## 13. 字符串操作

### 13.1 连接与比较（OPSALGS）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `ADDS` | 0 | R S8, R S8 → W S8 | 字符串连接 |
| `LTS` | 1 | R S8, R S8 → W S8 | 小于比较 |
| `GTS` | 2 | R S8, R S8 → W S8 | 大于比较 |
| `LES` | 3 | R S8, R S8 → W S8 | 小于等于比较 |

### 13.2 比较运算（OPSREN）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `GES` | 0 | R S8, R S8 → W S8 | 大于等于比较 |
| `EQS` | 1 | R S8, R S8 → W S8 | 相等比较 |
| `NES` | 2 | R S8, R S8 → W S8 | 不等比较 |
| `<保留>` | 3 | — | — |

---

## 14. 逻辑运算（OPLAONI）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LAND` | 0 | R S8, R S8 → W S8 | 逻辑与 |
| `LOR` | 1 | R S8, R S8 → W S8 | 逻辑或 |
| `LNOT` | 2 | R S8 → W S16 | 逻辑非 |
| `<保留>` | 3 | — | — |

---

## 15. 复合赋值运算

复合赋值指令的目标操作数为 `R_W`（既读又写）。

### 15.1 整数复合赋值（OPCIASMD）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CADDI` | 0 | R S8, R_W S16 | `目标 += 操作数` |
| `CSUBI` | 1 | R S8, R_W S16 | `目标 -= 操作数` |
| `CMULI` | 2 | R S8, R_W S16 | `目标 *= 操作数` |
| `CDIVI` | 3 | R S8, R_W S16 | `目标 /= 操作数` |

### 15.2 实数复合赋值（OPCRASMD）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CADDR` | 0 | R S8, R_W S16 | 复合加法 |
| `CSUBR` | 1 | R S8, R_W S16 | 复合减法 |
| `CMULR` | 2 | R S8, R_W S16 | 复合乘法 |
| `CDIVR` | 3 | R S8, R_W S16 | 复合除法 |

### 15.3 混合复合赋值（OPCSAIOO）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CADDS` | 0 | R S8, R_W S16 | 字符串复合连接（`目标 += 操作数`） |
| `CVADDS` | 1 | R S8, R_W S16 | 字符串反向连接（`目标 = 操作数 + 目标`） |
| `CMODI` | 2 | R S8, R_W S16 | 整数复合取模 |
| `CMODR` | 3 | R S8, R_W S16 | 实数复合取模 |

### 15.4 逻辑复合赋值（OPCLAON）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `CLAND` | 0 | R S8, R_W S16 | 复合逻辑与 |
| `CLOR` | 1 | R S8, R_W S16 | 复合逻辑或 |
| `CLNOT` | 2 | R_W S16 | 复合逻辑非 |
| `<保留>` | 3 | — | — |

---

## 16. 索引加载

### 16.1 从向量/结构体/字符串加载（LDIDX）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LDIDXVEC` | 0 | R S8(索引), R S8(向量) → W S8 | 从向量加载值 |
| `LDIDXVECX` | 1 | R S8(索引), R S8(向量) → W S8 | 从向量加载到 DynBox |
| `LDIDSTRUCT` | 2 | N8(字段), R S8(结构体) → W S8 | 从结构体加载字段 |
| `LDIDSTRING` | 3 | R S8(字符索引), R S8(字符串) → W S8 | 从字符串加载字符 |

### 16.2 从映射加载（LDIDXDICT）

按键类型分类。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LDIDXDICTI` | 0 | R S8(整数键), R S8(映射) → W S8 | 整数键加载 |
| `LDIDXDICTR` | 1 | R S8(实数键), R S8(映射) → W S8 | 实数键加载 |
| `LDIDXDICTB` | 2 | R S8(布尔键), R S8(映射) → W S8 | 布尔键加载 |
| `LDIDXDICTX` | 3 | R S8(动态键), R S8(映射) → W S8 | 动态类型键加载 |

### 16.3 从映射加载到动态类型（LDIDXDICTX）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LDIDXDICTIX` | 0 | R S8, R S8 → W S8 | 整数键加载到 DynBox |
| `LDIDXDICTRX` | 1 | R S8, R S8 → W S8 | 实数键加载到 DynBox |
| `LDIDXDICTBX` | 2 | R S8, R S8 → W S8 | 布尔键加载到 DynBox |
| `LDIDXDICTXX` | 3 | R S8, R S8 → W S8 | 动态键加载到 DynBox |

### 16.4 扩展索引加载（LDIDXEX）

支持 16 位栈偏移。注意结构体字段为 N24。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `LDIDXVECEXT` | 0 | R S16(索引), R S16(向量) → W S16 | 从向量加载（扩展） |
| `LDIDXVECXEXT` | 1 | R S16(索引), R S16(向量) → W S16 | 从向量加载到 DynBox（扩展） |
| `LDIDSTRUCTEXT` | 2 | N24(字段), R S16(结构体) → W S16 | 从结构体加载（扩展） |
| `LDIDSTRINGEXT` | 3 | R S16(字符索引), R S16(字符串) → W S16 | 从字符串加载（扩展） |

### 16.5 扩展映射加载（LDIDXDICTEX / LDIDXDICTEXX）

与 16.2/16.3 同构，操作数均为 `R S16(键), R S16(映射) → W S16`，区别仅在键类型（I/R/B/X）和是否装箱到 DynBox。共 8 个变体：

| 指令组 | 变体 |
|--------|------|
| `LDIDXDICTEX` (mode 0–3) | `LDIDXDICTIEXT`、`LDIDXDICTREXT`、`LDIDXDICTBEXT`、`LDIDXDICTXEXT` |
| `LDIDXDICTEXX` (mode 0–3) | `LDIDXDICTIXEXT`、`LDIDXDICTRXEXT`、`LDIDXDICTBXEXT`、`LDIDXDICTXXEXT` |

---

## 17. 索引存储

存储类指令的第一个容器操作数为 `R_M`（读且修改）。

### 17.1 存储到向量（STIDXVEC）

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `STIDXVECI` | 0 | R_M S8(向量), R S8(索引), R S8(整数值) | 存储整数 |
| `STIDXVECR` | 1 | R_M S8(向量), R S8(索引), R S8(实数值) | 存储实数 |
| `STIDXVECB` | 2 | R_M S8(向量), R S8(索引), R S8(布尔值) | 存储布尔 |
| `STIDXVECX` | 3 | R_M S8(向量), R S8(索引), R S8(动态值) | 存储动态类型 |

### 17.2 存储到映射（STIDXDICT*）

按「键类型 + 值类型」组合，共 4 组（键 I/R/B/X）× 4 组（值 I/R/B/X）= 16 个变体。命名规律 `STIDXDICT<键><值>`，如 `STIDXDICTIR` = 整数键、实数值。操作数均为 `R_M S8(映射), R S8(键), R S8(值)`。

> **存储 vs 设置**：`STIDXDICT*` 要求键已存在（否则 panic）；`STIDXMAP*`（§17.3）在键不存在时创建新条目。

| 指令组 | 变体 |
|--------|------|
| `STIDXDICTI` (mode 0–3) | `II`、`IR`、`IB`、`IX` |
| `STIDXDICTR` (mode 0–3) | `RI`、`RR`、`RB`、`RX` |
| `STIDXDICTB` (mode 0–3) | `BI`、`BR`、`BB`、`BX` |
| `STIDXDICTX` (mode 0–3) | `XI`、`XR`、`XB`、`XX` |

### 17.3 设置映射值（STIDXMAP*）

语义同 17.2，但键不存在时创建新条目。同样 16 个变体：

| 指令组 | 变体 |
|--------|------|
| `STIDXMAPI` (mode 0–3) | `II`、`IR`、`IB`、`IX` |
| `STIDXMAPR` (mode 0–3) | `RI`、`RR`、`RB`、`RX` |
| `STIDXMAPB` (mode 0–3) | `BI`、`BR`、`BB`、`BX` |
| `STIDXMAPX` (mode 0–3) | `XI`、`XR`、`XB`、`XX` |

### 17.4 存储到结构体（STIDSTRUCT）

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `STIDSTRUCT` | `OP6_MA10_B8_C8` | N10(字段), R_M S8(结构体), R S8(值) | 存储到结构体字段 |

### 17.5 扩展索引存储（STIDXEX）

支持 16 位栈偏移和类型编码。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `STIDXVECEXT` | 0 | T8(值类型), R_M S16(向量), R S16(索引), R S16(值) | 存储到向量（扩展） |
| `STIDXDICTEXT` | 1 | T4(键)+T4(值), R_M S16(映射), R S16(键), R S16(值) | 存储到映射（扩展） |
| `STIDXMAPEXT` | 2 | T4(键)+T4(值), R_M S16(映射), R S16(键), R S16(值) | 设置映射值（扩展） |
| `STIDSTRUCTEXT` | 3 | N24(字段), R_M S16(结构体), R S16(值) | 存储到结构体（扩展） |

类型 T 取值：0=整数，1=实数，2=布尔，3=动态类型（`STIDXEX` 专用编码，与 §10 的 T 略有差异）。

---

## 18. 容器解包（UNPACK）

> **已扩展**。原 `UNPACKSTRUCT` 已移除；现在的语义是「按数量解包向量」。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `UNPACKVEC` | 0 | N8(数量), R_ONLY S16(向量) | 解包向量前 N 个元素，按逆序压栈（作为 `woort_Value`） |
| `UNPACKVECX` | 1 | N8(数量), R_ONLY S16(向量) | 解包向量前 N 个元素到 DynBox，按逆序压栈 |
| `UNPACKVECALL` | 2 | N8, R_ONLY S8(向量) → W_ONLY S8(数量) | 全部解包，实际元素数写入目标 |
| `UNPACKVECXALL` | 3 | N8, R_ONLY S8(向量) → W_ONLY S8(数量) | 全部解包到 DynBox，实际元素数写入目标 |

---

## 19. 结构体索引装箱压栈（PUSHIDXSTBOX）

从结构体读取指定字段，装箱为指定类型后压入栈顶。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `PUSHIDXSTRUCT` | 0 | N8(字段), R_ONLY S16(结构体) | 索引字段并原样压栈（`woort_Value`） |
| `PUSHIDXSTBOXI` | 1 | N8(字段), R_ONLY S16(结构体) | 索引字段并装箱为整数压栈 |
| `PUSHIDXSTBOXR` | 2 | N8(字段), R_ONLY S16(结构体) | 索引字段并装箱为实数压栈 |
| `PUSHIDXSTBOXB` | 3 | N8(字段), R_ONLY S16(结构体) | 索引字段并装箱为布尔压栈 |

---

## 20. 变长参数收集（PACKARG）

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `PACKARG` | `OP6_MA10_BC16` | N10(跳过计数), W_ONLY S16(目标) | 从 `SB+3+跳过计数` 处开始收集变长参数，打包成向量存储到目标 |

跳过计数表示需要跳过的固定参数数量。变长参数数量存储在 `SB+3` 处。

---

## 21. 原子操作（ATOMIC）

> **新增指令组**。对静态存储区 `G[idx]`（32 位索引，位于 EX）执行原子操作。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `ASTORE` | 0 | R_ONLY S16, W_ONLY C32 (EX) | 原子存储（release 语义）：`G[c32] = [SB+s16]` |
| `ALOAD` | 1 | W_ONLY S16, R_ONLY C32 (EX) | 原子加载（acquire 语义）：`[SB+s16] = G[c32]` |
| `CAS` | 2 | R_ONLY S8, R_W S16, R_W C32 (EX) | 比较并交换：若 `G[c32]==expected` 则写入 `desired` |
| `<保留>` | 3 | — | — |

---

## 22. 一次性初始化守卫（JIFINITED）

| 指令 | 格式 | 操作数 | 说明 |
|------|------|--------|------|
| `JIFINITED` | `OP6_MABC26` + EX | BA26, R_W C32 (EX) | 三态标志守卫（见下） |

把 `G[c32]` 当作三态标志：`0`=未初始化、`1`=初始化中（其他线程）、`2`=已完成。

* `flag == 2`：跳转到 BA26；
* `flag == 0`：CAS `0→1` 成功则顺序执行（调用方随后执行初始化代码，结束后应置 `2`）；
* `flag == 1` 或 CAS 失败：自旋（带 GC 检查点）直到 `flag == 2`，然后跳转。

---

## 23. 陷阱与 Panic（TRAP）

> **新增指令组**。

| 变体 | Mode | 操作数 | 说明 |
|------|------|--------|------|
| `DEBUGTRAP` | 0 | — | 调试陷阱/断点，进入调试器（若已 attach） |
| `PANICS` | 1 | R_ONLY S16 | 带栈上的字符串消息 panic，触发 `ABORT` |
| `PANICC` | 2 | R_ONLY C24 | 带常量区字符串消息 panic，触发 `ABORT` |
| `<保留>` | 3 | — | — |

---

## 指令统计

| 分类 | 主指令数 | 变体总数 |
|------|----------|----------|
| 空操作 | 1 | 1 |
| 数据加载/存储（LOAD/STORE/LDSTEX） | 3 | 6 |
| 栈间移动（MOV） | 1 | 4 |
| 栈操作（PUSHCHK/PUSH/POP） | 3 | 12 |
| 整数除法检查（CHKDIVI） | 1 | 4 |
| 类型转换（CASTI/CASTR/CASTX） | 3 | 12 |
| 函数调用（CALLN*/CALL） | 4 | 6 |
| 返回与结果（RET/RESULT） | 2 | 4 |
| 跳转指令（JFWD/JBCK/J*CND/J*CMP） | 6 | 20 |
| 容器创建（CONS/CONSEX/MKCLOSURE） | 4 | 11 |
| 动态类型（DYN） | 1 | 4 |
| 整数运算（OPIASMD/OPIONLG/OPISREN） | 3 | 12 |
| 实数运算（OPRASMD/OPRONLG/OPRSREN） | 3 | 12 |
| 字符串操作（OPSALGS/OPSREN） | 2 | 7 |
| 逻辑运算（OPLAONI） | 1 | 3 |
| 复合赋值（OPCIASMD/OPCRASMD/OPCSAIOO/OPCLAON） | 4 | 15 |
| 索引加载（LDIDX 系列） | 6 | 24 |
| 索引存储（STIDX 系列） | 11 | 45 |
| 容器解包（UNPACK） | 1 | 4 |
| 结构体索引装箱（PUSHIDXSTBOX） | 1 | 4 |
| 变长参数收集（PACKARG） | 1 | 1 |
| 原子操作（ATOMIC） | 1 | 3 |
| 一次性初始化（JIFINITED） | 1 | 1 |
| 陷阱与 Panic（TRAP） | 1 | 3 |
| **总计** | **64 条枚举值**（含 `count`） | — |

> 主指令受 6 位编码约束，上限 64 条（`_Static_assert(WOORT_OPCODE_count <= 64)`）。实际可用主指令 63 条 + `count` 哨兵。
