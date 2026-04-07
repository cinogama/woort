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

typedef enum woort_BoxValueType
{
    WOORT_BOX_VALUE_TYPE_GCUNIT = 0b000,
    WOORT_BOX_VALUE_TYPE_REAL = 0b001,
    WOORT_BOX_VALUE_TYPE_INT = 0b010,
    WOORT_BOX_VALUE_TYPE_BOOL = 0b100,

    ////
    WOORT_BOX_VALUE_TYPE_NIL = 0b1000,
    WOORT_BOX_VALUE_TYPE_STRING,
    WOORT_BOX_VALUE_TYPE_VEC,
    WOORT_BOX_VALUE_TYPE_MAP,
    WOORT_BOX_VALUE_TYPE_STRUCT,
    WOORT_BOX_VALUE_TYPE_GCHANDLE,
    WOORT_BOX_VALUE_TYPE_CLOSURE,

} woort_BoxValueType;

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
WOORT_API void woort_set_nil(
    woort_StackValue dst);
#define woort_set_void woort_set_nil
WOORT_API void woort_set_int(
    woort_StackValue dst, woort_Int src);
WOORT_API void woort_set_real(
    woort_StackValue dst, woort_Real src);
WOORT_API void woort_set_float(
    woort_StackValue dst, float src);
WOORT_API void woort_set_bool(
    woort_StackValue dst, bool src);
WOORT_API void woort_set_string(
    woort_StackValue dst, woort_U8CString src);
WOORT_API void woort_set_buffer(
    woort_StackValue dst, const void* src, size_t len);
WOORT_API void woort_set_vec(
    woort_StackValue dst);
WOORT_API void woort_set_map(
    woort_StackValue dst);
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
WOORT_API void woort_set_box_bool(
    woort_StackValue dst, bool src);

WOORT_API void woort_set_union_without_value(
    woort_StackValue dst, woort_Int id);
WOORT_API void woort_set_union_value(
    woort_StackValue dst, woort_Int id, woort_StackValue val);
WOORT_API void woort_set_union_nil(
    woort_StackValue dst, woort_Int id);
#define woort_set_union_void woort_set_union_nil
WOORT_API void woort_set_union_int(
    woort_StackValue dst, woort_Int id, woort_Int src);
WOORT_API void woort_set_union_real(
    woort_StackValue dst, woort_Int id, woort_Real src);
WOORT_API void woort_set_union_float(
    woort_StackValue dst, woort_Int id, float src);
WOORT_API void woort_set_union_bool(
    woort_StackValue dst, woort_Int id, bool src);
WOORT_API void woort_set_union_string(
    woort_StackValue dst, woort_Int id, woort_U8CString src);
WOORT_API void woort_set_union_buffer(
    woort_StackValue dst, woort_Int id, const void* src, size_t len);
WOORT_API void woort_set_union_gchandle(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_StackValue hold,
    woort_GCHandle_UserDestructFunction close);
WOORT_API void woort_set_union_gcstruct(
    woort_StackValue dst,
    woort_Int id,
    void* addr,
    woort_GCHandle_UserMarkFunction mark,
    woort_GCHandle_UserDestructFunction close);
WOORT_API void woort_set_union_box_int(
    woort_StackValue dst, woort_Int id, woort_Int src);
WOORT_API void woort_set_union_box_real(
    woort_StackValue dst, woort_Int id, woort_Real src);
WOORT_API void woort_set_union_box_bool(
    woort_StackValue dst, woort_Int id, bool src);

#define woort_set_option_none(dst) woort_set_union_without_value(dst, 1)

#define woort_set_option_value(dst, src) woort_set_union_value(dst, 0, src)
#define woort_set_option_nil(dst) woort_set_union_nil(dst, 0)
#define woort_set_option_void(dst) woort_set_union_void(dst, 0)
#define woort_set_option_int(dst, src) woort_set_union_int(dst, 0, src)
#define woort_set_option_real(dst, src) woort_set_union_real(dst, 0, src)
#define woort_set_option_float(dst, src) woort_set_union_float(dst, 0, src)
#define woort_set_option_bool(dst, src) woort_set_union_bool(dst, 0, src)
#define woort_set_option_string(dst, src) woort_set_union_string(dst, 0, src)
#define woort_set_option_buffer(dst, src, len) woort_set_union_buffer(dst, 0, src, len)
#define woort_set_option_box_int(dst, src) woort_set_union_box_int(dst, 0, src)
#define woort_set_option_box_real(dst, src) woort_set_union_box_real(dst, 0, src)
#define woort_set_option_box_bool(dst, src) woort_set_union_box_bool(dst, 0, src)
#define woort_set_option_gchandle(dst, addr, hold, close) \
    woort_set_union_gchandle(dst, 0, addr, hold, close)
#define woort_set_option_gcstruct(dst, addr, mark, close) \
    woort_set_union_gcstruct(dst, 0, addr, mark, close)

/* ========== Result Ok/Err ========== */
#define woort_set_result_ok_value woort_set_option_value
#define woort_set_result_ok_nil woort_set_option_nil
#define woort_set_result_ok_void woort_set_option_void
#define woort_set_result_ok_int woort_set_option_int
#define woort_set_result_ok_real woort_set_option_real
#define woort_set_result_ok_float woort_set_option_float
#define woort_set_result_ok_bool woort_set_option_bool
#define woort_set_result_ok_string woort_set_option_string
#define woort_set_result_ok_buffer woort_set_option_buffer
#define woort_set_result_ok_box_int woort_set_option_box_int
#define woort_set_result_ok_box_real woort_set_option_box_real
#define woort_set_result_ok_box_bool woort_set_option_box_bool
#define woort_set_result_ok_gchandle woort_set_option_gchandle
#define woort_set_result_ok_gcstruct woort_set_option_gcstruct

#define woort_set_result_err_value(dst, src) woort_set_union_value(dst, 1, src)
#define woort_set_result_err_nil(dst) woort_set_union_nil(dst, 1)
#define woort_set_result_err_void(dst) woort_set_union_void(dst, 1)
#define woort_set_result_err_int(dst, src) woort_set_union_int(dst, 1, src)
#define woort_set_result_err_real(dst, src) woort_set_union_real(dst, 1, src)
#define woort_set_result_err_float(dst, src) woort_set_union_float(dst, 1, src)
#define woort_set_result_err_bool(dst, src) woort_set_union_bool(dst, 1, src)
#define woort_set_result_err_string(dst, src) woort_set_union_string(dst, 1, src)
#define woort_set_result_err_buffer(dst, src, len) woort_set_union_buffer(dst, 1, src, len)
#define woort_set_result_err_box_int(dst, src) woort_set_union_box_int(dst, 1, src)
#define woort_set_result_err_box_real(dst, src) woort_set_union_box_real(dst, 1, src)
#define woort_set_result_err_box_bool(dst, src) woort_set_union_box_bool(dst, 1, src)
#define woort_set_result_err_gchandle(dst, addr, hold, close) \
    woort_set_union_gchandle(dst, 1, addr, hold, close)
#define woort_set_result_err_gcstruct(dst, addr, mark, close) \
    woort_set_union_gcstruct(dst, 1, addr, mark, close)

/* ========== Return ========== */

#define woort_ret_value(src) (woort_set_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_void() WOORT_VM_CALL_STATUS_NORMAL
#define woort_ret_nil() (woort_set_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_int(src) (woort_set_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_real(src) (woort_set_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_float(src) (woort_set_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_bool(src) (woort_set_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_string(src) (woort_set_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_buffer(src, len) (woort_set_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_box_int(src) (woort_set_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_box_real(src) (woort_set_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_box_bool(src) (woort_set_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_gchandle(addr, hold, close) \
    (woort_set_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_gcstruct(addr, mark, close) \
    (woort_set_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/* --- Return Union --- */

#define woort_ret_union_without_value(id) (woort_set_union_without_value(-1, id), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_value(id, src) (woort_set_union_value(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_nil(id) (woort_set_union_nil(-1, id), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_void woort_ret_union_nil
#define woort_ret_union_int(id, src) (woort_set_union_int(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_real(id, src) (woort_set_union_real(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_float(id, src) (woort_set_union_float(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_bool(id, src) (woort_set_union_bool(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_string(id, src) (woort_set_union_string(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_buffer(id, src, len) (woort_set_union_buffer(-1, id, src, len), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_box_int(id, src) (woort_set_union_box_int(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_box_real(id, src) (woort_set_union_box_real(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_box_bool(id, src) (woort_set_union_box_bool(-1, id, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_gchandle(id, addr, hold, close) \
    (woort_set_union_gchandle(-1, id, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_union_gcstruct(id, addr, mark, close) \
    (woort_set_union_gcstruct(-1, id, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/* --- Return Option --- */

#define woort_ret_option_none() (woort_set_option_none(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_value(src) (woort_set_option_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_nil() (woort_set_option_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_void() (woort_set_option_void(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_int(src) (woort_set_option_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_real(src) (woort_set_option_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_float(src) (woort_set_option_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_bool(src) (woort_set_option_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_string(src) (woort_set_option_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_buffer(src, len) (woort_set_option_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_box_int(src) (woort_set_option_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_box_real(src) (woort_set_option_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_box_bool(src) (woort_set_option_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_gchandle(addr, hold, close) \
    (woort_set_option_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_option_gcstruct(addr, mark, close) \
    (woort_set_option_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/* --- Return Result Ok --- */
#define woort_ret_result_ok_value(src) (woort_set_result_ok_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_nil() (woort_set_result_ok_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_void() (woort_set_result_ok_void(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_int(src) (woort_set_result_ok_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_real(src) (woort_set_result_ok_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_float(src) (woort_set_result_ok_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_bool(src) (woort_set_result_ok_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_string(src) (woort_set_result_ok_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_buffer(src, len) (woort_set_result_ok_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_box_int(src) (woort_set_result_ok_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_box_real(src) (woort_set_result_ok_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_box_bool(src) (woort_set_result_ok_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_gchandle(addr, hold, close) \
    (woort_set_result_ok_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_ok_gcstruct(addr, mark, close) \
    (woort_set_result_ok_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/* --- Return Result Err --- */
#define woort_ret_result_err_value(src) (woort_set_result_err_value(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_nil() (woort_set_result_err_nil(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_void() (woort_set_result_err_void(-1), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_int(src) (woort_set_result_err_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_real(src) (woort_set_result_err_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_float(src) (woort_set_result_err_float(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_bool(src) (woort_set_result_err_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_string(src) (woort_set_result_err_string(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_buffer(src, len) (woort_set_result_err_buffer(-1, src, len), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_box_int(src) (woort_set_result_err_box_int(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_box_real(src) (woort_set_result_err_box_real(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_box_bool(src) (woort_set_result_err_box_bool(-1, src), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_gchandle(addr, hold, close) \
    (woort_set_result_err_gchandle(-1, addr, hold, close), WOORT_VM_CALL_STATUS_NORMAL)
#define woort_ret_result_err_gcstruct(addr, mark, close) \
    (woort_set_result_err_gcstruct(-1, addr, mark, close), WOORT_VM_CALL_STATUS_NORMAL)

/* Read. */
WOORT_API WOORT_NODISCARD woort_Int woort_int(woort_StackValue src);
WOORT_API WOORT_NODISCARD woort_Real woort_real(woort_StackValue src);
WOORT_API WOORT_NODISCARD float woort_float(woort_StackValue src);
WOORT_API WOORT_NODISCARD bool woort_bool(woort_StackValue src);
WOORT_API WOORT_NODISCARD woort_U8CString woort_string(woort_StackValue src);
WOORT_API WOORT_NODISCARD const void* woort_buffer(
    woort_StackValue src, size_t* out_len);
WOORT_API WOORT_NODISCARD void* woort_gcpointer(woort_StackValue src);
WOORT_API WOORT_NODISCARD woort_Int woort_unbox_int(woort_StackValue src);
WOORT_API WOORT_NODISCARD woort_Real woort_unbox_real(woort_StackValue src);
WOORT_API WOORT_NODISCARD bool woort_unbox_bool(woort_StackValue src);

WOORT_API WOORT_NODISCARD woort_BoxValueType woort_unbox_type(
    woort_StackValue src);

WOORT_API WOORT_NODISCARD woort_Int woort_union_get(
    woort_StackValue dst, woort_StackValue src);

#define woort_option_get(dst, src) \
    (0 == woort_union_get(dst, src))
#define woort_result_get(dst, src) \
    (0 == woort_union_get(dst, src))

/* ========== Vector ========== */

/* --- Vector Capacity --- */

WOORT_API WOORT_NODISCARD size_t woort_vec_len(
    woort_StackValue src);
WOORT_API void woort_vec_resize(
    woort_StackValue src, size_t new_size);

/* --- Vector Element Access --- */

WOORT_API void woort_vec_get(
    woort_StackValue dst,
    woort_StackValue src,
    size_t index);

WOORT_API void woort_vec_get_box(
    woort_StackValue dst_boxed,
    woort_StackValue src,
    size_t index);

/* --- Vector Modifiers --- */

WOORT_API void woort_vec_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue elem,
    woort_BoxValueType type);

WOORT_API void woort_vec_set_box(
    woort_StackValue src,
    size_t index,
    woort_StackValue boxed_elem);

WOORT_API void woort_vec_push(
    woort_StackValue src,
    woort_StackValue elem,
    woort_BoxValueType type);

WOORT_API void woort_vec_push_box(
    woort_StackValue src,
    woort_StackValue boxed_elem);

WOORT_API void woort_vec_pop(woort_StackValue src);

WOORT_API void woort_vec_insert(
    woort_StackValue src,
    size_t index,
    woort_StackValue elem,
    woort_BoxValueType type);

WOORT_API void woort_vec_insert_box(
    woort_StackValue src,
    size_t index,
    woort_StackValue boxed_elem);

WOORT_API void woort_vec_erase(
    woort_StackValue src,
    size_t index);

WOORT_API void woort_vec_clear(woort_StackValue src);

/* ========== Mapping ========== */

/* --- Mapping Capacity --- */

WOORT_API WOORT_NODISCARD size_t woort_map_len(woort_StackValue src);

WOORT_API void woort_map_reserve(
    woort_StackValue src,
    size_t reserve);

/* --- Mapping Lookup --- */

/*
 * Lookup key in map. If found, write value to dst and return true.
 * If not found, return false (dst is unmodified).
 */
WOORT_API WOORT_NODISCARD bool woort_map_get(
    woort_StackValue dst,
    woort_StackValue src,
    woort_StackValue key);

/*
 * Lookup by int key. If found, write value to dst and return true.
 */
WOORT_API WOORT_NODISCARD bool woort_map_get_int(
    woort_StackValue dst,
    woort_StackValue src,
    woort_Int key);

/*
 * Lookup by real key. If found, write value to dst and return true.
 */
WOORT_API WOORT_NODISCARD bool woort_map_get_real(
    woort_StackValue dst,
    woort_StackValue src,
    woort_Real key);

/*
 * Lookup by bool key. If found, write value to dst and return true.
 */
WOORT_API WOORT_NODISCARD bool woort_map_get_bool(
    woort_StackValue dst,
    woort_StackValue src,
    bool key);

/*
 * Lookup by string key. If found, write value to dst and return true.
 */
WOORT_API WOORT_NODISCARD bool woort_map_get_string(
    woort_StackValue dst,
    woort_StackValue src,
    woort_U8CString key);

/* --- Mapping Insert / Update --- */

/*
 * Insert or update: map[key] = val.
 * Returns true if the key was newly inserted, false if an existing key was updated.
 */
WOORT_API WOORT_NODISCARD bool woort_map_set(
    woort_StackValue src,
    woort_StackValue boxed_key,
    woort_StackValue boxed_val);

WOORT_API WOORT_NODISCARD bool woort_map_set_int(
    woort_StackValue src,
    woort_Int key,
    woort_StackValue boxed_val);

WOORT_API WOORT_NODISCARD bool woort_map_set_real(
    woort_StackValue src,
    woort_Real key,
    woort_StackValue boxed_val);

WOORT_API WOORT_NODISCARD bool woort_map_set_bool(
    woort_StackValue src,
    bool key,
    woort_StackValue boxed_val);

WOORT_API WOORT_NODISCARD bool woort_map_set_string(
    woort_StackValue src,
    woort_U8CString key,
    woort_StackValue boxed_val);

/* --- Mapping Erase --- */

/*
 * Erase a key-value pair from the map. Returns true if the key existed and was removed.
 */
WOORT_API WOORT_NODISCARD bool woort_map_erase(
    woort_StackValue src,
    woort_StackValue boxed_key);

WOORT_API WOORT_NODISCARD bool woort_map_erase_int(
    woort_StackValue src,
    woort_Int key);

WOORT_API WOORT_NODISCARD bool woort_map_erase_real(
    woort_StackValue src,
    woort_Real key);

WOORT_API WOORT_NODISCARD bool woort_map_erase_bool(
    woort_StackValue src,
    bool key);

WOORT_API WOORT_NODISCARD bool woort_map_erase_string(
    woort_StackValue src,
    woort_U8CString key);

/* --- Mapping Contains --- */

WOORT_API WOORT_NODISCARD bool woort_map_contains(
    woort_StackValue src,
    woort_StackValue boxed_key);

WOORT_API WOORT_NODISCARD bool woort_map_contains_int(
    woort_StackValue src,
    woort_Int key);

WOORT_API WOORT_NODISCARD bool woort_map_contains_real(
    woort_StackValue src,
    woort_Real key);

WOORT_API WOORT_NODISCARD bool woort_map_contains_bool(
    woort_StackValue src,
    bool key);

WOORT_API WOORT_NODISCARD bool woort_map_contains_string(
    woort_StackValue src,
    woort_U8CString key);

/* --- Mapping Iteration --- */

/*
 * Retrieve the key-value pair at the given iterator index.
 * Returns false if the index is out of range or the slot is empty (tombstone).
 * On success, writes key to out_key and value to out_val.
 */
WOORT_API WOORT_NODISCARD bool woort_map_iter(
    woort_StackValue src,
    size_t index,
    woort_StackValue out_boxed_key,
    woort_StackValue out_boxed_val);

/* ========== Struct ========== */

/* --- Struct Capacity --- */

WOORT_API WOORT_NODISCARD size_t woort_struct_len(
    woort_StackValue src);

/* --- Struct Field Access --- */

WOORT_API void woort_struct_get(
    woort_StackValue dst,
    woort_StackValue src,
    size_t index);

WOORT_API void woort_struct_set(
    woort_StackValue src,
    size_t index,
    woort_StackValue val);

#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
