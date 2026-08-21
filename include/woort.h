#pragma once

/** @brief Woort version encoded as (major, minor, patch, tweak). */
#define WOORT_VERSION WOORT_VERSION_WRAP(1, 0, 6, 17)

#ifndef WOORT_MSVC_RC_INCLUDE

#ifdef __cplusplus
#   include <cstddef>
#   include <cstdint>
#   include <clocale>
extern "C" {
#else
#   include <stdbool.h>
#   include <stddef.h>
#   include <stdint.h>
#   include <locale.h>
#endif // __cplusplus

#ifdef __cplusplus
#   if __has_cpp_attribute(nodiscard)
#       define WOORT_NODISCARD [[nodiscard]]
#   endif
#   if __has_cpp_attribute(deprecated)
#       define WOORT_DEPRECATED [[deprecated]]
#   endif
#else
#   if __has_c_attribute(nodiscard)
#       define WOORT_NODISCARD [[nodiscard]]
#   endif
#   if __has_c_attribute(deprecated)
#       define WOORT_DEPRECATED [[deprecated]]
#   endif
#endif

#if defined(_MSC_VER)
#   ifndef WOORT_NODISCARD
#       define WOORT_NODISCARD _Check_return_
#   endif
#   ifndef WOORT_DEPRECATED
#       define WOORT_DEPRECATED __declspec(deprecated)
#   endif
#elif defined(__clang__) || defined(__GNUC__)
#   ifndef WOORT_NODISCARD
#       ifdef __GNUC__
#           define WOORT_NODISCARD /* GCC cannot suppress warnings, ignore. */
#       else
#           define WOORT_NODISCARD __attribute__((warn_unused_result))
#       endif
#   endif
#   ifndef WOORT_DEPRECATED
#       define WOORT_DEPRECATED __attribute__((deprecated))
#   endif
#else
#   ifndef WOORT_NODISCARD
#       define WOORT_NODISCARD
#   endif
#   ifndef WOORT_DEPRECATED
#       define WOORT_DEPRECATED
#   endif
#endif

#ifdef _WIN32
#   define WOORT_IMPORT __declspec(dllimport)
#   define WOORT_EXPORT __declspec(dllexport)
#else
#   define WOORT_IMPORT extern
#   define WOORT_EXPORT extern
#endif

#ifdef WOORT_STATIC_LIB
#   define WOORT_API
#else
#   ifdef WOORT_IMPL
#       define WOORT_API WOORT_EXPORT
#   else
#       define WOORT_API WOORT_IMPORT
#   endif
#endif

/* ========== Version API ========== */

/**
 * @brief Get the woort runtime version string.
 * @return A version string in the form "major.minor.patch.tweak" (e.g. "1.0.5.1").
 */
WOORT_NODISCARD WOORT_API const char* woort_version(void);

/**
 * @brief Get the woort runtime version as a packed 64-bit integer.
 *
 * Each 16-bit slot holds one component (major, minor, patch, tweak),
 * from most-significant to least-significant.
 *
 * @return The version encoded as a 64-bit integer.
 */
WOORT_NODISCARD WOORT_API uint64_t woort_version_int(void);

/**
 * @brief Initialize the Woolang runtime.
 *
 * Must be called once before using any other woort API functions.
 * Must be paired with a corresponding woort_shutdown() call.
 *
 * @param argc  Command-line argument count (as passed to main).
 * @param argv  Command-line argument vector (as passed to main).
 */
WOORT_API void woort_init(int argc, char** argv);
#define woort_init(argc, argv)                          \
    do                                                  \
    {                                                   \
        woort_init(argc, argv);                         \
        setlocale(LC_CTYPE, woort_env_locale_name());   \
    } while (0)

typedef void(*woort_ShutdownPostCallback)(void*);

/**
 * @brief Shut down the Woolang runtime.
 *
 * Releases all runtime resources. No woort API functions may be called
 * after this function returns. Must be paired with a prior woort_init() call.
 */
WOORT_API void woort_shutdown(
    /* OPTIONAL */ woort_ShutdownPostCallback do_after_shutdown, void* custom_data);

/* ========== Runtime Options Help API ========== */

/**
 * @brief Print the available woort runtime command-line options to stdout.
 *
 * Lists every --woort-* option recognized by woort_init(), with accepted
 * values and defaults.
 */
WOORT_API void woort_print_runtime_help(void);

/* ========== Console I/O API ========== */

/**
 * @brief Read a line of UTF-8 text from the console (stdin).
 *
 * On Windows, reads UTF-16 from the console and converts to UTF-8.
 * On POSIX, reads from stdin (which is assumed to be UTF-8 after locale setup).
 *
 * The returned string is null-terminated (without the trailing newline).
 * Must be freed with woort_free().
 *
 * @return A malloc-allocated UTF-8 string, or NULL on EOF or read error.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_console_readline(void);

/**
 * @brief Free a buffer.
 * @param buf  The buffer to free (may be NULL).
 */
WOORT_API void woort_free(/* OPTIONAL */ void* buf);

/**
 * @brief Test whether stdin is an interactive console (terminal).
 *
 * Windows: checks the input handle is a character device (console).
 * POSIX: wraps isatty() on stdin.
 *
 * @return 1 if stdin is an interactive console, 0 otherwise.
 */
WOORT_API bool woort_stdin_isatty(void);

/**
 * @brief Read one raw UTF-8 byte from the console (char-at-a-time stream).
 *
 * Intended for key-event / live line-editor consumers. Independent from
 * woort_console_readline(): it does not wait for a full line.
 *
 * Windows: a real console is read with ReadConsoleW (UTF-16) and converted to
 *          UTF-8; redirected stdin (pipe/file) is byte-passthrough.
 * POSIX:   read(2) directly (NOT stdio), so it is safe under termios raw mode
 *          and never entangles with the libc stdin buffer.
 *
 * A Ctrl+C interrupt on Windows (ReadConsoleW failing with
 * ERROR_OPERATION_ABORTED) is delivered as the byte 0x03 (ETX) so callers can
 * map it to a "cancel" key event.
 *
 * Console input is a single OS stream; do not mix concurrent callers.
 *
 * @return The next byte (0-255), or EOF (-1) on end-of-stream / read error.
 */
WOORT_API int woort_console_getc(void);

/**
 * @brief Push back one byte (1-deep) onto the console byte stream.
 *
 * Re-read by the next woort_console_getc(). Only one byte of pushback is
 * supported; pushing EOF or calling twice without an intervening getc is
 * undefined beyond the 1-deep slot.
 *
 * @param ch  The byte (0-255) to push back; EOF is ignored.
 * @return The pushed byte, or EOF if @p ch was EOF.
 */
WOORT_API int woort_console_ungetc(int ch);

/**
 * @brief Get the default locale name for the current platform.
 *
 * Returns a platform-appropriate UTF-8 locale name string
 * (e.g. ".UTF-8" on Windows, "en_US.UTF-8" on macOS, "C.UTF-8" on Linux).
 * The returned string is statically allocated and must not be freed.
 *
 * @return A null-terminated locale name string (never NULL).
 */
WOORT_NODISCARD WOORT_API const char* woort_env_locale_name(void);

/**
 * @brief Status codes returned by VM call dispatch operations.
 *
 * When invoking or dispatching a function, the result may be one of the following statuses.
 */
typedef enum woort_VmCallStatus
{
    /**
     * @brief The target function returned normally with no special conditions to handle.
     */
    WOORT_VM_CALL_STATUS_NORMAL,

    /**
     * @brief The VM requests to suspend execution at the current state.
     *
     * If possible, execution can be resumed later.
     * YIELD is only returned when interpreted execution receives an interrupt request.
     * External dispatch operations may end with this status.
     * If an invoke operation attempts to end with YIELD, a PANIC is triggered.
     */
    WOORT_VM_CALL_STATUS_YIELD,

    /**
     * @brief The program has been terminated. The VM cannot continue executing.
     *
     * The VM will also be marked as aborted and will refuse to execute
     * any further operations.
     * ABORTED is only returned when interpreted execution receives an interrupt request.
     */
    WOORT_VM_CALL_STATUS_ABORTED,

    /**
     * @brief The next call stack level involves VM state changes requiring resync.
     *
     * The next level requests the previous level to perform reverse synchronization
     * or a checkpoint to restore to a normal state.
     *
     * Interpreted execution never ends with RESYNC.
     *
     * When JIT calls a native function and RESYNC occurs:
     *     Restore to post-call state, then execute a checkpoint.
     * When JIT calls a JIT function and RESYNC occurs:
     *     Do not synchronize, immediately end the current JIT function with RESYNC.
     * When interpreted execution calls a native function and RESYNC occurs:
     *     Restore to post-call state, then execute a checkpoint.
     * When interpreted execution calls a JIT function and RESYNC occurs:
     *     Perform a desynchronization, then execute a checkpoint.
     *
     * External invoke or dispatch operations may target external or JIT functions.
     * If this occurs, _woort_VMRuntime_dispatch must be called again to obtain the
     * accurate result.
     */
    WOORT_VM_CALL_STATUS_RESYNC,
} woort_VmCallStatus, woort_api;

/**
 * @brief Type tags for boxed dynamic values.
 */
typedef enum woort_BoxValueType
{
    WOORT_BOX_VALUE_TYPE_GCUNIT = 0b000,    /**< @brief GC-managed unit. */
    WOORT_BOX_VALUE_TYPE_REAL = 0b001,      /**< @brief Boxed double-precision float. */
    WOORT_BOX_VALUE_TYPE_INT = 0b010,       /**< @brief Boxed 64-bit signed integer. */
    WOORT_BOX_VALUE_TYPE_BOOL = 0b100,      /**< @brief Boxed boolean. */

    ////
    WOORT_BOX_VALUE_TYPE_NIL = 0b1000,      /**< @brief Nil value. */
    WOORT_BOX_VALUE_TYPE_STRING,            /**< @brief String value. */
    WOORT_BOX_VALUE_TYPE_VEC,               /**< @brief Vector (dynamic array) value. */
    WOORT_BOX_VALUE_TYPE_MAP,               /**< @brief Map (hash table) value. */
    WOORT_BOX_VALUE_TYPE_STRUCT,            /**< @brief Struct value. */
    WOORT_BOX_VALUE_TYPE_GCHANDLE,          /**< @brief GC handle (external resource). */
    WOORT_BOX_VALUE_TYPE_CLOSURE,           /**< @brief Closure value. */

} woort_BoxValueType;

/** @brief Opaque handle to a VM runtime instance. */
typedef struct woort_VMRuntime woort_VMRuntime;

typedef union woort_Value
#ifndef WOORT_IMPL
{
    char _[8];
}
#endif
woort_Value;

/** @brief Index into the VM evaluation stack. Negative values are relative to the current frame base. */
typedef int32_t woort_StackValue;

/**
 * @brief Sentinel value for ignoring an output parameter.
 *
 * Can be passed in the following contexts:
 *
 * - As `dst` to woort_bootup / woort_invoke / woort_spawn /
 *   woort_resume to discard the return value.
 * - As `hold` to woort_set_gchandle / woort_set_union_gchandle (and
 *   their option/result macro aliases) to not hold any GC unit.
 * - As `out_key_boxed` or `out_val_boxed` to woort_map_iter to
 *   discard the key or value output.
 */
#define WOORT_IGNORE ((woort_StackValue)-2)

 /**
  * @brief Special stack slot index for the function return value.
  *
  * Pass as `dst` to woort_set_* functions to write a value
  * into the calling function's return slot.
  *
  * Without violating calling conventions, this slot may also be used
  * as a temporary stack location for read/write operations, just like
  * any woort_StackValue obtained via woort_push_reserve.
  */
#define WOORT_RETURN_SLOT ((woort_StackValue)-1)

  /** @brief Default entry point function name. */
#define WOORT_DEFAULT_ENTRY "@entry"

/** @brief Signature for native (C) functions callable from Woolang. */
typedef woort_api(*woort_NativeFunction)(void);

/** @brief Signature for JIT (C) functions callable from Woolang. */
typedef woort_api(*woort_JitFunction)(
    woort_VMRuntime* vm, const woort_Value* bp, const woort_Value* sp);

/** @brief Opaque handle to a compiled code environment containing bytecode and constants. */
typedef struct woort_CodeEnv woort_CodeEnv;

/**
 * @brief Opaque handle to a loaded dynamic library (native or fake).
 */
typedef struct woort_Dylib woort_Dylib;

/**
 * @brief Unload method flags for woort_dylib_unload.
 */
typedef enum woort_DylibUnloadMethod
{
    WOORT_DYLIB_NONE = 0,
    /** Decrement reference count; free when it reaches zero. */
    WOORT_DYLIB_UNREF = 1 << 0,
    /** Remove from the named library registry. */
    WOORT_DYLIB_BURY = 1 << 1,
    /** Combination: unref + bury. */
    WOORT_DYLIB_UNREF_AND_BURY = WOORT_DYLIB_UNREF | WOORT_DYLIB_BURY,
} woort_DylibUnloadMethod;

typedef void (*woort_DylibEntryFunc)(woort_Dylib*);
typedef void (*woort_DylibLeaveFunc)(void);

/**
 * @brief Entry in a fake-library function table.
 */
typedef struct woort_ExternLibFunc
{
    const char* m_name;          /**< Function name (NULL = end of table). */
    /* OPTIONAL */ void* m_func_addr;  /**< Function pointer (NULL = end of table). */
} woort_ExternLibFunc;

/** Sentinel to terminate a woort_ExternLibFunc array. */
#define WOORT_EXTERN_LIB_FUNC_END { NULL, NULL }

/** @brief Opaque handle to an IR compiler used to build CodeEnv objects. */
typedef struct woort_IRCompiler woort_IRCompiler;

/** @brief Opaque handle to an IR function being compiled. */
typedef struct woort_IRFunction woort_IRFunction;

/** @brief Opaque handle to an IR virtual register (value operand). */
typedef struct woort_IRValue woort_IRValue;

/** @brief Opaque handle to an IR label (branch target). */
typedef struct woort_IRLabel woort_IRLabel;

/** @brief Opaque handle to a streaming file (virtual or disk). */
typedef struct woort_VFile woort_VFile;

/** @brief Index into the constant pool of a CodeEnv. */
typedef uint32_t woort_IRConstantIndex;

/** @brief Index into the static data area of a CodeEnv. */
typedef uint32_t woort_IRStaticIndex;

/** @brief The Woolang integer type (64-bit signed). */
typedef int64_t woort_Int;

/** @brief The Woolang handle type (64-bit unsigned). */
typedef uint64_t woort_Handle;

/** @brief The Woolang real (floating-point) type (double precision). */
typedef double woort_Real;

/** @brief A Woolang bytecode instruction word. */
typedef uint32_t woort_Bytecode;

/** @brief A UTF-8 encoded, null-terminated C string pointer. */
typedef const char* woort_U8CString;

/** @brief Alias for a const UTF-8 C string (used by the raw UTF-8 helpers). */
typedef const char* woort_string_t;

/*
Ensure char16_t and char32_t are available in pure C mode.
*/
#if !defined(__cplusplus) && !defined(_CHAR16T)
#   if defined(_MSC_VER)
#       ifndef _CHAR16T
#           define _CHAR16T
typedef uint16_t char16_t;
#       endif
#       ifndef _CHAR32T
#           define _CHAR32T
typedef uint32_t char32_t;
#       endif
#   elif defined(__clang__) || defined(__GNUC__)
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
#   else
typedef uint16_t char16_t;
typedef uint32_t char32_t;
#   endif
#endif

/** @brief A Unicode char. */
typedef char32_t woort_Char;

/** @brief Callback to mark GC references reachable from a user GC handle. */
typedef void (*woort_GCHandle_UserMarkFunction)(void*);

/** @brief Callback to destruct a user GC handle when collected. */
typedef void (*woort_GCHandle_UserDestructFunction)(void*);

/**
 * @brief Source location descriptor for debugging and diagnostics.
 */
typedef struct woort_SourceLocation
{
    /* OPTIONAL */ const char* m_filepath; /**< @brief Source file path (may be NULL). */

    uint32_t m_begin_line;   /**< @brief Start line number (1-based). */
    uint32_t m_begin_column; /**< @brief Start column number (1-based). */
    uint32_t m_end_line;     /**< @brief End line number (1-based). */
    uint32_t m_end_column;   /**< @brief End column number (1-based). */

} woort_SourceLocation;

/* ========== VM API ========== */

/**
 * @brief Create a new VM runtime instance.
 * @param[out] out_vm  Pointer to receive the new VM handle. Must not be NULL.
 * @return true on success, false on out-of-memory.
 */
WOORT_NODISCARD WOORT_API bool woort_VMRuntime_create(
    woort_VMRuntime** out_vm);

/**
 * @brief Destroy a VM runtime instance and release all associated resources.
 * @param vm  The VM handle to destroy. Must not be NULL.
 */
WOORT_API void woort_VMRuntime_destroy(
    woort_VMRuntime* vm);

/**
 * @brief Make a VM weak so the GC no longer treats it as a root object.
 *
 * NOTE: Before calling this function, ENSURE that the target VM can be
 * correctly marked by the GC through other means (e.g. by calling
 * woort_GC_mark_weak_vm_manually from a GCHandle mark callback).
 *
 * Once this function is called, the GC will skip automatic marking for
 * this VM. If this function is called recklessly, the VM's GC objects
 * may be collected unexpectedly.
 *
 * Must be called while the target VM is the current thread's active VM
 * (i.e. within the VM's GC guard scope).
 *
 * @param vm  The VM handle to weaken. Must not be NULL.
 */
WOORT_API void woort_VMRuntime_weaken(
    woort_VMRuntime* vm);

/**
 * @brief Manually mark a weak VM's stack and env during GC marking.
 *
 * This function marks the VM's env and all values on its stack as GC
 * reachable, keeping the VM's objects alive for the current GC cycle.
 *
 * Must be called during the GC marking phase, typically from a GCHandle
 * mark callback (woort_GCHandle_UserMarkFunction).
 *
 * @param vm  The weak VM handle to mark. Must not be NULL.
 */
WOORT_API void woort_GC_mark_weak_vm_manually(
    woort_VMRuntime* vm);

/**
 * @brief Manually mark a dropped CodeEnv as GC reachable during the marking phase.
 * @param env  The CodeEnv to mark. Must not be NULL.
 */
WOORT_API void woort_GC_mark_droped_env_manually(
    const woort_CodeEnv* env);

/**
 * @brief Manually mark a value as GC reachable during the marking phase.
 * @param val  The value to mark. Must not be NULL.
 */
WOORT_API void woort_GC_mark_internal_value_manually(
    const woort_Value* val);

/**
 * @brief Move a value into a destination with a mixed write barrier.
 *
 * Equivalent to writing *dst = *val with the appropriate GC write barrier.
 * Must be used when writing a GC-managed value into a location that may be
 * observed by the GC.
 *
 * @param dst  Destination address to write into. Must not be NULL.
 * @param val  Source value to read from. Must not be NULL.
 */
WOORT_API void woort_GC_set_internal_value_with_mixed_write_barrier(
    woort_Value* dst, const woort_Value* val);

/**
 * @brief Issue a delete barrier for a value being overwritten or removed.
 *
 * Must be called before overwriting or removing a GC-managed value reference
 * that is NOT covered by a write barrier. Ensures the GC does not lose track
 * of the old value during the marking phase.
 *
 * @param dst  The value slot being overwritten or removed. Must not be NULL.
 */
WOORT_API void woort_GC_internal_value_delete_barrier(
    const woort_Value* dst);

/**
 * @brief Flags that control GC allocation behavior.
 */
typedef enum woort_GCAllocate_Flag
{
    WOORT_GCALLOCATE_FLAG_NONE = 0,
    WOORT_GCALLOCATE_FLAG_AUTO_MARK = 1,

}woort_GCAllocate_Flag;

/**
 * @brief Allocate GC-managed memory of a given size.
 * @param sz         Size in bytes to allocate.
 * @param attribute  Allocation flags (see woort_GCAllocate_Flag).
 * @return Pointer to the allocated and initialized GC unit.
 */
WOORT_NODISCARD WOORT_API void* woort_GC_allocate(size_t sz, int attribute);

/**
 * @brief Allocate GC-managed memory and register it as a GC root.
 *
 * The allocated memory acts as a root, keeping itself and all objects
 * reachable from it alive. Use woort_GC_unregister_root() to remove
 * it from the root set when no longer needed.
 *
 * @param sz         Size in bytes to allocate.
 * @param attribute  Allocation flags (see woort_GCAllocate_Flag).
 * @return Pointer to the allocated and initialized GC unit.
 */
WOORT_NODISCARD WOORT_API void* woort_GC_allocate_as_root(size_t sz, int attribute);

/**
 * @brief Remove a previously registered root from the GC root set.
 * @param p  The root pointer to unregister. Must not be NULL.
 */
WOORT_API void woort_GC_unregister_root(void* p);

/**
 * @brief Manually mark a raw pointer as GC reachable during the marking phase.
 * @param p  The pointer to mark, or NULL.
 */
WOORT_API void woort_GC_mark_addr_manually(/* OPTIONAL */ void* p);

/**
 * @brief Write a raw pointer into a destination with a mixed write barrier.
 *
 * Must be used when writing a GC-managed pointer into a location that may
 * be observed by the GC, ensuring both the old and new values are properly
 * tracked.
 *
 * @param dst  Destination address to write into. Must not be NULL.
 * @param p    The pointer value to write, or NULL.
 */
WOORT_API void woort_GC_set_addr_with_mixed_write_barrier(void** dst, /* OPTIONAL */ void* p);

/**
 * @brief Issue a delete barrier for a raw pointer being overwritten or removed.
 * @param p  The pointer slot to issue the barrier for, or NULL.
 */
WOORT_API void woort_GC_addr_delete_barrier(/* OPTIONAL */ const void* p);

/**
 * @brief Opaque handle to a GC pin that holds an array of values reachable during GC.
 *
 * A GCPin acts as a GC root: all values stored in it are marked, preventing
 * them from being garbage collected.
 */
typedef struct woort_GCPin woort_GCPin;

/**
 * @brief Create a new GC pin capable of holding a fixed number of values.
 *
 * Must be called within a GC scope. When no VM is running, call
 * woort_GC_sync_marking_lock() first.
 *
 * @param count  Number of value slots in the pin.
 * @return Pointer to the created GC pin. Never NULL.
 */
WOORT_NODISCARD WOORT_API woort_GCPin* woort_GCPin_create(size_t count);

/**
 * @brief Destroy a GC pin and release it from the GC root set.
 *
 * Must be called within a GC scope. When no VM is running, call
 * woort_GC_sync_marking_lock() first.
 *
 * Once destroyed, value references held by the pin may be collected
 * unless they are otherwise reachable.
 *
 * @param pin  The GC pin to destroy. Must not be NULL.
 */
WOORT_API void woort_GCPin_destroy(woort_GCPin* pin);

/**
 * @brief Set a value in the GC pin from a StackValue, with write barrier.
 *
 * Requires an active VM on the current thread (which provides the GC
 * scope). The value is copied into the pin and becomes GC-reachable.
 *
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 * @param val  The StackValue to store.
 */
WOORT_API void woort_GCPin_set(woort_GCPin* pin, size_t idx, woort_StackValue val);

/**
 * @brief Copy a value from the GC pin into a StackValue.
 *
 * Requires an active VM on the current thread (which provides the GC scope).
 *
 * @param dst  Destination StackValue to write into.
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 */
WOORT_API void woort_GCPin_get(woort_StackValue dst, woort_GCPin* pin, size_t idx);

/**
 * @brief Set a value in the GC pin, creating a deep duplicate of boxed values.
 *
 * Unlike woort_GCPin_set, which stores a reference, this creates an
 * independent copy of boxed values (vec/map/struct). Requires an active
 * VM on the current thread (which provides the GC scope).
 *
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 * @param val  The StackValue whose boxed content (if any) to deep-copy.
 */
WOORT_API void woort_GCPin_set_dup_boxed(woort_GCPin* pin, size_t idx, woort_StackValue val);

/**
 * @brief Set a value in the GC pin from a raw woort_Value, with write barrier.
 *
 * Unlike woort_GCPin_set, this does not require an active VM, but it
 * still requires a GC scope. When no VM is running, call
 * woort_GC_sync_marking_lock() first.
 *
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 * @param val  Pointer to the woort_Value to copy from. Must not be NULL.
 */
WOORT_API void woort_GCPin_set_internal(woort_GCPin* pin, size_t idx, const woort_Value* val);

/**
 * @brief Copy a value from the GC pin into a woort_Value, with write barrier.
 *
 * Safe for writing into GC-observable locations (e.g., struct fields).
 *
 * Must be called within a GC scope. When no VM is running, call
 * woort_GC_sync_marking_lock() first.
 *
 * @param dst  Destination woort_Value to write into. Must not be NULL.
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 */
WOORT_API void woort_GCPin_get_internal(woort_Value* dst, woort_GCPin* pin, size_t idx);

/**
 * @brief Copy a value from the GC pin into a woort_Value, without write barrier.
 *
 * Only safe when dst points to a stack-local variable or other
 * non-GC-observable location. Use woort_GCPin_get_internal()
 * for GC-observable destinations.
 *
 * Must be called within a GC scope. When no VM is running, call
 * woort_GC_sync_marking_lock() first.
 *
 * @param dst  Destination woort_Value to write into. Must not be NULL.
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 */
WOORT_API void woort_GCPin_get_internal_without_barrier(
    woort_Value* dst, woort_GCPin* pin, size_t idx);

/**
 * @brief Same as woort_GCPin_set_dup_boxed, but takes a raw woort_Value pointer.
 *
 * Does not require an active VM, but still requires a GC scope. When no
 * VM is running, call woort_GC_sync_marking_lock() first. Creates a deep
 * duplicate of boxed values (vec/map/struct) so the pin owns an independent copy.
 *
 * @param pin  The GC pin. Must not be NULL.
 * @param idx  Index within the pin. Must be < count from create.
 * @param val  Pointer to the woort_Value whose boxed content to deep-copy. Must not be NULL.
 */
WOORT_API void woort_GCPin_set_dup_boxed_internal(
    woort_GCPin* pin, size_t idx, const woort_Value* val);

/**
 * @brief Acquire a GC marking lock with side-effect checkpoint sync.
 *
 * Call this in rare scenarios where the current thread has no running VM
 * (thus no GC checkpoint sync guarantee) but must safely transfer/copy
 * internal values such as woort_Value. GC access must occur between this
 * call and woort_GC_sync_marking_unlock().
 *
 * Once inside a temporary GC scope established by this call, the caller
 * must not use woort_VMRuntime_swap() to switch into another VM's scope,
 * as doing so may cause a deadlock.
 *
 * @return true if lock acquired (caller must call woort_GC_sync_marking_unlock),
 *         false if the thread already has a running VM (no extra lock needed,
 *         do NOT call woort_GC_sync_marking_unlock).
 */
WOORT_NODISCARD WOORT_API bool woort_GC_sync_marking_lock(void);

/**
 * @brief Release the GC marking lock acquired by woort_GC_sync_marking_lock().
 *
 * Must only be called when woort_GC_sync_marking_lock() returned true.
 */
WOORT_API void woort_GC_sync_marking_unlock(void);

/**
 * @brief Swap the current thread-local VM instance with a new one.
 * @param vm  The new VM instance to install, or NULL to detach.
 * @return The previously active VM instance, or NULL if none was active.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_VMRuntime* woort_VMRuntime_swap(
    /* OPTIONAL */ woort_VMRuntime* vm);

/**
 * @brief Get the current thread-local VM instance.
 * @return The current VM instance, or NULL if no VM is active on this thread.
 */
WOORT_NODISCARD WOORT_API  /* OPTIONAL */ woort_VMRuntime* woort_VMRuntime_current(void);

/**
 * @brief Get the runtime error message if the VM has aborted.
 * @param vm  The VM instance. Must not be NULL.
 * @return The error message string, or NULL if the VM has not aborted.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ const char* woort_VMRuntime_get_runtime_error_msg(
    woort_VMRuntime* vm);

typedef struct woort_VMRuntime_TraceCallstack_Iter
{
    woort_VMRuntime* m_vm;

    size_t m_next_tracing_depth;
    size_t m_next_tracing_offset_of_base;

} woort_VMRuntime_TraceCallstack_Iter;

typedef struct woort_VMRuntime_TraceCallstack
{
    size_t m_callstack_depth;

    bool m_has_location;

    /* OPTIONAL */ const char* m_function_name;
    /* OPTIONAL */ const char* m_file_or_lib_name;

    size_t m_location_begin[2];
    size_t m_location_end[2];

    /* OPTIONAL */ const woort_Bytecode* m_code_addr;

    size_t m_callstack_offset_of_base;

} woort_VMRuntime_TraceCallstack;

/**
 * @brief Begin iterating over the VM callstack for tracing.
 * @param vm  The VM instance. Must not be NULL.
 * @param[out] out_trace_iter  Pointer to receive the trace iterator. Must not be NULL.
 */
WOORT_API void woort_VMRuntime_trace_begin(
    woort_VMRuntime* vm,
    woort_VMRuntime_TraceCallstack_Iter* out_trace_iter);

/**
 * @brief Advance the trace iterator to the next callstack frame.
 * @param modify_trace_iter  The trace iterator to advance. Must not be NULL.
 * @param[out] out_result  Pointer to receive the trace callstack info. Must not be NULL.
 * @return true if a frame was retrieved, false if iteration is complete.
 */
WOORT_NODISCARD WOORT_API bool woort_VMRuntime_trace_next(
    woort_VMRuntime_TraceCallstack_Iter* modify_trace_iter,
    woort_VMRuntime_TraceCallstack* out_result);

/**
 * @brief Log a trace callstack entry.
 * @param trace  The trace callstack info to log. Must not be NULL.
 */
WOORT_API void woort_VMRuntime_log_trace(woort_VMRuntime_TraceCallstack* trace);

/**
 * @brief Print the full callstack backtrace of a VM.
 * @param vm  The VM instance. Must not be NULL.
 * @param max_depth  Maximum number of frames to print. Use 0 for unlimited.
 */
WOORT_API void woort_VMRuntime_print_backtrace(
    woort_VMRuntime* vm,
    size_t max_depth);

/* ========== IR API ========== */

/**
 * @brief Serialize a CodeEnv into a binary buffer.
 * @param code_env    The CodeEnv to serialize. Must not be NULL.
 * @param out_buffer  Receives the allocated binary buffer. Must not be NULL.
 * @param out_len     Receives the size of the binary buffer in bytes. Must not be NULL.
 * @return true on success, false on failure.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_save_binary(
    woort_CodeEnv* code_env, void** out_buffer, size_t* out_len);

typedef enum woort_CodeEnv_RestoreResult
{
    WOORT_CODEENV_RESTORE_OK = 0,

    /* File I/O errors */
    WOORT_CODEENV_RESTORE_FAIL_READ = 1,  /* Cannot read from vfile */
    WOORT_CODEENV_RESTORE_FAIL_ALLOC = 2,  /* Memory allocation failed */

    /* Header verification errors */
    WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH = 3,
    WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH = 4,

    /* Structural errors */
    WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE = 5,  /* Code size exceeds available data */
    WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV = 6,  /* Failed to create CodeEnv */
    WOORT_CODEENV_RESTORE_FAIL_INVALID_STRPOOL = 7,  /* String pool size invalid */

    /* Constant data errors */
    WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA = 8,  /* Data truncated / unterminated */
    WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE = 9,  /* Unknown constant type tag */
    WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET = 10, /* Invalid offset into pool */
    WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE = 11, /* Cannot resolve external function/library */
}woort_CodeEnv_RestoreResult;

/**
 * @brief Deserialize a CodeEnv from a binary stream.
 * @param f             The VFile to read binary data from. Must not be NULL.
 * @param out_code_env  Receives the restored CodeEnv. Must not be NULL.
 * @return WOORT_CODEENV_RESTORE_OK on success, or an error code on failure.
 */
WOORT_NODISCARD WOORT_API woort_CodeEnv_RestoreResult woort_CodeEnv_restore_binary(
    woort_VFile* f, woort_CodeEnv** out_code_env);

/** @brief Get a human-readable description for a restore result code. */
WOORT_NODISCARD WOORT_API const char* woort_CodeEnv_restore_failed_desc(
    woort_CodeEnv_RestoreResult rt);

/**
 * @brief Release a CodeEnv and free all associated resources.
 * @param code_env  The CodeEnv to destroy. Must not be NULL.
 */
WOORT_API void woort_CodeEnv_drop(
    woort_CodeEnv* code_env);

/**
 * @brief Query the compiled bytecode address of a function within a CodeEnv.
 * @param code_env     The code environment to query. Must not be NULL.
 * @param f            The IR function handle to look up.
 * @param[out] out_f_addr  Pointer to receive the bytecode address. Must not be NULL.
 * @return true if the function was found, false otherwise.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_query_function(
    woort_CodeEnv* code_env,
    woort_IRFunction* f,
    const woort_Bytecode** out_f_addr);

/**
 * @brief Acquire exclusive access to a CodeEnv (thread-safe lock).
 * @param code_env  The code environment to lock. Must not be NULL.
 */
WOORT_API void woort_CodeEnv_lock(
    woort_CodeEnv* code_env);

/**
 * @brief Release exclusive access to a CodeEnv (thread-safe unlock).
 * @param code_env  The code environment to unlock. Must not be NULL.
 */
WOORT_API void woort_CodeEnv_unlock(
    woort_CodeEnv* code_env);

/**
 * @brief Look up the source location closest to a given bytecode offset.
 *
 * bytecode_offset is relative to m_code_begin.
 *
 * @param env             The code environment. Must not be NULL.
 * @param bytecode_offset The bytecode offset to look up.
 * @param[out] out_location  Pointer to receive the source location. Must not be NULL.
 * @return true if a matching source location was found.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_find_srcloc_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location);

/**
 * @brief Look up the bytecode offset closest to a given source location.
 *
 * filepath can be any string pointer (internally compared using strcmp).
 *
 * @param env       The code environment. Must not be NULL.
 * @param filepath  The source file path to look up.
 * @param line      The source line number.
 * @param[out] out_bytecode_offset  Pointer to receive the bytecode offset. Must not be NULL.
 * @return true if a matching entry was found.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_find_offset_by_srcloc(
    const woort_CodeEnv* env,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset);

/**
 * @brief Look up the function name for a given bytecode offset.
 *
 * Searches the function boundary table to find the function that contains
 * the given bytecode offset.
 *
 * @param env             The code environment. Must not be NULL.
 * @param bytecode_offset The bytecode offset to look up.
 * @return The function name, or NULL if no function boundary contains the offset
 *         or the function is anonymous.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ const char* woort_CodeEnv_find_function_name_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset);

/**
 * @brief Set a breakpoint (trap) at the given bytecode address.
 *
 * Replaces the bytecode instruction at @p code with the DEBUGTRAP opcode,
 * saving the original instruction in an internal lookup table so it can be
 * restored later by woort_CodeEnv_clear_trap().  Thread-safe: acquires the
 * CodeEnv mutex internally.
 *
 * @param env   The code environment. Must not be NULL.
 * @param code  Pointer to the bytecode instruction to trap.  Must point into
 *              the range [env->m_code_begin, env->m_code_end).
 * @return true  if the trap was set successfully (the address was not already
 *               trapped).
 * @return false if the address was already trapped or the internal hashmap
 *               ran out of memory.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_set_trap(
    woort_CodeEnv* env,
    woort_Bytecode* code);

/**
 * @brief Clear a breakpoint (trap) at the given bytecode address.
 *
 * Restores the original bytecode instruction that was overwritten by
 * woort_CodeEnv_set_trap() and removes the trap record.  Thread-safe:
 * acquires the CodeEnv mutex internally.
 *
 * @param env   The code environment. Must not be NULL.
 * @param code  Pointer to the bytecode instruction whose trap should be
 *              cleared.  Must point into the range
 *              [env->m_code_begin, env->m_code_end).
 * @return true  if the trap was found and cleared successfully.
 * @return false if no trap was set at the given address.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_clear_trap(
    woort_CodeEnv* env,
    woort_Bytecode* code);

/**
 * @brief Retrieve the original (pre-trap) bytecode instruction at the given
 *        address.
 *
 * If the instruction at @p code has been replaced by a DEBUGTRAP opcode
 * (i.e. a breakpoint is active), this function looks up and returns the
 * original instruction from the internal trap record.  If no trap is set,
 * the bytecode is returned as-is.  Thread-safe: acquires the CodeEnv mutex
 * internally when a trap lookup is needed.
 *
 * This is primarily useful for disassembly or inspection of bytecode that
 * may have active breakpoint traps.
 *
 * @param env   The code environment. Must not be NULL.
 * @param code  Pointer to the bytecode instruction to read.  Must point into
 *              the range [env->m_code_begin, env->m_code_end).
 * @return The original bytecode instruction at the given address.
 */
WOORT_NODISCARD WOORT_API woort_Bytecode woort_CodeEnv_raw_trap(
    woort_CodeEnv* env,
    const woort_Bytecode* code);

/**
 * @brief Disassemble and print codes to stdout.
 * @param env  The code environment to dump. Must not be NULL.
 */
WOORT_API void woort_CodeEnv_dumps(
    const woort_CodeEnv* env);

/**
 * @brief Register an extern constant with the given name and IR constant index.
 * @param env   The code environment. Must not be NULL.
 * @param name  The name of the extern constant. Must not be NULL.
 * @param cidx  The IR constant index to associate with the name.
 * @return true on success, false if the name already exists or out of memory.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_register_extern_constant(
    woort_CodeEnv* env,
    const char* name,
    woort_IRConstantIndex cidx);

/**
 * @brief Find the IR constant index of an extern constant by name.
 * @param env       The code environment. Must not be NULL.
 * @param name      The name of the extern constant to look up. Must not be NULL.
 * @param[out] out_cidx  Pointer to receive the IR constant index. Must not be NULL.
 * @return true if the extern constant was found, false otherwise.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_find_extern_constant(
    const woort_CodeEnv* env,
    const char* name,
    woort_IRConstantIndex* out_cidx);

/**
 * @brief Associate a dynamic library handle with a CodeEnv.
 *
 * The library's reference count is incremented.  When the CodeEnv is
 * garbage-collected, woort_dylib_unload(WOORT_DYLIB_UNREF) is called on
 * every associated library, ensuring the library outlives the CodeEnv
 * but is released when the CodeEnv is no longer needed.
 *
 * @param env  The code environment. Must not be NULL.
 * @param lib  The library handle to associate. Must not be NULL.
 * @return true on success, false on out-of-memory.
 */
WOORT_NODISCARD WOORT_API bool woort_CodeEnv_add_extern_lib(
    woort_CodeEnv* env,
    woort_Dylib* lib);

/**
 * @brief JIT-compile every function in a code environment.
 *
 * Compiles all functions in @p cenv to native code through
 * woort_JIT_compile_env() and rewrites the script call opcodes in place so
 * the VM executes the compiled bodies on subsequent invocations. The CodeEnv
 * write lock is held for the whole compilation, serializing it against a
 * concurrent dejit.
 *
 * @param cenv  The code environment to compile. Must not be NULL.
 *
 * @note Do not call this on a code environment that is booted through
 *       woort_bootup(). woort_bootup() JIT-compiles the code environment
 *       itself when its @p jit argument is true, so a separate call is at
 *       best redundant and, when the VM is already executing @p cenv's
 *       bytecode, corrupts execution by rewriting the opcodes underneath
 *       the running interpreter. To run a booted code environment under
 *       JIT, pass @c jit = true to woort_bootup() instead.
 */
WOORT_API void woort_CodeEnv_jit(woort_CodeEnv* cenv);

/* ========== IR Compiler ========== */

/**
 * @brief Create a new IR compiler instance.
 * @return The new compiler handle, or NULL on out-of-memory.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_IRCompiler* woort_IRCompiler_create(void);

/**
 * @brief Close and destroy an IR compiler instance.
 * @param c  The compiler to close. Must not be NULL.
 */
WOORT_API void woort_IRCompiler_close(
    woort_IRCompiler* c);

/**
 * @brief Intern a string in the compiler's string pool.
 *
 * Returns a stable pointer for the given string.  Identical content
 * returns the same pointer, enabling pointer-equality comparisons in
 * source-location lookups.  Required by woort_IRFunction_push_srcloc().
 *
 * @param c    The compiler. Must not be NULL.
 * @param str  The string to intern. May be NULL.
 * @return A stable pointer to the interned string, or NULL on OOM
 *         or if str is NULL.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ const char* woort_IRCompiler_intern_string(
    woort_IRCompiler* c,
    /* OPTIONAL */ const char* str);

/**
 * @brief Add a new function definition to the compiler.
 * @param c              The compiler. Must not be NULL.
 * @param param_count    The number of parameters the function accepts.
 * @param captured_count The number of captured upvalues the function closes over.
 * @param[out] out_f     Pointer to receive the IR function handle. Must not be NULL.
 * @return true on success, false on out-of-memory.
 */
WOORT_NODISCARD WOORT_API bool woort_IRCompiler_add_function(
    woort_IRCompiler* c,
    uint32_t param_count,
    uint32_t captured_count,
    woort_IRFunction** out_f);

/**
 * @brief Allocate a new constant pool slot and return its index.
 * @param c  The compiler. Must not be NULL.
 * @return The index of the newly allocated constant slot.
 */
WOORT_NODISCARD WOORT_API woort_IRConstantIndex woort_IRCompiler_add_constant(
    woort_IRCompiler* c);

/**
 * @brief Allocate a new static data slot and return its index.
 * @param c  The compiler. Must not be NULL.
 * @return The index of the newly allocated static slot.
 */
WOORT_NODISCARD WOORT_API woort_IRStaticIndex woort_IRCompiler_add_static(
    woort_IRCompiler* c);

/**
 * @brief Finalize compilation and produce a CodeEnv.
 *
 * After this call the compiler is consumed and must not be used again.
 *
 * @param c              The compiler. Must not be NULL.
 * @param[out] out_cenv  Pointer to receive the compiled CodeEnv. Must not be NULL.
 * @return true on success, false on out-of-memory.
 */
WOORT_NODISCARD WOORT_API bool woort_IRCompiler_finish(
    woort_IRCompiler* c,
    woort_CodeEnv** out_cenv);

/**
 * @brief Create a new virtual register in the function.
 * @param f  The IR function. Must not be NULL.
 * @return The new IR value handle, or NULL on out-of-memory.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_IRValue* woort_IRFunction_new_vreg(
    woort_IRFunction* f);

/**
 * @brief Get the virtual register for a function parameter (pre-allocated at SB+3+idx).
 * @param f         The IR function. Must not be NULL.
 * @param param_idx Zero-based parameter index.
 * @return The IR value handle for the parameter, or NULL if out of range.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f,
    uint32_t param_idx);

/**
 * @brief Get the virtual register for a captured variable (closure upvalue).
 * @param f            The IR function. Must not be NULL.
 * @param captured_idx Zero-based captured variable index.
 * @return The IR value handle for the captured variable, or NULL on out-of-memory.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_captured(
    woort_IRFunction* f,
    uint32_t captured_idx);

/**
 * @brief Create a new label (branch target) in the function.
 * @param f  The IR function. Must not be NULL.
 * @return The new label handle, or NULL on out-of-memory.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_IRLabel* woort_IRFunction_new_label(
    woort_IRFunction* f);

/**
 * @brief Get an IR value representing constant pool entry G[idx].
 *
 * Multiple calls with the same const_index return the same IRValue* (naturally deduplicated).
 * The returned IRValue* has m_source set to WOORT_IRVALUE_SOURCE_CONST.
 *
 * @param f    The IR function. Must not be NULL.
 * @param idx  The constant pool index.
 * @return The IR value for the constant, or NULL on out-of-memory.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ const woort_IRValue* woort_IRFunction_fetch_const(
    woort_IRFunction* f,
    woort_IRConstantIndex idx);

/**
 * @brief Push a source location onto the function's source location stack.
 *
 * Subsequently emitted IR instructions will be associated with the top-of-stack
 * source location.
 *
 * @param f             The IR function. Must not be NULL.
 * @param filepath      Interned source file path string (may be NULL).
 * @param begin_line    Start line number.
 * @param begin_column  Start column number.
 * @param end_line      End line number.
 * @param end_column    End column number.
 * @return true on success, false on out-of-memory.
 */
WOORT_NODISCARD WOORT_API bool woort_IRFunction_push_srcloc(
    woort_IRFunction* f,
    /* OPTIONAL */ const char* filepath,
    uint32_t begin_line,
    uint32_t begin_column,
    uint32_t end_line,
    uint32_t end_column);

/**
 * @brief Pop the top-of-stack source location.
 *
 * The stack must not be empty, otherwise an assert is triggered.
 *
 * @param f  The IR function. Must not be NULL.
 */
WOORT_API void woort_IRFunction_pop_srcloc(woort_IRFunction* f);

/**
 * @brief Set the name of an IR function.
 *
 * The name string need not be interned; it will be copied into the
 * CodeEnv's string pool during woort_IRCompiler_finish().
 * The pointer must remain valid until after finish() completes.
 *
 * @param f     The IR function. Must not be NULL.
 * @param name  The function name. Must not be NULL.
 */
WOORT_API void woort_IRFunction_set_name(
    woort_IRFunction* f,
    const char* name);

/**
 * @brief Record a local variable name -> IRValue mapping for debug info.
 *
 * During woort_IRCompiler_finish(), each recorded mapping is resolved to
 * a stack offset and transferred into the CodeEnv's m_pdb.m_local_var_debug_info.
 *
 * @param f     The IR function. Must not be NULL.
 * @param name  The variable name. Must not be NULL.
 * @param v     The IR value representing the variable. Must not be NULL.
 */
WOORT_API void woort_IRFunction_record_local_var(
    woort_IRFunction* f,
    const char* name,
    woort_IRValue* v);

/**
 * @brief Record a static variable name -> IRStaticIndex mapping for debug info.
 *
 * During woort_IRCompiler_finish(), each recorded mapping is transferred
 * into the CodeEnv's m_pdb.m_static_var_debug_info.
 *
 * @param c     The IR compiler. Must not be NULL.
 * @param name  The variable name. Must not be NULL.
 * @param idx   The static data index to associate with the variable.
 */
WOORT_API void woort_IRCompiler_record_static_var(
    woort_IRCompiler* c,
    const char* name,
    woort_IRStaticIndex idx);

/**
 * @name IR Instruction Emission
 * @brief All woort_IR_* functions append an IROp to the linear instruction list of @p f.
 * @return true on success, false on out-of-memory.
 * @{
 */

 /** @name Data Movement */
 /**@{*/

 /** @brief No-operation: occupies a code slot but has no runtime effect. */
WOORT_NODISCARD WOORT_API bool woort_IR_NOP(
    woort_IRFunction* f);

/** @brief Move: dst = src. */
WOORT_NODISCARD WOORT_API bool woort_IR_MOV(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Load static: dst = Static[idx]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LOAD(
    woort_IRFunction* f,
    woort_IRValue* dst,
    woort_IRStaticIndex idx);

/** @brief Store static: Static[idx] = src. */
WOORT_NODISCARD WOORT_API bool woort_IR_STORE(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    const woort_IRValue* src);

/** @brief Load through pvalue pointer: dst = *ptr. */
WOORT_NODISCARD WOORT_API bool woort_IR_LOADPVALUE(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* ptr);

/** @brief Store through pvalue pointer (with write barrier): *ptr = src. */
WOORT_NODISCARD WOORT_API bool woort_IR_STOREPVALUE(
    woort_IRFunction* f,
    const woort_IRValue* ptr,
    const woort_IRValue* src);

/** @brief Box src into a new pvalue pointer: dst = new box(src). */
WOORT_NODISCARD WOORT_API bool woort_IR_MKPVALUE(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Stack Operations */
/**@{*/

/** @brief Push value onto stack with overflow check. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHCHK(
    woort_IRFunction* f,
    const woort_IRValue* src);

/** @brief Push static storage value onto stack with overflow check.
 *  Equivalent to LOAD + PUSHCHK but without intermediate vreg. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHSTATICCHK(
    woort_IRFunction* f,
    woort_IRStaticIndex src);

/** @brief Pop top of stack into dst. */
WOORT_NODISCARD WOORT_API bool woort_IR_POP(
    woort_IRFunction* f,
    woort_IRValue* dst);

/** @brief Pop count items from stack (discard). */
WOORT_NODISCARD WOORT_API bool woort_IR_POPR(
    woort_IRFunction* f,
    uint32_t count);

/** @brief Pop count_src items from stack (discard, count from register). */
WOORT_NODISCARD WOORT_API bool woort_IR_POPRS(
    woort_IRFunction* f,
    const woort_IRValue* count_src);

/**@}*/

/** @name Type Conversions */
/**@{*/

/** @brief Convert integer to real: dst = (woort_Real)src. */
WOORT_NODISCARD WOORT_API bool woort_IR_ITOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert integer to string: dst = tostring(src). */
WOORT_NODISCARD WOORT_API bool woort_IR_ITOS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert real to integer: dst = (woort_Int)src. */
WOORT_NODISCARD WOORT_API bool woort_IR_RTOI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert real to string: dst = tostring(src). */
WOORT_NODISCARD WOORT_API bool woort_IR_RTOS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @}*/

/** @name Function Calls */
/**@{*/

/**
 * @brief Call a Woolang function (native, without overflow check).
 * @param f       The IR function being compiled.
 * @param target  Constant pool index of the callee.
 * @param argc    Number of arguments already pushed onto the stack.
 * @param dst     Destination register for the return value (may be NULL for void calls).
 */
WOORT_NODISCARD WOORT_API bool woort_IR_CALLNWO(
    woort_IRFunction* f,
    woort_IRConstantIndex target,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

/**
 * @brief Call a Woolang function (with frame pointer setup).
 * @param f       The IR function being compiled.
 * @param target  Constant pool index of the callee.
 * @param argc    Number of arguments already pushed onto the stack.
 * @param dst     Destination register for the return value (may be NULL for void calls).
 */
WOORT_NODISCARD WOORT_API bool woort_IR_CALLNFP(
    woort_IRFunction* f,
    woort_IRConstantIndex target,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

/**
 * @brief Call a JIT-compiled function.
 * @param f       The IR function being compiled.
 * @param target  Constant pool index of the callee.
 * @param argc    Number of arguments already pushed onto the stack.
 * @param dst     Destination register for the return value (may be NULL for void calls).
 */
WOORT_NODISCARD WOORT_API bool woort_IR_CALLNJIT(
    woort_IRFunction* f,
    woort_IRConstantIndex target,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

/**
 * @brief Indirect call through a function value (closure / first-class function).
 * @param f        The IR function being compiled.
 * @param func_val Register holding the callable value.
 * @param argc     Number of arguments already pushed onto the stack.
 * @param dst      Destination register for the return value (may be NULL for void calls).
 */
WOORT_NODISCARD WOORT_API bool woort_IR_CALL(
    woort_IRFunction* f,
    const woort_IRValue* func_val,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

/**@}*/

/** @name Closures / Containers */
/**@{*/

/**
 * @brief Create a closure: dst = new Closure(func_idx, captures...).
 * @param f          The IR function being compiled.
 * @param dst        Destination register for the new closure.
 * @param elem_count Number of captured upvalues on the stack.
 * @param func_idx   Constant pool index of the enclosed function.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_MKCLOSURE(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count,
    woort_IRConstantIndex func_idx);

/**
 * @brief Create a vector: dst = new Vec(capacity = elem_count).
 * @param f          The IR function being compiled.
 * @param dst        Destination register for the new vector.
 * @param elem_count Number of elements already pushed onto the stack.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_MKVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count);

/**
 * @brief Create a map: dst = new Map(capacity = kvpair_count).
 * @param f            The IR function being compiled.
 * @param dst          Destination register for the new map.
 * @param kvpair_count Number of key-value pairs already pushed onto the stack.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_MKMAP(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t kvpair_count);

/**
 * @brief Create a struct: dst = new Struct(fields...).
 * @param f          The IR function being compiled.
 * @param dst        Destination register for the new struct.
 * @param elem_count Number of field values already pushed onto the stack.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_MKSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count);

/**
 * @brief Create a tagged union: dst = new Union(union_id, src).
 * @param f         The IR function being compiled.
 * @param dst       Destination register for the new union value.
 * @param src       Source register holding the union payload value.
 * @param union_id  Variant tag identifying which union case this is.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_MKUNION(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src,
    uint32_t union_id);

/**@}*/

/** @name Dynamic Typing (Boxing / Unboxing) */
/**@{*/

/** @brief Box a value into a dynamic container: dst = Box(typ, src). */
WOORT_NODISCARD WOORT_API bool woort_IR_BOXDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Unbox a dynamic value: dst = unbox(src, typ). Panics if type mismatches. */
WOORT_NODISCARD WOORT_API bool woort_IR_UNBOXDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Check dynamic type: dst = (unbox_type(src) == typ). */
WOORT_NODISCARD WOORT_API bool woort_IR_CHECKDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Box and push a dynamic value onto the stack. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHBOXDYN(
    woort_IRFunction* f,
    uint8_t typ,
    const woort_IRValue* src);

/**@}*/

/** @name String / Boxed Conversions */
/**@{*/

/** @brief Convert string value to T8-specified type: dst = cast_to<T>(src). */
WOORT_NODISCARD WOORT_API bool woort_IR_CASTSTO(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Convert T8-typed value to string: dst = tostring(src). */
WOORT_NODISCARD WOORT_API bool woort_IR_CASTSFROM(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Unbox a BOXED value to T8-specified type: dst = unbox_to<T>(src). */
WOORT_NODISCARD WOORT_API bool woort_IR_CASTDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Assert BOXED value is of T8-specified type; panic if not. */
WOORT_NODISCARD WOORT_API bool woort_IR_ASSERTDYN(
    woort_IRFunction* f,
    uint8_t typ,
    const woort_IRValue* src);

/**@}*/

/** @name Integer Arithmetic (dst = a OP b) */
/**@{*/

/** @brief Integer addition: dst = a + b. */
WOORT_NODISCARD WOORT_API bool woort_IR_ADDI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer subtraction: dst = a - b. */
WOORT_NODISCARD WOORT_API bool woort_IR_SUBI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer multiplication: dst = a * b. */
WOORT_NODISCARD WOORT_API bool woort_IR_MULI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer division overflow check: panics if a == INT64_MIN. */
WOORT_NODISCARD WOORT_API bool woort_IR_CHKDIVIL(
    woort_IRFunction* f,
    const woort_IRValue* a);
/** @brief Integer division guard: panics if a == 0 or a == -1. */
WOORT_NODISCARD WOORT_API bool woort_IR_CHKDIVIR(
    woort_IRFunction* f,
    const woort_IRValue* a);
/** @brief Integer division zero guard: panics if a == 0. */
WOORT_NODISCARD WOORT_API bool woort_IR_CHKDIVIRZ(
    woort_IRFunction* f,
    const woort_IRValue* a);
/** @brief Integer division full check: panics if b == 0 or (b == -1 and a == INT64_MIN). */
WOORT_NODISCARD WOORT_API bool woort_IR_CHKDIVILR(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer division: dst = a / b. */
WOORT_NODISCARD WOORT_API bool woort_IR_DIVI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer modulo: dst = a % b. */
WOORT_NODISCARD WOORT_API bool woort_IR_MODI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer negation: dst = -src. */
WOORT_NODISCARD WOORT_API bool woort_IR_NEGI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Integer Comparison (dst = (a OP b) ? 1 : 0) */
/**@{*/

/** @brief Integer less-than: dst = (a < b). */
WOORT_NODISCARD WOORT_API bool woort_IR_LTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer greater-than: dst = (a > b). */
WOORT_NODISCARD WOORT_API bool woort_IR_GTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer less-or-equal: dst = (a <= b). */
WOORT_NODISCARD WOORT_API bool woort_IR_LEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer greater-or-equal: dst = (a >= b). */
WOORT_NODISCARD WOORT_API bool woort_IR_GEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer equal: dst = (a == b). */
WOORT_NODISCARD WOORT_API bool woort_IR_EQI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer not-equal: dst = (a != b). */
WOORT_NODISCARD WOORT_API bool woort_IR_NEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/**@}*/

/** @name Real Arithmetic (dst = a OP b) */
/**@{*/

/** @brief Real addition: dst = a + b. */
WOORT_NODISCARD WOORT_API bool woort_IR_ADDR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real subtraction: dst = a - b. */
WOORT_NODISCARD WOORT_API bool woort_IR_SUBR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real multiplication: dst = a * b. */
WOORT_NODISCARD WOORT_API bool woort_IR_MULR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real division: dst = a / b. */
WOORT_NODISCARD WOORT_API bool woort_IR_DIVR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real modulo: dst = fmod(a, b). */
WOORT_NODISCARD WOORT_API bool woort_IR_MODR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real negation: dst = -src. */
WOORT_NODISCARD WOORT_API bool woort_IR_NEGR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Real Comparison (dst = (a OP b) ? 1 : 0) */
/**@{*/

/** @brief Real less-than: dst = (a < b). */
WOORT_NODISCARD WOORT_API bool woort_IR_LTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real greater-than: dst = (a > b). */
WOORT_NODISCARD WOORT_API bool woort_IR_GTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real less-or-equal: dst = (a <= b). */
WOORT_NODISCARD WOORT_API bool woort_IR_LER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real greater-or-equal: dst = (a >= b). */
WOORT_NODISCARD WOORT_API bool woort_IR_GER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real equal: dst = (a == b). */
WOORT_NODISCARD WOORT_API bool woort_IR_EQR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real not-equal: dst = (a != b). */
WOORT_NODISCARD WOORT_API bool woort_IR_NER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/**@}*/

/** @name String Operations */
/**@{*/

/** @brief String concatenation: dst = a .. b. */
WOORT_NODISCARD WOORT_API bool woort_IR_ADDS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String less-than (lexicographic): dst = (a < b). */
WOORT_NODISCARD WOORT_API bool woort_IR_LTS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String greater-than (lexicographic): dst = (a > b). */
WOORT_NODISCARD WOORT_API bool woort_IR_GTS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String less-or-equal (lexicographic): dst = (a <= b). */
WOORT_NODISCARD WOORT_API bool woort_IR_LES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String greater-or-equal (lexicographic): dst = (a >= b). */
WOORT_NODISCARD WOORT_API bool woort_IR_GES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String equal: dst = (a == b). */
WOORT_NODISCARD WOORT_API bool woort_IR_EQS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String not-equal: dst = (a != b). */
WOORT_NODISCARD WOORT_API bool woort_IR_NES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/**@}*/

/** @name Logical Operations */
/**@{*/

/** @brief Logical AND: dst = a && b. */
WOORT_NODISCARD WOORT_API bool woort_IR_LAND(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Logical OR: dst = a || b. */
WOORT_NODISCARD WOORT_API bool woort_IR_LOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Logical NOT: dst = !src. */
WOORT_NODISCARD WOORT_API bool woort_IR_LNOT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Index Load (Read element from container) */
/**@{*/

/** @brief Load vector element by integer index (bounds-checked): dst = container[idx]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load vector element by integer index (unchecked): dst = container[idx]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDVECX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load struct field by constant index: dst = container[idx]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    uint32_t idx);

/** @brief Load string character by integer index: dst = container[idx]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDSTRING(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by integer key: dst = container[key]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by real key: dst = container[key]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by bool key: dst = container[key]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTB(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by boxed (dynamic) key: dst = container[key]. */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by integer key (boxed result): dst = box(container[key]). */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTIX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by real key (boxed result): dst = box(container[key]). */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTRX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by bool key (boxed result): dst = box(container[key]). */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTBX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by dynamic key (boxed result): dst = box(container[key]). */
WOORT_NODISCARD WOORT_API bool woort_IR_LDIDDICTXX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/**@}*/

/** @name Index Store — Vector (container[idx] = val) */
/**@{*/

/** @brief Store integer value into vector at index. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDVECI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Store real value into vector at index. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDVECR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Store bool value into vector at index. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDVECB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Store boxed (dynamic) value into vector at index. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDVECX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/**@}*/

/** @name Index Store — Dict (container[key] = val)
 *
 * Naming convention: STIDDICT{KeyType}{ValueType} where
 * I=int, R=real, B=bool, X=boxed(dynamic).
 * @{*/

 /** @brief Dict[int] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTII(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[int] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTIR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[int] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTIB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[int] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTIX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTRI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTRR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTRB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTRX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTBI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTBR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTBB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTBX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTXI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTXR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTXB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDDICTXX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/**@}*/

/** @name Index Store — Map (container[key] = val)
 *
 * Same key/value type convention as Dict: I=int, R=real, B=bool, X=boxed(dynamic).
 * @{*/

 /** @brief Map[int] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPII(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[int] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPIR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[int] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPIB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[int] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPIX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPRI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPRR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPRB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPRX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPBI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPBR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPBB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPBX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = int. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPXI(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = real. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPXR(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = bool. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPXB(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = boxed. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDMAPXX(
    woort_IRFunction* f,
    const woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/**@}*/

/** @name Index Store — Struct (container[idx] = val, idx is a constant) */
/**@{*/

/** @brief Store value into struct field at constant index. */
WOORT_NODISCARD WOORT_API bool woort_IR_STIDSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* c,
    uint32_t idx,
    const woort_IRValue* val);

/**@}*/

/** @name Unpacking */
/**@{*/

/**
 * @brief Unpack vector and unbox elements onto stack.
 * @param f IR function
 * @param count Number of elements to unpack
 * @param val Vector value to unpack
 * @return true on success, false on OOM
 * @note Emits UNPACKVEC bytecode (mode=0)
 */
WOORT_NODISCARD WOORT_API bool woort_IR_UNPACKVEC(
    woort_IRFunction* f,
    uint8_t count,
    const woort_IRValue* val);

/**
 * @brief Unpack vector without unboxing onto stack.
 * @param f IR function
 * @param count Number of elements to unpack
 * @param val Vector value to unpack
 * @return true on success, false on OOM
 * @note Emits UNPACKVECX bytecode (mode=1)
 */
WOORT_NODISCARD WOORT_API bool woort_IR_UNPACKVECX(
    woort_IRFunction* f,
    uint8_t count,
    const woort_IRValue* val);

/**
 * @brief Unpack all vector elements, unboxing at least count elements.
 * @param f IR function
 * @param dst Output slot to store actual unpacked count
 * @param count Minimum number of elements to unpack and unbox
 * @param val Vector value to unpack
 * @return true on success, false on OOM
 * @note Emits UNPACKVECALL bytecode (mode=2)
 */
WOORT_NODISCARD WOORT_API bool woort_IR_UNPACKVECALL(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t count,
    const woort_IRValue* val);

/**
 * @brief Unpack all vector elements without unboxing, writing count to dst.
 * @param f IR function
 * @param dst Output slot to store actual unpacked count
 * @param count Minimum number of elements to unpack
 * @param val Vector value to unpack
 * @return true on success, false on OOM
 * @note Emits UNPACKVECXALL bytecode (mode=3)
 */
WOORT_NODISCARD WOORT_API bool woort_IR_UNPACKVECXALL(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t count,
    const woort_IRValue* val);

/**@}*/

/** @name Struct Field Push to Stack */
/**@{*/

/** @brief Push struct field at constant index onto the stack. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHIDSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed int struct field at index onto stack. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHIDSTBOXI(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed real struct field at index onto stack. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHIDSTBOXR(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed bool struct field at index onto stack. */
WOORT_NODISCARD WOORT_API bool woort_IR_PUSHIDSTBOXB(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/**
 * @brief Atomic store operation (release semantics).
 * @param f    The IR function. Must not be NULL.
 * @param idx  Static index of the atomic variable.
 * @param src  The value to store.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_ASTORE(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    const woort_IRValue* src);

/**
 * @brief Atomic load operation (acquire semantics).
 * @param f    The IR function. Must not be NULL.
 * @param dst  The destination virtual register to store the loaded value.
 * @param idx  Static index of the atomic variable.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_ALOAD(
    woort_IRFunction* f,
    woort_IRValue* dst,
    woort_IRStaticIndex idx);

/**
 * @brief Compare-and-swap operation.
 * @param f        The IR function. Must not be NULL.
 * @param idx      Static index of the atomic variable.
 * @param expected The expected current value (compare and load).
 * @param desired  The desired new value to swap in if comparison succeeds.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_CAS(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    woort_IRValue* expected,
    const woort_IRValue* desired);

/**
 * @brief Pack remaining arguments into an array for variadic function calls.
 * @param f                The IR function. Must not be NULL.
 * @param named_param_count Number of named parameters to skip (arguments already bound).
 * @param dst              The destination to store the packed argument array.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_PACKARG(
    woort_IRFunction* f,
    uint16_t named_param_count,
    woort_IRValue* dst);

/**@}*/

/** @} */ /* end IR Instruction Emission group */

/* ============ Control Flow ============ */

/**
 * @brief Bind a label to the current emission position.
 * @param f      The IR function. Must not be NULL.
 * @param label  The label to bind. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_bind(woort_IRFunction* f, woort_IRLabel* label);

/**
 * @brief Unconditional jump to target label.
 * @param f       The IR function. Must not be NULL.
 * @param target  The label to jump to. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_jmp(woort_IRFunction* f, woort_IRLabel* target);

/**
 * @brief Thread-safe once-only initialization guard.
 *
 * Emits a JIFINITED instruction that performs an atomic check on the static
 * data value. The 64-bit integer at that slot acts as a tri-state
 * flag: 0 = uninitialized, 1 = initializing (another thread), 2 = initialized.
 *
 * - If flag == 2: jump to @p target (initialization already complete).
 * - If flag == 0: atomically CAS flag 0 -> 1; on success, fall through to
 *   the next instruction so the caller can emit initialization code.
 * - If flag == 1 or CAS failed: spin (with GC checkpoints) until flag
 *   becomes 2, then jump to @p target.
 *
 * @param f          The IR function. Must not be NULL.
 * @param cond_idx   Static index of the atomic flag slot.
 * @param target     The label to jump to when initialized. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_jifinited(
    woort_IRFunction* f,
    woort_IRStaticIndex cond_idx,
    woort_IRLabel* target);

/**
 * @brief Conditional jump: if (cond != 0) goto target.
 * @param f       The IR function. Must not be NULL.
 * @param cond    The condition register.
 * @param target  The label to jump to. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc(
    woort_IRFunction* f,
    const woort_IRValue* cond,
    woort_IRLabel* target);

/**
 * @brief Conditional jump: if (cond == 0) goto target.
 * @param f       The IR function. Must not be NULL.
 * @param cond    The condition register.
 * @param target  The label to jump to. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_jccz(
    woort_IRFunction* f,
    const woort_IRValue* cond,
    woort_IRLabel* target);

/** @name Compare-and-Jump
 * @brief Combined comparison and conditional branch: if (a OP b) goto target.
 * @return true on success, false on OOM.
 * @{ */

 /** @brief Jump if a < b. */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc_lt(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a <= b. */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc_le(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a == b. */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc_eq(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a > b. */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc_gt(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a >= b. */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc_ge(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a != b. */
WOORT_NODISCARD WOORT_API bool woort_IR_jcc_ne(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @} */

/* ============ Trap / Panic ============ */

/**
 * @brief Emit a debug trap (breakpoint).
 * @param f  The IR function. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_debugtrap(woort_IRFunction* f);

/**
 * @brief Emit a panic with a string message.
 * @param f    The IR function. Must not be NULL.
 * @param msg  The string value to use as the panic message.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_panic(
    woort_IRFunction* f, const woort_IRValue* msg);

/* ============ Return ============ */

/**
 * @brief Return a value from the current function.
 * @param f    The IR function. Must not be NULL.
 * @param val  The return value register.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_ret(woort_IRFunction* f, const woort_IRValue* val);

/**
 * @brief Return void from the current function.
 * @param f  The IR function. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_IR_ret_void(woort_IRFunction* f);

/**
 * @name CodeEnv Constant Pool Setters
 * @brief Set the value of a constant pool entry in a locked CodeEnv.
 *
 * @note These functions MUST be called between woort_CodeEnv_lock() and
 *       woort_CodeEnv_unlock(). The lock ensures thread-safe access to the
 *       constant pool and proper GC write barrier handling.
 *
 * @note The constant pool index @p cidx must have been allocated via
 *       woort_IRCompiler_add_constant() BEFORE calling woort_IRCompiler_finish().
 *       After finish(), the constant pool layout is fixed and no new constants
 *       can be added.
 *
 * Usage pattern:
 * @code
 *   woort_IRCompiler_add_constant(irc);  // allocate before finish
 *   woort_IRCompiler_finish(irc, &cenv);
 *   woort_CodeEnv_lock(cenv);
 *   woort_CodeEnv_set_const_int(cenv, cidx, 42);  // set value
 *   woort_CodeEnv_unlock(cenv);
 * @endcode
 * @{
 */

 /**
  * @brief Set a constant pool entry to an integer value.
  * @param code_env  The locked code environment.
  * @param cidx      The constant pool index (must be allocated before finish).
  * @param val       The integer value to store.
  */
WOORT_API void woort_CodeEnv_set_const_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val);

/**
 * @brief Set a constant pool entry to a real (floating-point) value.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       The real value to store.
 */
WOORT_API void woort_CodeEnv_set_const_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val);

/**
 * @brief Set a constant pool entry to a buffer value.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param buf       The buffer data to store (a GCString is created).
 * @param buflen    The length of the buffer in bytes.
 */
WOORT_API void woort_CodeEnv_set_const_buffer(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const void* buf,
    size_t buflen);

/**
 * @brief Set a constant pool entry to a script function entry point.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       Pointer to the bytecode of the script function entry point.
 *
 * @note This creates a reference to a script function. For closures that
 *       capture the environment, use woort_CodeEnv_set_const_script_closure().
 */
WOORT_API void woort_CodeEnv_set_const_script_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val);

/**
 * @brief Set a constant pool entry to an extern (native) function.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       Pointer to the native function entry point.
 *
 * @note The native function must follow the woort_api calling convention
 *       and return woort_VmCallStatus.
 */
WOORT_API void woort_CodeEnv_set_const_extern_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_NativeFunction val);

/**
 * @brief Set a constant pool entry to a script closure (function + env).
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       Pointer to the bytecode entry point of the script function.
 *
 * @note This creates a GCClosure wrapping the script function. The closure
 *       inherits the CodeEnv from the bytecode address.
 */
WOORT_API void woort_CodeEnv_set_const_script_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val);

/**
 * @brief Set a constant pool entry to an extern (native) closure.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       Pointer to the native function entry point.
 *
 * @note This creates a GCClosure wrapping the native function. The closure
 *       can be called from script code via CALLNFP/CALLNWO.
 */
WOORT_API void woort_CodeEnv_set_const_extern_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_NativeFunction val);

/**
 * @brief Set a constant pool entry to a boxed integer value.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       The integer value to box.
 *
 * @note A DynBox object is allocated on the GC heap and the constant holds
 *       a reference to this box. Use woort_unbox_int() to retrieve the value.
 */
WOORT_API void woort_CodeEnv_set_const_box_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val);

/**
 * @brief Set a constant pool entry to a boxed real value.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       The real value to box.
 *
 * @note A DynBox object is allocated on the GC heap and the constant holds
 *       a reference to this box. Use woort_unbox_real() to retrieve the value.
 */
WOORT_API void woort_CodeEnv_set_const_box_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val);

/**
 * @brief Set a constant pool entry to a boxed boolean value.
 * @param code_env  The locked code environment.
 * @param cidx      The constant pool index (must be allocated before finish).
 * @param val       The boolean value to box.
 *
 * @note A DynBox object is allocated on the GC heap and the constant holds
 *       a reference to this box. Use woort_unbox_bool() to retrieve the value.
 */
WOORT_API void woort_CodeEnv_set_const_box_bool(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    bool val);

/**
 * @brief Set a constant pool entry to a struct composed of member constants.
* @param code_env     The locked code environment.
* @param cidx         The constant pool index for the struct (must be allocated before finish).
* @param members      Array of constant indices for each struct field.
* @param member_count Number of fields in the struct.
*
* @note Each member must already have its value set via one of the other
*       set_const functions before calling this function.
*/
WOORT_API void woort_CodeEnv_set_const_struct(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    /* OPTIONAL if member_count == 0 */ const woort_IRConstantIndex* members,
    size_t member_count);

/** @} */ /* end CodeEnv Constant Pool Setters */

/* ========== CodeEnv Static Storage Access ========== */

/**
 * @brief Write a typed value into a static storage slot.
 *
 * Static slots are the per-CodeEnv storage for global/static variables,
 * located after the constant pool in @ref woort_CodeEnv::m_data_begin.
 * They are zero-initialized at CodeEnv creation and normally written by
 * bytecode STORE instructions during execution. This function allows the
 * host to inject values before booting the CodeEnv (e.g., for REPL session
 * state restoration).
 *
 * @param code_env  The locked code environment.
 * @param sidx      The static storage index (allocated via
 *                  woort_IRCompiler_add_static() before finish()).
 * @param val       The value to store.
 *
 * @note The caller must hold woort_CodeEnv_lock() around this call.
 */
WOORT_API void woort_CodeEnv_set_static_value(
    woort_CodeEnv* code_env,
    woort_IRStaticIndex sidx,
    const woort_Value* val);

/**
 * @brief Read a typed value from a static storage slot.
 *
 * @param code_env  The locked code environment.
 * @param sidx      The static storage index.
 * @return The value stored in the slot.
 *
 * @note The caller must hold woort_CodeEnv_lock() around this call.
 */
WOORT_API void woort_CodeEnv_get_static_value(
    woort_CodeEnv* code_env,
    woort_IRStaticIndex sidx,
    woort_Value* out_val);

/**
 * @brief Get the number of static storage slots in a CodeEnv.
 *
 * Static slots are located after the constant pool in
 * @ref woort_CodeEnv::m_data_begin. This returns the count of those
 * slots (i.e., total data slots minus constant pool slots).
 *
 * @param code_env  The code environment. Must not be NULL.
 * @return The number of static storage slots.
 */
WOORT_NODISCARD WOORT_API size_t woort_CodeEnv_get_static_storage_count(
    const woort_CodeEnv* code_env);

/* ============ Runtime API ============ */

/* For ease of writing, aliases for types/methods are provided here. */

typedef woort_VMRuntime woort_vm;
typedef woort_StackValue woort_value;
typedef woort_CodeEnv woort_codeenv;
typedef woort_IRConstantIndex woort_constidx;
typedef woort_VmCallStatus woort_callstatus;

/**
 * @brief Create a new VM runtime instance (convenience wrapper).
 *
 * This is a simplified alternative to woort_VMRuntime_create().
 * Instead of using an output-parameter pattern, it returns the new
 * VM handle directly, or NULL on failure.
 *
 * Internally delegates to woort_VMRuntime_create().
 * The caller must destroy the VM with woort_vm_close (alias for
 * woort_VMRuntime_destroy) when done.
 *
 * @return Pointer to the new VM runtime, or NULL on out-of-memory.
 */
WOORT_NODISCARD WOORT_API
/* OPTIONAL */ woort_VMRuntime* woort_vm_create(void);

#define woort_vm_close woort_VMRuntime_destroy
#define woort_vm_swap woort_VMRuntime_swap
#define woort_vm_get_runtime_error woort_VMRuntime_get_runtime_error_msg
#define woort_codeenv_drop woort_CodeEnv_drop

typedef enum woort_PanicReason
{
    WOORT_PANIC_BAD_BYTE_CODE = 0xD001,
    WOORT_PANIC_STACK_OVERFLOW = 0xD002,
    WOORT_PANIC_CODE_NOT_FOUND = 0xD003,
    WOORT_PANIC_BAD_CALLSTACK = 0xD004,
    WOORT_PANIC_BAD_TYPE = 0xD005,
    WOORT_PANIC_BAD_VM_REQUEST = 0xD006,
    WOORT_PANIC_ABORTED = 0xD007,
    WOORT_PANIC_INDEX_OUT_OF_RANGE = 0xD008,
    WOORT_PANIC_USER = 0xD009,
    WOORT_PANIC_INTEGER_DIV_FAIL = 0xD00A,
    WOORT_PANIC_OUT_OF_MEMORY = 0xD00B,
    WOORT_PANIC_ALREADY_CLOSED = 0xD00C,
} woort_PanicReason;

WOORT_API void woort_raise_panic(
    woort_PanicReason reason,
    const char* funcname,
    const char* location,
    int line,
    const char* msgfmt,
    ...);

#define woort_panic(REASON, MSGFMT, ...) \
    woort_raise_panic((woort_PanicReason)(REASON), __FUNCTION__, __FILE__, __LINE__, MSGFMT,##__VA_ARGS__)

/**
 * @brief Reserve space on the VM evaluation stack.
 * @param count      Number of stack slots to reserve.
 * @param[out] out_stack  Pointer to receive the base stack index of the reserved region.
 * @return true on success, false on stack overflow.
 */
WOORT_NODISCARD WOORT_API bool woort_push_reserve(
    size_t count, woort_StackValue* out_stack);

/**
 * @brief Pop (discard) the top count values from the VM evaluation stack.
 * @param count  Number of values to pop.
 */
WOORT_API void woort_pop(size_t count);

/**
 * @brief Get a direct pointer to a value on the VM evaluation stack.
 * @param src  Stack slot index (positive for absolute, negative for frame-relative).
 * @return Pointer to the woort_Value at the given stack slot.
 * @note This is an internal API. The returned pointer is valid until the
 *       stack is resized or the VM enters a GC checkpoint.
 */
WOORT_NODISCARD WOORT_API woort_Value* woort_internal_value(woort_StackValue src);

/**
 * @brief Import (copy) a value from another VM's stack into the current VM.
 * @param dst        Destination stack slot in the current VM.
 * @param src_vm     Source VM instance.
 * @param src_in_vm  Source stack slot in the source VM.
 */
WOORT_API void woort_import_value(
    woort_StackValue dst,
    woort_VMRuntime* src_vm,
    woort_StackValue src_in_vm);

/**
 * @brief Load the default entry function from a CodeEnv, optionally
 *        JIT-compile it, and invoke it.
 *
 * When @p jit is true, the code environment is JIT-compiled via
 * woort_JIT_compile_env() before invocation. Reserves a stack slot,
 * loads the extern constant named WOORT_DEFAULT_ENTRY ("@entry"), and
 * invokes it.
 *
 * @note Passing @p jit = true is a request, not a guarantee: JIT code is
 *       only generated when JIT is actually enabled (e.g. not disabled at
 *       startup) and the running platform supports it. Otherwise the VM
 *       silently falls back to interpreting the bytecode, and the call
 *       proceeds normally.
 *
 * This is the preferred entry point and supersedes the deprecated
 * woort_bootup_codeenv().
 *
 * @param dst   Stack slot for the return value, or WOORT_IGNORE to discard.
 * @param cenv  The code environment holding the compiled bytecode. Must not be NULL.
 * @param jit   If true, JIT-compile the code environment before invocation.
 * @return The call status (NORMAL or ABORTED).
 */
WOORT_NODISCARD WOORT_API woort_VmCallStatus woort_bootup(
    woort_StackValue dst, woort_CodeEnv* cenv, bool jit);

/**
 * @brief Invoke a function value and wait for completion.
 * @param dst  Stack slot for the return value, or WOORT_IGNORE to discard.
 * @param f    Stack slot holding the callable value.
 * @return The call status (NORMAL or ABORTED).
 */
WOORT_NODISCARD WOORT_API woort_VmCallStatus woort_invoke(
    woort_StackValue dst, woort_StackValue f);

/**
 * @brief Spawn a new coroutine from a function value.
 * @param dst  Stack slot for the return value, or WOORT_IGNORE to discard.
 * @param f    Stack slot holding the callable value.
 * @return The call status (NORMAL, YIELD or ABORTED).
 */
WOORT_NODISCARD WOORT_API woort_VmCallStatus woort_spawn(
    woort_StackValue dst, woort_StackValue f);

/**
 * @brief Resume a previously yielded coroutine.
 * @param dst  Stack slot for the return value, or WOORT_IGNORE to discard.
 * @return The call status (NORMAL, YIELD or ABORTED).
 */
WOORT_NODISCARD WOORT_API woort_VmCallStatus woort_resume(
    woort_StackValue dst);

/**
 * @brief Load a constant from a CodeEnv into a stack slot.
 * @param dst       Destination stack slot.
 * @param code_env  The code environment holding the constant pool.
 * @param cidx      The constant pool index to load.
 */
WOORT_API void woort_load_const(
    woort_StackValue dst, const woort_CodeEnv* code_env, woort_IRConstantIndex cidx);

/**
 * @brief Load an extern constant from a CodeEnv into a stack slot by name.
 *
 * Looks up the extern constant with the given name in the code environment,
 * then copies its value into the destination stack slot.
 *
 * @param dst       Destination stack slot.
 * @param code_env  The code environment holding the constant pool. Must not be NULL.
 * @param name      The name of the extern constant to look up. Must not be NULL.
 * @return true if the extern constant was found and loaded, false otherwise.
 */
WOORT_NODISCARD WOORT_API bool woort_load_extern_const(
    woort_StackValue dst,
    const woort_CodeEnv* code_env,
    const char* name);

/**
 * @name Stack Value Setters
 * @brief Write a typed value into a VM stack slot.
 *
 * @param dst  Target stack slot index.
 * @param src  The value to write (type varies per function).
 * @{
 */

 /** @brief Copy a value from one stack slot to another: dst = src. */
WOORT_API void woort_set_value(
    woort_StackValue dst, woort_StackValue src);

/** @brief Shallow-copy a boxed value: new container for VEC/MAP/STRUCT, direct assign for others. */
WOORT_API void woort_set_dup_boxed(
    woort_StackValue boxed_dst, woort_StackValue boxed_src);

/** @brief Set a stack slot to nil. */
WOORT_API void woort_set_nil(
    woort_StackValue dst);

/** @brief Alias for woort_set_nil — set a void return slot. */
#define woort_set_void woort_set_nil

/** @brief Set a stack slot to an integer value. */
WOORT_API void woort_set_int(
    woort_StackValue dst, woort_Int src);

/** @brief Set a stack slot to an pointer (cast to integer). */
#define woort_set_pointer(dst, src) (woort_set_int(dst, (woort_Int)(intptr_t)(src)))

/** @brief Set a stack slot to a boxed pointer (cast to integer). */
#define woort_set_box_pointer(dst, src) (woort_set_box_int(dst, (woort_Int)(intptr_t)(src)))

/** @brief Set a stack slot to a real (double) value. */
WOORT_API void woort_set_real(
    woort_StackValue dst, woort_Real src);

/** @brief Set a stack slot to a single-precision float value. */
WOORT_API void woort_set_float(
    woort_StackValue dst, float src);

/** @brief Set a stack slot to a boolean value. */
WOORT_API void woort_set_bool(
    woort_StackValue dst, bool src);

/** @brief Set a stack slot to a string value (UTF-8, null-terminated). */
WOORT_API void woort_set_string(
    woort_StackValue dst, woort_U8CString src);

WOORT_API void woort_set_string_fmt(
    woort_StackValue dst, woort_U8CString fmt, ...);

/** @brief Set a stack slot to a byte buffer value (copied). */
WOORT_API void woort_set_buffer(
    woort_StackValue dst, const void* src, size_t len);

/** @brief Set a stack slot to an empty vector. */
WOORT_API void woort_set_vec(
    woort_StackValue dst);

/** @brief Set a stack slot to an empty map. */
WOORT_API void woort_set_map(
    woort_StackValue dst);

/** @brief Set a stack slot to an empty struct with the given capacity. */
WOORT_API void woort_set_struct(
    woort_StackValue dst, size_t cap);

/**
 * @brief Set a stack slot to a GC handle (external resource).
 * @param dst    Target stack slot.
 * @param addr   Pointer to the external resource.
 * @param hold   Stack slot holding a reference to prevent premature collection,
 *               or WOORT_IGNORE to not hold any value.
 * @param close  Destructor callback invoked when the handle is collected.
 * @param dylib  Optional dynamic library to keep alive for the lifetime of the handle, or NULL.
 */
WOORT_API void woort_set_gchandle(
    woort_StackValue dst,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib);

/**
 * @brief Set a stack slot to a GC-managed struct (external object with mark callback).
 * @param dst    Target stack slot.
 * @param addr   Pointer to the external object.
 * @param mark   Mark callback to trace GC references within the object.
 * @param close  Destructor callback invoked when collected.
 * @param dylib  Optional dynamic library to keep alive for the lifetime of the handle, or NULL.
 */
WOORT_API void woort_set_gcstruct(
    woort_StackValue dst,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib);

/** @brief Set a stack slot to a boxed integer. */
WOORT_API void woort_set_box_int(
    woort_StackValue dst, woort_Int src);

/** @brief Set a stack slot to a boxed real. */
WOORT_API void woort_set_box_real(
    woort_StackValue dst, woort_Real src);

/** @brief Set a stack slot to a boxed float. */
#define woort_set_box_float(DST, SRC) \
    woort_set_box_real(DST, (woort_Real)SRC)

/** @brief Set a stack slot to a boxed boolean. */
WOORT_API void woort_set_box_bool(
    woort_StackValue dst, bool src);

/** @} */ /* end Stack Value Setters */

/**
 * @name Union Value Setters
 * @brief Write a tagged union value into a VM stack slot.
 *
 * @param dst  Target stack slot index.
 * @param id   Union variant tag (discriminant).
 * @param src  The value payload (type varies per function).
 * @{
 */

 /** @brief Set union to a variant with no inline payload (unit-like). */
WOORT_API void woort_set_union_without_value(
    woort_StackValue dst, woort_Int id);

/** @brief Set union to a variant carrying a stack value: dst[id] = val. */
WOORT_API void woort_set_union_value(
    woort_StackValue dst, woort_Int id, woort_StackValue val);

/** @brief Set union to a nil variant. */
WOORT_API void woort_set_union_nil(
    woort_StackValue dst, woort_Int id);

/** @brief Alias for woort_set_union_nil — set union to a void variant. */
#define woort_set_union_void woort_set_union_nil

/** @brief Set union to an integer variant. */
WOORT_API void woort_set_union_int(
    woort_StackValue dst, woort_Int id, woort_Int src);

/** @brief Set union to a pointer variant (cast to integer). */
#define woort_set_union_pointer(dst, id, src) (woort_set_union_int(dst, id, (woort_Int)(intptr_t)(src)))

/** @brief Set union to a real variant. */
WOORT_API void woort_set_union_real(
    woort_StackValue dst, woort_Int id, woort_Real src);

/** @brief Set union to a float variant. */
WOORT_API void woort_set_union_float(
    woort_StackValue dst, woort_Int id, float src);

/** @brief Set union to a bool variant. */
WOORT_API void woort_set_union_bool(
    woort_StackValue dst, woort_Int id, bool src);

/** @brief Set union to a string variant. */
WOORT_API void woort_set_union_string(
    woort_StackValue dst, woort_Int id, woort_U8CString src);

WOORT_API void woort_set_union_string_fmt(
    woort_StackValue dst, woort_Int id, woort_U8CString fmt, ...);

/** @brief Set union to a buffer variant (copied). */
WOORT_API void woort_set_union_buffer(
    woort_StackValue dst, woort_Int id, const void* src, size_t len);

/** @brief Set union to a vec variant (new GCVec, pre-sized to @p cap). */
WOORT_API void woort_set_union_vec(
    woort_StackValue dst, woort_Int id, size_t cap);

/** @brief Set union to a map variant (new GCMap, pre-reserved to @p reserve). */
WOORT_API void woort_set_union_map(
    woort_StackValue dst, woort_Int id, size_t reserve);

/** @brief Set union to a struct variant (new GCStruct with @p cap fields). */
WOORT_API void woort_set_union_struct(
    woort_StackValue dst, woort_Int id, size_t cap);

/**
 * @brief Set union to a GC handle variant.
 * @param dst    Target stack slot.
 * @param id     Variant tag.
 * @param addr   Pointer to the external resource.
 * @param hold   Stack slot holding a reference to prevent premature collection,
 *               or WOORT_IGNORE to not hold any value.
 * @param close  Destructor callback.
 * @param dylib  Optional dynamic library to keep alive for the lifetime of the handle, or NULL.
 */
WOORT_API void woort_set_union_gchandle(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib);

/**
 * @brief Set union to a GC struct variant.
 * @param dst    Target stack slot.
 * @param id     Variant tag.
 * @param addr   Pointer to the external object.
 * @param mark   Mark callback.
 * @param close  Destructor callback.
 * @param dylib  Optional dynamic library to keep alive for the lifetime of the handle, or NULL.
 */
WOORT_API void woort_set_union_gcstruct(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close,
    /* OPTIONAL */ woort_Dylib* dylib);

/** @brief Set union to a boxed integer variant. */
WOORT_API void woort_set_union_box_int(
    woort_StackValue dst, woort_Int id, woort_Int src);

/** @brief Set union to a boxed pointer variant (cast to integer). */
#define woort_set_union_box_pointer(dst, id, src) (woort_set_union_box_int(dst, id, (woort_Int)(intptr_t)(src)))

/** @brief Set union to a boxed real variant. */
WOORT_API void woort_set_union_box_real(
    woort_StackValue dst, woort_Int id, woort_Real src);

/** @brief Set union to a boxed boolean variant. */
WOORT_API void woort_set_union_box_bool(
    woort_StackValue dst, woort_Int id, bool src);

/** @} */ /* end Union Value Setters */

/**
 * @name Option Setters (Union with id=0 for value, id=1 for none)
 * @brief Convenience macros for writing Option<T> values to a stack slot.
 *        id=0 is the value variant, id=1 is the none variant.
 * @{
 */

 /** @brief Set Option to none (no value). */
#define woort_set_option_none(dst) woort_set_union_without_value(dst, 1)

/** @brief Set option::value(stack_value). */
#define woort_set_option_value(dst, src) woort_set_union_value(dst, 0, src)
/** @brief Set option::value(nil). */
#define woort_set_option_nil(dst) woort_set_union_nil(dst, 0)
/** @brief Set option::value(void) — alias for nil. */
#define woort_set_option_void(dst) woort_set_union_void(dst, 0)
/** @brief Set option::value(int). */
#define woort_set_option_int(dst, src) woort_set_union_int(dst, 0, src)
/** @brief Set option::value(pointer). */
#define woort_set_option_pointer(dst, src) woort_set_union_pointer(dst, 0, src)
/** @brief Set option::value(real). */
#define woort_set_option_real(dst, src) woort_set_union_real(dst, 0, src)
/** @brief Set option::value(float). */
#define woort_set_option_float(dst, src) woort_set_union_float(dst, 0, src)
/** @brief Set option::value(bool). */
#define woort_set_option_bool(dst, src) woort_set_union_bool(dst, 0, src)
/** @brief Set option::value(string). */
#define woort_set_option_string(dst, src) woort_set_union_string(dst, 0, src)
/** @brief Set option::value(string) built with format. */
#define woort_set_option_string_fmt(dst, fmt, ...) woort_set_union_string_fmt(dst, 0, fmt,##__VA_ARGS__)
/** @brief Set option::value(buffer). */
#define woort_set_option_buffer(dst, src, len) woort_set_union_buffer(dst, 0, src, len)
/** @brief Set option::value(box_int). */
#define woort_set_option_box_int(dst, src) woort_set_union_box_int(dst, 0, src)
/** @brief Set option::value(box_pointer). */
#define woort_set_option_box_pointer(dst, src) woort_set_union_box_pointer(dst, 0, src)
/** @brief Set option::value(box_real). */
#define woort_set_option_box_real(dst, src) woort_set_union_box_real(dst, 0, src)
/** @brief Set option::value(box_bool). */
#define woort_set_option_box_bool(dst, src) woort_set_union_box_bool(dst, 0, src)
/** @brief Set option::value(gchandle). */
#define woort_set_option_gchandle(dst, addr, hold, close, dylib, ...) \
    woort_set_union_gchandle(dst, 0, addr, hold, close, dylib,##__VA_ARGS__)
/** @brief Set option::value(gcstruct). */
#define woort_set_option_gcstruct(dst, addr, mark, close, dylib, ...) \
    woort_set_union_gcstruct(dst, 0, addr, mark, close, dylib,##__VA_ARGS__)

/** @} */ /* end Option Setters */

/**
 * @name Result Ok Setters (Union with id=0 for Ok)
 * @brief Convenience macros for writing Result<T, E>::Ok values to a stack slot.
 *        Aliases for the corresponding woort_set_option_* macros.
 * @{
 */

 /** @brief Set Result::Ok(stack_value). */
#define woort_set_result_ok_value woort_set_option_value
/** @brief Set Result::Ok(nil). */
#define woort_set_result_ok_nil woort_set_option_nil
/** @brief Set Result::Ok(void). */
#define woort_set_result_ok_void woort_set_option_void
/** @brief Set Result::Ok(int). */
#define woort_set_result_ok_int woort_set_option_int
/** @brief Set Result::Ok(pointer). */
#define woort_set_result_ok_pointer woort_set_option_pointer
/** @brief Set Result::Ok(real). */
#define woort_set_result_ok_real woort_set_option_real
/** @brief Set Result::Ok(float). */
#define woort_set_result_ok_float woort_set_option_float
/** @brief Set Result::Ok(bool). */
#define woort_set_result_ok_bool woort_set_option_bool
/** @brief Set Result::Ok(string). */
#define woort_set_result_ok_string woort_set_option_string
/** @brief Set Result::Ok(string) built with format. */
#define woort_set_result_ok_string_fmt woort_set_option_string_fmt
/** @brief Set Result::Ok(buffer). */
#define woort_set_result_ok_buffer woort_set_option_buffer
/** @brief Set Result::Ok(box_int). */
#define woort_set_result_ok_box_int woort_set_option_box_int
/** @brief Set Result::Ok(box_pointer). */
#define woort_set_result_ok_box_pointer woort_set_option_box_pointer
/** @brief Set Result::Ok(box_real). */
#define woort_set_result_ok_box_real woort_set_option_box_real
/** @brief Set Result::Ok(box_bool). */
#define woort_set_result_ok_box_bool woort_set_option_box_bool
/** @brief Set Result::Ok(gchandle). */
#define woort_set_result_ok_gchandle woort_set_option_gchandle
/** @brief Set Result::Ok(gcstruct). */
#define woort_set_result_ok_gcstruct woort_set_option_gcstruct

/** @} */ /* end Result Ok Setters */

/**
 * @name Result Err Setters (Union with id=1 for Err)
 * @brief Convenience macros for writing Result<T, E>::Err values to a stack slot.
 * @{
 */

 /** @brief Set Result::Err(stack_value). */
#define woort_set_result_err_value(dst, src) woort_set_union_value(dst, 1, src)
/** @brief Set Result::Err(nil). */
#define woort_set_result_err_nil(dst) woort_set_union_nil(dst, 1)
/** @brief Set Result::Err(void). */
#define woort_set_result_err_void(dst) woort_set_union_void(dst, 1)
/** @brief Set Result::Err(int). */
#define woort_set_result_err_int(dst, src) woort_set_union_int(dst, 1, src)
/** @brief Set Result::Err(pointer). */
#define woort_set_result_err_pointer(dst, src) woort_set_union_pointer(dst, 1, src)
/** @brief Set Result::Err(real). */
#define woort_set_result_err_real(dst, src) woort_set_union_real(dst, 1, src)
/** @brief Set Result::Err(float). */
#define woort_set_result_err_float(dst, src) woort_set_union_float(dst, 1, src)
/** @brief Set Result::Err(bool). */
#define woort_set_result_err_bool(dst, src) woort_set_union_bool(dst, 1, src)
/** @brief Set Result::Err(string). */
#define woort_set_result_err_string(dst, src) woort_set_union_string(dst, 1, src)
/** @brief Set Result::Err(string) built with format. */
#define woort_set_result_err_string_fmt(dst, fmt, ...) woort_set_union_string_fmt(dst, 1, fmt,##__VA_ARGS__)
/** @brief Set Result::Err(buffer). */
#define woort_set_result_err_buffer(dst, src, len) woort_set_union_buffer(dst, 1, src, len)
/** @brief Set Result::Err(box_int). */
#define woort_set_result_err_box_int(dst, src) woort_set_union_box_int(dst, 1, src)
/** @brief Set Result::Err(box_pointer). */
#define woort_set_result_err_box_pointer(dst, src) woort_set_union_box_pointer(dst, 1, src)
/** @brief Set Result::Err(box_real). */
#define woort_set_result_err_box_real(dst, src) woort_set_union_box_real(dst, 1, src)
/** @brief Set Result::Err(box_bool). */
#define woort_set_result_err_box_bool(dst, src) woort_set_union_box_bool(dst, 1, src)
/** @brief Set Result::Err(gchandle). */
#define woort_set_result_err_gchandle(dst, addr, hold, close, dylib, ...) \
    woort_set_union_gchandle(dst, 1, addr, hold, close, dylib,##__VA_ARGS__)
/** @brief Set Result::Err(gcstruct). */
#define woort_set_result_err_gcstruct(dst, addr, mark, close, dylib, ...) \
    woort_set_union_gcstruct(dst, 1, addr, mark, close, dylib,##__VA_ARGS__)

/** @} */ /* end Result Err Setters */

/* ========== Return ========== */

/**
 * @name Return Macros (Plain)
 * @brief Set the return slot (WOORT_RETURN_SLOT) and return WOORT_VM_CALL_STATUS_NORMAL.
 *        These macros are intended for use inside native function implementations
 *        to return a typed value to the caller.
 * @{
 */

 /** Return result has been stored into WOORT_RETURN_SLOT, do nothing else. */
#define woort_ret() WOORT_VM_CALL_STATUS_NORMAL
/** @brief Return a stack value: set slot WOORT_RETURN_SLOT and return NORMAL. */
#define woort_ret_value(src) (woort_set_value(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return void (no value). */
#define woort_ret_void() woort_ret()
/** @brief Return nil. */
#define woort_ret_nil() (woort_set_nil(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return an integer. */
#define woort_ret_int(src) (woort_set_int(WOORT_RETURN_SLOT, src), woort_ret())
#define woort_ret_pointer(src) (woort_set_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return a real. */
#define woort_ret_real(src) (woort_set_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a float. */
#define woort_ret_float(src) (woort_set_float(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a boolean. */
#define woort_ret_bool(src) (woort_set_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a string. */
#define woort_ret_string(src) (woort_set_string(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a string build with format. */
#define woort_ret_string_fmt(src, ...) (woort_set_string_fmt(WOORT_RETURN_SLOT, src,##__VA_ARGS__), woort_ret())
/** @brief Return a buffer. */
#define woort_ret_buffer(src, len) (woort_set_buffer(WOORT_RETURN_SLOT, src, len), woort_ret())
/** @brief Return a boxed integer. */
#define woort_ret_box_int(src) (woort_set_box_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a boxed pointer. */
#define woort_ret_box_pointer(src) (woort_set_box_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return a boxed real. */
#define woort_ret_box_real(src) (woort_set_box_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a boxed boolean. */
#define woort_ret_box_bool(src) (woort_set_box_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return a GC handle. */
#define woort_ret_gchandle(addr, hold, close, dylib, ...) \
    (woort_set_gchandle(WOORT_RETURN_SLOT, addr, hold, close, dylib,##__VA_ARGS__), woort_ret())
/** @brief Return a GC struct. */
#define woort_ret_gcstruct(addr, mark, close, dylib, ...) \
    (woort_set_gcstruct(WOORT_RETURN_SLOT, addr, mark, close, dylib,##__VA_ARGS__), woort_ret())

/** @} */ /* end Return Macros (Plain) */

/**
 * @name Return Union Macros
 * @brief Set return slot (WOORT_RETURN_SLOT) to a tagged union variant and return woort_ret().
 * @{
 */

 /** @brief Return a union with no inline payload. */
#define woort_ret_union_without_value(id) (woort_set_union_without_value(WOORT_RETURN_SLOT, id), woort_ret())
/** @brief Return a union carrying a stack value. */
#define woort_ret_union_value(id, src) (woort_set_union_value(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with nil payload. */
#define woort_ret_union_nil(id) (woort_set_union_nil(WOORT_RETURN_SLOT, id), woort_ret())
/** @brief Alias for woort_ret_union_nil. */
#define woort_ret_union_void woort_ret_union_nil
/** @brief Return a union with int payload. */
#define woort_ret_union_int(id, src) (woort_set_union_int(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with pointer payload. */
#define woort_ret_union_pointer(id, src) (woort_set_union_int(WOORT_RETURN_SLOT, id, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return a union with real payload. */
#define woort_ret_union_real(id, src) (woort_set_union_real(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with float payload. */
#define woort_ret_union_float(id, src) (woort_set_union_float(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with bool payload. */
#define woort_ret_union_bool(id, src) (woort_set_union_bool(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with string payload. */
#define woort_ret_union_string(id, src) (woort_set_union_string(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with string payload built with format. */
#define woort_ret_union_string_fmt(id, fmt, ...) (woort_set_union_string_fmt(WOORT_RETURN_SLOT, id, fmt,##__VA_ARGS__), woort_ret())
/** @brief Return a union with buffer payload. */
#define woort_ret_union_buffer(id, src, len) (woort_set_union_buffer(WOORT_RETURN_SLOT, id, src, len), woort_ret())
/** @brief Return a union with boxed int payload. */
#define woort_ret_union_box_int(id, src) (woort_set_union_box_int(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with boxed pointer payload. */
#define woort_ret_union_box_pointer(id, src) (woort_set_union_box_int(WOORT_RETURN_SLOT, id, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return a union with boxed real payload. */
#define woort_ret_union_box_real(id, src) (woort_set_union_box_real(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with boxed bool payload. */
#define woort_ret_union_box_bool(id, src) (woort_set_union_box_bool(WOORT_RETURN_SLOT, id, src), woort_ret())
/** @brief Return a union with GC handle payload. */
#define woort_ret_union_gchandle(id, addr, hold, close, dylib, ...) \
    (woort_set_union_gchandle(WOORT_RETURN_SLOT, id, addr, hold, close, dylib,##__VA_ARGS__), woort_ret())
/** @brief Return a union with GC struct payload. */
#define woort_ret_union_gcstruct(id, addr, mark, close, dylib, ...) \
    (woort_set_union_gcstruct(WOORT_RETURN_SLOT, id, addr, mark, close, dylib,##__VA_ARGS__), woort_ret())

/** @} */ /* end Return Union Macros */

/**
 * @name Return Option Macros
 * @brief Set return slot (WOORT_RETURN_SLOT) to Option<T> (value=0, none=1) and return woort_ret().
 * @{
 */

 /** @brief Return option::none. */
#define woort_ret_option_none() (woort_set_option_none(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return option::value(stack_value). */
#define woort_ret_option_value(src) (woort_set_option_value(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(nil). */
#define woort_ret_option_nil() (woort_set_option_nil(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return option::value(void). */
#define woort_ret_option_void() (woort_set_option_void(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return option::value(int). */
#define woort_ret_option_int(src) (woort_set_option_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(pointer). */
#define woort_ret_option_pointer(src) (woort_set_option_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return option::value(real). */
#define woort_ret_option_real(src) (woort_set_option_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(float). */
#define woort_ret_option_float(src) (woort_set_option_float(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(bool). */
#define woort_ret_option_bool(src) (woort_set_option_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(string). */
#define woort_ret_option_string(src) (woort_set_option_string(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(string) built with format. */
#define woort_ret_option_string_fmt(fmt, ...) (woort_set_option_string_fmt(WOORT_RETURN_SLOT, fmt,##__VA_ARGS__), woort_ret())
/** @brief Return option::value(buffer). */
#define woort_ret_option_buffer(src, len) (woort_set_option_buffer(WOORT_RETURN_SLOT, src, len), woort_ret())
/** @brief Return option::value(box_int). */
#define woort_ret_option_box_int(src) (woort_set_option_box_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(box_pointer). */
#define woort_ret_option_box_pointer(src) (woort_set_option_box_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return option::value(box_real). */
#define woort_ret_option_box_real(src) (woort_set_option_box_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(box_bool). */
#define woort_ret_option_box_bool(src) (woort_set_option_box_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return option::value(gchandle). */
#define woort_ret_option_gchandle(addr, hold, close, dylib, ...) \
    (woort_set_option_gchandle(WOORT_RETURN_SLOT, addr, hold, close, dylib,##__VA_ARGS__), woort_ret())
/** @brief Return option::value(gcstruct). */
#define woort_ret_option_gcstruct(addr, mark, close, dylib, ...) \
    (woort_set_option_gcstruct(WOORT_RETURN_SLOT, addr, mark, close, dylib,##__VA_ARGS__), woort_ret())

/** @} */ /* end Return Option Macros */

/**
 * @name Return Result::Ok Macros
 * @brief Set return slot (WOORT_RETURN_SLOT) to Result<T,E>::Ok and return woort_ret().
 *        Aliases for the corresponding woort_ret_option_* macros.
 * @{
 */

 /** @brief Return Result::Ok(stack_value). */
#define woort_ret_result_ok_value(src) (woort_set_result_ok_value(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(nil). */
#define woort_ret_result_ok_nil() (woort_set_result_ok_nil(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return Result::Ok(void). */
#define woort_ret_result_ok_void() (woort_set_result_ok_void(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return Result::Ok(int). */
#define woort_ret_result_ok_int(src) (woort_set_result_ok_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(pointer). */
#define woort_ret_result_ok_pointer(src) (woort_set_result_ok_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return Result::Ok(real). */
#define woort_ret_result_ok_real(src) (woort_set_result_ok_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(float). */
#define woort_ret_result_ok_float(src) (woort_set_result_ok_float(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(bool). */
#define woort_ret_result_ok_bool(src) (woort_set_result_ok_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(string). */
#define woort_ret_result_ok_string(src) (woort_set_result_ok_string(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(string) built with format. */
#define woort_ret_result_ok_string_fmt(fmt, ...) (woort_set_result_ok_string_fmt(WOORT_RETURN_SLOT, fmt,##__VA_ARGS__), woort_ret())
/** @brief Return Result::Ok(buffer). */
#define woort_ret_result_ok_buffer(src, len) (woort_set_result_ok_buffer(WOORT_RETURN_SLOT, src, len), woort_ret())
/** @brief Return Result::Ok(box_int). */
#define woort_ret_result_ok_box_int(src) (woort_set_result_ok_box_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(box_pointer). */
#define woort_ret_result_ok_box_pointer(src) (woort_set_result_ok_box_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return Result::Ok(box_real). */
#define woort_ret_result_ok_box_real(src) (woort_set_result_ok_box_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(box_bool). */
#define woort_ret_result_ok_box_bool(src) (woort_set_result_ok_box_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Ok(gchandle). */
#define woort_ret_result_ok_gchandle(addr, hold, close, dylib, ...) \
    (woort_set_result_ok_gchandle(WOORT_RETURN_SLOT, addr, hold, close, dylib,##__VA_ARGS__), woort_ret())
/** @brief Return Result::Ok(gcstruct). */
#define woort_ret_result_ok_gcstruct(addr, mark, close, dylib, ...) \
    (woort_set_result_ok_gcstruct(WOORT_RETURN_SLOT, addr, mark, close, dylib,##__VA_ARGS__), woort_ret())

/** @} */ /* end Return Result::Ok Macros */

/**
 * @name Return Result::Err Macros
 * @brief Set return slot (WOORT_RETURN_SLOT) to Result<T,E>::Err and return woort_ret().
 * @{
 */

 /** @brief Return Result::Err(stack_value). */
#define woort_ret_result_err_value(src) (woort_set_result_err_value(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(nil). */
#define woort_ret_result_err_nil() (woort_set_result_err_nil(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return Result::Err(void). */
#define woort_ret_result_err_void() (woort_set_result_err_void(WOORT_RETURN_SLOT), woort_ret())
/** @brief Return Result::Err(int). */
#define woort_ret_result_err_int(src) (woort_set_result_err_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(pointer). */
#define woort_ret_result_err_pointer(src) (woort_set_result_err_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return Result::Err(real). */
#define woort_ret_result_err_real(src) (woort_set_result_err_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(float). */
#define woort_ret_result_err_float(src) (woort_set_result_err_float(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(bool). */
#define woort_ret_result_err_bool(src) (woort_set_result_err_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(string). */
#define woort_ret_result_err_string(src) (woort_set_result_err_string(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(string) built with format. */
#define woort_ret_result_err_string_fmt(fmt, ...) (woort_set_result_err_string_fmt(WOORT_RETURN_SLOT, fmt,##__VA_ARGS__), woort_ret())
/** @brief Return Result::Err(buffer). */
#define woort_ret_result_err_buffer(src, len) (woort_set_result_err_buffer(WOORT_RETURN_SLOT, src, len), woort_ret())
/** @brief Return Result::Err(box_int). */
#define woort_ret_result_err_box_int(src) (woort_set_result_err_box_int(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(box_pointer). */
#define woort_ret_result_err_box_pointer(src) (woort_set_result_err_box_int(WOORT_RETURN_SLOT, (woort_Int)(intptr_t)(src)), woort_ret())
/** @brief Return Result::Err(box_real). */
#define woort_ret_result_err_box_real(src) (woort_set_result_err_box_real(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(box_bool). */
#define woort_ret_result_err_box_bool(src) (woort_set_result_err_box_bool(WOORT_RETURN_SLOT, src), woort_ret())
/** @brief Return Result::Err(gchandle). */
#define woort_ret_result_err_gchandle(addr, hold, close, dylib, ...) \
    (woort_set_result_err_gchandle(WOORT_RETURN_SLOT, addr, hold, close, dylib,##__VA_ARGS__), woort_ret())
/** @brief Return Result::Err(gcstruct). */
#define woort_ret_result_err_gcstruct(addr, mark, close, dylib, ...) \
    (woort_set_result_err_gcstruct(WOORT_RETURN_SLOT, addr, mark, close, dylib,##__VA_ARGS__), woort_ret())

/** @} */ /* end Return Result::Err Macros */

/**
 * @brief Trigger a panic indicating the current Native-call has encountered a completely unexpected situation
 *        severe enough that continuing execution may lead to a crash. Panic blocks the current thread and awaits
 *        further instructions from the user, which may include:
 *          1) abort the entire program,
 *          2) treat as Abort, or
 *          3) attach a debugger and break immediately at the current location.
 *
 * @param fmt  Printf-style format string for the panic message.
 * @param ...  Arguments for the format string.
 *
 * @note Regardless of the user's choice, WooRT makes no guarantees about any program behavior after a panic occurs.
 *       The program may crash even after the user makes a selection.
 */
WOORT_NODISCARD WOORT_API woort_api woort_ret_panic(const char* fmt, ...);

/**
 * @brief Request to pause VM execution, preserving the state after the current Native-function call completes.
 *        Expects to be resumed later via woort_resume, continuing from the preserved state.
 */
WOORT_NODISCARD WOORT_API woort_api woort_ret_yield(void);

/**
 * @name Stack Value Readers
 * @brief Read a typed value from a VM stack slot.
 *
 * @param src  Source stack slot index.
 * @return     The value read from the slot (type varies per function).
 * @{
 */

 /** @brief Read a raw integer from a stack slot. */
WOORT_NODISCARD WOORT_API woort_Int woort_int(woort_StackValue src);
/** @brief Read a raw pointer from a stack slot (integer cast to void*). */
#define woort_pointer(src) ((void*)woort_int(src))
/** @brief Read a raw real (double) from a stack slot. */
WOORT_NODISCARD WOORT_API woort_Real woort_real(woort_StackValue src);
/** @brief Read a single-precision float from a stack slot. */
WOORT_NODISCARD WOORT_API float woort_float(woort_StackValue src);
/** @brief Read a boolean from a stack slot. */
WOORT_NODISCARD WOORT_API bool woort_bool(woort_StackValue src);
/** @brief Read a string pointer from a stack slot. */
WOORT_NODISCARD WOORT_API woort_U8CString woort_string(woort_StackValue src);
/** @brief Read a buffer pointer and its length from a stack slot. */
WOORT_NODISCARD WOORT_API const void* woort_buffer(
    woort_StackValue src, size_t* out_len);
/** @brief Read a raw GC pointer from a stack slot. */
WOORT_NODISCARD WOORT_API void* woort_gcpointer(woort_StackValue src);
/** @brief Unbox and read an integer from a boxed stack slot. */
WOORT_NODISCARD WOORT_API woort_Int woort_unbox_int(woort_StackValue src);
/** @brief Unbox and read a real from a boxed stack slot. */
WOORT_NODISCARD WOORT_API woort_Real woort_unbox_real(woort_StackValue src);
/** @brief Unbox and read a float from a boxed stack slot. */
#define woort_unbox_float(SRC) ((float)woort_unbox_real(SRC))
/** @brief Unbox and read a pointer from a boxed stack slot. */
#define woort_unbox_pointer(SRC) ((void*)woort_unbox_int(SRC))
/** @brief Unbox and read a boolean from a boxed stack slot. */
WOORT_NODISCARD WOORT_API bool woort_unbox_bool(woort_StackValue src);
/** @brief Query the type tag of a boxed dynamic value. */
WOORT_NODISCARD WOORT_API woort_BoxValueType woort_unbox_type(
    woort_StackValue src);
/**
 * @brief Unbox a dynamic value, writing the inner value to dst.
 * @param dst  Destination stack slot for the unboxed value.
 * @param src  Source stack slot holding the boxed value.
 * @return The type tag of the unboxed value.
 */
WOORT_NODISCARD WOORT_API woort_BoxValueType woort_unbox(
    woort_StackValue dst,
    woort_StackValue src);

/** @} */ /* end Stack Value Readers */

/**
 * @brief Get the union variant from a stack slot and copy the payload.
 * @param dst  Destination stack slot for the payload value.
 * @param src  Source stack slot holding the union.
 * @return The union discriminant (variant id).
 */
WOORT_NODISCARD WOORT_API woort_Int woort_union_get(
    woort_StackValue dst, woort_StackValue src);

/**
 * @name Option / Result Access Macros
 * @brief Check if an Option or Result is the "success" variant (id=0).
 * @{
 */

 /** @brief Check if Option is value. Returns true if the union id == 0 (value), copies payload to dst. */
#define woort_option_get(dst, src) \
    (0 == woort_union_get(dst, src))
/** @brief Check if Result is Ok. Returns true if the union id == 0 (Ok), copies payload to dst. */
#define woort_result_get(dst, src) \
    (0 == woort_union_get(dst, src))

/** @} */

/* ========== Vector ========== */

/** @name Vector Capacity */
/**@{*/

/**
 * @brief Get the number of elements in a vector.
 * @param src  Stack slot holding the vector.
 * @return Number of elements.
 */
WOORT_NODISCARD WOORT_API size_t woort_vec_len(
    woort_StackValue src);

/**
 * @brief Resize a vector to the given number of elements.
 * @param src       Stack slot holding the vector.
 * @param new_size  Desired number of elements.
 */
WOORT_API void woort_vec_resize(
    woort_StackValue src, size_t new_size);

/**
 * @brief Resize a vector to the given number of elements, filling new slots
 *        with an explicit initial value.
 * @param src       Stack slot holding the vector.
 * @param new_size  Desired number of elements.
 * @param init_val  Stack slot holding the value to use for new elements.
 */
WOORT_API void woort_vec_resize_with(
    woort_StackValue src, size_t new_size, woort_StackValue init_val);

/**
 * @brief Shrink a vector to the given number of elements.
 * @param src       Stack slot holding the vector.
 * @param new_size  Desired number of elements (must be <= current size).
 * @return true on success, false if new_size exceeds current size.
 */
WOORT_NODISCARD WOORT_API bool woort_vec_shrink(
    woort_StackValue src, size_t new_size);


/**@}*/

/** @name Vector Element Access */
/**@{*/

/**
 * @brief Read an element from a vector (boxed).
 * @param dst_boxed  Destination stack slot for the boxed element.
 * @param src        Stack slot holding the vector.
 * @param index      Zero-based element index.
 * @return true if the index was in range, false if out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_vec_get(
    woort_StackValue dst_boxed,
    woort_StackValue src,
    size_t index);

/**@}*/

/** @name Vector Modifiers */
/**@{*/

/**
 * @brief Write an element to a vector position.
 * @param src        Stack slot holding the vector.
 * @param index      Zero-based element index.
 * @param boxed_elem Stack slot holding the boxed element to write.
 * @return true if the index was in range, false if out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_vec_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue boxed_elem);

/**
 * @brief Append an element to the end of a vector.
 * @param src        Stack slot holding the vector.
 * @param boxed_elem Stack slot holding the boxed element to append.
 */
WOORT_API void woort_vec_push(
    woort_StackValue src,
    woort_StackValue boxed_elem);

/**
 * @brief Remove the last element from a vector.
 * @param src  Stack slot holding the vector.
 * @return true on success, false if the vector is empty.
 */
WOORT_NODISCARD WOORT_API bool woort_vec_pop(woort_StackValue src);

/**
 * @brief Insert an element at the given index, shifting subsequent elements.
 * @param src        Stack slot holding the vector.
 * @param index      Zero-based insertion position.
 * @param boxed_elem Stack slot holding the boxed element to insert.
 * @return true on success, false if index is out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_vec_insert(
    woort_StackValue src,
    size_t index,
    woort_StackValue boxed_elem);

/**
 * @brief Remove the element at the given index, shifting subsequent elements.
 * @param src    Stack slot holding the vector.
 * @param index  Zero-based position of the element to remove.
 * @return true on success, false if index is out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_vec_erase(
    woort_StackValue src,
    size_t index);

/**
 * @brief Remove all elements from a vector.
 * @param src  Stack slot holding the vector.
 */
WOORT_API void woort_vec_clear(woort_StackValue src);

/**
 * @brief Copy all elements from src vector into dst vector.
 *
 * dst is cleared first, then all elements from src are copied.
 * @param dst  Stack slot holding the destination vector.
 * @param src  Stack slot holding the source vector.
 */
WOORT_API void woort_vec_copy(
    woort_StackValue dst,
    woort_StackValue src);

/**
 * @brief Swap the contents of two vectors.
 * @param a  Stack slot holding the first vector.
 * @param b  Stack slot holding the second vector.
 */
WOORT_API void woort_vec_swap(
    woort_StackValue a,
    woort_StackValue b);

/**@}*/

/* ========== Mapping ========== */

/** @name Mapping Capacity */
/**@{*/

/**
 * @brief Get the number of key-value pairs in a map.
 * @param src  Stack slot holding the map.
 * @return Number of entries.
 */
WOORT_NODISCARD WOORT_API size_t woort_map_len(woort_StackValue src);

/**
 * @brief Reserve capacity for at least the given number of entries.
 * @param src      Stack slot holding the map.
 * @param reserve  Minimum number of entries to reserve space for.
 */
WOORT_API void woort_map_reserve(
    woort_StackValue src,
    size_t reserve);

/**@}*/

/** @name Mapping Lookup */
/**@{*/

/**
 * @brief Lookup key in map. If found, write boxed value to dst and return true.
 * If not found, return false (dst is unmodified).
 * @param dst         Destination stack slot for the boxed value.
 * @param src         Stack slot holding the map.
 * @param key_boxed   Stack slot holding the boxed key.
 * @return true if the key was found.
 */
WOORT_NODISCARD WOORT_API bool woort_map_get(
    woort_StackValue dst,
    woort_StackValue src,
    woort_StackValue key_boxed);

/**
 * @brief Lookup by int key. If found, write boxed value to dst and return true.
 * @param dst   Destination stack slot.
 * @param src   Stack slot holding the map.
 * @param key   Integer key to look up.
 * @return true if the key was found.
 */
WOORT_NODISCARD WOORT_API bool woort_map_get_by_int(
    woort_StackValue dst,
    woort_StackValue src,
    woort_Int key);

/**
 * @brief Lookup by real key. If found, write boxed value to dst and return true.
 * @param dst   Destination stack slot.
 * @param src   Stack slot holding the map.
 * @param key   Real key to look up.
 * @return true if the key was found.
 */
WOORT_NODISCARD WOORT_API bool woort_map_get_by_real(
    woort_StackValue dst,
    woort_StackValue src,
    woort_Real key);

/**
 * @brief Lookup by bool key. If found, write boxed value to dst and return true.
 * @param dst   Destination stack slot.
 * @param src   Stack slot holding the map.
 * @param key   Boolean key to look up.
 * @return true if the key was found.
 */
WOORT_NODISCARD WOORT_API bool woort_map_get_by_bool(
    woort_StackValue dst,
    woort_StackValue src,
    bool key);

/**
 * @brief Lookup by string key. If found, write boxed value to dst and return true.
 * @param dst   Destination stack slot.
 * @param src   Stack slot holding the map.
 * @param key   String key to look up.
 * @return true if the key was found.
 */
WOORT_NODISCARD WOORT_API bool woort_map_get_by_string(
    woort_StackValue dst,
    woort_StackValue src,
    woort_U8CString key);

/**@}*/

/** @name Mapping Insert / Update */
/**@{*/

/**
 * @brief Insert or update: map[key] = val.
 * @param src        Stack slot holding the map.
 * @param key_boxed  Stack slot holding the boxed key.
 * @param val_boxed  Stack slot holding the boxed value.
 * @return true if the key was newly inserted, false if an existing key was updated.
 */
WOORT_NODISCARD WOORT_API bool woort_map_set(
    woort_StackValue src,
    woort_StackValue key_boxed,
    woort_StackValue val_boxed);

/** @brief Insert/update with int key. Returns true if newly inserted. */
WOORT_NODISCARD WOORT_API bool woort_map_set_by_int(
    woort_StackValue src,
    woort_Int key,
    woort_StackValue val_boxed);

/** @brief Insert/update with pointer key. Returns true if newly inserted. */
#define woort_map_set_by_pointer(src, ptr, val_boxed) \
    woort_map_set_by_int((src), (woort_Int)(intptr_t)(ptr), (val_boxed))

/** @brief Insert/update with real key. Returns true if newly inserted. */
WOORT_NODISCARD WOORT_API bool woort_map_set_by_real(
    woort_StackValue src,
    woort_Real key,
    woort_StackValue val_boxed);

/** @brief Insert/update with bool key. Returns true if newly inserted. */
WOORT_NODISCARD WOORT_API bool woort_map_set_by_bool(
    woort_StackValue src,
    bool key,
    woort_StackValue val_boxed);

/** @brief Insert/update with string key. Returns true if newly inserted. */
WOORT_NODISCARD WOORT_API bool woort_map_set_by_string(
    woort_StackValue src,
    woort_U8CString key,
    woort_StackValue val_boxed);

/**@}*/

/** @name Mapping Erase */
/**@{*/

/**
 * @brief Erase a key-value pair from the map.
 * @param src        Stack slot holding the map.
 * @param key_boxed  Stack slot holding the boxed key.
 * @return true if the key existed and was removed.
 */
WOORT_NODISCARD WOORT_API bool woort_map_erase(
    woort_StackValue src,
    woort_StackValue key_boxed);

/** @brief Erase by int key. Returns true if found and removed. */
WOORT_NODISCARD WOORT_API bool woort_map_erase_by_int(
    woort_StackValue src,
    woort_Int key);

/** @brief Erase by real key. Returns true if found and removed. */
WOORT_NODISCARD WOORT_API bool woort_map_erase_by_real(
    woort_StackValue src,
    woort_Real key);

/** @brief Erase by bool key. Returns true if found and removed. */
WOORT_NODISCARD WOORT_API bool woort_map_erase_by_bool(
    woort_StackValue src,
    bool key);

/** @brief Erase by string key. Returns true if found and removed. */
WOORT_NODISCARD WOORT_API bool woort_map_erase_by_string(
    woort_StackValue src,
    woort_U8CString key);

/** @brief Clear all key-value pairs from the map. */
WOORT_API void woort_map_clear(woort_StackValue src);

/**
 * @brief Copy all key-value pairs from src map into dst map.
 *
 * dst is cleared first, then all entries from src are copied.
 * @param dst  Stack slot holding the destination map.
 * @param src  Stack slot holding the source map.
 */
WOORT_API void woort_map_copy(
    woort_StackValue dst,
    woort_StackValue src);

/**
 * @brief Swap the contents of two maps.
 * @param a  Stack slot holding the first map.
 * @param b  Stack slot holding the second map.
 */
WOORT_API void woort_map_swap(
    woort_StackValue a,
    woort_StackValue b);

/**@}*/

/** @name Mapping Contains */
/**@{*/

/** @brief Check if a boxed key exists in the map. */
WOORT_NODISCARD WOORT_API bool woort_map_contains(
    woort_StackValue src,
    woort_StackValue key_boxed);

/** @brief Check if an int key exists in the map. */
WOORT_NODISCARD WOORT_API bool woort_map_contains_int(
    woort_StackValue src,
    woort_Int key);

/** @brief Check if a real key exists in the map. */
WOORT_NODISCARD WOORT_API bool woort_map_contains_real(
    woort_StackValue src,
    woort_Real key);

/** @brief Check if a bool key exists in the map. */
WOORT_NODISCARD WOORT_API bool woort_map_contains_bool(
    woort_StackValue src,
    bool key);

/** @brief Check if a string key exists in the map. */
WOORT_NODISCARD WOORT_API bool woort_map_contains_string(
    woort_StackValue src,
    woort_U8CString key);

/**@}*/

/** @name Mapping Iteration */
/**@{*/

/**
 * @brief Retrieve the key-value pair at the given iterator index.
 *
 * Returns false if the index is out of range or the slot is empty (tombstone).
 * On success, writes key to out_key_boxed and value to out_val_boxed.
 *
 * @param src            Stack slot holding the map.
 * @param index          Zero-based iterator index.
 * @param out_key_boxed  Destination for the boxed key, or WOORT_IGNORE to discard.
 * @param out_val_boxed  Destination for the boxed value, or WOORT_IGNORE to discard.
 * @return true if a valid entry was found at the given index.
 */
WOORT_NODISCARD WOORT_API bool woort_map_iter(
    woort_StackValue src,
    size_t index,
    woort_StackValue out_key_boxed,
    woort_StackValue out_val_boxed);

/**@}*/

/* ========== Serialize ========== */

/**
 * @brief Flags for controlling serialization behavior.
 *
 * Combine flags with bitwise OR. Default (0) produces compact output and
 * represents recursive structures as placeholder literals.
 */
typedef enum woort_SerializeFlag
{
    /** No flags: compact output, recursive structures output {...} / [...]. */
    WOORT_SERIALIZE_FLAG_NONE = 0,

    /** Pretty-print: produce indented output with newlines. */
    WOORT_SERIALIZE_FLAG_PRETTY = 1 << 0,

    /** Fail with error instead of outputting placeholder literals for non-deserializeable types. */
    WOORT_SERIALIZE_FLAG_STRICT = 1 << 1,

    /** Emit "null" instead of "nil" for NIL values. */
    WOORT_SERIALIZE_FLAG_USE_NULL = 1 << 2,

} woort_SerializeFlag;

/** @name Serialize */
/**@{*/

/**
 * @brief Serialize a boxed dynamic value to its Woolang literal string.
 *
 * The returned string is heap-allocated and must be freed by the caller with woort_free().
 *
 * @param src    Source stack slot holding the boxed value.
 * @param flags  Bitmask of woort_SerializeFlag values.
 * @return NUL-terminated string on success, NULL on failure (unsupported type, cycle, OOM).
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_serialize_dynbox(
    woort_StackValue src, uint32_t flags);

/**
 * @brief Serialize a map to its Woolang literal string.
 *
 * The returned string is heap-allocated and must be freed by the caller with woort_free().
 *
 * @param src    Source stack slot holding the map.
 * @param flags  Bitmask of woort_SerializeFlag values.
 * @return NUL-terminated string on success, NULL on failure (cycle, OOM).
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_serialize_map(
    woort_StackValue src, uint32_t flags);

/**
 * @brief Serialize a vec to its Woolang literal string.
 *
 * The returned string is heap-allocated and must be freed by the caller with woort_free().
 *
 * @param src    Source stack slot holding the vec.
 * @param flags  Bitmask of woort_SerializeFlag values.
 * @return NUL-terminated string on success, NULL on failure (cycle, OOM).
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_serialize_vec(
    woort_StackValue src, uint32_t flags);

/**@}*/

/** @name Deserialize */
/**@{*/

/**
 * @brief Parse a Woolang literal string into a boxed dynamic value.
 *
 * Supports nil/null, true/false, integers, reals, strings, arrays, and maps.
 *
 * @param dst  Destination stack slot for the boxed value.
 * @param str  NUL-terminated Woolang literal string.
 * @return true on success, false on parse error or OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_deserialize_dynbox(
    woort_StackValue dst, const char* str);

/**
 * @brief Parse a Woolang literal string into a map.
 *
 * The input must be a valid map literal `{...}`.
 *
 * @param dst  Destination stack slot for the map.
 * @param str  NUL-terminated Woolang literal string.
 * @return true on success, false on parse error, wrong type, or OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_deserialize_map(
    woort_StackValue dst, const char* str);

/**
 * @brief Parse a Woolang literal string into a vec.
 *
 * The input must be a valid array literal `[...]`.
 *
 * @param dst  Destination stack slot for the vec.
 * @param str  NUL-terminated Woolang literal string.
 * @return true on success, false on parse error, wrong type, or OOM.
 */
WOORT_NODISCARD WOORT_API bool woort_deserialize_vec(
    woort_StackValue dst, const char* str);

/**@}*/

/* ========== Struct ========== */

/** @name Struct Capacity */
/**@{*/

/**
 * @brief Get the number of fields in a struct.
 * @param src  Stack slot holding the struct.
 * @return Number of fields.
 */
WOORT_NODISCARD WOORT_API size_t woort_struct_len(
    woort_StackValue src);

/**@}*/

/** @name Struct Field Access */
/**@{*/

/**
 * @brief Read a field from a struct.
 * @param dst    Destination stack slot.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 */
WOORT_API void woort_struct_get(
    woort_StackValue dst,
    woort_StackValue src,
    size_t index);

/**
 * @brief Write a field in a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @param val    Stack slot holding the value to write.
 */
WOORT_API void woort_struct_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue val);

/**
 * @brief Read an int field from a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @return The int value.
 */
WOORT_NODISCARD WOORT_API woort_Int woort_struct_get_int(
    woort_StackValue src,
    size_t index);

/**
 * @brief Read a real field from a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @return The real value.
 */
WOORT_NODISCARD WOORT_API woort_Real woort_struct_get_real(
    woort_StackValue src,
    size_t index);

/**
 * @brief Read a string field from a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @return The string value.
 */
WOORT_NODISCARD WOORT_API woort_U8CString woort_struct_get_string(
    woort_StackValue src,
    size_t index);

/**
 * @brief Read a bool field from a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @return The bool value.
 */
WOORT_NODISCARD WOORT_API bool woort_struct_get_bool(
    woort_StackValue src,
    size_t index);

/**
 * @brief Write an int field in a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @param val    The int value to write.
 */
WOORT_API void woort_struct_set_int(
    woort_StackValue src,
    size_t index,
    woort_Int val);

/**
 * @brief Write a real field in a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @param val    The real value to write.
 */
WOORT_API void woort_struct_set_real(
    woort_StackValue src,
    size_t index,
    woort_Real val);

/**
 * @brief Write a string field in a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @param val    The string value to write.
 */
WOORT_API void woort_struct_set_string(
    woort_StackValue src,
    size_t index,
    woort_U8CString val);

/**
 * @brief Write a bool field in a struct.
 * @param src    Stack slot holding the struct.
 * @param index  Zero-based field index.
 * @param val    The bool value to write.
 */
WOORT_API void woort_struct_set_bool(
    woort_StackValue src,
    size_t index,
    bool val);

#define woort_struct_get_float(src, index) \
    ((float)woort_struct_get_real((src), (index)))

#define woort_struct_set_float(src, index, val) \
    woort_struct_set_real((src), (index), (woort_Real)(val))

#define woort_struct_get_pointer(src, index) \
    ((void*)woort_struct_get_int((src), (index)))

#define woort_struct_set_pointer(src, index, val) \
    woort_struct_set_int((src), (index), (woort_Int)(val))

/**@}*/

/* ================================================================
 * Path utilities
 * ================================================================ */

 /**
   * @brief Get the directory containing the executable.
   *
   * The result is cached after the first call. Fills @p buf following
   * snprintf semantics:
   *  - Returns the path length EXCLUDING the NUL terminator.
   *  - Success iff the returned value is less than @p bufsz.
   *  - If @p bufsz is 0, @p buf may be NULL; nothing is written and only the
   *    required length is returned.
   *  - If @p bufsz is too small, the output is truncated (still NUL-terminated)
   *    and the full required length is returned.
   *  - Returns 0 on failure (unsupported platform or OS error).
   *
   * Typical usage:
   * @code
   *   size_t need = woort_exe_path(NULL, 0) + 1;   // +1 for NUL
   *   char* path = (char*)malloc(need);
   *   woort_exe_path(path, need);
   *   ...
   *   free(path);
   * @endcode
   */
WOORT_NODISCARD WOORT_API size_t woort_exe_path(/* OPTIONAL */ char* buf, size_t bufsz);

/**
  * @brief Set the directory containing the executable.
  *
  * Overrides the auto-detected value used by @ref woort_exe_path. @p path must
  * be a directory (not an executable file path) and must not be NULL.
  *
  * @param path  The directory containing the executable. Must not be NULL.
  * @return true on success, false on failure (e.g. out of memory).
  */
WOORT_NODISCARD WOORT_API bool woort_set_exe_path(const char* path);

/**
 * @brief Get the current working directory.
 *
 * Fills @p buf following snprintf semantics:
 *  - Returns the path length EXCLUDING the NUL terminator.
 *  - Success iff the returned value is less than @p bufsz.
 *  - If @p bufsz is 0, @p buf may be NULL; nothing is written and only the
 *    required length is returned.
 *  - If @p bufsz is too small, the output is truncated (still NUL-terminated)
 *    and the full required length is returned.
 *  - Returns 0 on failure (unsupported platform or OS error).
 *
 * Typical usage:
 * @code
 *   size_t need = woort_work_path(NULL, 0) + 1;   // +1 for NUL
 *   char* path = (char*)malloc(need);
 *   woort_work_path(path, need);
 *   ...
 *   free(path);
 * @endcode
 */
WOORT_NODISCARD WOORT_API size_t woort_work_path(/* OPTIONAL */ char* buf, size_t bufsz);

/**
 * @brief Set the current working directory.
 * @param path  The new working directory path.
 * @return true on success, false on failure.
 */
WOORT_NODISCARD WOORT_API bool woort_set_work_path(const char* path);

/**
 * @brief Get the directory part of a file path.
 *
 * Fills @p buf with everything before the last directory separator
 * (after normalization), following snprintf semantics:
 *  - Returns the directory length EXCLUDING the NUL terminator.
 *  - Success iff the returned value is less than @p bufsz.
 *  - If @p bufsz is 0, @p buf may be NULL; nothing is written and only the
 *    required length is returned.
 *  - If @p bufsz is too small, the output is truncated (still NUL-terminated)
 *    and the full required length is returned.
 *  - Returns 0 if @p path is NULL or contains no directory separator.
 *
 * @par Guarantee
 *  When @p path is non-empty, the returned length is always strictly less than
 *  @c strlen(path), so passing @c bufsz = @c strlen(path)+1 is always
 *  sufficient. @p buf may alias @p path for in-place stripping:
 *  @code
 *  woort_get_file_loc(path, path, strlen(path) + 1);   // in-place
 *  @endcode
 *
 * @param path  The file path to extract the directory from. May be NULL.
 * @param buf   Destination buffer. May be NULL iff @p bufsz is 0. May alias
 *              @p path for in-place operation.
 * @param bufsz Capacity of @p buf in bytes.
 * @return Directory length excluding NUL (always < @c strlen(path) for
 *         non-empty @p path); 0 for NULL @p path.
 */
WOORT_NODISCARD WOORT_API size_t woort_get_file_loc(
    /* OPTIONAL */ const char* path,
    /* OPTIONAL */ char* buf,
    size_t bufsz);

/**
 * @brief Normalize path separators in-place.
 *
 * On Windows: replaces '\\' with '/' and uppercases the drive letter.
 * On other platforms: no-op.
 *
 * @param path  The path string to normalize in-place. May be NULL (no-op).
 */
WOORT_API void woort_normalize_path(/* OPTIONAL */ char* path);

/* ================================================================
 * Virtual File System
 * ================================================================ */

 /** @brief Virtual file scheme prefix ("woovf://"). */
#define WOORT_VFS_SCHEME      "woovf://"
#define WOORT_VFS_SCHEME_LEN  8

/**
 * @brief Create or overwrite a virtual file.
 *
 * If a file with the same path already exists and has enable_modify set,
 * its content is replaced.  Otherwise the call fails.
 *
 * @param filepath      The virtual file path (without scheme prefix).
 * @param data          Raw content buffer.
 * @param length        Size of the buffer in bytes.
 * @param enable_modify Whether the file can be removed or overwritten later.
 * @return true on success.
 */
WOORT_NODISCARD WOORT_API bool woort_vfs_create(
    const char* filepath,
    /* OPTIONAL */ const void* data,
    size_t length,
    bool enable_modify);

/**
 * @brief Remove a virtual file.
 *
 * The file must have been created with enable_modify = true.
 *
 * @param filepath  The virtual file path (without scheme prefix).
 * @return true if the file was found and removed.
 */
WOORT_NODISCARD WOORT_API bool woort_vfs_remove(const char* filepath);

/**
 * @brief Check whether a URI uses the virtual file scheme ("woovf://").
 *
 * @param uri  The URI to check.
 * @return true if the URI starts with the virtual file scheme.
 */
WOORT_NODISCARD WOORT_API bool woort_vfs_is_virtual_uri(
    /* OPTIONAL */ const char* uri);

/**
 * @brief Check whether a virtual file exists.
 *
 * The path may be supplied with or without the "woovf://" prefix.
 *
 * @param filepath  The virtual file path.
 * @return true if the file exists in the VFS registry.
 */
WOORT_NODISCARD WOORT_API bool woort_vfs_exists(const char* filepath);

/**
 * @brief Get all registered virtual file paths.
 *
 * Returns a malloc'd NULL-terminated array of malloc'd strings.
 * The caller must free each string and then the array itself.
 * Alternatively, call with out_paths = NULL to just get the count.
 *
 * @param out_paths  Receives the string array (may be NULL to get count only).
 * @return The number of entries.
 */
WOORT_NODISCARD WOORT_API size_t woort_vfs_get_all_paths(
    /* OPTIONAL */ char*** out_paths);

/**
 * @brief Check whether a real file on disk exists and is readable.
 *
 * @param path  The file system path to check.
 * @return true if the path points to a readable file.
 */
WOORT_NODISCARD WOORT_API bool woort_fs_is_file_readable(
    /* OPTIONAL */ const char* path);

/**
 * @brief Resolve a file path by searching through virtual and real filesystems.
 *
 * Searches in the following order:
 *   1. If filepath is already a virtual URI ("woovf://..."), returns it.
 *   2. For each caller-supplied search directory, try dir + "/" + filepath.
 *   3. Current working directory + "/" + filepath.
 *   4. Executable directory + "/" + filepath.
 *   5. filepath as-is (real filesystem).
 *   6. "woovf://" + filepath (virtual filesystem only).
 *
 * The result is normalize'd (backslash → slash on Windows).
 *
 * @param filepath         The file path to resolve.
 * @param search_dirs      Optional list of directories to search first.
 * @param search_dir_count Number of entries in search_dirs.
 * @param out_resolved_path Receives the malloc'd resolved path (may be NULL).
 * @return true if the file was found (virtual or real).
 */
WOORT_NODISCARD WOORT_API bool woort_vfs_resolve_path(
    const char* filepath,
    /* OPTIONAL */ const char* const* search_dirs,
    size_t search_dir_count,
    /* OPTIONAL */ char** out_resolved_path);

/**
 * @brief Open a file for streaming read (virtual or disk).
 *
 * If the path starts with "woovf://" the VFS is queried; otherwise the
 * file is opened from disk via fopen.  The returned woort_VFile* must
 * be closed with woort_vfile_close().
 *
 * @param filepath  The file path or virtual URI.
 * @param out_file  Receives the opened file handle.
 * @return true on success.
 */
WOORT_NODISCARD WOORT_API bool woort_vfile_open(
    const char* filepath,
    woort_VFile** out_file);

/**
 * @brief Wrap an external memory buffer as a read-only VFile.
 *
 * The returned VFile reads from @p buf without copying, owning, or
 * freeing it.  The caller must keep @p buf alive for the lifetime of
 * the VFile and call woort_vfile_close() when done.
 *
 * @param buf       Pointer to the buffer (may be NULL, yielding a
 *                  zero-length file).
 * @param buflen    Number of bytes in the buffer.
 * @param out_file  Receives the new VFile handle.
 * @return true on success.
 */
WOORT_NODISCARD WOORT_API bool woort_vfile_open_reader(
    /* OPTIONAL */ const void* buf,
    size_t buflen,
    woort_VFile** out_file);

/**
 * @brief Read up to @p size bytes from a streaming file handle.
 *
 * @param file    The file handle.
 * @param buffer  Destination buffer (may be NULL to skip/advance).
 * @param size    Maximum number of bytes to read.
 * @return The actual number of bytes read.
 */
WOORT_NODISCARD WOORT_API size_t woort_vfile_read(
    woort_VFile* file,
    /* OPTIONAL */ void* buffer,
    size_t size);

/**
 * @brief Seek to a position within a streaming file handle.
 *
 * @param file    The file handle.
 * @param offset  Byte offset.
 * @param whence  SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return true on success.
 */
WOORT_NODISCARD WOORT_API bool woort_vfile_seek(
    woort_VFile* file,
    int64_t offset,
    int whence);

/**
 * @brief Get the current read position of a file handle.
 *
 * @param file  The file handle.
 * @return The current byte offset, or -1 on error.
 */
WOORT_NODISCARD WOORT_API int64_t woort_vfile_tell(
    woort_VFile* file);

/**
 * @brief Get the total size of a file handle.
 *
 * @param file  The file handle.
 * @return The file size in bytes, or -1 on error.
 */
WOORT_NODISCARD WOORT_API int64_t woort_vfile_size(
    woort_VFile* file);

/**
 * @brief Close a streaming file handle and release all resources.
 *
 * @param file  The file handle.
 */
WOORT_API void woort_vfile_close(woort_VFile* file);


/* ================================================================
 * Dynamic library loading
 * ================================================================ */

 /**
  * @brief Register a "fake" library backed by a user-supplied function table.
  *
  * The library is registered under the given name in the global library
  * registry.  Subsequent lookups with woort_dylib_load_func will search the
  * function table linearly.
  *
  * @param libname           Unique name for this library.
  * @param funcs             NULL-terminated array of name/function pairs.
  * @param dependence_dylib  Optional library that this fake lib depends on.
  *                          Its reference count is incremented.
  * @return A handle to the fake library, or NULL if the name is already taken
  *         or memory allocation fails.
  */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_Dylib* woort_dylib_fake(
    const char* libname,
    const woort_ExternLibFunc* funcs,
    /* OPTIONAL */ woort_Dylib* dependence_dylib);

/**
 * @brief Load a native dynamic library.
 *
 * The library is searched for in this order:
 *   1. Relative to script_path (if provided)
 *   2. Relative to the current working directory
 *   3. Relative to the executable directory
 *   4. The given path exactly as-is
 *   5. OS default library search path (only if script_path is NULL)
 *
 * A platform-specific extension (.dll/.so/.dylib) is appended in steps 1-3.
 *
 * @param libname          Unique name under which to register the library.
 * @param path             Library file path or base name.
 * @param script_path      Optional script path for relative resolution.
 * @param panic_when_fail  If true, panics on failure instead of returning NULL.
 * @return A handle to the loaded library, or NULL on failure.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_Dylib* woort_dylib_load(
    const char* libname,
    const char* path,
    /* OPTIONAL */ const char* script_path,
    bool panic_when_fail);

/**
 * @brief Look up a function by name in a loaded library.
 *
 * For native libraries, this uses the platform's symbol lookup
 * (GetProcAddress / dlsym).  For fake libraries, the function table
 * is searched linearly.
 *
 * @param lib       Library handle obtained from woort_dylib_load or woort_dylib_fake.
 * @param funcname  Name of the function to look up.
 * @return The function pointer, or NULL if not found.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ void* woort_dylib_load_func(
    woort_Dylib* lib,
    const char* funcname);

/**
 * @brief Reverse-lookup a function name by its address.
 *
 * Searches the internal resolved-function table (populated by prior
 * woort_dylib_load_func calls on the same dylib) for an entry whose
 * address matches @p func_addr.  The returned pointer is valid as long
 * as the dylib is not freed; it points to internally managed memory.
 *
 * @param lib       Library handle obtained from woort_dylib_load or woort_dylib_fake.
 * @param func_addr Function address to look up.
 * @return The function name if found in the resolved table, or NULL.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ const char* woort_dylib_get_func_name(
    woort_Dylib* lib,
    /* OPTIONAL */ void* func_addr);

/**
 * @brief Unload a dynamic library.
 *
 * Reference counting and registry removal are controlled by the method flags:
 *   - WOORT_DYLIB_UNREF: decrements the reference count; the library is
 *     actually freed when the count reaches zero.
 *   - WOORT_DYLIB_BURY: removes the library from the global name registry
 *     without changing the reference count.
 *
 * @param lib     The library handle to unload.
 * @param method  Bitmask of WOORT_DYLIB_UNREF and/or WOORT_DYLIB_BURY.
 */
WOORT_API void woort_dylib_unload(
    woort_Dylib* lib,
    woort_DylibUnloadMethod method);

/**
 * @brief Increase the reference count of a dynamic library.
 *
 * Keeps the library alive even after callers that previously loaded it
 * call woort_dylib_unload with WOORT_DYLIB_UNREF.  Each call to
 * woort_dylib_keep must be matched by a corresponding
 * woort_dylib_unload(..., WOORT_DYLIB_UNREF) to eventually release the library.
 *
 * @param lib  The library handle to retain.
 */
WOORT_API void woort_dylib_keep(woort_Dylib* lib);

/**
 * @brief Get the handle of the built-in "woolang" fake library.
 *
 * This library is automatically registered during woort_init() and contains
 * the core runtime native functions (return_it_self, bad_function, panic,
 * print).  The returned handle is valid until woort_shutdown() is called.
 *
 * @return The library handle, or NULL if woort_init has not been called.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ woort_Dylib* woort_get_builtin_lib(void);

/* ========== WAIPO Debugger ========== */

/**
 * @brief Result of an attempt to attach a debugger.
 */
typedef enum woort_DebuggerAttachResult {
    WOORT_DEBUGGER_ATTACH_RESULT_FAILED,            /* OOM or other failure          */
    WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED,  /* a debugger was already attached */
    WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS,           /* the debugger was attached      */

} woort_DebuggerAttachResult;

/**
 * @brief Attach a WAIPO (Watch And Inspect Program Operation) debugger to the VM.
 *
 * Allocates a debugger instance and registers it with the current VM runtime.
 * The debugger will be notified of VM events such as breakpoints and step
 * operations.
 *
 * If a debugger is already attached, the new instance is released (its
 * destroy callback, if any, is invoked) and WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED
 * is returned; the previously-attached debugger remains active.
 *
 * @return WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS on a new attachment,
 *         WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED if one was already attached,
 *         WOORT_DEBUGGER_ATTACH_RESULT_FAILED on out-of-memory.
 */
WOORT_NODISCARD WOORT_API woort_DebuggerAttachResult woort_WAIPO_Debugger_attach(void);

/**
 * @brief Breakdown all VMs by sending a debug callback request.
 *
 * Iterates over all registered root VMs and sets the
 * WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK flag on each one.
 * When a VM reaches its next checkpoint, it will invoke the currently
 * attached debugger callback (if any).
 */
WOORT_API void woort_VMRuntime_Debugger_breakdown_all_vm(void);

/* ========== Ctrl+C Signal Handling ========== */

/**
 * @brief Register the Ctrl+C (SIGINT) signal handler.
 *
 * On the first SIGINT the WAIPO debugger is attached and every registered
 * root VM receives a debug-callback request.  Consecutive SIGINT within a
 * 2‑second window are counted; after 4 hits the process logs a message and
 * calls abort().
 *
 * Call once during program startup (after woort_init) to enable interactive
 * debugging via Ctrl+C.  Call woort_ctrlc_teardown() during shutdown to
 * restore the default signal disposition.
 */
WOORT_API void woort_ctrlc_setup(void);

/**
 * @brief Restore the default SIGINT disposition (SIG_DFL).
 */
WOORT_API void woort_ctrlc_teardown(void);

/**
 * @brief Action returned by a panic handler to control what happens next.
 */
typedef enum woort_PanicHandler_Action
{
    /** Go ahead with program termination (abort). */
    WOORT_PANIC_HANDLER_ACTION_ABORT,
    /** Continue execution, suppressing the panic. */
    WOORT_PANIC_HANDLER_ACTION_CONTINUE,
    /** Delegate to the default handler (print error, trace, and abort). */
    WOORT_PANIC_HANDLER_ACTION_USE_DEFAULT_HANDLER,
}woort_PanicHandler_Action;

/**
 * @brief Prototype of a user-supplied panic-handler callback.
 * @param vm        The VM instance where the panic occurred, or NULL if no VM running.
 * @param funcname  Name of the function where the panic was raised.
 * @param location  File where the panic was raised.
 * @param line      Line where the panic was raised.
 * @param reason    The panic reason code (see woort_PanicReason enum).
 * @param message   A human-readable description of the panic.
 * @return          A woort_PanicHandler_Action indicating how to proceed.
 */
typedef woort_PanicHandler_Action(*woort_PanicHandlerFunction)(
    /* OPTIONAL */ woort_VMRuntime* vm,
    const char* funcname,
    const char* location,
    int line,
    int reason,
    const char* message);

/**
 * @brief Install a custom panic-handler callback and return the previous one.
 * @param callback  The new panic-handler callback, or NULL to unregister.
 * @return The previously installed callback, or NULL if none was set.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */  woort_PanicHandlerFunction woort_set_panic_callback(
    /* OPTIONAL */ woort_PanicHandlerFunction callback);

/* ========== String / Unicode Conversion API ========== */

/**
 * @brief Get the Unicode code point at a byte index in a UTF-8 string.
 * @param str    The UTF-8 string.
 * @param index  Byte offset into the string.
 * @param out_ch Receives the Unicode code point on success.
 * @return true on success, false if index is out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_str_get_char(
    const char* str, size_t index, char32_t* out_ch);

/**
 * @brief Get the Unicode code point at a byte index in a UTF-8 string with explicit length.
 * @param str    The UTF-8 string.
 * @param size   Length of the string in bytes.
 * @param index  Byte offset into the string.
 * @param out_ch Receives the Unicode code point on success.
 * @return true on success, false if index is out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_strn_get_char(
    const char* str, size_t size, size_t index, char32_t* out_ch);

/**
 * @brief Convert a UTF-8 string to a wide-character string.
 * @param str    The UTF-8 input string.
 * @param outbuf Output buffer for the wide-character result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in wchar_t units (including space for null terminator).
 * @return The number of wchar_t units in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_str_to_wstr(
    const char* str, /* OPTIONAL */ wchar_t* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-8 string (with explicit length) to a wide-character string.
 * @param str    The UTF-8 input string.
 * @param size   Length of the string in bytes.
 * @param outbuf Output buffer for the wide-character result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in wchar_t units (including space for null terminator).
 * @return The number of wchar_t units in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_strn_to_wstr(
    const char* str, size_t size, /* OPTIONAL */ wchar_t* outbuf, size_t buflen);

/**
 * @brief Convert a wide-character string to a UTF-8 string.
 * @param str    The wide-character input string.
 * @param outbuf Output buffer for the UTF-8 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in bytes (including space for null terminator).
 * @return The number of bytes in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_wstr_to_str(
    const wchar_t* str, /* OPTIONAL */ char* outbuf, size_t buflen);

/**
 * @brief Convert a wide-character string (with explicit length) to a UTF-8 string.
 * @param str    The wide-character input string.
 * @param size   Length of the string in wide characters.
 * @param outbuf Output buffer for the UTF-8 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in bytes (including space for null terminator).
 * @return The number of bytes in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_wstrn_to_str(
    const wchar_t* str, size_t size, /* OPTIONAL */ char* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-8 string to a UTF-16 string.
 * @param str    The UTF-8 input string.
 * @param outbuf Output buffer for the UTF-16 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in char16_t units (including space for null terminator).
 * @return The number of char16_t units in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_str_to_u16str(
    const char* str, /* OPTIONAL */ char16_t* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-8 string (with explicit length) to a UTF-16 string.
 * @param str    The UTF-8 input string.
 * @param size   Length of the string in bytes.
 * @param outbuf Output buffer for the UTF-16 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in char16_t units (including space for null terminator).
 * @return The number of char16_t units in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_strn_to_u16str(
    const char* str, size_t size, /* OPTIONAL */ char16_t* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-16 string to a UTF-8 string.
 * @param str    The UTF-16 input string.
 * @param outbuf Output buffer for the UTF-8 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in bytes (including space for null terminator).
 * @return The number of bytes in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_u16str_to_str(
    const char16_t* str, /* OPTIONAL */ char* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-16 string (with explicit length) to a UTF-8 string.
 * @param str    The UTF-16 input string.
 * @param size   Length of the string in UTF-16 code units.
 * @param outbuf Output buffer for the UTF-8 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in bytes (including space for null terminator).
 * @return The number of bytes in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_u16strn_to_str(
    const char16_t* str, size_t size, /* OPTIONAL */ char* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-8 string to a UTF-32 string.
 * @param str    The UTF-8 input string.
 * @param outbuf Output buffer for the UTF-32 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in char32_t units (including space for null terminator).
 * @return The number of char32_t units in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_str_to_u32str(
    const char* str, /* OPTIONAL */ char32_t* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-8 string (with explicit length) to a UTF-32 string.
 * @param str    The UTF-8 input string.
 * @param size   Length of the string in bytes.
 * @param outbuf Output buffer for the UTF-32 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in char32_t units (including space for null terminator).
 * @return The number of char32_t units in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_strn_to_u32str(
    const char* str, size_t size, /* OPTIONAL */ char32_t* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-32 string to a UTF-8 string.
 * @param str    The UTF-32 input string.
 * @param outbuf Output buffer for the UTF-8 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in bytes (including space for null terminator).
 * @return The number of bytes in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_u32str_to_str(
    const char32_t* str, /* OPTIONAL */ char* outbuf, size_t buflen);

/**
 * @brief Convert a UTF-32 string (with explicit length) to a UTF-8 string.
 * @param str    The UTF-32 input string.
 * @param size   Length of the string in UTF-32 code units.
 * @param outbuf Output buffer for the UTF-8 result. May be NULL if buflen is 0.
 * @param buflen Size of outbuf in bytes (including space for null terminator).
 * @return The number of bytes in the conversion result (excluding null terminator).
 */
WOORT_NODISCARD WOORT_API size_t woort_u32strn_to_str(
    const char32_t* str, size_t size, /* OPTIONAL */ char* outbuf, size_t buflen);

/* ========== Raw UTF-8 Helpers ========== */

/*
 * Low-level UTF-8 / UTF-16 / UTF-32 conversion primitives.
 *
 * These are the raw building blocks used internally by woort. Unlike the
 * buffer-based woort_strn_to_* / woort_*strn_to_str family above (which write
 * into a caller-supplied buffer), several helpers here return freshly
 * malloc-allocated buffers that MUST be released with woort_free().
 */

/** @brief Maximum byte length of a single UTF-8 code point. */
#define WOORT_UTF8MAXLEN 6
/** @brief Maximum unit length of a single UTF-16 code point. */
#define WOORT_UTF16MAXLEN 2

/** @brief Length (in bytes) of the UTF-8 character starting at @p u8charp. */
WOORT_NODISCARD WOORT_API size_t woort_u8charnlen(const char* u8charp, size_t bytelen);

/** @brief Count the number of UTF-8 characters in a byte range. */
WOORT_NODISCARD WOORT_API size_t woort_u8strnlen(const char* u8str, size_t bytelen);

/**
 * @brief Inspect the first UTF-8 character in a byte range.
 * @param out_charsz  Receives the byte length of the character.
 * @return true if the character is well-formed, false if it is an invalid lead byte.
 */
WOORT_NODISCARD WOORT_API bool woort_u8strnchar(
    const char* u8str, size_t bytelen, size_t* out_charsz);

/** @brief Advance @p from UTF-8 characters into @p u8str, returning the sub-pointer and remaining length. */
WOORT_NODISCARD WOORT_API const char* woort_u8substr(
    const char* u8str, size_t bytelen, size_t from, size_t* out_len);

/** @brief Extract the UTF-8 substring [from, tail]. */
WOORT_NODISCARD WOORT_API const char* woort_u8substrr(
    const char* u8str, size_t bytelen, size_t from, size_t tail, size_t* out_len);

/** @brief Extract @p length UTF-8 characters starting at @p from. */
WOORT_NODISCARD WOORT_API const char* woort_u8substrn(
    const char* u8str, size_t bytelen, size_t from, size_t length, size_t* out_len);

/**
 * @brief Decode the UTF-8 character at @p u8charp into a code point.
 * @return The byte length consumed (0 if @p bytelen is 0).
 */
WOORT_NODISCARD WOORT_API size_t woort_u8combineu32(
    const char* u8charp, size_t bytelen, char32_t* out_c32);

/** @brief Encode a code point as UTF-8 into @p out_c8 (max WOORT_UTF8MAXLEN bytes). */
WOORT_API void woort_u32exractu8(
    char32_t ch32, char out_c8[WOORT_UTF8MAXLEN], size_t* out_u8len);

/** @brief Decode the UTF-8 character at @p u8charp into UTF-16 (1 or 2 units). */
WOORT_NODISCARD WOORT_API size_t woort_u8combineu16(
    const char* u8charp, size_t bytelen,
    char16_t out_c16[WOORT_UTF16MAXLEN], size_t* out_u16len);

/** @brief Encode a UTF-16 unit sequence as UTF-8. Returns the number of UTF-16 units consumed (1 or 2). */
WOORT_NODISCARD WOORT_API size_t woort_u16exractu8(
    const char16_t* u16charp, size_t charcount,
    char out_c8[WOORT_UTF8MAXLEN], size_t* out_u8len);

/** @brief Return true if @p ch is a UTF-16 high (leading) surrogate. */
WOORT_NODISCARD WOORT_API bool woort_u16hisurrogate(char16_t ch);

/** @brief Return true if @p ch is a UTF-16 low (trailing) surrogate. */
WOORT_NODISCARD WOORT_API bool woort_u16losurrogate(char16_t ch);

/**
 * @brief Escape a UTF-8 string into a quoted Woolang string literal.
 *
 * The returned buffer is malloc-allocated and must be freed with woort_free().
 * @param force_unicode  If non-zero, always emit \uXXXX escapes for non-ASCII.
 * @return NUL-terminated quoted string literal, or NULL on allocation failure.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_u8enstring(
    const char* u8str, size_t bytelen, int force_unicode);

/**
 * @brief Unescape a Woolang string literal (with or without surrounding quotes).
 *
 * The returned buffer is malloc-allocated and must be freed with woort_free().
 * @param out_len  Optional receiver for the decoded byte length.
 * @return NUL-terminated decoded string, or NULL on allocation failure.
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_u8destring(
    const char* enu8str_zero_term, /* OPTIONAL */ size_t* out_len);

/**
 * @brief Convert a UTF-8 string to a UTF-32 string.
 *
 * The returned buffer is malloc-allocated (count units + NUL) and must be
 * freed with woort_free().
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char32_t* woort_u8strtou32(
    const char* u8str, size_t bytelen, size_t* out_len);

/**
 * @brief Convert a UTF-32 string to a UTF-8 string.
 *
 * The returned buffer is malloc-allocated and must be freed with woort_free().
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_u32strtou8(
    const char32_t* u32charp, size_t u32len, size_t* out_len);

/**
 * @brief Convert a UTF-8 string to a UTF-16 string.
 *
 * The returned buffer is malloc-allocated and must be freed with woort_free().
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char16_t* woort_u8strtou16(
    const char* u8str, size_t bytelen, size_t* out_len);

/**
 * @brief Convert a UTF-16 string to a UTF-8 string.
 *
 * The returned buffer is malloc-allocated and must be freed with woort_free().
 */
WOORT_NODISCARD WOORT_API /* OPTIONAL */ char* woort_u16strtou8(
    const char16_t* u16charp, size_t u16len, size_t* out_len);

/** @brief Count code units in a NUL-terminated UTF-16 string. */
WOORT_NODISCARD WOORT_API size_t woort_u16strcount(const char16_t* u16str);

/** @brief Count code units in a NUL-terminated UTF-32 string. */
WOORT_NODISCARD WOORT_API size_t woort_u32strcount(const char32_t* u32str);

/** @brief Return true if @p ch32 fits in a single UTF-16 unit (BMP). */
WOORT_NODISCARD WOORT_API bool woort_u32isu16(char32_t ch32);

/**
 * @brief Get the Unicode code point at a character index in a UTF-8 string.
 * @return true on success, false if @p index is out of range.
 */
WOORT_NODISCARD WOORT_API bool woort_u8stridx(
    const char* str, size_t size, size_t index, char32_t* out_ch);

/* ========== REPL support ========== */

/** @brief Opaque handle to a REPL printer that buffers serialized Woolang values and flushes them as UTF-8 text. */
typedef struct woort_REPLPrinter woort_REPLPrinter;

/** @brief Callback invoked by woort_REPLPrinter_flush() to deliver flushed UTF-8 text. Parameters are the text buffer and its length in bytes. */
typedef void(*woort_REPLPrinter_ResultCallback)(const char*, size_t, void*);

/** @brief Outcome codes returned by woort_REPLPrinter_flush(). */
typedef enum woort_REPLPrinter_FlushResult
{
    WOORT_REPL_PRINTER_FLUSH_OK,      /**< @brief The buffered output was flushed successfully. */
    WOORT_REPL_PRINTER_FLUSH_NOTHING, /**< @brief Nothing to flush; the print buffer was empty. */

    WOORT_REPL_PRINTER_FLUSH_FAILED,  /**< @brief The flush failed (e.g. allocation failure). */

} woort_REPLPrinter_FlushResult;

/**
 * @brief Create a new REPL printer.
 * @param callback   Optional callback invoked on flush; if NULL, flushed text is written to stdout.
 * @param out_printer Output handle receiving the newly created printer.
 * @return true on success, false on allocation failure.
 */
WOORT_NODISCARD WOORT_API bool woort_REPLPrinter_create(
    /* OPTIONAL */ woort_REPLPrinter_ResultCallback callback,
    /* OPTIONAL */ void* param,
    woort_REPLPrinter** out_printer);

/** @brief Destroy a REPL printer and release all associated resources. */
WOORT_API void woort_REPLPrinter_destroy(woort_REPLPrinter* printer);

/**
 * @brief Flush the buffered output of a REPL printer.
 *
 * Writes the buffered UTF-8 text to the result callback (or stdout when no callback was set) and clears the buffer.
 * @param printer The printer whose buffer to flush.
 * @return A woort_REPLPrinter_FlushResult indicating the outcome of the flush.
 */
WOORT_NODISCARD WOORT_API woort_REPLPrinter_FlushResult woort_REPLPrinter_flush(
    woort_REPLPrinter* printer);

/* ========== WooDyn supports ========== */

WOORT_NODISCARD WOORT_API bool woort_woodyn_init(void);

/* ========== ANSI Escape Code Macros ========== */

/**
 * @name ANSI Terminal Control
 * @brief Macros for ANSI escape code sequences.
 *
 * You can use these macros to specify ANSI_XXX as a wide character string.
 * NOTE: After use, you MUST re-define them as nothing to avoid collisions.
 * @{
 */

 /** @brief ANSI escape sequence introducer. */
#   define WOORT_ANSI_ESC "\033["
/** @brief ANSI sequence terminator. */
#   define WOORT_ANSI_END "m"
/** @brief Reset all attributes. */
#   define WOORT_ANSI_RST WOORT_ANSI_ESC "0m"
/** @brief Bold / increased intensity. */
#   define WOORT_ANSI_HIL WOORT_ANSI_ESC "1m"
/** @brief Faint / decreased intensity. */
#   define WOORT_ANSI_FAINT WOORT_ANSI_ESC "2m"
/** @brief Italic. */
#   define WOORT_ANSI_ITALIC WOORT_ANSI_ESC "3m"
/** @brief Underline. */
#   define WOORT_ANSI_UNDERLNE WOORT_ANSI_ESC "4m"
/** @brief No underline. */
#   define WOORT_ANSI_NUNDERLNE WOORT_ANSI_ESC "24m"
/** @brief Slow blink. */
#   define WOORT_ANSI_SLOW_BLINK WOORT_ANSI_ESC "5m"
/** @brief Fast blink. */
#   define WOORT_ANSI_FAST_BLINK WOORT_ANSI_ESC "6m"
/** @brief Inverse / reverse video. */
#   define WOORT_ANSI_INV WOORT_ANSI_ESC "7m"
/** @brief Conceal / fade. */
#   define WOORT_ANSI_FADE WOORT_ANSI_ESC "8m"

/** @name Foreground Colors */
/**@{*/
/** @brief Black foreground. */
#   define WOORT_ANSI_BLK WOORT_ANSI_ESC "30m"
/** @brief Gray foreground (bright black). */
#   define WOORT_ANSI_GRY WOORT_ANSI_ESC "1;30m"
/** @brief Red foreground. */
#   define WOORT_ANSI_RED WOORT_ANSI_ESC "31m"
/** @brief Bright red foreground. */
#   define WOORT_ANSI_HIR WOORT_ANSI_ESC "1;31m"
/** @brief Green foreground. */
#   define WOORT_ANSI_GRE WOORT_ANSI_ESC "32m"
/** @brief Bright green foreground. */
#   define WOORT_ANSI_HIG WOORT_ANSI_ESC "1;32m"
/** @brief Yellow foreground. */
#   define WOORT_ANSI_YEL WOORT_ANSI_ESC "33m"
/** @brief Bright yellow foreground. */
#   define WOORT_ANSI_HIY WOORT_ANSI_ESC "1;33m"
/** @brief Blue foreground. */
#   define WOORT_ANSI_BLU WOORT_ANSI_ESC "34m"
/** @brief Bright blue foreground. */
#   define WOORT_ANSI_HIB WOORT_ANSI_ESC "1;34m"
/** @brief Magenta foreground. */
#   define WOORT_ANSI_MAG WOORT_ANSI_ESC "35m"
/** @brief Bright magenta foreground. */
#   define WOORT_ANSI_HIM WOORT_ANSI_ESC "1;35m"
/** @brief Cyan foreground. */
#   define WOORT_ANSI_CLY WOORT_ANSI_ESC "36m"
/** @brief Bright cyan foreground. */
#   define WOORT_ANSI_HIC WOORT_ANSI_ESC "1;36m"
/** @brief White foreground. */
#   define WOORT_ANSI_WHI WOORT_ANSI_ESC "37m"
/** @brief Bright white foreground. */
#   define WOORT_ANSI_HIW WOORT_ANSI_ESC "1;37m"
/**@}*/

/** @name Background Colors */
/**@{*/
/** @brief Black background. */
#   define WOORT_ANSI_BBLK WOORT_ANSI_ESC "40m"
/** @brief Gray background (bright black). */
#   define WOORT_ANSI_BGRY WOORT_ANSI_ESC "1;40m"
/** @brief Red background. */
#   define WOORT_ANSI_BRED WOORT_ANSI_ESC "41m"
/** @brief Bright red background. */
#   define WOORT_ANSI_BHIR WOORT_ANSI_ESC "1;41m"
/** @brief Green background. */
#   define WOORT_ANSI_BGRE WOORT_ANSI_ESC "42m"
/** @brief Bright green background. */
#   define WOORT_ANSI_BHIG WOORT_ANSI_ESC "1;42m"
/** @brief Yellow background. */
#   define WOORT_ANSI_BYEL WOORT_ANSI_ESC "43m"
/** @brief Bright yellow background. */
#   define WOORT_ANSI_BHIY WOORT_ANSI_ESC "1;43m"
/** @brief Blue background. */
#   define WOORT_ANSI_BBLU WOORT_ANSI_ESC "44m"
/** @brief Bright blue background. */
#   define WOORT_ANSI_BHIB WOORT_ANSI_ESC "1;44m"
/** @brief Magenta background. */
#   define WOORT_ANSI_BMAG WOORT_ANSI_ESC "45m"
/** @brief Bright magenta background. */
#   define WOORT_ANSI_BHIM WOORT_ANSI_ESC "1;45m"
/** @brief Cyan background. */
#   define WOORT_ANSI_BCLY WOORT_ANSI_ESC "46m"
/** @brief Bright cyan background. */
#   define WOORT_ANSI_BHIC WOORT_ANSI_ESC "1;46m"
/** @brief White background. */
#   define WOORT_ANSI_BWHI WOORT_ANSI_ESC "47m"
/** @brief Bright white background. */
#   define WOORT_ANSI_BHIW WOORT_ANSI_ESC "1;47m"
/**@}*/

/** @} */ /* end ANSI Terminal Control group */

/** @brief Backward-compatible aliases for the original ANSI macro names. */
#   define ANSI_ESC WOORT_ANSI_ESC
#   define ANSI_END WOORT_ANSI_END
#   define ANSI_RST WOORT_ANSI_RST
#   define ANSI_HIL WOORT_ANSI_HIL
#   define ANSI_FAINT WOORT_ANSI_FAINT
#   define ANSI_ITALIC WOORT_ANSI_ITALIC
#   define ANSI_UNDERLNE WOORT_ANSI_UNDERLNE
#   define ANSI_NUNDERLNE WOORT_ANSI_NUNDERLNE
#   define ANSI_SLOW_BLINK WOORT_ANSI_SLOW_BLINK
#   define ANSI_FAST_BLINK WOORT_ANSI_FAST_BLINK
#   define ANSI_INV WOORT_ANSI_INV
#   define ANSI_FADE WOORT_ANSI_FADE
#   define ANSI_BLK WOORT_ANSI_BLK
#   define ANSI_GRY WOORT_ANSI_GRY
#   define ANSI_RED WOORT_ANSI_RED
#   define ANSI_HIR WOORT_ANSI_HIR
#   define ANSI_GRE WOORT_ANSI_GRE
#   define ANSI_HIG WOORT_ANSI_HIG
#   define ANSI_YEL WOORT_ANSI_YEL
#   define ANSI_HIY WOORT_ANSI_HIY
#   define ANSI_BLU WOORT_ANSI_BLU
#   define ANSI_HIB WOORT_ANSI_HIB
#   define ANSI_MAG WOORT_ANSI_MAG
#   define ANSI_HIM WOORT_ANSI_HIM
#   define ANSI_CLY WOORT_ANSI_CLY
#   define ANSI_HIC WOORT_ANSI_HIC
#   define ANSI_WHI WOORT_ANSI_WHI
#   define ANSI_HIW WOORT_ANSI_HIW
#   define ANSI_BBLK WOORT_ANSI_BBLK
#   define ANSI_BGRY WOORT_ANSI_BGRY
#   define ANSI_BRED WOORT_ANSI_BRED
#   define ANSI_BHIR WOORT_ANSI_BHIR
#   define ANSI_BGRE WOORT_ANSI_BGRE
#   define ANSI_BHIG WOORT_ANSI_BHIG
#   define ANSI_BYEL WOORT_ANSI_BYEL
#   define ANSI_BHIY WOORT_ANSI_BHIY
#   define ANSI_BBLU WOORT_ANSI_BBLU
#   define ANSI_BHIB WOORT_ANSI_BHIB
#   define ANSI_BMAG WOORT_ANSI_BMAG
#   define ANSI_BHIM WOORT_ANSI_BHIM
#   define ANSI_BCLY WOORT_ANSI_BCLY
#   define ANSI_BHIC WOORT_ANSI_BHIC
#   define ANSI_BWHI WOORT_ANSI_BWHI
#   define ANSI_BHIW WOORT_ANSI_BHIW

/* ---------------------------- */

#undef WOORT_API

/**
 * @brief Redefine WOORT_API for dllexport.
 *
 * This definition is only active after the public API declarations are closed,
 * and is used by the woolang implementation source files.
 */
#ifdef _WIN32
#   ifdef __cplusplus
#       define WOORT_API extern "C" WOORT_EXPORT
#   else
#       define WOORT_API WOORT_EXPORT
#   endif
#else
#   ifdef __cplusplus
#       define WOORT_API extern "C"
#   else
#       define WOORT_API WOORT_EXPORT
#   endif
#endif

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* WOORT_MSVC_RC_INCLUDE */
