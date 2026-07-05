# 值与装箱（Values & Boxing）

本文档介绍 WooRT 的值表示、装箱方案以及内置容器/复合类型的公开 API。权威定义见 [`src/woort_value.h`](../src/woort_value.h) 与 [`include/woort.h`](../include/woort.h) 的 `Stack Value Setters / Readers`、`Vector`、`Map`、`Struct`、`Union` 各节。

## 两种值类型

WooRT 有两种「值」表示，对应两个层面：

| 类型 | 大小 | 可见性 | 用途 |
|------|------|--------|------|
| `woort_Value` | 8 字节 | 仅 `src` 内部（公开为不透明 `char[8]`） | VM 栈槽的原始存储，无标签联合体 |
| `woort_DynBox` | 8 字节 | 公开 API 容器元素的标准载荷 | 动态（装箱）值，自带 3 位类型标签 |

### woort_Value：无标签栈槽联合体

```c
union woort_Value {
    woort_GCUnit*           m_gcinstance;   /* 任意 GC 对象 */
    woort_Int               m_integer;      /* 64 位整数 */
    woort_Real              m_real;         /* double */
    const woort_GCString*   m_string;
    woort_GCVec*            m_vec;
    woort_GCMap*            m_map;
    woort_GCStruct*         m_struct;
    const woort_GCClosure*  m_closure;
    const woort_GCHandle*   m_gchandle;
    const woort_Bytecode*   m_script_function;
    woort_NativeFunction    m_native_function;
    woort_JitFunction       m_jit_function;
    woort_DynBox            m_dynamic;
    woort_RetBP             m_ret_bp;       /* 调用帧：方式 + 偏移 */
    const void*             m_ret_addr;
};
```

栈槽**没有运行时类型标签**——具体存的是什么由当前执行的字节码指令决定（静态类型）。这也是为什么几乎所有算术/比较指令都按类型族特化（`ADDI`/`ADDR`/`ADDS`，详见 [opcodes.md](./opcodes.md)）。

### woort_DynBox：带标签的装箱值

容器（vec/map）的元素、动态类型的值统一用 `woort_DynBox` 表示。它是一个 `uint64_t`，**低 3 位是类型标签**，从而能无分配地内联存储标量：

```c
typedef uint64_t woort_BoxedValue;
typedef union woort_DynBox { woort_BoxedValue m_boxed; } woort_DynBox;
```

```
Boxed 值:      | ............................... | 3 位 type |
Boxed GCUnit:  | ...地址高 61 位...              | 0 | 0 | 0 |
Boxed Float63: | ........... Float63 ............ |       1 |
Boxed Int62:   | ............ Int62 ............. |    1 | 0 |
Boxed Bool:    | ........... Bool61 ............. | 1 | 0 | 0 |
```

类型标签与 `woort_BoxValueType` 对应：

```c
typedef enum woort_BoxValueType {
    WOORT_BOX_VALUE_TYPE_GCUNIT = 0b000,   /* GC 对象（含扩展装箱） */
    WOORT_BOX_VALUE_TYPE_REAL   = 0b001,   /* 内联 Float63 或扩展装箱实数 */
    WOORT_BOX_VALUE_TYPE_INT    = 0b010,   /* 内联 Int62 或扩展装箱整数 */
    WOORT_BOX_VALUE_TYPE_BOOL   = 0b100,   /* 内联 Bool */

    WOORT_BOX_VALUE_TYPE_NIL    = 0b1000,  /* nil（≥此值的标签用于 GC 对象种类细分） */
    WOORT_BOX_VALUE_TYPE_STRING,
    WOORT_BOX_VALUE_TYPE_VEC,
    WOORT_BOX_VALUE_TYPE_MAP,
    WOORT_BOX_VALUE_TYPE_STRUCT,
    WOORT_BOX_VALUE_TYPE_GCHANDLE,
    WOORT_BOX_VALUE_TYPE_CLOSURE,
} woort_BoxValueType;
```

> **为什么 GCUnit 标签是 000？** 所有 GC 对象由 woomem 分配，天然 8 字节对齐，指针低 3 位本来就是 `000`。因此 GC 指针本身就是它的装箱表示，无需额外编码。

### 装箱细节

| 标量 | 内联条件 | 编码方式 | 溢出处理 |
|------|----------|----------|----------|
| **Float63** | 所有「两个最高指数位不同」的有限 double | 丢弃 bit62、左移 1 位、设 tag `001` | 溢出到 `woort_BoxedExValue` |
| **Int62** | `[-2^61, 2^61-1]` | `(val << 2) \| 0b010` | 溢出到 `woort_BoxedExValue` |
| **Bool** | 总是内联 | `(val << 3) \| 0b100` | — |

`woort_BoxedExValue` 是装不下的整数/实数的「溢出盒子」，它本身是一个 GC 对象（proxy 为 `WOORT_EX_BOX_PROXY`，无 mark/无析构），其装箱形式就是它的指针（tag `000`）。区分一个 tag=000 的值到底是字符串、向量还是扩展盒子，需要查它的 `m_proxy`。

### 装箱/拆箱：公开 API（栈槽形式）

公开 API 通过**栈槽**间接构造/读取装箱值，开发者通常**不直接**操作 `woort_DynBox`：

```c
/* 写栈槽（构造装箱值，优先内联，溢出时分配 ExValue） */
void woort_set_box_int(woort_StackValue dst, woort_Int src);
void woort_set_box_real(woort_StackValue dst, woort_Real src);
#define woort_set_box_float(DST, SRC) woort_set_box_real(DST, (woort_Real)SRC)
void woort_set_box_bool(woort_StackValue dst, bool src);

/* 读栈槽（拆箱读取） */
woort_Int          woort_unbox_int(woort_StackValue src);
woort_Real         woort_unbox_real(woort_StackValue src);
#define woort_unbox_float(SRC) ((float)woort_unbox_real(SRC))
bool               woort_unbox_bool(woort_StackValue src);
woort_BoxValueType woort_unbox_type(woort_StackValue src);
woort_BoxValueType woort_unbox(woort_StackValue dst, woort_StackValue src);  /* 拆箱并返回类型 */
```

### 装箱/拆箱：内部 API（DynBox 直接形式）

> 以下函数定义在 `src/woort_value.h`，供 WooRT 内部（容器实现、常量池填充等）使用，**不**在 `include/woort.h` 公开。仅在编写新的内置容器或扩展类型时需要。

```c
/* 构造（优先内联，溢出时分配 ExValue） */
woort_DynBox woort_DynBox_box_int(woort_Int val);
woort_DynBox woort_DynBox_box_real(woort_Real val);
woort_DynBox woort_DynBox_box_bool(bool val);
woort_DynBox woort_DynBox_box(woort_Value val, woort_BoxValueType type);

/* 用于常量池（带 GC 写屏障） */
woort_DynBox woort_DynBox_box_int_for_env_constant(woort_CodeEnv* cenv, woort_Int val);
woort_DynBox woort_DynBox_box_real_for_env_constant(woort_CodeEnv* cenv, woort_Real val);

/* 带写屏障的装箱（写入 GC 可观测位置时用，见 gc.md） */
void woort_DynBox_box_int_with_barrier(woort_DynBox* dst, woort_Int val);
void woort_DynBox_box_real_with_barrier(woort_DynBox* dst, woort_Real val);
void woort_DynBox_box_bool_with_barrier(woort_DynBox* dst, bool val);
void woort_DynBox_box_with_barrier(woort_DynBox* dst, woort_Value val, woort_BoxValueType type);

/* 检查/拆箱 */
bool              woort_DynBox_check(woort_DynBox val, woort_BoxValueType type);
bool              woort_DynBox_unbox(woort_DynBox val, woort_BoxValueType type, woort_Value* out_val);
void              woort_DynBox_unbox_no_check(woort_DynBox val, woort_Value* out_val);
woort_BoxValueType woort_DynBox_unbox_no_check_and_get_type(woort_DynBox val, woort_Value* out_val);

/* 哈希/相等（用于 map 键） */
size_t woort_DynBox_hash(woort_DynBox val);
bool   woort_DynBox_equal(woort_DynBox a, woort_DynBox b);
```

---

## 内置容器类型

所有内置容器都是 GC 对象（继承 `woort_GCUnit`，详见 [gc.md](./gc.md)）。公开 API 以 `woort_StackValue` 为中心——容器存放在某个栈槽，函数通过槽索引操作它。

### GC 对象种类一览

| 类型 | 内部结构 | 元素载荷 | 用途 |
|------|----------|----------|------|
| `woort_GCString` | `m_length` + 柔性 `m_content[]` | UTF-8 字节 | 字符串 |
| `woort_GCVec` | `m_space`, `m_length`, `m_datas[]` | `woort_DynBox` | 动态数组 |
| `woort_GCMap` | 开放寻址哈希 + 侵入式链表桶 | `woort_DynBox` 键/值 | 映射（字典） |
| `woort_GCStruct` | `m_size` + 柔性 `m_datas[]` | `woort_Value`（无标签） | 结构体（异构元组） |
| `woort_GCClosure` | 函数指针 + `m_datas[]` 捕获 | `woort_Value` | 闭包 |
| `woort_GCHandle` | 用户指针 + mark/destruct 回调 | 外部资源 | 外部资源句柄 |
| `woort_BoxedExValue` | `m_is_int` + `m_real`/`m_int` | 标量 | 装箱溢出盒 |

注意 **`GCVec`/`GCMap` 的元素是 `woort_DynBox`（带标签），而 `GCStruct`/`GCClosure` 的槽是 `woort_Value`（无标签）**。这反映了语义：向量/映射是同质的动态集合，结构体/闭包是异构的、编译期已知布局的复合值。

### 字符串

字符串通过 `woort_set_string` / `woort_set_string_fmt` / `woort_set_buffer` 创建（写入栈槽）：

```c
woort_set_string(dst, "hello");                  /* C 字符串 */
woort_set_string_fmt(dst, "x=%d", x);            /* 格式化 */
woort_set_buffer(dst, buf, len);                 /* 任意字节缓冲 */
```

读取：`woort_string(src)` 返回 `woort_U8CString`（UTF-8，NUL 终止）；`woort_buffer(src, &len)` 返回缓冲区与长度。

内部构造（`src/woort_gc_string.h`）：

```c
typedef struct woort_GCString {
    woort_GCUnit m_gc_unit;
    size_t m_length;
    char m_content[];   /* 柔性数组，UTF-8，不含 NUL；读取时按 m_length */
} woort_GCString;
```

### 向量（Vector）

向量是 `woort_DynBox` 元素的动态数组。API（`include/woort.h` 的 `Vector` 节）：

```c
woort_set_vec(dst);                              /* 空向量 */
size_t      woort_vec_len(src);
void        woort_vec_resize(src, new_size);
void        woort_vec_resize_with(src, new_size, init_val);
bool        woort_vec_shrink(src, new_size);
bool        woort_vec_get(dst_boxed, src, index);        /* 取出（装箱值） */
bool        woort_vec_set(src, index, boxed_elem);       /* 写入（装箱值） */
void        woort_vec_push(src, boxed_elem);
bool        woort_vec_pop(src);
bool        woort_vec_insert(src, index, boxed_elem);
bool        woort_vec_erase(src, index);
void        woort_vec_clear(src);
void        woort_vec_copy(dst, src);
void        woort_vec_swap(a, b);
```

> `vec_get`/`vec_set`/`vec_push` 操作的是**装箱值**（DynBox）。

### 映射（Map）

映射是 `woort_DynBox` 键 → `woort_DynBox` 值的哈希表。提供「按键具体类型」的特化访问，避免频繁装箱：

```c
woort_set_map(dst);                              /* 空映射 */
size_t      woort_map_len(src);
void        woort_map_reserve(src, reserve);

/* 通用（装箱键） */
bool        woort_map_get(dst, src, key_boxed);
bool        woort_map_set(src, key_boxed, val_boxed);   /* 返回 true=新增，false=更新 */
bool        woort_map_erase(src, key_boxed);
bool        woort_map_contains(src, key_boxed);

/* 特化键类型 */
bool woort_map_get_by_int / _by_real / _by_bool / _by_string(dst, src, key);
bool woort_map_set_by_int / _by_real / _by_bool / _by_string(src, key, val_boxed);
#define woort_map_set_by_pointer(src, ptr, val) woort_map_set_by_int(src, (woort_Int)(intptr_t)ptr, val)
bool woort_map_erase_by_int / _by_real / _by_bool / _by_string(src, key);
bool woort_map_contains_int / _real / _bool / _string(src, key);

void woort_map_clear(src);
void woort_map_copy(dst, src);
void woort_map_swap(a, b);

/* 迭代：按下标顺序，out_key_boxed/out_val_boxed 可传 WOORT_IGNORE 丢弃 */
bool woort_map_iter(src, index, out_key_boxed, out_val_boxed);
```

### 结构体（Struct）

结构体是 `woort_Value` 元素的定长复合（编译期已知字段类型）。提供类型特化的读写：

```c
woort_set_struct(dst, cap);                      /* 空结构体（指定容量） */
size_t        woort_struct_len(src);
void          woort_struct_get(dst, src, index);
void          woort_struct_set(src, index, val);
woort_Int     woort_struct_get_int(src, index);
woort_Real    woort_struct_get_real(src, index);
woort_U8CString woort_struct_get_string(src, index);
bool          woort_struct_get_bool(src, index);
void          woort_struct_set_int(src, index, val);
void          woort_struct_set_real(src, index, val);
void          woort_struct_set_string(src, index, val);
void          woort_struct_set_bool(src, index, val);
#define woort_struct_get_float(src, index)  ((float)woort_struct_get_real(src, index))
#define woort_struct_get_pointer(src, index) ((void*)woort_struct_get_int(src, index))
```

---

## 复合值：Union / Option / Result

WooRT 支持**带标签联合**（tagged union）：一个判别 id（整数）+ 一个负载值。Option 和 Result 是建立在 union 约定之上的语法糖。

### Union

```c
/* 写：dst ← (id, payload) */
void woort_set_union_without_value(dst, id);           /* unit 变体（无负载）*/
void woort_set_union_value(dst, id, val);              /* 负载为栈槽值 */
void woort_set_union_nil(dst, id);                     /* 负载为 nil */
#define woort_set_union_void woort_set_union_nil
void woort_set_union_int(dst, id, src);                /* 负载为整数 */
#define woort_set_union_pointer(dst, id, src) woort_set_union_int(dst, id, (woort_Int)(intptr_t)src)
void woort_set_union_real(dst, id, src);
void woort_set_union_float(dst, id, src);
void woort_set_union_bool(dst, id, src);
void woort_set_union_string(dst, id, src);
void woort_set_union_string_fmt(dst, id, fmt, ...);
void woort_set_union_buffer(dst, id, src, len);
void woort_set_union_gchandle(dst, id, addr, hold, close, dylib);
void woort_set_union_gcstruct(dst, id, addr, mark, close, dylib);
void woort_set_union_box_int / _box_real / _box_bool(dst, id, src);

/* 读：返回 id，负载复制到 dst */
woort_Int woort_union_get(dst, src);
```

### Option（约定：id=0 为 Some，id=1 为 None）

```c
woort_set_option_value(dst, src);            /* Some(src)，即 union id=0 + src */
woort_set_option_none(dst);                  /* None，即 union id=1 */
woort_set_option_int(dst, src);              /* Some(int) */
woort_set_option_real(dst, src);
woort_set_option_bool(dst, src);
woort_set_option_string(dst, src);
/* ... 其余类型（_float/_pointer/_buffer/_box_*/_gchandle/_gcstruct）同理 ... */

#define woort_option_get(dst, src) (0 == woort_union_get(dst, src))  /* true = Some */
```

### Result（约定：id=0 为 Ok，id=1 为 Err）

Result 宏是 Option 宏的别名（id 仍为 0/1，只是语义不同）：

```c
woort_set_result_ok_value(dst, src);         /* Ok(src)，别名 woort_set_option_value */
woort_set_result_err_value(dst, src);        /* Err(src) */
woort_set_result_ok_int / _real / _bool / _string / ... (dst, src);
woort_set_result_err_int / _real / _bool / _string / ... (dst, src);

#define woort_result_get(dst, src) (0 == woort_union_get(dst, src))  /* true = Ok */
```

### 对应的返回宏

原生函数中可用 `woort_ret_option_*` / `woort_ret_result_*` 系列宏直接返回这些复合值（写 `WOORT_RETURN_SLOT` 并返回 `NORMAL`）。

---

## GC 句柄（GCHandle）

`woort_set_gchandle` / `woort_set_gcstruct` 用于把**外部（非 WooRT）资源**纳入 GC 管理：

```c
void woort_set_gchandle(dst, addr, hold, close, dylib);
void woort_set_gcstruct(dst, addr, mark, close, dylib);
```

* `addr`：外部资源指针。
* `hold`：一个栈槽，其引用用于阻止该资源过早被回收（可传 `WOORT_IGNORE`）。
* `close`（`woort_GCHandle_UserDestructFunction`）：资源被 GC 回收时调用的析构回调。
* `mark`（仅 `set_gcstruct`，`woort_GCHandle_UserMarkFunction`）：标记回调，用于让 GC 追踪外部对象内部的 WooRT 引用。
* `dylib`：可选，关联的动态库（库卸载时一并处理）。

`gchandle` 与 `gcstruct` 的区别：后者带 mark 回调，GC 能穿透它追踪内部引用；前者是不可穿透的不透明句柄。

---

## 容器解包与字段压栈（IR 层）

在 IR 层，容器的解包与字段读取有专用指令（见 [opcodes.md](./opcodes.md) §18–§19）：

* `UNPACKVEC/UNPACKVECX/UNPACKVECALL/UNPACKVECXALL`：向量解包到栈。
* `PUSHIDSTRUCT/PUSHIDSTBOX{I,R,B}`：结构体字段读取并（可选）装箱压栈。
* `MKVEC/MKMAP/MKSTRUCT/MKUNION`：从栈顶元素构造容器。

这些是 Woolang 前端实现「解构」「展开参数」等语法的基础。
