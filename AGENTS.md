# AGENTS.md - WooRT Codebase Guide

## Project Overview

WooRT (Woolang Runtime V1.0) is a runtime environment for the Woolang scripting language. It provides:
- An efficient interpreter
- A GC-based memory manager (woomem)
- Debugging and intervention support
- Code generation interfaces

## Build Commands

### Configure
```bash
# Windows (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64

# macOS/Linux
cmake -B build -DCMAKE_BUILD_TYPE=Debug
# For Release:
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

### Build
```bash
# Windows
cmake --build build --config Debug
cmake --build build --config Release

# macOS/Linux
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

### Test
```bash
# Run all tests
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure

# Run a single test (using ctest regex)
ctest --test-dir build -C Debug -R <test_name> --output-on-failure

# Run with verbose output
ctest --test-dir build -C Debug -V
```

### Clean
```bash
# Remove build directory
rm -rf build   # Unix
rd /s /q build # Windows
```

## Code Style Guidelines

### Language Standard
- C11 is required
- No C++ code in this project

### Header Guards
Use `#pragma once` at the top of all header files:

```c
#pragma once

/*
 * filename.h
 */
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

### File Organization

```c
/* 1. Header guard */
#pragma once

/*
 * filename.h
 */

/* 2. Project headers */
#include "woort.h"

/* 3. Dependency headers */
#include "woort_diagnosis.h"

/* 4. System headers */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 5. Type definitions */

/* 6. Function declarations */
```

### Comments
- Use C-style comments (`/* */`), not C++ style (`//`)
- Chinese comments are acceptable in this codebase
- Document enum values and struct purposes inline

### Error Handling

1. **Return values**: Functions that can fail return `bool` (`true` = success)
2. **Output parameters**: Use pointer-to-pointer for output (`Type** out_result`)
3. **NODISCARD**: Mark functions whose return value must be checked:

```c
WOORT_NODISCARD bool woort_hashmap_find(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);
```

4. **Panic for unrecoverable errors**: Use `woort_panic()` for fatal errors:

```c
void woort_panic(woort_PanicReason reason, const char* msgfmt, ...);
```

### Function Declaration Style

```c
/* Parameter name on its own line for long signatures */
WOORT_NODISCARD bool woort_hashmap_get_or_emplace(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);

/* Short signatures on one line */
void woort_hashmap_clear(woort_HashMap* map);
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

### Static Assertions

Use `_Static_assert` for compile-time checks:

```c
_Static_assert(sizeof(woort_Value) == sizeof(woort_value),
    "woort_Value and woort_value must have the same size");
```

### Inline Functions

Use `static inline` for header-defined functions:

```c
static inline void woort_GC_mixed_write_barrier_value(
    woort_Value* modified_value,
    woort_Value src_value)
{
    /* implementation */
}
```

### Macros

Multi-line macros should use backslash continuation:

```c
#define woort_RuntimeFunction_kind(function) (      \
    (woort_RuntimeFunction_Kind)(                   \
        ((woort_RuntimeFunction)(function)) >> 62))
```

### Platform-Specific Code

Use preprocessor detection for platform differences:

```c
#if defined(_MSC_VER)
    /* MSVC-specific code */
#elif defined(__clang__) || defined(__GNUC__)
    /* Clang/GCC-specific code */
#else
    /* Fallback */
#endif
```

### Optional Parameters

Document optional parameters with `/* OPTIONAL */`:

```c
/* OPTIONAL */ void* woomem_alloc_normal(size_t size);
/* OPTIONAL */ void* woomem_alloc_attrib(size_t size, int attrib);
```

## Testing Guidelines

- Test files go in `test/` directory
- Test executable is `woort_test`
- Use native functions to test VM behavior
- Example test pattern:

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

## Dependencies

- **woomem**: GC-based memory manager (submodule in `3rd/woomem/`)
- Initialize submodules before building:
  ```bash
  git submodule sync --recursive
  git submodule update --init --recursive
  ```

## Supported Platforms

- Windows Server 2022 (MSVC)
- macOS ARM64
- Ubuntu 22.04 ARM64
- Ubuntu 20.04