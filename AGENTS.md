# AGENTS.md - WooRT Codebase Guide

## Project Overview

WooRT (Woolang Runtime V1.0) is a C11 runtime for the Woolang scripting language: bytecode interpreter, GC-based memory manager (woomem submodule), IR compiler, and debugging support. The single public API header is `include/woort.h`.

## Build Commands

```bash
# Submodules (required before first build)
git submodule sync --recursive && git submodule update --init --recursive

# Configure (Windows MSVC)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Configure (macOS ARM64)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64

# Configure (Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug          # Windows
cmake --build build -j$(nproc)              # Linux
cmake --build build -j$(sysctl -n hw.ncpu)  # macOS

# Test (all)
ctest --test-dir build -C Debug --output-on-failure

# Test (single)
ctest --test-dir build -C Debug -R test_c_api --output-on-failure
```

Test names match the source filename without extension: `test/test_c_api.c` → `-R test_c_api`.

**Source auto-discovery:** `src/CMakeLists.txt` uses `GLOB_RECURSE` for `.c`/`.h` files and `test/CMakeLists.txt` uses `GLOB` for `*.c` files. Adding or removing files requires no CMakeLists changes.

CI builds both Debug and Release on all platforms (see `.gitlab-ci.yml`).

## Build Quirks

- The shared library outputs as `libwoort` (not `woort`): `libwoort.dll`/`libwoort.so`, with `_debug` postfix on 64-bit Debug builds, `32`/`32_debug` on 32-bit. Use `BUILD_SHARED_LIBS=ON` to build shared.
- MSVC builds require `/experimental:c11atomics` (auto-enabled in CMakeLists for VS 2022 v193x-195x).
- Source builds define `WOORT_IMPL=1` privately; public consumers do not define this.
- `/source-charset:utf-8` is forced on MSVC.

## Code Style

### Language
- C11 only (no C++). `/* */` comments only, never `//`. Chinese comments are acceptable.
- `#pragma once` for all headers.
- Inline functions use `static inline`.
- `_Static_assert` for size/alignment checks.

### Include Order
1. `"woort.h"` (project public header)
2. Project internal headers (`"woort_codeenv.h"`, etc.)
3. Dependency headers (`"woomem.h"`)
4. System headers (`<stdlib.h>`, etc.)

### Naming Conventions

| Category | Pattern | Example |
|---|---|---|
| Types | `woort_ModuleName` | `woort_VMRuntime`, `woort_HashMap` |
| Functions | `woort_module_function` | `woort_hashmap_init`, `woort_VMRuntime_create` |
| Macros/Constants | `WOORT_MACRO_NAME` | `WOORT_NODISCARD`, `WOORT_API` |
| Struct members | `m_member_name` | `m_size`, `m_bucket_count` |
| Enum values | `WOORT_ENUM_VALUE` | `WOORT_HASHMAP_RESULT_OK` |
| Globals | `g_variable_name` | `g_gc_in_marking` |
| Internal functions | `_woort_module_func` | `_woort_hashmap_rehash` |

### WOORT_NODISCARD (Required)

**Every non-void function must be marked `WOORT_NODISCARD`.** It expands to `[[nodiscard]]` on C23-compilers, `_Check_return_` on MSVC, `__attribute__((warn_unused_result))` on Clang, and is empty on GCC (warnings cannot be suppressed).

### `/* OPTIONAL */` Annotation (Required)

**Any member, parameter, local, or return value that may be NULL must be annotated `/* OPTIONAL */` before the type.**

```c
/* struct member */
typedef struct woort_HashMap {
    /* OPTIONAL */ struct woort_HashMapEntry** m_buckets;
} woort_HashMap;

/* function parameter */
void woomem_init(/* OPTIONAL */ woomem_UserContext user_ctx);

/* function return value */
WOORT_NODISCARD /* OPTIONAL */ void* woomem_alloc_normal(size_t size);
```

### Function Declaration Style

Short signatures on one line. Longer signatures with one parameter per line:

```c
void woort_hashmap_clear(woort_HashMap* map);

WOORT_NODISCARD bool woort_hashmap_get_or_emplace(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);
```

### Error Handling

1. `bool` return for success/failure (`true` = success)
2. Output via `Type** out_result` pointer-to-pointer
3. `woort_panic(reason, msgfmt, ...)` for unrecoverable errors (declared in `src/woort_diagnosis.h`)
4. Result enums for multi-outcome: `woort_hashmap_Result` with `WOORT_HASHMAP_RESULT_*`

### Platform Detection

```c
#if defined(_MSC_VER)
#elif defined(__clang__) || defined(__GNUC__)
```

## Memory Management (woomem submodule)

- GC-managed objects inherit from `woort_GCUnit` (first member must be `const woort_GCUnitProxy* m_proxy`)
- Allocate with `woort_GCUnit_alloc_attrib(ATTRIB, SIZE)` macro (requires `"woomem.h"` included first)
- Write barriers are **mandatory** when writing GC references:
  - `woort_GC_mixed_write_barrier_value()` for `woort_Value` fields
  - `woort_GC_mixed_write_barrier_dynbox()` for `woort_DynBox` fields
  - `woort_GC_mixed_write_barrier_gcunit()` for raw GC pointer fields

## Project Structure

```
include/woort.h         # Single monolithic public API header (extern "C" guarded)
src/                    # Implementation (.h/.c), compiled into libwoort
src/woort_vm.h          # VM internals (included by tests)
src/woort_opcode.h      # Bytecode opcode definitions
src/woort_codeenv.h     # CodeEnv internals
src/woort_ir_compiler.h # IR compiler internals
src/woort_value.h       # Value type internals
src/woort_gc*.h/.c      # GC object types (string, vec, map, struct, closure, handle)
test/                   # Each .c file → separate test executable + ctest entry
3rd/woomem/             # GC allocator (git submodule)
```

## Testing

Each `test/*.c` is compiled into its own executable and registered as a ctest via `get_filename_component(name ${src} NAME_WE)`. The test name matches the filename without `.c`.

```c
#include "woort.h"
#include "woort_vm.h"

int main(int argc, char** argv) {
    woort_init(0, NULL);
    woort_VMRuntime* vm;
    woort_VMRuntime_create(&vm);
    /* ... test code ... */
    woort_VMRuntime_destroy(vm);
    woort_shutdown();
    return 0;
}
```

Test sources include internal headers (`woort_vm.h`, `woort_codeenv.h`, etc.) directly from `src/` — the test CMakeLists adds `../src` to the include path.

## Testing Gotchas

- `woort_init(int argc, char** argv)` requires two arguments. Tests pass `woort_init(0, NULL)`.
- Tests using only the IR compiler (no VM execution) call `woort_IRCompiler_create()` directly, not `woort_VMRuntime_create()`.

## No Lint / Formatter

There are no lint, formatter, or typecheck commands in this repo. Do not attempt to run them.

## Architecture Docs

`doc/` contains architecture documentation: `ir.md`, `opcodes.md`, and `runtime.md`.

## Supported Platforms

Windows (MSVC), macOS ARM64, Ubuntu 22.04 ARM64, Ubuntu 20.04 x86_64. CI runs on GitLab (`.gitlab-ci.yml`).
