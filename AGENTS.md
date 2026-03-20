# AGENTS.md - WooRT Codebase Guide

## Project Overview

WooRT (Woolang Runtime V1.0) is a C11 runtime for the Woolang scripting language featuring a bytecode interpreter, GC-based memory manager (woomem), and debugging support.

## Build Commands

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64          # Windows
cmake -B build -DCMAKE_BUILD_TYPE=Debug                   # macOS/Linux

# Build
cmake --build build --config Debug                         # Windows
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)  # Unix

# Test
ctest --test-dir build -C Debug --output-on-failure        # All tests
ctest --test-dir build -C Debug -R <test_name> --output-on-failure  # Single test

# Submodules (required before first build)
git submodule sync --recursive && git submodule update --init --recursive
```

## Code Style

### Language
- C11 only (no C++)
- Use `/* */` comments, not `//`
- Chinese comments are acceptable

### Header Guards
```c
#pragma once

/*
 * filename.h
 */
```

### Include Order
```c
#include "woort.h"           /* 1. Project public header */
#include "woort_diagnosis.h" /* 2. Project internal headers */
#include "woomem.h"          /* 3. Dependency headers */
#include <stdint.h>          /* 4. System headers */
```

### Naming Conventions

| Type | Pattern | Example |
|------|---------|---------|
| Types | `woort_ModuleName` | `woort_VMRuntime`, `woort_HashMap` |
| Functions | `woort_module_function` | `woort_hashmap_init`, `woort_VMRuntime_create` |
| Macros/Constants | `WOORT_MACRO_NAME` | `WOORT_NODISCARD`, `WOORT_API` |
| Struct members | `m_member_name` | `m_size`, `m_bucket_count` |
| Enum values | `WOORT_ENUM_VALUE` | `WOORT_HASHMAP_RESULT_OK` |
| Globals | `g_variable_name` | `g_gc_in_marking` |
| Internal functions | `_woort_module_func` | `_woort_hashmap_rehash` |

### WOORT_NODISCARD (Required)

**任何返回值类型非 void 的函数，都必须标记为 WOORT_NODISCARD。**

```c
WOORT_NODISCARD bool woort_hashmap_find(woort_HashMap* map, const void* key, void** out_value_addr);
WOORT_NODISCARD woort_hashmap_Result woort_hashmap_insert(woort_HashMap* map, const void* key, const void* value);
```

### /* OPTIONAL */ (Required)

**如果一个成员、参数、局部变量、返回值可能为空（NULL），类型前都必须加 `/* OPTIONAL */`。**

```c
/* 结构体成员 */
typedef struct woort_HashMap {
    /* OPTIONAL */ struct woort_HashMapEntry** m_buckets;
    /* OPTIONAL */ struct woort_HashMapEntry* m_free_entries;
} woort_HashMap;

/* 函数参数 */
void woomem_init(
    /* OPTIONAL */ woomem_UserContext user_ctx,
    /* OPTIONAL */ woomem_MarkCallbackFunc marker);

/* 函数返回值 */
WOORT_NODISCARD /* OPTIONAL */ void* woomem_alloc_normal(size_t size);
WOORT_NODISCARD /* OPTIONAL */ woort_HashMapEntry* _woort_hashmap_get_free_entry(woort_HashMap* map);
```

### Error Handling

1. **Return bool for success/failure** (`true` = success)
2. **Output via pointer-to-pointer**: `Type** out_result`
3. **Panic for unrecoverable errors**: `woort_panic(reason, msgfmt, ...)`
4. **Result enums for multiple outcomes**: `woort_hashmap_Result` with `WOORT_HASHMAP_RESULT_*`

### Function Declaration Style
```c
void woort_hashmap_clear(woort_HashMap* map);  /* Short: one line */

/* Long: parameter per line */
WOORT_NODISCARD bool woort_hashmap_get_or_emplace(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);
```

### Inline Functions
```c
static inline void woort_GC_mixed_write_barrier_value(woort_Value* modified_value, woort_Value src_value)
{
    if (g_gc_in_marking)
        woomem_try_mark_unit((intptr_t)src_value.m_gcinstance);
    *modified_value = src_value;
}
```

### Macros
```c
#define woort_RuntimeFunction_kind(function) ((woort_RuntimeFunction_Kind)(((woort_RuntimeFunction)(function)) >> 62))
```

### Platform Detection
```c
#if defined(_MSC_VER)
    /* MSVC */
#elif defined(__clang__) || defined(__GNUC__)
    /* Clang/GCC */
#endif
```

### C++ Compatibility
Public headers (`include/woort.h`) must have `extern "C"` guards.

### Static Assertions
```c
_Static_assert(sizeof(woort_Value) == sizeof(woort_value),
    "woort_Value and woort_value must have the same size");
```

## Memory Management (woomem)

- GC-managed objects inherit from `woort_GCUnit`
- Use `woort_GCUnit_alloc_attrib(ATTRIB, SIZE)` macro
- Write barriers required when writing to GC-managed memory:
  - `woort_GC_mixed_write_barrier_value()`
  - `woort_GC_mixed_write_barrier_dynbox()`

## Project Structure

```
woort/
  include/woort.h    # Public API
  src/               # Implementation (.h/.c)
  test/              # Test files
  3rd/woomem/        # GC memory manager (submodule)
  doc/               # Documentation
```

## Testing

Test files in `test/` directory. Pattern:
```c
#include "woort.h"
#include "woort_vm.h"

int main(int argc, char** argv) {
    woort_init();
    
    woort_VMRuntime* vm;
    woort_VMRuntime_create(&vm);
    
    /* test code */
    
    woort_VMRuntime_destroy(vm);
    woort_shutdown();
    return 0;
}
```

## Supported Platforms

Windows Server 2022 (MSVC), macOS ARM64, Ubuntu 22.04 ARM64, Ubuntu 20.04
