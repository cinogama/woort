#pragma once

#ifdef __cplusplus
#   include <cstddef>
#   include <cstdint>
extern "C" {
#else
#   include <stdbool.h>
#   include <stddef.h>
#   include <stdint.h>
#endif // __cplusplus

#if defined(_MSC_VER)
#define WOORT_NODISCARD _Check_return_
#define WOORT_DEPRECATED __declspec(deprecated)
#elif defined(__clang__) || defined(__GNUC__)
#define WOORT_NODISCARD __attribute__((warn_unused_result))
#define WOORT_DEPRECATED __attribute__((deprecated))
#else
#define WOORT_NODISCARD
#define WOORT_DEPRECATED
#endif

#ifdef _WIN32
#   define WOORT_IMPORT __declspec(dllimport)
#   define WOORT_EXPORT __declspec(dllexport)
#else
#   define WOORT_IMPORT extern
#   define WOORT_EXPORT extern
#endif

#ifdef WOORT_AS_DYLIB
#   ifdef WOORT_IMPL
#       define WOORT_API WOORT_EXPORT
#   else
#       define WOORT_API WOORT_IMPORT
#   endif
#else
#   define WOORT_API
#endif

/**
 * @brief Initialize the Woolang runtime.
 *
 * Must be called once before using any other woort API functions.
 * Must be paired with a corresponding woort_shutdown() call.
 */
WOORT_API void woort_init(void);

/**
 * @brief Shut down the Woolang runtime.
 *
 * Releases all runtime resources. No woort API functions may be called
 * after this function returns. Must be paired with a prior woort_init() call.
 */
WOORT_API void woort_shutdown(void);

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
    WOORT_BOX_VALUE_TYPE_GCUNIT = 0b000,  /**< @brief GC-managed unit. */
    WOORT_BOX_VALUE_TYPE_REAL = 0b001,     /**< @brief Boxed double-precision float. */
    WOORT_BOX_VALUE_TYPE_INT = 0b010,      /**< @brief Boxed 64-bit signed integer. */
    WOORT_BOX_VALUE_TYPE_BOOL = 0b100,     /**< @brief Boxed boolean. */

    ////
    WOORT_BOX_VALUE_TYPE_NIL = 0b1000,     /**< @brief Nil value. */
    WOORT_BOX_VALUE_TYPE_STRING,           /**< @brief String value. */
    WOORT_BOX_VALUE_TYPE_VEC,              /**< @brief Vector (dynamic array) value. */
    WOORT_BOX_VALUE_TYPE_MAP,              /**< @brief Map (hash table) value. */
    WOORT_BOX_VALUE_TYPE_STRUCT,           /**< @brief Struct value. */
    WOORT_BOX_VALUE_TYPE_GCHANDLE,         /**< @brief GC handle (external resource). */
    WOORT_BOX_VALUE_TYPE_CLOSURE,          /**< @brief Closure value. */

} woort_BoxValueType;

/** @brief Opaque handle to a VM runtime instance. */
typedef struct woort_VMRuntime woort_VMRuntime;

/** @brief Index into the VM evaluation stack. Negative values are relative to the current frame base. */
typedef int32_t woort_StackValue;

/** @brief Signature for native (C) functions callable from Woolang. */
typedef woort_api(*woort_NativeFunction)(void);

/** @brief Opaque handle to a compiled code environment containing bytecode and constants. */
typedef struct woort_CodeEnv woort_CodeEnv;

/** @brief Opaque handle to an IR compiler used to build CodeEnv objects. */
typedef struct woort_IRCompiler woort_IRCompiler;

/** @brief Opaque handle to an IR function being compiled. */
typedef struct woort_IRFunction woort_IRFunction;

/** @brief Opaque handle to an IR virtual register (value operand). */
typedef struct woort_IRValue woort_IRValue;

/** @brief Opaque handle to an IR label (branch target). */
typedef struct woort_IRLabel woort_IRLabel;

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
WOORT_API WOORT_NODISCARD bool woort_VMRuntime_create(
    woort_VMRuntime** out_vm);

/**
 * @brief Destroy a VM runtime instance and release all associated resources.
 * @param vm  The VM handle to destroy. Must not be NULL.
 */
WOORT_API void woort_VMRuntime_destroy(
    woort_VMRuntime* vm);

/**
 * @brief Swap the current thread-local VM instance with a new one.
 * @param vm  The new VM instance to install, or NULL to detach.
 * @return The previously active VM instance, or NULL if none was active.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_VMRuntime* woort_VMRuntime_swap(
    /* OPTIONAL */ woort_VMRuntime* vm);

/* ========== IR API ========== */

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
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_query_function(
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
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_find_srcloc_by_offset(
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
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_find_offset_by_srcloc(
    const woort_CodeEnv* env,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset);

/**
 * @brief Set a breakpoint trap at the given bytecode address.
 * @param code  Pointer to the bytecode instruction to trap.
 * @return true on success, false if the address is invalid/already traped/out of memory.
 */
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_set_trap(
    woort_Bytecode* code);

/**
 * @brief Clear a breakpoint trap at the given bytecode address, restoring the original instruction.
 * @param code  Pointer to the bytecode instruction whose trap should be cleared.
 * @return true on success, false if the address is invalid or not trapped.
 */
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_clear_trap(
    woort_Bytecode* code);

/* ========== IR Compiler ========== */

/**
 * @brief Create a new IR compiler instance.
 * @return The new compiler handle, or NULL on out-of-memory.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRCompiler* woort_IRCompiler_create(void);

/**
 * @brief Close and destroy an IR compiler instance.
 * @param c  The compiler to close. Must not be NULL.
 */
WOORT_API void woort_IRCompiler_close(
    woort_IRCompiler* c);

/**
 * @brief Add a new function definition to the compiler.
 * @param c              The compiler. Must not be NULL.
 * @param param_count    The number of parameters the function accepts.
 * @param[out] out_f     Pointer to receive the IR function handle. Must not be NULL.
 * @return true on success, false on out-of-memory.
 */
WOORT_API WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* c,
    uint32_t param_count,
    woort_IRFunction** out_f);

/**
 * @brief Allocate a new constant pool slot and return its index.
 * @param c  The compiler. Must not be NULL.
 * @return The index of the newly allocated constant slot.
 */
WOORT_API WOORT_NODISCARD woort_IRConstantIndex woort_IRCompiler_add_constant(
    woort_IRCompiler* c);

/**
 * @brief Allocate a new static data slot and return its index.
 * @param c  The compiler. Must not be NULL.
 * @return The index of the newly allocated static slot.
 */
WOORT_API WOORT_NODISCARD woort_IRStaticIndex woort_IRCompiler_add_static(
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
WOORT_API WOORT_NODISCARD bool woort_IRCompiler_finish(
    woort_IRCompiler* c,
    woort_CodeEnv** out_cenv);

/**
 * @brief Create a new virtual register in the function.
 * @param f  The IR function. Must not be NULL.
 * @return The new IR value handle, or NULL on out-of-memory.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_new_vreg(
    woort_IRFunction* f);

/**
 * @brief Get the virtual register for a function parameter (pre-allocated at SB+3+idx).
 * @param f         The IR function. Must not be NULL.
 * @param param_idx Zero-based parameter index.
 * @return The IR value handle for the parameter, or NULL if out of range.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f,
    uint32_t param_idx);

/**
 * @brief Create a new label (branch target) in the function.
 * @param f  The IR function. Must not be NULL.
 * @return The new label handle, or NULL on out-of-memory.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRLabel* woort_IRFunction_new_label(
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
WOORT_API WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* f,
    woort_IRConstantIndex idx);

/**
 * @brief Push a source location onto the function's source location stack.
 *
 * Subsequently emitted IR instructions will be associated with the top-of-stack
 * source location. filepath must be an intern pointer obtained via
 * woort_IRCompiler_intern_string().
 *
 * @param f             The IR function. Must not be NULL.
 * @param filepath      Interned source file path string (may be NULL).
 * @param begin_line    Start line number.
 * @param begin_column  Start column number.
 * @param end_line      End line number.
 * @param end_column    End column number.
 * @return true on success, false on out-of-memory.
 */
WOORT_API WOORT_NODISCARD bool woort_IRFunction_push_srcloc(
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

/* ========== Instruction Emission API ========== */

/**
 * @name IR Instruction Emission
 * @brief All woort_IR_* functions append an IROp to the linear instruction list of @p f.
 * @return true on success, false on out-of-memory.
 * @{
 */

 /** @name Data Movement */
 /**@{*/

 /** @brief No-operation: occupies a code slot but has no runtime effect. */
 WOORT_API WOORT_NODISCARD bool woort_IR_NOP(
     woort_IRFunction* f);

 /** @brief Move: dst = src. */
WOORT_API WOORT_NODISCARD bool woort_IR_MOV(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Load static: dst = Static[idx]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LOAD(
    woort_IRFunction* f,
    woort_IRValue* dst,
    woort_IRStaticIndex idx);

/** @brief Store static: Static[idx] = src. */
WOORT_API WOORT_NODISCARD bool woort_IR_STORE(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    const woort_IRValue* src);

/**@}*/

/** @name Stack Operations */
/**@{*/

/** @brief Push value onto stack with overflow check. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHCHK(
    woort_IRFunction* f,
    const woort_IRValue* src);

/** @brief Pop top of stack into dst. */
WOORT_API WOORT_NODISCARD bool woort_IR_POP(
    woort_IRFunction* f,
    woort_IRValue* dst);

/** @brief Pop count items from stack (discard). */
WOORT_API WOORT_NODISCARD bool woort_IR_POPR(
    woort_IRFunction* f,
    uint32_t count);

/** @brief Pop count_src items from stack (discard, count from register). */
WOORT_API WOORT_NODISCARD bool woort_IR_POPRS(
    woort_IRFunction* f,
    const woort_IRValue* count_src);

/**@}*/

/** @name Type Conversions */
/**@{*/

/** @brief Convert integer to real: dst = (woort_Real)src. */
WOORT_API WOORT_NODISCARD bool woort_IR_ITOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert integer to string: dst = tostring(src). */
WOORT_API WOORT_NODISCARD bool woort_IR_ITOS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert real to integer: dst = (woort_Int)src. */
WOORT_API WOORT_NODISCARD bool woort_IR_RTOI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert real to string: dst = tostring(src). */
WOORT_API WOORT_NODISCARD bool woort_IR_RTOS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert string to integer: dst = parse_int(src). */
WOORT_API WOORT_NODISCARD bool woort_IR_STOI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Convert string to real: dst = parse_real(src). */
WOORT_API WOORT_NODISCARD bool woort_IR_STOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Function Calls */
/**@{*/

/**
 * @brief Call a Woolang function (native, without overflow check).
 * @param f       The IR function being compiled.
 * @param target  Constant pool index of the callee.
 * @param argc    Number of arguments already pushed onto the stack.
 * @param dst     Destination register for the return value (may be NULL for void calls).
 */
WOORT_API WOORT_NODISCARD bool woort_IR_CALLNWO(
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
WOORT_API WOORT_NODISCARD bool woort_IR_CALLNFP(
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
WOORT_API WOORT_NODISCARD bool woort_IR_CALLNJIT(
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
WOORT_API WOORT_NODISCARD bool woort_IR_CALL(
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
WOORT_API WOORT_NODISCARD bool woort_IR_MKCLOSURE(
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
WOORT_API WOORT_NODISCARD bool woort_IR_MKVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count);

/**
 * @brief Create a map: dst = new Map(capacity = kvpair_count).
 * @param f            The IR function being compiled.
 * @param dst          Destination register for the new map.
 * @param kvpair_count Number of key-value pairs already pushed onto the stack.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_MKMAP(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t kvpair_count);

/**
 * @brief Create a struct: dst = new Struct(fields...).
 * @param f          The IR function being compiled.
 * @param dst        Destination register for the new struct.
 * @param elem_count Number of field values already pushed onto the stack.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_MKSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count);

/**@}*/

/** @name Dynamic Typing (Boxing / Unboxing) */
/**@{*/

/** @brief Box a value into a dynamic container: dst = Box(typ, src). */
WOORT_API WOORT_NODISCARD bool woort_IR_BOXDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Unbox a dynamic value: dst = unbox(src, typ). Panics if type mismatches. */
WOORT_API WOORT_NODISCARD bool woort_IR_UNBOXDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Check dynamic type: dst = (unbox_type(src) == typ). */
WOORT_API WOORT_NODISCARD bool woort_IR_CHECKDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

/** @brief Box and push a dynamic value onto the stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHBOXDYN(
    woort_IRFunction* f,
    uint8_t typ,
    const woort_IRValue* src);

/**@}*/

/** @name Integer Arithmetic (dst = a OP b) */
/**@{*/

/** @brief Integer addition: dst = a + b. */
WOORT_API WOORT_NODISCARD bool woort_IR_ADDI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer subtraction: dst = a - b. */
WOORT_API WOORT_NODISCARD bool woort_IR_SUBI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer multiplication: dst = a * b. */
WOORT_API WOORT_NODISCARD bool woort_IR_MULI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer division: dst = a / b. */
WOORT_API WOORT_NODISCARD bool woort_IR_DIVI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer modulo: dst = a % b. */
WOORT_API WOORT_NODISCARD bool woort_IR_MODI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer negation: dst = -src. */
WOORT_API WOORT_NODISCARD bool woort_IR_NEGI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Integer Comparison (dst = (a OP b) ? 1 : 0) */
/**@{*/

/** @brief Integer less-than: dst = (a < b). */
WOORT_API WOORT_NODISCARD bool woort_IR_LTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer greater-than: dst = (a > b). */
WOORT_API WOORT_NODISCARD bool woort_IR_GTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer less-or-equal: dst = (a <= b). */
WOORT_API WOORT_NODISCARD bool woort_IR_LEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer greater-or-equal: dst = (a >= b). */
WOORT_API WOORT_NODISCARD bool woort_IR_GEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer equal: dst = (a == b). */
WOORT_API WOORT_NODISCARD bool woort_IR_EQI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Integer not-equal: dst = (a != b). */
WOORT_API WOORT_NODISCARD bool woort_IR_NEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/**@}*/

/** @name Real Arithmetic (dst = a OP b) */
/**@{*/

/** @brief Real addition: dst = a + b. */
WOORT_API WOORT_NODISCARD bool woort_IR_ADDR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real subtraction: dst = a - b. */
WOORT_API WOORT_NODISCARD bool woort_IR_SUBR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real multiplication: dst = a * b. */
WOORT_API WOORT_NODISCARD bool woort_IR_MULR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real division: dst = a / b. */
WOORT_API WOORT_NODISCARD bool woort_IR_DIVR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real modulo: dst = fmod(a, b). */
WOORT_API WOORT_NODISCARD bool woort_IR_MODR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real negation: dst = -src. */
WOORT_API WOORT_NODISCARD bool woort_IR_NEGR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Real Comparison (dst = (a OP b) ? 1 : 0) */
/**@{*/

/** @brief Real less-than: dst = (a < b). */
WOORT_API WOORT_NODISCARD bool woort_IR_LTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real greater-than: dst = (a > b). */
WOORT_API WOORT_NODISCARD bool woort_IR_GTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real less-or-equal: dst = (a <= b). */
WOORT_API WOORT_NODISCARD bool woort_IR_LER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real greater-or-equal: dst = (a >= b). */
WOORT_API WOORT_NODISCARD bool woort_IR_GER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real equal: dst = (a == b). */
WOORT_API WOORT_NODISCARD bool woort_IR_EQR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Real not-equal: dst = (a != b). */
WOORT_API WOORT_NODISCARD bool woort_IR_NER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/**@}*/

/** @name String Operations */
/**@{*/

/** @brief String concatenation: dst = a .. b. */
WOORT_API WOORT_NODISCARD bool woort_IR_ADDS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String less-than (lexicographic): dst = (a < b). */
WOORT_API WOORT_NODISCARD bool woort_IR_LTS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String greater-than (lexicographic): dst = (a > b). */
WOORT_API WOORT_NODISCARD bool woort_IR_GTS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String less-or-equal (lexicographic): dst = (a <= b). */
WOORT_API WOORT_NODISCARD bool woort_IR_LES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String greater-or-equal (lexicographic): dst = (a >= b). */
WOORT_API WOORT_NODISCARD bool woort_IR_GES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String equal: dst = (a == b). */
WOORT_API WOORT_NODISCARD bool woort_IR_EQS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief String not-equal: dst = (a != b). */
WOORT_API WOORT_NODISCARD bool woort_IR_NES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/**@}*/

/** @name Logical Operations */
/**@{*/

/** @brief Logical AND: dst = a && b. */
WOORT_API WOORT_NODISCARD bool woort_IR_LAND(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Logical OR: dst = a || b. */
WOORT_API WOORT_NODISCARD bool woort_IR_LOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/** @brief Logical NOT: dst = !src. */
WOORT_API WOORT_NODISCARD bool woort_IR_LNOT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Index Load (Read element from container) */
/**@{*/

/** @brief Load vector element by integer index (bounds-checked): dst = container[idx]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load vector element by integer index (unchecked): dst = container[idx]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXVECX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load struct field by constant index: dst = container[idx]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    uint32_t idx);

/** @brief Load string character by integer index: dst = container[idx]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXSTRING(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by integer key: dst = container[key]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by real key: dst = container[key]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by bool key: dst = container[key]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTB(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/** @brief Load map element by boxed (dynamic) key: dst = container[key]. */
WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/**@}*/

/** @name Index Store — Vector (container[idx] = val) */
/**@{*/

/** @brief Store integer value into vector at index. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Store real value into vector at index. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Store bool value into vector at index. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Store boxed (dynamic) value into vector at index. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/**@}*/

/** @name Index Store — Dict (container[key] = val)
 *
 * Naming convention: STIDXDICT{KeyType}{ValueType} where
 * I=int, R=real, B=bool, X=boxed(dynamic).
 * @{*/

 /** @brief Dict[int] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTII(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[int] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTIR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[int] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTIB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[int] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTIX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[real] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[bool] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Dict[boxed] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/**@}*/

/** @name Index Store — Map (container[key] = val)
 *
 * Same key/value type convention as Dict: I=int, R=real, B=bool, X=boxed(dynamic).
 * @{*/

 /** @brief Map[int] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPII(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[int] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPIR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[int] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPIB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[int] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPIX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[real] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[bool] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = int. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = real. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = bool. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/** @brief Map[boxed] = boxed. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

/**@}*/

/** @name Index Store — Struct (container[idx] = val, idx is a constant) */
/**@{*/

/** @brief Store value into struct field at constant index. */
WOORT_API WOORT_NODISCARD bool woort_IR_STIDXSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* c,
    uint32_t idx,
    const woort_IRValue* val);

/**@}*/

/** @name Unpacking */
/**@{*/

/** @brief Unpack all struct fields onto the stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_UNPACKSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* src);

/** @brief Unpack vector elements onto the stack (bounds-checked). */
WOORT_API WOORT_NODISCARD bool woort_IR_UNPACKVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/** @brief Unpack vector elements onto the stack (unchecked). */
WOORT_API WOORT_NODISCARD bool woort_IR_UNPACKVECX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/**@}*/

/** @name Struct Field Push to Stack */
/**@{*/

/** @brief Push struct field at constant index onto the stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed int struct field at index onto stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXI(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed real struct field at index onto stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXR(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed bool struct field at index onto stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXB(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/** @brief Push boxed (dynamic) struct field at index onto stack. */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXX(
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
WOORT_API WOORT_NODISCARD bool woort_IR_ASTORE(
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
WOORT_API WOORT_NODISCARD bool woort_IR_ALOAD(
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
WOORT_API WOORT_NODISCARD bool woort_IR_CAS(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    woort_IRValue* expected,
    const woort_IRValue* desired);

/**@}*/

/** @} */ /* end IR Instruction Emission group */

/* ============ Control Flow ============ */

/**
 * @brief Bind a label to the current emission position.
 * @param f      The IR function. Must not be NULL.
 * @param label  The label to bind. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_bind(woort_IRFunction* f, woort_IRLabel* label);

/**
 * @brief Unconditional jump to target label.
 * @param f       The IR function. Must not be NULL.
 * @param target  The label to jump to. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_jmp(woort_IRFunction* f, woort_IRLabel* target);

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
WOORT_API WOORT_NODISCARD bool woort_IR_jifinited(
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
WOORT_API WOORT_NODISCARD bool woort_IR_jcc(
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
WOORT_API WOORT_NODISCARD bool woort_IR_jccz(
    woort_IRFunction* f,
    const woort_IRValue* cond,
    woort_IRLabel* target);

/** @name Compare-and-Jump
 * @brief Combined comparison and conditional branch: if (a OP b) goto target.
 * @return true on success, false on OOM.
 * @{ */

 /** @brief Jump if a < b. */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc_lt(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a <= b. */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc_le(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a == b. */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc_eq(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a > b. */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc_gt(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a >= b. */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc_ge(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/** @brief Jump if a != b. */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc_ne(
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
WOORT_API WOORT_NODISCARD bool woort_IR_debugtrap(woort_IRFunction* f);

/**
 * @brief Emit a panic with a string message.
 * @param f    The IR function. Must not be NULL.
 * @param msg  The string value to use as the panic message.
 * @return true on success, false on OOM.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_panic(
    woort_IRFunction* f, const woort_IRValue* msg);

/* ============ Return ============ */

/**
 * @brief Return a value from the current function.
 * @param f    The IR function. Must not be NULL.
 * @param val  The return value register.
 * @return true on success, false on OOM.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_ret(woort_IRFunction* f, const woort_IRValue* val);

/**
 * @brief Return void from the current function.
 * @param f  The IR function. Must not be NULL.
 * @return true on success, false on OOM.
 */
WOORT_API WOORT_NODISCARD bool woort_IR_ret_void(woort_IRFunction* f);

/* ============ Runtime API ============ */

/**
 * @brief Reserve space on the VM evaluation stack.
 * @param count      Number of stack slots to reserve.
 * @param[out] out_stack  Pointer to receive the base stack index of the reserved region.
 * @return true on success, false on stack overflow.
 */
WOORT_API WOORT_NODISCARD bool woort_push_reserve(
    size_t count, woort_StackValue* out_stack);

/**
 * @brief Pop (discard) the top count values from the VM evaluation stack.
 * @param count  Number of values to pop.
 */
WOORT_API void woort_pop(size_t count);

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
 * @brief Invoke a function value and wait for completion.
 * @param dst  Stack slot for the return value.
 * @param f    Stack slot holding the callable value.
 * @return The call status (NORMAL, YIELD, ABORTED, or RESYNC).
 */
WOORT_API WOORT_NODISCARD woort_VmCallStatus woort_invoke(
    woort_StackValue dst, woort_StackValue f);

/**
 * @brief Spawn a new coroutine from a function value.
 * @param dst  Stack slot for the return value (when coroutine completes).
 * @param f    Stack slot holding the callable value.
 * @return The call status.
 */
WOORT_API WOORT_NODISCARD woort_VmCallStatus woort_spawn(
    woort_StackValue dst, woort_StackValue f);

/**
 * @brief Resume a previously yielded coroutine.
 * @param dst  Stack slot for the return value.
 * @return The call status.
 */
WOORT_API WOORT_NODISCARD woort_VmCallStatus woort_resume(
    woort_StackValue dst);

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
  * @brief Set a constant pool entry to a string value.
  * @param code_env  The locked code environment.
  * @param cidx      The constant pool index (must be allocated before finish).
  * @param val       The NUL-terminated string to store (a GCString is created).
  */
 WOORT_API void woort_CodeEnv_set_const_string(
     woort_CodeEnv* code_env,
     woort_IRConstantIndex cidx,
     woort_U8CString val);

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
     const woort_IRConstantIndex* members,
     size_t member_count);

/** @} */ /* end CodeEnv Constant Pool Setters */

/**
 * @brief Load a constant from a CodeEnv into a stack slot.
 * @param dst       Destination stack slot.
 * @param code_env  The code environment holding the constant pool.
 * @param cidx      The constant pool index to load.
 */
WOORT_API void woort_load_const(
    woort_StackValue dst, woort_CodeEnv* code_env, woort_IRConstantIndex cidx);

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

/** @brief Set a stack slot to nil. */
WOORT_API void woort_set_nil(
    woort_StackValue dst);

/** @brief Alias for woort_set_nil — set a void return slot. */
#define woort_set_void woort_set_nil

/** @brief Set a stack slot to an integer value. */
WOORT_API void woort_set_int(
    woort_StackValue dst, woort_Int src);

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
 * @param hold   Stack slot holding a reference to prevent premature collection.
 * @param close  Destructor callback invoked when the handle is collected.
 */
WOORT_API void woort_set_gchandle(
    woort_StackValue dst,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close);

/**
 * @brief Set a stack slot to a GC-managed struct (external object with mark callback).
 * @param dst    Target stack slot.
 * @param addr   Pointer to the external object.
 * @param mark   Mark callback to trace GC references within the object.
 * @param close  Destructor callback invoked when collected.
 */
WOORT_API void woort_set_gcstruct(
    woort_StackValue dst,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close);

/** @brief Set a stack slot to a boxed integer. */
WOORT_API void woort_set_box_int(
    woort_StackValue dst, woort_Int src);

/** @brief Set a stack slot to a boxed real. */
WOORT_API void woort_set_box_real(
    woort_StackValue dst, woort_Real src);

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

/** @brief Set union to a buffer variant (copied). */
WOORT_API void woort_set_union_buffer(
    woort_StackValue dst, woort_Int id, const void* src, size_t len);

/**
 * @brief Set union to a GC handle variant.
 * @param dst    Target stack slot.
 * @param id     Variant tag.
 * @param addr   Pointer to the external resource.
 * @param hold   Stack slot holding a reference to prevent premature collection.
 * @param close  Destructor callback.
 */
WOORT_API void woort_set_union_gchandle(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close);

/**
 * @brief Set union to a GC struct variant.
 * @param dst    Target stack slot.
 * @param id     Variant tag.
 * @param addr   Pointer to the external object.
 * @param mark   Mark callback.
 * @param close  Destructor callback.
 */
WOORT_API void woort_set_union_gcstruct(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close);

/** @brief Set union to a boxed integer variant. */
WOORT_API void woort_set_union_box_int(
    woort_StackValue dst, woort_Int id, woort_Int src);

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
/** @brief Set option::value(real). */
#define woort_set_option_real(dst, src) woort_set_union_real(dst, 0, src)
/** @brief Set option::value(float). */
#define woort_set_option_float(dst, src) woort_set_union_float(dst, 0, src)
/** @brief Set option::value(bool). */
#define woort_set_option_bool(dst, src) woort_set_union_bool(dst, 0, src)
/** @brief Set option::value(string). */
#define woort_set_option_string(dst, src) woort_set_union_string(dst, 0, src)
/** @brief Set option::value(buffer). */
#define woort_set_option_buffer(dst, src, len) woort_set_union_buffer(dst, 0, src, len)
/** @brief Set option::value(box_int). */
#define woort_set_option_box_int(dst, src) woort_set_union_box_int(dst, 0, src)
/** @brief Set option::value(box_real). */
#define woort_set_option_box_real(dst, src) woort_set_union_box_real(dst, 0, src)
/** @brief Set option::value(box_bool). */
#define woort_set_option_box_bool(dst, src) woort_set_union_box_bool(dst, 0, src)
/** @brief Set option::value(gchandle). */
#define woort_set_option_gchandle(dst, addr, hold, close) \
    woort_set_union_gchandle(dst, 0, addr, hold, close)
/** @brief Set option::value(gcstruct). */
#define woort_set_option_gcstruct(dst, addr, mark, close) \
    woort_set_union_gcstruct(dst, 0, addr, mark, close)

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
/** @brief Set Result::Ok(real). */
#define woort_set_result_ok_real woort_set_option_real
/** @brief Set Result::Ok(float). */
#define woort_set_result_ok_float woort_set_option_float
/** @brief Set Result::Ok(bool). */
#define woort_set_result_ok_bool woort_set_option_bool
/** @brief Set Result::Ok(string). */
#define woort_set_result_ok_string woort_set_option_string
/** @brief Set Result::Ok(buffer). */
#define woort_set_result_ok_buffer woort_set_option_buffer
/** @brief Set Result::Ok(box_int). */
#define woort_set_result_ok_box_int woort_set_option_box_int
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
/** @brief Set Result::Err(real). */
#define woort_set_result_err_real(dst, src) woort_set_union_real(dst, 1, src)
/** @brief Set Result::Err(float). */
#define woort_set_result_err_float(dst, src) woort_set_union_float(dst, 1, src)
/** @brief Set Result::Err(bool). */
#define woort_set_result_err_bool(dst, src) woort_set_union_bool(dst, 1, src)
/** @brief Set Result::Err(string). */
#define woort_set_result_err_string(dst, src) woort_set_union_string(dst, 1, src)
/** @brief Set Result::Err(buffer). */
#define woort_set_result_err_buffer(dst, src, len) woort_set_union_buffer(dst, 1, src, len)
/** @brief Set Result::Err(box_int). */
#define woort_set_result_err_box_int(dst, src) woort_set_union_box_int(dst, 1, src)
/** @brief Set Result::Err(box_real). */
#define woort_set_result_err_box_real(dst, src) woort_set_union_box_real(dst, 1, src)
/** @brief Set Result::Err(box_bool). */
#define woort_set_result_err_box_bool(dst, src) woort_set_union_box_bool(dst, 1, src)
/** @brief Set Result::Err(gchandle). */
#define woort_set_result_err_gchandle(dst, addr, hold, close) \
    woort_set_union_gchandle(dst, 1, addr, hold, close)
/** @brief Set Result::Err(gcstruct). */
#define woort_set_result_err_gcstruct(dst, addr, mark, close) \
    woort_set_union_gcstruct(dst, 1, addr, mark, close)

/** @} */ /* end Result Err Setters */

/* ========== Return ========== */

/**
 * @name Return Macros (Plain)
 * @brief Set the return slot (-1) and return WOORT_VM_CALL_STATUS_NORMAL.
 *        These macros are intended for use inside native function implementations
 *        to return a typed value to the caller.
 * @{
 */

 /** @brief Return a stack value: set slot -1 and return NORMAL. */
#define woort_ret_value(src) (woort_set_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return void (no value). */
#define woort_ret_void() WOORT_VM_CALL_STATUS_NORMAL
/** @brief Return nil. */
#define woort_ret_nil() (woort_set_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return an integer. */
#define woort_ret_int(src) (woort_set_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a real. */
#define woort_ret_real(src) (woort_set_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a float. */
#define woort_ret_float(src) (woort_set_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a boolean. */
#define woort_ret_bool(src) (woort_set_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a string. */
#define woort_ret_string(src) (woort_set_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a buffer. */
#define woort_ret_buffer(src, len) (woort_set_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a boxed integer. */
#define woort_ret_box_int(src) (woort_set_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a boxed real. */
#define woort_ret_box_real(src) (woort_set_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a boxed boolean. */
#define woort_ret_box_bool(src) (woort_set_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a GC handle. */
#define woort_ret_gchandle(addr, hold, close) \
    (woort_set_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a GC struct. */
#define woort_ret_gcstruct(addr, mark, close) \
    (woort_set_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/** @} */ /* end Return Macros (Plain) */

/**
 * @name Return Union Macros
 * @brief Set return slot (-1) to a tagged union variant and return WOORT_VM_CALL_STATUS_NORMAL.
 * @{
 */

 /** @brief Return a union with no inline payload. */
#define woort_ret_union_without_value(id) (woort_set_union_without_value(-1, id), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union carrying a stack value. */
#define woort_ret_union_value(id, src) (woort_set_union_value(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with nil payload. */
#define woort_ret_union_nil(id) (woort_set_union_nil(-1, id), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Alias for woort_ret_union_nil. */
#define woort_ret_union_void woort_ret_union_nil
/** @brief Return a union with int payload. */
#define woort_ret_union_int(id, src) (woort_set_union_int(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with real payload. */
#define woort_ret_union_real(id, src) (woort_set_union_real(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with float payload. */
#define woort_ret_union_float(id, src) (woort_set_union_float(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with bool payload. */
#define woort_ret_union_bool(id, src) (woort_set_union_bool(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with string payload. */
#define woort_ret_union_string(id, src) (woort_set_union_string(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with buffer payload. */
#define woort_ret_union_buffer(id, src, len) (woort_set_union_buffer(-1, id, src, len), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with boxed int payload. */
#define woort_ret_union_box_int(id, src) (woort_set_union_box_int(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with boxed real payload. */
#define woort_ret_union_box_real(id, src) (woort_set_union_box_real(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with boxed bool payload. */
#define woort_ret_union_box_bool(id, src) (woort_set_union_box_bool(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with GC handle payload. */
#define woort_ret_union_gchandle(id, addr, hold, close) \
    (woort_set_union_gchandle(-1, id, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return a union with GC struct payload. */
#define woort_ret_union_gcstruct(id, addr, mark, close) \
    (woort_set_union_gcstruct(-1, id, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/** @} */ /* end Return Union Macros */

/**
 * @name Return Option Macros
 * @brief Set return slot (-1) to Option<T> (value=0, none=1) and return WOORT_VM_CALL_STATUS_NORMAL.
 * @{
 */

 /** @brief Return option::none. */
#define woort_ret_option_none() (woort_set_option_none(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(stack_value). */
#define woort_ret_option_value(src) (woort_set_option_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(nil). */
#define woort_ret_option_nil() (woort_set_option_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(void). */
#define woort_ret_option_void() (woort_set_option_void(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(int). */
#define woort_ret_option_int(src) (woort_set_option_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(real). */
#define woort_ret_option_real(src) (woort_set_option_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(float). */
#define woort_ret_option_float(src) (woort_set_option_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(bool). */
#define woort_ret_option_bool(src) (woort_set_option_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(string). */
#define woort_ret_option_string(src) (woort_set_option_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(buffer). */
#define woort_ret_option_buffer(src, len) (woort_set_option_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(box_int). */
#define woort_ret_option_box_int(src) (woort_set_option_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(box_real). */
#define woort_ret_option_box_real(src) (woort_set_option_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(box_bool). */
#define woort_ret_option_box_bool(src) (woort_set_option_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(gchandle). */
#define woort_ret_option_gchandle(addr, hold, close) \
    (woort_set_option_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return option::value(gcstruct). */
#define woort_ret_option_gcstruct(addr, mark, close) \
    (woort_set_option_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/** @} */ /* end Return Option Macros */

/**
 * @name Return Result::Ok Macros
 * @brief Set return slot (-1) to Result<T,E>::Ok and return WOORT_VM_CALL_STATUS_NORMAL.
 *        Aliases for the corresponding woort_ret_option_* macros.
 * @{
 */

 /** @brief Return Result::Ok(stack_value). */
#define woort_ret_result_ok_value(src) (woort_set_result_ok_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(nil). */
#define woort_ret_result_ok_nil() (woort_set_result_ok_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(void). */
#define woort_ret_result_ok_void() (woort_set_result_ok_void(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(int). */
#define woort_ret_result_ok_int(src) (woort_set_result_ok_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(real). */
#define woort_ret_result_ok_real(src) (woort_set_result_ok_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(float). */
#define woort_ret_result_ok_float(src) (woort_set_result_ok_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(bool). */
#define woort_ret_result_ok_bool(src) (woort_set_result_ok_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(string). */
#define woort_ret_result_ok_string(src) (woort_set_result_ok_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(buffer). */
#define woort_ret_result_ok_buffer(src, len) (woort_set_result_ok_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(box_int). */
#define woort_ret_result_ok_box_int(src) (woort_set_result_ok_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(box_real). */
#define woort_ret_result_ok_box_real(src) (woort_set_result_ok_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(box_bool). */
#define woort_ret_result_ok_box_bool(src) (woort_set_result_ok_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(gchandle). */
#define woort_ret_result_ok_gchandle(addr, hold, close) \
    (woort_set_result_ok_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Ok(gcstruct). */
#define woort_ret_result_ok_gcstruct(addr, mark, close) \
    (woort_set_result_ok_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/** @} */ /* end Return Result::Ok Macros */

/**
 * @name Return Result::Err Macros
 * @brief Set return slot (-1) to Result<T,E>::Err and return WOORT_VM_CALL_STATUS_NORMAL.
 * @{
 */

 /** @brief Return Result::Err(stack_value). */
#define woort_ret_result_err_value(src) (woort_set_result_err_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(nil). */
#define woort_ret_result_err_nil() (woort_set_result_err_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(void). */
#define woort_ret_result_err_void() (woort_set_result_err_void(-1), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(int). */
#define woort_ret_result_err_int(src) (woort_set_result_err_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(real). */
#define woort_ret_result_err_real(src) (woort_set_result_err_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(float). */
#define woort_ret_result_err_float(src) (woort_set_result_err_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(bool). */
#define woort_ret_result_err_bool(src) (woort_set_result_err_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(string). */
#define woort_ret_result_err_string(src) (woort_set_result_err_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(buffer). */
#define woort_ret_result_err_buffer(src, len) (woort_set_result_err_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(box_int). */
#define woort_ret_result_err_box_int(src) (woort_set_result_err_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(box_real). */
#define woort_ret_result_err_box_real(src) (woort_set_result_err_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(box_bool). */
#define woort_ret_result_err_box_bool(src) (woort_set_result_err_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(gchandle). */
#define woort_ret_result_err_gchandle(addr, hold, close) \
    (woort_set_result_err_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
/** @brief Return Result::Err(gcstruct). */
#define woort_ret_result_err_gcstruct(addr, mark, close) \
    (woort_set_result_err_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/** @} */ /* end Return Result::Err Macros */

/**
 * @brief Trigger a panic indicating the current Native-call has encountered a completely unexpected situation
 *        severe enough that continuing execution may lead to a crash. Panic blocks the current thread and awaits
 *        further instructions from the user, which may include:
 *          1) abort the entire program,
 *          2) treat as Abort (equivalent to woort_ret_abort), or
 *          3) attach a debugger and break immediately at the current location.
 *
 * @param fmt  Printf-style format string for the panic message.
 * @param ...  Arguments for the format string.
 *
 * @note Regardless of the user's choice, WooRT makes no guarantees about any program behavior after a panic occurs.
 *       The program may crash even after the user makes a selection.
 */
WOORT_API WOORT_NODISCARD woort_api woort_ret_panic(const char* fmt, ...);

/**
 * @brief Request to abort further VM execution. Use this when the VM has entered an unrecoverable state where
 *        continuing execution in the current context would lead to unpredictable results. For example, when
 *        woort_invoke returns WOORT_VM_CALL_STATUS_ABORTED, the call stack may be corrupted, requiring
 *        woort_ret_abort to abort the current Native-function call and prevent the VM from continuing.
 */
WOORT_API WOORT_NODISCARD woort_api woort_ret_abort(void);

/**
 * @brief Request to pause VM execution, preserving the state after the current Native-function call completes.
 *        Expects to be resumed later via woort_resume, continuing from the preserved state.
 */
WOORT_API WOORT_NODISCARD woort_api woort_ret_yield(void);

/**
 * @name Stack Value Readers
 * @brief Read a typed value from a VM stack slot.
 *
 * @param src  Source stack slot index.
 * @return     The value read from the slot (type varies per function).
 * @{
 */

 /** @brief Read a raw integer from a stack slot. */
WOORT_API WOORT_NODISCARD woort_Int woort_int(woort_StackValue src);
/** @brief Read a raw real (double) from a stack slot. */
WOORT_API WOORT_NODISCARD woort_Real woort_real(woort_StackValue src);
/** @brief Read a single-precision float from a stack slot. */
WOORT_API WOORT_NODISCARD float woort_float(woort_StackValue src);
/** @brief Read a boolean from a stack slot. */
WOORT_API WOORT_NODISCARD bool woort_bool(woort_StackValue src);
/** @brief Read a string pointer from a stack slot. */
WOORT_API WOORT_NODISCARD woort_U8CString woort_string(woort_StackValue src);
/** @brief Read a buffer pointer and its length from a stack slot. */
WOORT_API WOORT_NODISCARD const void* woort_buffer(
    woort_StackValue src, size_t* out_len);
/** @brief Read a raw GC pointer from a stack slot. */
WOORT_API WOORT_NODISCARD void* woort_gcpointer(woort_StackValue src);
/** @brief Unbox and read an integer from a boxed stack slot. */
WOORT_API WOORT_NODISCARD woort_Int woort_unbox_int(woort_StackValue src);
/** @brief Unbox and read a real from a boxed stack slot. */
WOORT_API WOORT_NODISCARD woort_Real woort_unbox_real(woort_StackValue src);
/** @brief Unbox and read a boolean from a boxed stack slot. */
WOORT_API WOORT_NODISCARD bool woort_unbox_bool(woort_StackValue src);
/** @brief Query the type tag of a boxed dynamic value. */
WOORT_API WOORT_NODISCARD woort_BoxValueType woort_unbox_type(
    woort_StackValue src);
/**
 * @brief Unbox a dynamic value, writing the inner value to dst.
 * @param dst  Destination stack slot for the unboxed value.
 * @param src  Source stack slot holding the boxed value.
 * @return The type tag of the unboxed value.
 */
WOORT_API WOORT_NODISCARD woort_BoxValueType woort_unbox(
    woort_StackValue dst,
    woort_StackValue src);

/** @} */ /* end Stack Value Readers */

/**
 * @brief Get the union variant from a stack slot and copy the payload.
 * @param dst  Destination stack slot for the payload value.
 * @param src  Source stack slot holding the union.
 * @return The union discriminant (variant id).
 */
WOORT_API WOORT_NODISCARD woort_Int woort_union_get(
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
WOORT_API WOORT_NODISCARD size_t woort_vec_len(
    woort_StackValue src);

/**
 * @brief Resize a vector to the given number of elements.
 * @param src       Stack slot holding the vector.
 * @param new_size  Desired number of elements.
 */
WOORT_API void woort_vec_resize(
    woort_StackValue src, size_t new_size);

/**@}*/

/** @name Vector Element Access */
/**@{*/

/**
 * @brief Read an element from a vector (boxed).
 * @param dst_boxed  Destination stack slot for the boxed element.
 * @param src        Stack slot holding the vector.
 * @param index      Zero-based element index.
 */
WOORT_API void woort_vec_get(
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
 * @param elem_boxed Stack slot holding the boxed element to write.
 */
WOORT_API void woort_vec_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue elem_boxed);

/**
 * @brief Append an element to the end of a vector.
 * @param src        Stack slot holding the vector.
 * @param elem_boxed Stack slot holding the boxed element to append.
 */
WOORT_API void woort_vec_push(
    woort_StackValue src,
    woort_StackValue elem_boxed);

/**
 * @brief Remove the last element from a vector.
 * @param src  Stack slot holding the vector.
 */
WOORT_API void woort_vec_pop(woort_StackValue src);

/**
 * @brief Insert an element at the given index, shifting subsequent elements.
 * @param src        Stack slot holding the vector.
 * @param index      Zero-based insertion position.
 * @param elem_boxed Stack slot holding the boxed element to insert.
 */
WOORT_API void woort_vec_insert(
    woort_StackValue src,
    size_t index,
    woort_StackValue elem_boxed);

/**
 * @brief Remove the element at the given index, shifting subsequent elements.
 * @param src    Stack slot holding the vector.
 * @param index  Zero-based position of the element to remove.
 */
WOORT_API void woort_vec_erase(
    woort_StackValue src,
    size_t index);

/**
 * @brief Remove all elements from a vector.
 * @param src  Stack slot holding the vector.
 */
WOORT_API void woort_vec_clear(woort_StackValue src);

/**@}*/

/* ========== Mapping ========== */

/** @name Mapping Capacity */
/**@{*/

/**
 * @brief Get the number of key-value pairs in a map.
 * @param src  Stack slot holding the map.
 * @return Number of entries.
 */
WOORT_API WOORT_NODISCARD size_t woort_map_len(woort_StackValue src);

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
WOORT_API WOORT_NODISCARD bool woort_map_get(
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
WOORT_API WOORT_NODISCARD bool woort_map_get_by_int(
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
WOORT_API WOORT_NODISCARD bool woort_map_get_by_real(
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
WOORT_API WOORT_NODISCARD bool woort_map_get_by_bool(
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
WOORT_API WOORT_NODISCARD bool woort_map_get_by_string(
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
WOORT_API WOORT_NODISCARD bool woort_map_set(
    woort_StackValue src,
    woort_StackValue key_boxed,
    woort_StackValue val_boxed);

/** @brief Insert/update with int key. Returns true if newly inserted. */
WOORT_API WOORT_NODISCARD bool woort_map_set_by_int(
    woort_StackValue src,
    woort_Int key,
    woort_StackValue val_boxed);

/** @brief Insert/update with real key. Returns true if newly inserted. */
WOORT_API WOORT_NODISCARD bool woort_map_set_by_real(
    woort_StackValue src,
    woort_Real key,
    woort_StackValue val_boxed);

/** @brief Insert/update with bool key. Returns true if newly inserted. */
WOORT_API WOORT_NODISCARD bool woort_map_set_by_bool(
    woort_StackValue src,
    bool key,
    woort_StackValue val_boxed);

/** @brief Insert/update with string key. Returns true if newly inserted. */
WOORT_API WOORT_NODISCARD bool woort_map_set_by_string(
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
WOORT_API WOORT_NODISCARD bool woort_map_erase(
    woort_StackValue src,
    woort_StackValue key_boxed);

/** @brief Erase by int key. Returns true if found and removed. */
WOORT_API WOORT_NODISCARD bool woort_map_erase_by_int(
    woort_StackValue src,
    woort_Int key);

/** @brief Erase by real key. Returns true if found and removed. */
WOORT_API WOORT_NODISCARD bool woort_map_erase_by_real(
    woort_StackValue src,
    woort_Real key);

/** @brief Erase by bool key. Returns true if found and removed. */
WOORT_API WOORT_NODISCARD bool woort_map_erase_by_bool(
    woort_StackValue src,
    bool key);

/** @brief Erase by string key. Returns true if found and removed. */
WOORT_API WOORT_NODISCARD bool woort_map_erase_by_string(
    woort_StackValue src,
    woort_U8CString key);

/**@}*/

/** @name Mapping Contains */
/**@{*/

/** @brief Check if a boxed key exists in the map. */
WOORT_API WOORT_NODISCARD bool woort_map_contains(
    woort_StackValue src,
    woort_StackValue key_boxed);

/** @brief Check if an int key exists in the map. */
WOORT_API WOORT_NODISCARD bool woort_map_contains_int(
    woort_StackValue src,
    woort_Int key);

/** @brief Check if a real key exists in the map. */
WOORT_API WOORT_NODISCARD bool woort_map_contains_real(
    woort_StackValue src,
    woort_Real key);

/** @brief Check if a bool key exists in the map. */
WOORT_API WOORT_NODISCARD bool woort_map_contains_bool(
    woort_StackValue src,
    bool key);

/** @brief Check if a string key exists in the map. */
WOORT_API WOORT_NODISCARD bool woort_map_contains_string(
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
 * @param out_key_boxed  Destination for the boxed key.
 * @param out_val_boxed  Destination for the boxed value.
 * @return true if a valid entry was found at the given index.
 */
WOORT_API WOORT_NODISCARD bool woort_map_iter(
    woort_StackValue src,
    size_t index,
    woort_StackValue out_key_boxed,
    woort_StackValue out_val_boxed);

/**@}*/

/* ========== Struct ========== */

/** @name Struct Capacity */
/**@{*/

/**
 * @brief Get the number of fields in a struct.
 * @param src  Stack slot holding the struct.
 * @return Number of fields.
 */
WOORT_API WOORT_NODISCARD size_t woort_struct_len(
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

/**@}*/

/* ---------------------------- */

#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
