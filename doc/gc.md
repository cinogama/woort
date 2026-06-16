# 垃圾回收（GC）

本文档介绍 WooRT 的 GC 架构：woomem 分层、`woort_GCUnit` 对象模型、两阶段分配模式、写屏障，以及 root set 与标记同步。权威定义见 [`3rd/woomem/include/woomem.h`](../3rd/woomem/include/woomem.h)、[`src/woort_gc.h`](../src/woort_gc.h)、[`src/woort_gc_units.h`](../src/woort_gc_units.h)、[`src/woort_gc.c`](../src/woort_gc.c)。

## 分层架构

```
┌─────────────────────────────────────────────┐
│  应用 / Woolang 前端                          │
├─────────────────────────────────────────────┤
│  WooRT 运行时（VM、容器、IR）                  │
│  ├─ woort_GCUnit / woort_GCUnitProxy（类型表）│
│  ├─ 写屏障（src/woort_gc.h，static inline）   │
│  └─ GC 生命周期（bootup/shutdown/root VM）    │
├─────────────────────────────────────────────┤
│  woomem（GC 子模块，独立仓库）                 │
│  ├─ 堆分配 / mark-sweep                       │
│  ├─ marking 状态标志 + mark API               │
│  └─ 回调 WooRT 的 marker/destructor trampoline │
└─────────────────────────────────────────────┘
```

* **woomem** 拥有堆、执行 mark/sweep、维护全局 marking 状态，并通过回调通知 WooRT。
* **WooRT** 在 woomem 之上加了「类型表」（`woort_GCUnitProxy`）和「写屏障」，使具体对象类型（string/vec/map/...）可被正确标记与析构。

## 全局状态（woomem）

```c
extern uint8_t woomem_gc_marking_round_counter;       /* mark 轮次计数 */
extern bool    woomem_gc_marking_state_flag;          /* true = 当前处于 mark 阶段 */
extern size_t  woomem_gc_memory_size_after_last_round_sweep;
```

`woomem_gc_marking_state_flag` 是**所有写屏障的门控**——mark 阶段之外，写屏障退化为普通赋值，零开销。

### 分配属性（woomem_Attrib）

```c
WOOMEM_ATTRIB_NEED_SWEEP      /* 需要在 sweep 阶段回收 */
WOOMEM_ATTRIB_AUTO_MARK       /* woomem 自动扫描对象内部的内嵌指针 */
WOOMEM_ATTRIB_MARK_CALLBACK   /* 调用注册的 mark 回调 */
WOOMEM_ATTRIB_FREE_CALLBACK   /* sweep 时调用析构回调 */
```

WooRT 的封装宏（`src/woort_gc_units.h`）：

| 宏 | 含义 |
|----|------|
| `WOORT_GCUNIT_ALLOC_ATTRIB_O` | 普通对象（仅 `NEED_SWEEP`） |
| `_A` | `AUTO_MARK` |
| `_M` | `MARK_CALLBACK` |
| `_F` | `FREE_CALLBACK` |
| `_AM`/`_AF`/`_MF`/`_AMF` | 组合 |

---

## woort_GCUnit：对象模型

所有 GC 对象都以 `woort_GCUnit` 为**首成员**：

```c
typedef struct woort_GCUnitProxy {
    /* OPTIONAL */ woort_GCUnitProxy_MarkCallback   m_marker;     /* mark 回调 */
    /* OPTIONAL */ woort_GCUnitProxy_DestructCallback m_destructor; /* 析构回调 */
} woort_GCUnitProxy;

struct woort_GCUnit {
    const woort_GCUnitProxy* m_proxy;   /* 指向静态类型表 */
};
```

`m_proxy` 是该类型的「虚表」：一个指向静态分配的 `woort_GCUnitProxy` 常量的指针，携带该类型的 mark + 析构回调。因为 `woort_GCUnit` 总是首成员，任何 `woort_GCUnit*` 都能向上转型为具体类型。

woomem 通过两个 trampoline 回调到 WooRT，再由 proxy 分派（`src/woort_gc.c`）：

```c
static void _woort_GC_marker_callback(void* unit) {
    woort_GCUnit* gcunit = unit;
    gcunit->m_proxy->m_marker(gcunit);
}
static void _woort_GC_destroier_callback(void* unit) {
    woort_GCUnit* gcunit = unit;
    gcunit->m_proxy->m_destructor(gcunit);
}
```

每个具体类型导出一个单例 proxy，例如：

| 类型 | Proxy 常量 | 定义 |
|------|-----------|------|
| `woort_GCString` | `WOORT_GCSTRING_UNIT_PROXY` | `src/woort_gc_string.h` |
| `woort_GCVec` | `WOORT_GCVEC_UNIT_PROXY` | `src/woort_gc_vec.h` |
| `woort_GCMap` | `WOORT_GCMAP_UNIT_PROXY` | `src/woort_gc_map.h` |
| `woort_GCStruct` | `WOORT_GCSTRUCT_UNIT_PROXY` | `src/woort_gc_struct.h` |
| `woort_GCClosure` | `WOORT_GCCLOSURE_UNIT_PROXY` | `src/woort_gc_closure.h` |
| `woort_GCHandle` | `WOORT_GCHANDLE_UNIT_PROXY` | `src/woort_gc_gchandle.h` |
| `woort_BoxedExValue` | `WOORT_EX_BOX_PROXY`（mark=NULL，析构=NULL） | `src/woort_value.h` |

---

## 两阶段分配模式（必需）

woomem 的并发 mark/sweep 要求分配与「向 GC 注册」分离，否则 GC 的 marker 回调可能命中一个半构造（`m_proxy` 还是 NULL）的对象。因此 WooRT 强制使用**两阶段分配**：

```c
/* 阶段 1：分配未初始化、未跟踪的内存（不暴露给 GC）*/
MyType* obj = woort_GCUnit_alloc_delay_init(sizeof(MyType));

/* 阶段 1.5：写入 m_proxy 及 mark 回调会读取的所有字段 */
obj->m_gc_unit.m_proxy = &MY_TYPE_UNIT_PROXY;
obj->m_field = ...;

/* 阶段 2：向 woomem 注册，此刻起对象对 GC 可见 */
woort_GCUnit_init_delay_alloc(ATTRIB, obj);
```

* `woort_GCUnit_alloc_delay_init(sz)`：循环调用 `woomem_allocate_begin`，OOM 时触发一次 GC 后重试。返回的内存**尚未注册**。
* `woort_GCUnit_init_delay_alloc(ATTRIB, PTR)` 宏：展开为 `woomem_allocate_end(PTR, NEED_SWEEP | ATTRIB)`。这是对象对 GC 可见的时刻。

> **可能失败的分配**：若需要在分配后、注册前执行可能失败的操作，改用 `woomem_allocate_begin(sz)` 直接分配，失败可中途放弃；成功初始化字段后再 `woort_GCUnit_init_delay_alloc`。

还有 `woort_GCUnit_realloc(ptr, sz)` 用于原地扩容（调用 `woomem_reallocate`）。

---

## 写屏障（Write Barriers）

写屏障保证在 mark 阶段「被覆盖的旧值」与「写入的新值」都不会被错误回收。所有写屏障都在 `src/woort_gc.h` 中以 `static inline` 定义，**由 `woomem_gc_marking_state_flag` 门控**——非 mark 阶段时退化为普通赋值。

> AGENTS.md 规定：**写入 GC 引用时必须使用写屏障**。这是硬性要求。

### 三类写屏障

| 类别 | 用途 | 函数（按槽位类型） |
|------|------|-------------------|
| **mixed** | 覆盖一个**已存在**的槽（旧值和新值都要保活） | `_gcaddr` / `_gcunit` / `_value` / `_dynbox` |
| **init** | **首次**初始化一个槽（只需保活新值） | `_gcaddr` / `_gcunit` / `_value` / `_dynbox` |
| **delete** | 销毁/移除一个**未被写屏障覆盖**的引用 | `_gcaddr` / `_gcunit` / `_value` / `_dynbox` |

后缀含义：

| 后缀 | 槽位类型 | 标记方式 |
|------|----------|----------|
| `_gcaddr` | 裸 `void*`，可能指向对象内部（内部指针） | `woomem_mark_fuzzy_unit`（src 与 old 都用 fuzzy） |
| `_gcunit` | 裸 `void*`，指向对象**头部**（`woort_GCUnit`） | src 用 `mark_unit_head`（精确），old 用 `mark_fuzzy_unit_head` |
| `_value` | `woort_Value` 联合体 | 经 `m_gcinstance` 字段 fuzzy 标记 |
| `_dynbox` | `woort_DynBox`（装箱值） | 仅当 `boxed != 0 && (boxed & 0b111) == 0`（即 GCUNIT 标签）时标记 |

### 使用场景

```c
/* 写一个 GC 对象指针到容器内部的槽（覆盖旧值）*/
woort_GC_mixed_write_barrier_gcunit(&container->m_slot, new_obj);

/* 首次写入一个 woort_Value 槽 */
woort_GC_init_write_barrier_value(&slot, val);

/* 从容器移除一个元素前，对被移除值发 delete barrier */
woort_GC_delete_barrier_dynbox(removed_box);
```

公开 API 层的封装：

| 公开函数 | 内部调用 |
|----------|----------|
| `woort_GC_set_internal_value_with_mixed_write_barrier(dst, val)` | `mixed_write_barrier_value` |
| `woort_GC_internal_value_delete_barrier(dst)` | `delete_barrier_value` |
| `woort_GC_set_addr_with_mixed_write_barrier(dst, p)` | `mixed_write_barrier_gcaddr` |
| `woort_GC_addr_delete_barrier(p)` | `delete_barrier_gcaddr` |

`woort_DynBox_box_*_with_barrier`（见 [values.md](./values.md)）内部使用 `init_write_barrier_dynbox`，用于向容器写入装箱值。

> **fuzzy vs head**：fuzzy 标记（`mark_fuzzy_unit[_head]`）允许传入内部指针或已被 sweep 的地址；head 标记（`mark_unit_head`）要求传入有效的对象头部指针。具体见 woomem 头文件。

---

## GC 生命周期

```c
bool woort_GC_bootup(size_t reserving_memory_size);   /* woort_init 内部调用 */
void woort_GC_shutdown(void);                          /* woort_shutdown 内部调用 */
```

`woort_GC_bootup` 调用 `woomem_init`，注册四个回调：

| 回调 | 作用 |
|------|------|
| `_woort_GC_start_callback` | mark 阶段开始：遍历所有 root VM，请求 GC 检查点 |
| `_woort_GC_stop_mark_callback` | mark 阶段结束 |
| `_woort_GC_marker_callback` | 分派到对象的 `m_proxy->m_marker` |
| `_woort_GC_destroier_callback` | 分派到对象的 `m_proxy->m_destructor` |

### 回收触发

**没有** `woort_GC_collect`。回收由 woomem 的 `woomem_trigger_gc(bool async)` 触发，WooRT 在两处调用：

1. **分配失败**：`_woort_GCUnit_alloc_failed`（`src/woort_gc_units.c`）临时脱离当前 VM（保存/恢复 `m_sp`），同步触发 GC 后重试分配。
2. **关闭**：`woort_GC_shutdown` 执行最后一次 mark → finalize → sweep。

---

## Root Set

GC 的根集合包括：

### 1. 注册的 VM（自动 root）

```c
bool woort_GC_register_root_vm(vm);
void woort_GC_unregister_root_vm(vm);
void woort_GC_foreach_root_vm(callback, user_data);
```

每个运行中的 VM 都是 root。mark 开始时，`_woort_GC_start_callback` 遍历所有 root VM，请求 GC 检查点，等待 VM 自标记其栈/env（`woort_VMRuntime_mark_vm_after_sync`），或由 GC 线程代理标记。

### 2. 弱 VM（手动标记）

```c
void woort_VMRuntime_weaken(vm);                 /* 让 GC 不再自动当作 root */
void woort_GC_mark_weak_vm_manually(vm);         /* 在 mark 回调中手动标记 */
```

`weaken` 后，用户必须从 GCHandle 的 mark 回调中调用 `mark_weak_vm_manually` 来保活该 VM 的对象。

### 3. 显式 root

```c
void* woort_GC_allocate_as_root(sz, attribute);  /* 分配并注册为 root */
void  woort_GC_unregister_root(p);               /* 从 root 集移除 */
void  woort_GC_mark_addr_manually(p);            /* 手动标记一个指针 */
void  woort_GC_mark_droped_env_manually(env);    /* 手动标记 dropped CodeEnv */
void  woort_GC_mark_internal_value_manually(val);/* 手动标记一个 woort_Value */
```

### 4. GC Pin

`woort_GCPin` 是固定大小的值槽数组，本身作为 root：

```c
woort_GCPin* woort_GCPin_create(count);
void  woort_GCPin_destroy(pin);
void  woort_GCPin_set(pin, idx, stack_val);
void  woort_GCPin_get(dst, pin, idx);
void  woort_GCPin_set_dup_boxed(pin, idx, val);     /* 深拷贝 vec/map/struct */
void  woort_GCPin_set_internal(pin, idx, val_ptr);  /* 直接从 woort_Value 写（无需 active VM）*/
void  woort_GCPin_get_internal(dst, pin, idx);
```

`_internal` 变体不要求 active VM，但仍要求 GC 作用域（见下）。

---

## GC 作用域与标记同步锁

每个线程要么有一个 active VM（VM 运行提供 GC 检查点保证），要么需要显式获取 GC 作用域：

```c
bool woort_GC_sync_marking_lock(void);     /* 返回 true = 调用方必须配对 unlock */
void woort_GC_sync_marking_unlock(void);
```

* **用途**：当一个线程**没有运行中的 VM**，但又必须安全地操作 `woort_Value`（如拷贝值到 GCPin）时使用。
* **机制**：获取 GC 阶段切换锁的读锁，阻塞 GC 在 mark/sweep 阶段间切换。
* **返回值语义**：返回 `true` 表示调用方获得了锁（**必须**调用 `unlock`）；返回 `false` 表示当前线程已有 active VM（自带检查点，**不要**调用 `unlock`）。
* **限制**：持锁期间禁止 `woort_VMRuntime_swap`（会死锁）。

典型用法：

```c
bool locked = woort_GC_sync_marking_lock();
/* ... 安全地操作 woort_Value / GCPin ... */
if (locked) woort_GC_sync_marking_unlock();
```

---

## 内部容器（非 GC）

`src/` 中还有两个**非 GC** 的容器原语，用于 GC 上下文、CodeEnv 等内部数据结构，**不**通过 `woort.h` 公开：

* **`woort_HashMap`**（`src/woort_hashmap.h`）：桶数组哈希表，键/值为任意定长 blob（由 `hash_fn`/`equal_fn` 配置）。
* **`woort_OrderMap`**（`src/woort_ordermap.h`）：有序红黑树，支持范围查询（`lower_bound`/`upper_bound`/`find_le`）。

> 这些**不是** Woolang 层的 map。Woolang 的 map 是 GC 对象 `woort_GCMap`（键/值为 `woort_DynBox`），见 [values.md](./values.md)。

---

## 内部实现要点（开发者）

* 写入 GC 引用必须配写屏障——这是 AGENTS.md 的硬性要求。选错屏障类别（mixed/init/delete）或槽位后缀（gcaddr/gcunit/value/dynbox）会导致 GC 丢失对象或误标。
* 新增 GC 类型时：(1) 定义以 `woort_GCUnit m_gc_unit` 为首成员的结构；(2) 实现 mark/destructor；(3) 导出单例 `woort_GCUnitProxy`；(4) 用两阶段分配构造。
* mark 回调中若需要访问其他 VM（如弱 VM），使用 `woort_GC_mark_weak_vm_manually` 等手动标记 API。
* `woort_DynBox` 的 GCUNIT 判定：`boxed != 0 && (boxed & 0b111) == 0`（低 3 位全 0）。这是 `_dynbox` 系写屏障判断是否需要标记的依据。
