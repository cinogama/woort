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

WOORT_API void woort_init(void);
WOORT_API void woort_shutdown(void);

/*
 * When invoking or dispatching a function, the result may be one of the following statuses.
 */
typedef enum woort_VmCallStatus
{
    /*
     * WOORT_VM_CALL_STATUS_NORMAL
     * The target function returned normally with no special conditions to handle.
     */
    WOORT_VM_CALL_STATUS_NORMAL,

    /*
     * WOORT_VM_CALL_STATUS_YIELD
     * The VM requests to suspend execution at the current state. If possible,
     * execution can be resumed later.
     *
     * YIELD is only returned when interpreted execution receives an interrupt request
     * External dispatch operations may end with this status
     * If an invoke operation attempts to end with YIELD, a PANIC is triggered
     */
    WOORT_VM_CALL_STATUS_YIELD,

    /*
     * WOORT_VM_CALL_STATUS_ABORTED
     * The program has been terminated. The VM cannot continue executing the current
     * call stack. The VM will also be marked as aborted and will refuse to execute
     * any further operations.
     *
     * ABORTED is only returned when interpreted execution receives an interrupt request
     */
    WOORT_VM_CALL_STATUS_ABORTED,

    /*
     * WOORT_VM_CALL_STATUS_RESYNC
     * The next call stack level involves some new VM state changes. The next level
     * requests the previous level to perform reverse synchronization or a checkpoint
     * to restore to a normal state.
     *
     * Interpreted execution never ends with RESYNC
     *
     * When JIT calls a native function and RESYNC occurs:
     *     Restore to post-call state, then execute a checkpoint
     * When JIT calls a JIT function and RESYNC occurs:
     *     Do not synchronize, immediately end the current JIT function with RESYNC
     * When interpreted execution calls a native function and RESYNC occurs:
     *     Restore to post-call state, then execute a checkpoint
     * When interpreted execution calls a JIT function and RESYNC occurs:
     *     Perform a desynchronization, then execute a checkpoint
     *
     * External invoke or dispatch operations may target external or JIT functions.
     * If this occurs, _woort_VMRuntime_dispatch must be called again to obtain the
     * accurate result.
     */
    WOORT_VM_CALL_STATUS_RESYNC,
} woort_VmCallStatus, woort_api;

typedef struct woort_VMRuntime woort_VMRuntime;
typedef int32_t woort_StackValue;

typedef woort_api(*woort_NativeFunction)(woort_VMRuntime* vm);

typedef struct woort_CodeEnv woort_CodeEnv;
typedef struct woort_IRCompiler woort_IRCompiler;
typedef struct woort_IRFunction woort_IRFunction;
typedef struct woort_IRValue woort_IRValue;
typedef struct woort_IRLabel woort_IRLabel;

typedef uint32_t woort_IRConstantIndex;
typedef uint32_t woort_IRStaticIndex;

typedef int64_t woort_Int;
typedef double woort_Real;
typedef uint32_t woort_Bytecode;

typedef const char* woort_U8CString;

typedef void (*woort_GCHandle_UserMarkFunction)(void*);
typedef void (*woort_GCHandle_UserDestructFunction)(void*);

typedef struct woort_SourceLocation
{
    /* OPTIONAL */ const char* m_filepath;

    uint32_t m_begin_line;
    uint32_t m_begin_column;
    uint32_t m_end_line;
    uint32_t m_end_column;

} woort_SourceLocation;

/* ========== VM API ========== */

WOORT_API WOORT_NODISCARD bool woort_VMRuntime_create(
    woort_VMRuntime** out_vm);

WOORT_API void woort_VMRuntime_destroy(
    woort_VMRuntime* vm);

WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_VMRuntime* woort_VMRuntime_swap(
    /* OPTIONAL */ woort_VMRuntime* vm);

WOORT_API WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm,
    const woort_Bytecode* func);

/* ========== IR API ========== */

WOORT_API void woort_CodeEnv_drop(
    woort_CodeEnv* code_env);

WOORT_API WOORT_NODISCARD bool woort_CodeEnv_query_function(
    woort_CodeEnv* code_env,
    woort_IRFunction* f,
    const woort_Bytecode** out_f_addr);

WOORT_API void woort_CodeEnv_lock(
    woort_CodeEnv* code_env);

WOORT_API void woort_CodeEnv_unlock(
    woort_CodeEnv* code_env);

/*
 * Given a bytecode offset, find the closest matching source location.
 * bytecode_offset is relative to m_code_begin.
 * Returns true if a matching source location was found.
 */
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_find_srcloc_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location);


/*
 * Given a source location (filepath + line number), find the closest matching bytecode offset.
 * filepath can be any string pointer (internally compared using strcmp).
 * Returns true if a matching entry was found.
 */
WOORT_API WOORT_NODISCARD bool woort_CodeEnv_find_offset_by_srcloc(
    const woort_CodeEnv* env,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset);


WOORT_API WOORT_NODISCARD bool woort_CodeEnv_set_trap(
    woort_Bytecode* code);

/* ========== IR Compiler ========== */

WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRCompiler* woort_IRCompiler_create(void);

WOORT_API void woort_IRCompiler_close(
    woort_IRCompiler* c);

WOORT_API WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* c,
    uint32_t param_count,
    woort_IRFunction** out_f);

WOORT_API WOORT_NODISCARD woort_IRConstantIndex woort_IRCompiler_add_constant(
    woort_IRCompiler* c);

WOORT_API WOORT_NODISCARD woort_IRStaticIndex woort_IRCompiler_add_static(
    woort_IRCompiler* c);

WOORT_API WOORT_NODISCARD bool woort_IRCompiler_finish(
    woort_IRCompiler* c,
    woort_CodeEnv** out_cenv);

/*
 * Create a new virtual register.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_new_vreg(
    woort_IRFunction* f);

/*
 * Get the virtual register for a function parameter (pre-allocated at SB+3+idx).
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f,
    uint32_t param_idx);

/*
 * Create a new label.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ woort_IRLabel* woort_IRFunction_new_label(
    woort_IRFunction* f);

/*
 * Get a value representing the constant G[idx].
 * Multiple calls with the same const_index return the same IRValue* (naturally deduplicated).
 * The returned IRValue* has m_source set to WOORT_IRVALUE_SOURCE_CONST.
 * Returns NULL on OOM.
 */
WOORT_API WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* f,
    woort_IRConstantIndex idx);

/*
 * Push a source location onto the stack. Subsequently emitted IR instructions
 * will be associated with the top-of-stack source location.
 * filepath must be an intern pointer obtained via woort_IRCompiler_intern_string().
 * Returns false on OOM.
 */
WOORT_API WOORT_NODISCARD bool woort_IRFunction_push_srcloc(
    woort_IRFunction* f,
    /* OPTIONAL */ const char* filepath,
    uint32_t begin_line,
    uint32_t begin_column,
    uint32_t end_line,
    uint32_t end_column);

/*
 * Pop the top-of-stack source location.
 * The stack must not be empty, otherwise an assert is triggered.
 */
WOORT_API void woort_IRFunction_pop_srcloc(woort_IRFunction* f);

/* ========== Instruction Emission API ========== */

/*
 * All woort_IR_* functions append an IROp to the linear instruction list of f.
 * Returns false on OOM.
 */

 /* --- Data Movement --- */

WOORT_API WOORT_NODISCARD bool woort_IR_MOV(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_LOAD(
    woort_IRFunction* f,
    woort_IRValue* dst,
    woort_IRStaticIndex idx);

WOORT_API WOORT_NODISCARD bool woort_IR_STORE(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    const woort_IRValue* src);

/* --- Stack Operations --- */
WOORT_API WOORT_NODISCARD bool woort_IR_PUSHCHK(
    woort_IRFunction* f,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_POP(
    woort_IRFunction* f,
    woort_IRValue* dst);

WOORT_API WOORT_NODISCARD bool woort_IR_POPR(
    woort_IRFunction* f,
    uint32_t count);

WOORT_API WOORT_NODISCARD bool woort_IR_POPRS(
    woort_IRFunction* f,
    const woort_IRValue* count_src);

/* --- Type Conversions --- */

WOORT_API WOORT_NODISCARD bool woort_IR_ITOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_ITOS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_RTOI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_RTOS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_STOI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_STOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/* --- Function Calls --- */

WOORT_API WOORT_NODISCARD bool woort_IR_CALLNWO(
    woort_IRFunction* f,
    woort_IRConstantIndex target,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

WOORT_API WOORT_NODISCARD bool woort_IR_CALLNFP(
    woort_IRFunction* f,
    woort_IRConstantIndex target,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

WOORT_API WOORT_NODISCARD bool woort_IR_CALLNJIT(
    woort_IRFunction* f,
    woort_IRConstantIndex target,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

WOORT_API WOORT_NODISCARD bool woort_IR_CALL(
    woort_IRFunction* f,
    const woort_IRValue* func_val,
    uint32_t argc,
    /* OPTIONAL */ woort_IRValue* dst);

/* --- Closures / Containers --- */

WOORT_API WOORT_NODISCARD bool woort_IR_MKCLOSURE(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count,
    woort_IRConstantIndex func_idx);

WOORT_API WOORT_NODISCARD bool woort_IR_MKVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count);

WOORT_API WOORT_NODISCARD bool woort_IR_MKMAP(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t kvpair_count);

WOORT_API WOORT_NODISCARD bool woort_IR_MKSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint32_t elem_count);

/* --- Dynamic Typing --- */

WOORT_API WOORT_NODISCARD bool woort_IR_BOXDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_UNBOXDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_CHECKDYN(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t typ,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_PUSHBOXDYN(
    woort_IRFunction* f,
    uint8_t typ,
    const woort_IRValue* src);

/* --- Integer Arithmetic (dst = src1 op src2) --- */

WOORT_API WOORT_NODISCARD bool woort_IR_ADDI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_SUBI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_MULI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_DIVI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_MODI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_NEGI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/* --- Integer Comparison --- */

WOORT_API WOORT_NODISCARD bool woort_IR_LTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_GTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_LEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_GEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_EQI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_NEI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* --- Real Arithmetic --- */

WOORT_API WOORT_NODISCARD bool woort_IR_ADDR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_SUBR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_MULR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_DIVR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_MODR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_NEGR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/* --- Real Comparison --- */

WOORT_API WOORT_NODISCARD bool woort_IR_LTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_GTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_LER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_GER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_EQR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_NER(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* --- String --- */

WOORT_API WOORT_NODISCARD bool woort_IR_ADDS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_LTS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_GTS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_LES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_GES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_EQS(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_NES(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* --- Logical --- */

WOORT_API WOORT_NODISCARD bool woort_IR_LAND(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_LOR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

WOORT_API WOORT_NODISCARD bool woort_IR_LNOT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/* --- Index Load --- */

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXVECX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    uint32_t idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXSTRING(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTI(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTR(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTB(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

WOORT_API WOORT_NODISCARD bool woort_IR_LDIDXDICTX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* container,
    const woort_IRValue* idx);

/* --- Index Store (no return value) --- */

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXVECX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTII(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTIR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTIB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTIX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTRX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTBX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXDICTXX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPII(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPIR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPIB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPIX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPRX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPBX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXI(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXR(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXB(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXMAPXX(
    woort_IRFunction* f,
    woort_IRValue* c,
    const woort_IRValue* idx,
    const woort_IRValue* val);

WOORT_API WOORT_NODISCARD bool woort_IR_STIDXSTRUCT(
    woort_IRFunction* f,
    woort_IRValue* c,
    uint32_t idx,
    const woort_IRValue* val);

/* --- Unpacking --- */

WOORT_API WOORT_NODISCARD bool woort_IR_UNPACKSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_UNPACKVEC(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

WOORT_API WOORT_NODISCARD bool woort_IR_UNPACKVECX(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

/* --- Struct Field Push to Stack --- */

WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXI(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXR(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXB(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

WOORT_API WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXX(
    woort_IRFunction* f,
    const woort_IRValue* src,
    uint32_t idx);

/* ============ Control Flow ============ */

/* Bind a label to the current position */
WOORT_API WOORT_NODISCARD bool woort_IR_bind(woort_IRFunction* f, woort_IRLabel* label);

/* Unconditional jump */
WOORT_API WOORT_NODISCARD bool woort_IR_jmp(woort_IRFunction* f, woort_IRLabel* target);

/* Conditional jump: if (cond != 0) goto target */
WOORT_API WOORT_NODISCARD bool woort_IR_jcc(
    woort_IRFunction* f,
    const woort_IRValue* cond,
    woort_IRLabel* target);

/* Conditional jump: if (cond == 0) goto target */
WOORT_API WOORT_NODISCARD bool woort_IR_jccz(
    woort_IRFunction* f,
    const woort_IRValue* cond,
    woort_IRLabel* target);

/* Compare-and-jump */

WOORT_API WOORT_NODISCARD bool woort_IR_jcc_lt(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

WOORT_API WOORT_NODISCARD bool woort_IR_jcc_le(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

WOORT_API WOORT_NODISCARD bool woort_IR_jcc_eq(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

WOORT_API WOORT_NODISCARD bool woort_IR_jcc_gt(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

WOORT_API WOORT_NODISCARD bool woort_IR_jcc_ge(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

WOORT_API WOORT_NODISCARD bool woort_IR_jcc_ne(
    woort_IRFunction* f,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRLabel* target);

/* ============ Return ============ */

WOORT_API WOORT_NODISCARD bool woort_IR_ret(woort_IRFunction* f, const woort_IRValue* val);
WOORT_API WOORT_NODISCARD bool woort_IR_ret_void(woort_IRFunction* f);

/* ============ Runtime API ============ */
/*
NOTE: woort_CodeEnv_constant_set_* must be called after woort_CodeEnv_lock
*/
WOORT_API void woort_CodeEnv_set_const_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val);
WOORT_API void woort_CodeEnv_set_const_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val);
WOORT_API void woort_CodeEnv_set_const_string(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_U8CString val);
WOORT_API void woort_CodeEnv_set_const_script_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val);
WOORT_API void woort_CodeEnv_set_const_extern_function(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val);
WOORT_API void woort_CodeEnv_set_const_script_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val);
WOORT_API void woort_CodeEnv_set_const_extern_closure(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    const woort_Bytecode* val);
WOORT_API void woort_CodeEnv_set_const_box_int(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Int val);
WOORT_API void woort_CodeEnv_set_const_box_real(
    woort_CodeEnv* code_env,
    woort_IRConstantIndex cidx,
    woort_Real val);

// 
WOORT_API void woort_set_value(
    woort_StackValue dst, woort_StackValue src);
WOORT_API void woort_set_int(
    woort_StackValue dst, woort_Int src);
WOORT_API void woort_set_real(
    woort_StackValue dst, woort_Real src);
WOORT_API void woort_set_float(
    woort_StackValue dst, float src);
WOORT_API void woort_set_string(
    woort_StackValue dst, woort_U8CString src);
WOORT_API void woort_set_buffer(
    woort_StackValue dst, const void* src, size_t len);
WOORT_API void woort_set_vec(
    woort_StackValue dst, size_t cap);
WOORT_API void woort_set_map(
    woort_StackValue dst, size_t reserve);
WOORT_API void woort_set_struct(
    woort_StackValue dst, size_t cap);
WOORT_API void woort_set_gchandle(
    woort_StackValue dst, 
    void* addr,
    woort_StackValue hold, 
    woort_GCHandle_UserDestructFunction close);
WOORT_API void woort_set_gcstruct(
    woort_StackValue dst,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close);
WOORT_API void woort_set_box_int(
    woort_StackValue dst, woort_Int src);
WOORT_API void woort_set_box_real(
    woort_StackValue dst, woort_Real src);

WOORT_API WOORT_NODISCARD woort_Int woort_int(woort_StackValue src);
WOORT_API WOORT_NODISCARD woort_Real woort_real(woort_StackValue src);
WOORT_API WOORT_NODISCARD float woort_float(woort_StackValue src);
WOORT_API WOORT_NODISCARD woort_U8CString woort_string(woort_StackValue src);
WOORT_API WOORT_NODISCARD const void* woort_buffer(
    woort_StackValue src, size_t* out_len);
WOORT_API WOORT_NODISCARD void* woort_gcpointer(woort_StackValue src);

#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
