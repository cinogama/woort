# WooRT IR API 设计计划书

## 1. 概述

### 1.1 目标

为 WooRT 运行时提供一个 IR（中间表示）接口，用于生成 CodeEnv，实现：

1. **抽象指令细节**：用户无需关心操作数的位宽限制、指令变体选择
2. **内置栈槽分配**：将无限 SSA 虚拟寄存器映射到栈槽
3. **生成紧凑高效字节码**：自动选择最优指令变体
4. **支持 IR 层面优化**：常量折叠、死代码消除、基本块重排

### 1.2 设计原则

- **无类型系统**：类型检查由 Woolang 编译器前端负责，IR 操作码按类型特化
- **SSA 形式**：每个值唯一定义，便于优化分析
- **局部变量抽象**：提供 `IRLocal` 自动转换为 Phi 节点
- **渐进式 API**：简单场景简单用，高级场景可精细控制

### 1.3 层次结构

```
woort_IRModule          (模块，对应 CodeEnv)
├── woort_IRFunction    (函数)
│   ├── woort_IRBlock   (基本块)
│   │   ├── woort_IRInst (指令)
│   │   └── ...
│   └── ...
└── ...
```

---

## 2. 核心数据类型

### 2.1 IR 值

```c
typedef struct woort_IRValue
{
    uint32_t            m_id;           /* SSA 值 ID */
    woort_IRBlock*      m_def_block;    /* 定义所在块 */
    woort_IRInst*       m_def_inst;     /* 定义指令 */
} woort_IRValue;
```

### 2.2 IR 局部变量

```c
typedef struct woort_IRLocal
{
    uint32_t            m_id;           /* 局部变量 ID */
    woort_IRFunction*   m_function;     /* 所属函数 */
    woort_IRValue**     m_current_vals; /* [block_id] -> current SSA value */
} woort_IRLocal;
```

### 2.3 操作码（类型特化）

```c
typedef enum woort_IROpcode
{
    /* 终止指令 */
    WOORT_IR_OP_RET,
    WOORT_IR_OP_BR,
    WOORT_IR_OP_COND_BR,

    /* 整数算术 */
    WOORT_IR_OP_ADD_I,
    WOORT_IR_OP_SUB_I,
    WOORT_IR_OP_MUL_I,
    WOORT_IR_OP_DIV_I,
    WOORT_IR_OP_MOD_I,
    WOORT_IR_OP_NEG_I,

    /* 实数算术 */
    WOORT_IR_OP_ADD_R,
    WOORT_IR_OP_SUB_R,
    WOORT_IR_OP_MUL_R,
    WOORT_IR_OP_DIV_R,
    WOORT_IR_OP_MOD_R,
    WOORT_IR_OP_NEG_R,

    /* 整数比较 */
    WOORT_IR_OP_LT_I,
    WOORT_IR_OP_LE_I,
    WOORT_IR_OP_GT_I,
    WOORT_IR_OP_GE_I,
    WOORT_IR_OP_EQ_I,
    WOORT_IR_OP_NE_I,

    /* 实数比较 */
    WOORT_IR_OP_LT_R,
    WOORT_IR_OP_LE_R,
    WOORT_IR_OP_GT_R,
    WOORT_IR_OP_GE_R,
    WOORT_IR_OP_EQ_R,
    WOORT_IR_OP_NE_R,

    /* 字符串操作 */
    WOORT_IR_OP_ADD_S,
    WOORT_IR_OP_LT_S,
    WOORT_IR_OP_LE_S,
    WOORT_IR_OP_GT_S,
    WOORT_IR_OP_GE_S,
    WOORT_IR_OP_EQ_S,
    WOORT_IR_OP_NE_S,

    /* 逻辑运算 */
    WOORT_IR_OP_AND,
    WOORT_IR_OP_OR,
    WOORT_IR_OP_NOT,

    /* 容器创建 */
    WOORT_IR_OP_MKVEC,
    WOORT_IR_OP_MKMAP,
    WOORT_IR_OP_MKSTRUCT,

    /* 容器加载 */
    WOORT_IR_OP_LDVEC,
    WOORT_IR_OP_LDSTR,
    WOORT_IR_OP_LDSTRUCT,
    WOORT_IR_OP_LDMAP_I,
    WOORT_IR_OP_LDMAP_R,
    WOORT_IR_OP_LDMAP_B,
    WOORT_IR_OP_LDMAP_X,

    /* 容器存储 - 向量 */
    WOORT_IR_OP_STVEC_I,
    WOORT_IR_OP_STVEC_R,
    WOORT_IR_OP_STVEC_B,
    WOORT_IR_OP_STVEC_X,

    /* 容器存储 - 字典 */
    WOORT_IR_OP_STMAP_I_I,
    WOORT_IR_OP_STMAP_I_R,
    WOORT_IR_OP_STMAP_I_B,
    WOORT_IR_OP_STMAP_I_X,
    WOORT_IR_OP_STMAP_R_I,
    WOORT_IR_OP_STMAP_R_R,
    WOORT_IR_OP_STMAP_R_B,
    WOORT_IR_OP_STMAP_R_X,
    WOORT_IR_OP_STMAP_B_I,
    WOORT_IR_OP_STMAP_B_R,
    WOORT_IR_OP_STMAP_B_B,
    WOORT_IR_OP_STMAP_B_X,
    WOORT_IR_OP_STMAP_X_I,
    WOORT_IR_OP_STMAP_X_R,
    WOORT_IR_OP_STMAP_X_B,
    WOORT_IR_OP_STMAP_X_X,

    /* 容器存储 - 结构体 */
    WOORT_IR_OP_STSTRUCT,

    /* 函数 */
    WOORT_IR_OP_CALL,
    WOORT_IR_OP_MKCLOSURE,

    /* 类型转换 */
    WOORT_IR_OP_CAST_I_TO_R,
    WOORT_IR_OP_CAST_R_TO_I,
    WOORT_IR_OP_BOX_DYN,
    WOORT_IR_OP_UNBOX_DYN,

    /* Phi */
    WOORT_IR_OP_PHI,

    /* 常量 */
    WOORT_IR_OP_CONST_INT,
    WOORT_IR_OP_CONST_REAL,
    WOORT_IR_OP_CONST_BOOL,
    WOORT_IR_OP_CONST_STR,
    WOORT_IR_OP_CONST_NULL,

    /* 参数 */
    WOORT_IR_OP_PARAM,

} woort_IROpcode;
```

### 2.4 指令

```c
typedef struct woort_IRInst
{
    woort_IROpcode          m_op;
    woort_IRValue*          m_result;
    woort_IRValue**         m_operands;
    uint32_t                m_operand_count;
    woort_IRInst*           m_next;
    woort_IRInst*           m_prev;
    woort_IRBlock**         m_phi_src_blocks;
} woort_IRInst;
```

### 2.5 基本块

```c
typedef struct woort_IRBlock
{
    woort_IRFunction*       m_function;
    uint32_t                m_id;
    woort_IRInst*           m_first;
    woort_IRInst*           m_last;
    woort_IRBlock*          m_next;
    woort_IRBlock*          m_prev;
    woort_IRBlock**         m_preds;
    uint32_t                m_pred_count;
    woort_IRBlock**         m_succs;
    uint32_t                m_succ_count;
    woort_IRInst*           m_phis;
    bool                    m_is_sealed;
} woort_IRBlock;
```

### 2.6 函数

```c
typedef struct woort_IRFunction
{
    woort_IRModule*         m_module;
    const char*             m_name;
    uint32_t                m_id;
    uint32_t                m_param_count;
    woort_IRBlock*          m_entry_block;
    woort_IRBlock*          m_block_list;
    uint32_t                m_next_value_id;
    uint32_t                m_next_local_id;
} woort_IRFunction;
```

### 2.7 模块

```c
typedef struct woort_IRModule
{
    woort_IRFunction**      m_functions;
    uint32_t                m_function_count;
    uint32_t                m_function_capacity;
    woort_IRArena*          m_arena;
} woort_IRModule;
```

---

## 3. Builder API

### 3.1 模块与函数

```c
WOORT_NODISCARD bool woort_IRModule_create(woort_IRModule** out_module);
void woort_IRModule_destroy(woort_IRModule* module);

WOORT_NODISCARD bool woort_IRModule_add_function(
    woort_IRModule* module,
    const char* name,
    uint32_t param_count,
    woort_IRFunction** out_func);
```

### 3.2 Builder 创建与基本块

```c
WOORT_NODISCARD bool woort_IRBuilder_create(
    woort_IRFunction* func,
    woort_IRBuilder** out_builder);
void woort_IRBuilder_destroy(woort_IRBuilder* builder);

WOORT_NODISCARD bool woort_IRBuilder_create_block(
    woort_IRBuilder* builder,
    woort_IRBlock** out_block);
void woort_IRBuilder_position_at_end(woort_IRBuilder* builder, woort_IRBlock* block);
woort_IRBlock* woort_IRBuilder_get_insert_block(woort_IRBuilder* builder);
void woort_IRBlock_seal(woort_IRBlock* block);
```

### 3.3 局部变量

```c
WOORT_NODISCARD bool woort_IRBuilder_create_local(
    woort_IRBuilder* builder,
    woort_IRLocal** out_local);
void woort_IRBuilder_set_local(woort_IRBuilder* builder, woort_IRLocal* local, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_get_local(
    woort_IRBuilder* builder,
    woort_IRLocal* local,
    woort_IRValue** out_value);
```

### 3.4 终止指令

```c
void woort_IRBuilder_ret_void(woort_IRBuilder* builder);
void woort_IRBuilder_ret(woort_IRBuilder* builder, woort_IRValue* value);
void woort_IRBuilder_br(woort_IRBuilder* builder, woort_IRBlock* dest);
void woort_IRBuilder_cond_br(
    woort_IRBuilder* builder,
    woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);
```

### 3.5 整数运算

```c
WOORT_NODISCARD bool woort_IRBuilder_add_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_sub_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_mul_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_div_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_mod_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_neg_i(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
```

### 3.6 实数运算

```c
WOORT_NODISCARD bool woort_IRBuilder_add_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_sub_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_mul_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_div_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_mod_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_neg_r(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
```

### 3.7 整数比较

```c
WOORT_NODISCARD bool woort_IRBuilder_lt_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_le_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_gt_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ge_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_eq_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ne_i(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
```

### 3.8 实数比较

```c
WOORT_NODISCARD bool woort_IRBuilder_lt_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_le_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_gt_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ge_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_eq_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ne_r(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
```

### 3.9 字符串操作

```c
WOORT_NODISCARD bool woort_IRBuilder_add_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_lt_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_le_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_gt_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ge_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_eq_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ne_s(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
```

### 3.10 逻辑运算

```c
WOORT_NODISCARD bool woort_IRBuilder_and(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_or(
    woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_not(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
```

### 3.11 常量

```c
WOORT_NODISCARD bool woort_IRBuilder_const_int(
    woort_IRBuilder* builder, int64_t value, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_const_real(
    woort_IRBuilder* builder, double value, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_const_bool(
    woort_IRBuilder* builder, bool value, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_const_str(
    woort_IRBuilder* builder, const char* str, size_t len, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_const_null(
    woort_IRBuilder* builder, woort_IRValue** out_result);
```

### 3.12 参数

```c
WOORT_NODISCARD bool woort_IRBuilder_param(
    woort_IRBuilder* builder, uint32_t index, woort_IRValue** out_result);
```

### 3.13 类型转换

```c
WOORT_NODISCARD bool woort_IRBuilder_cast_i_to_r(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_cast_r_to_i(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_box_dyn(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_unbox_dyn(
    woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result);
```

### 3.14 容器创建

```c
WOORT_NODISCARD bool woort_IRBuilder_mkvec(
    woort_IRBuilder* builder, woort_IRValue** elems, uint32_t count, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_mkmap(
    woort_IRBuilder* builder, woort_IRValue** kvs, uint32_t count, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_mkstruct(
    woort_IRBuilder* builder, woort_IRValue** fields, uint32_t count, woort_IRValue** out_result);
```

### 3.15 容器加载

```c
WOORT_NODISCARD bool woort_IRBuilder_ldvec(
    woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ldstr(
    woort_IRBuilder* builder, woort_IRValue* str, woort_IRValue* index, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ldstruct(
    woort_IRBuilder* builder, woort_IRValue* st, uint32_t field_index, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ldmap_i(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ldmap_r(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ldmap_b(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result);
WOORT_NODISCARD bool woort_IRBuilder_ldmap_x(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result);
```

### 3.16 容器存储 - 向量

```c
WOORT_NODISCARD bool woort_IRBuilder_stvec_i(
    woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stvec_r(
    woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stvec_b(
    woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stvec_x(
    woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value);
```

### 3.17 容器存储 - 字典

```c
WOORT_NODISCARD bool woort_IRBuilder_stmap_i_i(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_i_r(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_i_b(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_i_x(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_r_i(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_r_r(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_r_b(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_r_x(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_b_i(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_b_r(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_b_b(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_b_x(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_x_i(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_x_r(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_x_b(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
WOORT_NODISCARD bool woort_IRBuilder_stmap_x_x(
    woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value);
```

### 3.18 容器存储 - 结构体

```c
WOORT_NODISCARD bool woort_IRBuilder_ststruct(
    woort_IRBuilder* builder, woort_IRValue* st, uint32_t field_index, woort_IRValue* value);
```

### 3.19 函数调用

```c
WOORT_NODISCARD bool woort_IRBuilder_call(
    woort_IRBuilder* builder,
    woort_IRValue* func,
    woort_IRValue** args,
    uint32_t arg_count,
    /* OPTIONAL */ woort_IRValue** out_result);
```

### 3.20 代码生成

```c
WOORT_NODISCARD bool woort_IRModule_codegen(
    woort_IRModule* module,
    woort_CodeEnv** out_codeenv);
```

---

## 4. IRLocal 与自动 Phi 插入

### 4.1 使用示例

```c
/* int sum = 0; for i in 0..n { sum = sum + i; } return sum; */

woort_IRLocal* sum;
woort_IRBuilder_create_local(builder, &sum);

/* sum = 0 */
woort_IRValue* zero;
woort_IRBuilder_const_int(builder, 0, &zero);
woort_IRBuilder_set_local(builder, sum, zero);

/* 循环体 */
woort_IRBuilder_position_at_end(builder, loop_body);
woort_IRValue* current;
woort_IRBuilder_get_local(builder, sum, &current);  /* 自动 Phi */
woort_IRValue* i = ...;
woort_IRValue* new_sum;
woort_IRBuilder_add_i(builder, current, i, &new_sum);
woort_IRBuilder_set_local(builder, sum, new_sum);

/* 返回 */
woort_IRBuilder_position_at_end(builder, exit_block);
woort_IRValue* final_sum;
woort_IRBuilder_get_local(builder, sum, &final_sum);
woort_IRBuilder_ret(builder, final_sum);
```

### 4.2 seal 机制

```c
/* 封闭块：标记该块不再有新的前驱 */
void woort_IRBlock_seal(woort_IRBlock* block);
```

典型使用：
- 线性代码：每构建完一个块就封闭
- 循环：循环体块在构建完后封闭
- 条件分支：then/else 块封闭后，merge 块在所有前驱添加后封闭

---

## 5. 文件组织

```
src/
├── ir/
│   ├── woort_ir.h              /* IR 公共头文件 */
│   ├── woort_ir_arena.h        /* Arena 分配器 */
│   ├── woort_ir_value.h        /* 值定义 */
│   ├── woort_ir_inst.h         /* 指令定义 */
│   ├── woort_ir_block.h        /* 基本块定义 */
│   ├── woort_ir_local.h        /* 局部变量定义 */
│   ├── woort_ir_function.h     /* 函数定义 */
│   ├── woort_ir_module.h       /* 模块定义 */
│   ├── woort_ir_builder.h      /* Builder API */
│   ├── woort_ir_codegen.h      /* 字节码生成 */
│   └── woort_ir_stackalloc.h   /* 栈槽分配 */
└── ...
```

---

## 6. 实现计划

| 阶段 | 内容 | 状态 |
|------|------|------|
| 一 | Arena + IR 结构 + Builder（无 Phi） | ✅ 已完成 |
| 二 | IRLocal + 自动 Phi 插入 | ✅ 已完成 |
| 三 | 栈槽分配 + 字节码发射 | ✅ 已完成 |
| 四 | 控制流 + CFG 分析 | ✅ 已完成 |
| 五 | 优化 Pass | 待开始 |
| 六 | 完善与测试 | ✅ 已完成 |

---

## 7. 当前实现状态

### 7.1 已实现功能

- **Arena 内存分配器**：高效的内存池管理
- **IR 核心数据结构**：Value, Inst, Block, Function, Module
- **Builder API**：完整的指令发射接口
  - 整数/实数/字符串算术和比较运算
  - 逻辑运算
  - 常量创建
  - 参数访问
  - 类型转换
  - 容器创建和操作
  - 函数调用
  - 控制流（br, cond_br, ret）
- **IRLocal 自动 Phi 插入**：基于 Braun 算法
- **栈槽分配器**：简单的线性分配
- **字节码生成**：支持基础指令

### 7.2 测试覆盖

- 简单函数返回常量
- 二元运算
- 局部变量读写

### 7.3 待实现

- 更多指令的字节码生成支持
- 常量折叠优化
- 死代码消除
- 基本块重排
- 循环优化