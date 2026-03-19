# AGENTS.md - WooRT Codebase Guide

## Project Overview

WooRT (Woolang Runtime V1.0) is a C11 runtime for the Woolang scripting language with an interpreter, GC-based memory manager (woomem), and code generation interfaces.

## Build Commands

### Configure
```bash
# Windows (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64

# macOS/Linux
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

### Build
```bash
cmake --build build --config Debug      # Windows
cmake --build build --config Release    # Windows
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)  # Unix
```

### Test
```bash
# Run all tests
ctest --test-dir build -C Debug --output-on-failure

# Run a single test (regex match)
ctest --test-dir build -C Debug -R <test_name> --output-on-failure

# Verbose output
ctest --test-dir build -C Debug -V
```

### Clean
```bash
rm -rf build   # Unix
rd /s /q build # Windows
```

## Code Style Guidelines

### Language & Headers
- C11 is required; no C++ code
- Use `#pragma once` header guards
- Use C-style comments (`/* */`), not C++ (`//`); Chinese comments are acceptable

### Include Order
```c
#pragma once
/* filename.h */
#include "woort.h"           /* 1. Project public headers */
#include "woort_diagnosis.h" /* 2. Project internal headers */
#include <stdint.h>          /* 3. System headers */
```

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Types (struct/enum/typedef) | `woort_ModuleName` | `woort_VMRuntime`, `woort_HashMap` |
| Functions | `woort_module_function` | `woort_hashmap_init`, `woort_VMRuntime_create` |
| Macros/Constants | `WOORT_MACRO_NAME` | `WOORT_NODISCARD`, `WOORT_API` |
| Struct members | `m_member_name` | `m_size`, `m_bucket_count` |
| Enum values | `WOORT_ENUM_VALUE` | `WOORT_HASHMAP_RESULT_OK` |
| Global variables | `g_variable_name` | `g_gc_in_marking` |
| Internal functions | `_woort_function` | `_woort_hash_int` |

### Error Handling

1. **Return values**: Functions that can fail return `bool` (`true` = success)
2. **Output parameters**: Use pointer-to-pointer (`Type** out_result`)
3. **NODISCARD**: Mark functions whose return value must be checked with `WOORT_NODISCARD`
4. **Panic**: Use `woort_panic(reason, fmt, ...)` for unrecoverable errors
5. **Optional parameters**: Document with `/* OPTIONAL */` before the type

```c
WOORT_NODISCARD bool woort_hashmap_find(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);

/* OPTIONAL */ void* woomem_alloc_normal(size_t size);
```

### Function Declaration

Short signatures on one line; long signatures with parameters on separate lines:
```c
void woort_hashmap_clear(woort_HashMap* map);

WOORT_NODISCARD bool woort_hashmap_get_or_emplace(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);
```

### C++ Compatibility

Public headers must include `extern "C"` guards:
```c
#ifdef __cplusplus
extern "C" {
#endif
/* declarations */
#ifdef __cplusplus
}
#endif
```

### Platform-Specific Code
```c
#if defined(_MSC_VER)
    /* MSVC-specific */
#elif defined(__clang__) || defined(__GNUC__)
    /* Clang/GCC-specific */
#endif
```

### Inline Functions & Macros

Use `static inline` for header-defined functions. Multi-line macros use backslash continuation:
```c
#define woort_RuntimeFunction_kind(function) (  \
    (woort_RuntimeFunction_Kind)(               \
        ((woort_RuntimeFunction)(function)) >> 62))
```

### Static Assertions

Use `_Static_assert` for compile-time checks.

## Testing

- Test files go in `test/` directory
- Test executable is `woort_test`
- Use static helper functions for test cases; call from a main test runner
- Return `bool` (`true` = pass) from test functions

## Dependencies

- **woomem**: GC-based memory manager (submodule in `3rd/woomem/`)
- Initialize before building:
  ```bash
  git submodule sync --recursive
  git submodule update --init --recursive
  ```

## Supported Platforms

- Windows Server 2022 (MSVC)
- macOS ARM64
- Ubuntu 22.04 ARM64
- Ubuntu 20.04