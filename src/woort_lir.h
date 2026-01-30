#pragma once

/*
woort_lir.h
*/

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_vector.h"

typedef int16_t woort_RegisterStorageId;

// Register.
typedef struct woort_LIRRegister
{
    /* NOTE: SIZE_MAX means not active */
    size_t m_alive_range[2];

    /* Used in finalized only. */
    woort_RegisterStorageId m_assigned_bp_offset;

}woort_LIRRegister;

// Label.
typedef struct woort_LIRLabel
{
#ifndef NDEBUG
    struct woort_LIRFunction* m_function;
#endif
    struct woort_LIR* m_binded_lir;

}woort_LIRLabel;

// Constant.
typedef uint64_t woort_LIR_ConstantStorage;

// Static storage.
typedef uint64_t woort_LIR_StaticStorage;

typedef struct woort_LIR_CS
{
    bool m_is_constant;
    union {
        woort_LIR_ConstantStorage m_constant;
        woort_LIR_StaticStorage   m_static;
    };

}woort_LIR_CS;

typedef enum woort_LIR_OpnumFormal
{
    WOORT_LIR_OPNUMFORMAL_CS,
    WOORT_LIR_OPNUMFORMAL_CS_R,
    WOORT_LIR_OPNUMFORMAL_S_R,
    WOORT_LIR_OPNUMFORMAL_R,
    WOORT_LIR_OPNUMFORMAL_R_R,
    WOORT_LIR_OPNUMFORMAL_R_R_R,
    WOORT_LIR_OPNUMFORMAL_R_R_COUNT16,
    WOORT_LIR_OPNUMFORMAL_R_COUNT16,
    WOORT_LIR_OPNUMFORMAL_R_R_LABEL,
    WOORT_LIR_OPNUMFORMAL_R_LABEL,
    WOORT_LIR_OPNUMFORMAL_LABEL,

} woort_LIR_OpnumFormal;

typedef struct woort_LIR_OpnumFormal_CS
{
    woort_LIR_CS m_cs;

} woort_LIR_OpnumFormal_CS;

typedef struct woort_LIR_OpnumFormal_CS_R
{
    woort_LIR_CS m_cs;
    woort_LIRRegister* m_r;

} woort_LIR_OpnumFormal_CS_R;

typedef struct woort_LIR_OpnumFormal_S_R
{
    woort_LIR_StaticStorage m_s;
    woort_LIRRegister* m_r;

} woort_LIR_OpnumFormal_S_R;

typedef struct woort_LIR_OpnumFormal_R
{
    woort_LIRRegister* m_r;

} woort_LIR_OpnumFormal_R;

typedef struct woort_LIR_OpnumFormal_R_R
{
    woort_LIRRegister* m_r1;
    woort_LIRRegister* m_r2;

} woort_LIR_OpnumFormal_R_R;

typedef struct woort_LIR_OpnumFormal_R_R_R
{
    woort_LIRRegister* m_r1;
    woort_LIRRegister* m_r2;
    woort_LIRRegister* m_r3;

} woort_LIR_OpnumFormal_R_R_R;

typedef struct woort_LIR_OpnumFormal_R_R_COUNT16
{
    woort_LIRRegister* m_r1;
    woort_LIRRegister* m_r2;
    uint16_t m_count16;

} woort_LIR_OpnumFormal_R_R_COUNT16;

typedef struct woort_LIR_OpnumFormal_R_COUNT16
{
    woort_LIRRegister* m_r;
    uint16_t m_count16;

} woort_LIR_OpnumFormal_R_COUNT16;

typedef struct woort_LIR_OpnumFormal_R_LABEL
{
    woort_LIRRegister* m_r;
    woort_LIRLabel* m_label;

    /* NOTE: If JZ or JNZ jumped too far, this flag will be marked */
    bool m_externed;

} woort_LIR_OpnumFormal_R_LABEL;

typedef struct woort_LIR_OpnumFormal_R_R_LABEL
{
    woort_LIRRegister* m_r1;
    woort_LIRRegister* m_r2;
    woort_LIRLabel* m_label;

    /* NOTE: If JZ or JNZ jumped too far, this flag will be marked */
    bool m_externed;

} woort_LIR_OpnumFormal_R_R_LABEL;

typedef struct woort_LIR_OpnumFormal_LABEL
{
    woort_LIRLabel* m_label;

} woort_LIR_OpnumFormal_LABEL;

/*
Checklist:
    When adding a new LIR instruction, besides adding the corresponding
enum value here, you also need to:


1. Add the corresponding WOORT_LIR_OPNUM_FORMAL_XXXX format macro to
    confirm the instruction's operands.

2. Add the corresponding union member in woort_LIR_Opnums.

3. Add the emission for the corresponding instruction in woort_LIR_emit.

4. If the instruction involves jump labels, you need to prepare far label
    handling in _woort_LIRCompiler_commit_function.

5. If the instruction introduces a new operand form involving registers,
    you need to add register live range marking for that form in
    _woort_LIRFunction_register_allocation.

6. Add the corresponding function declaration and definition for
    woort_LIRFunction_emit_XXXX.
*/
typedef enum woort_LIR_Opcode
{
    WOORT_LIR_OPCODE_LOAD,
    WOORT_LIR_OPCODE_STORE,
    WOORT_LIR_OPCODE_PUSH,
    WOORT_LIR_OPCODE_PUSHCS,
    WOORT_LIR_OPCODE_POP,
    WOORT_LIR_OPCODE_POPCS,
    WOORT_LIR_OPCODE_CASTITOR,
    WOORT_LIR_OPCODE_CASTITOS,
    WOORT_LIR_OPCODE_CASTRTOI,
    WOORT_LIR_OPCODE_CASTRTOS,
    WOORT_LIR_OPCODE_JMP,
    WOORT_LIR_OPCODE_JNZ,
    WOORT_LIR_OPCODE_JZ,
    WOORT_LIR_OPCODE_JEQ,
    WOORT_LIR_OPCODE_JNEQ,
    WOORT_LIR_OPCODE_CALLNWO,
    WOORT_LIR_OPCODE_CALLNFP,
    WOORT_LIR_OPCODE_CALL,
    WOORT_LIR_OPCODE_RET,
    WOORT_LIR_OPCODE_MKARR,
    WOORT_LIR_OPCODE_MKMAP,
    WOORT_LIR_OPCODE_MKSTRUCT,
    WOORT_LIR_OPCODE_MKCLOSURE,
    WOORT_LIR_OPCODE_ADDI,
    WOORT_LIR_OPCODE_SUBI,
    WOORT_LIR_OPCODE_MULI,
    WOORT_LIR_OPCODE_DIVI,
    WOORT_LIR_OPCODE_MODI,
    WOORT_LIR_OPCODE_NEGI,
    WOORT_LIR_OPCODE_LTI,
    WOORT_LIR_OPCODE_GTI,
    WOORT_LIR_OPCODE_ELTI,
    WOORT_LIR_OPCODE_EGTI,
    WOORT_LIR_OPCODE_EQI,
    WOORT_LIR_OPCODE_NEQI,
    WOORT_LIR_OPCODE_ADDR,
    WOORT_LIR_OPCODE_SUBR,
    WOORT_LIR_OPCODE_MULR,
    WOORT_LIR_OPCODE_DIVR,
    WOORT_LIR_OPCODE_MODR,
    WOORT_LIR_OPCODE_NEGR,
    WOORT_LIR_OPCODE_LTR,
    WOORT_LIR_OPCODE_GTR,
    WOORT_LIR_OPCODE_ELTR,
    WOORT_LIR_OPCODE_EGTR,
    WOORT_LIR_OPCODE_EQR,
    WOORT_LIR_OPCODE_NEQR,
    WOORT_LIR_OPCODE_ADDS,
    WOORT_LIR_OPCODE_LTS,
    WOORT_LIR_OPCODE_GTS,
    WOORT_LIR_OPCODE_ELTS,
    WOORT_LIR_OPCODE_EGTS,
    WOORT_LIR_OPCODE_EQS,
    WOORT_LIR_OPCODE_NEQS,
    WOORT_LIR_OPCODE_LOR,
    WOORT_LIR_OPCODE_LAND,
    WOORT_LIR_OPCODE_LNOT,

} woort_LIR_Opcode;

#define WOORT_LIR_OPNUM_FORMAL_LOAD CS_R
#define WOORT_LIR_OPNUM_FORMAL_STORE S_R
#define WOORT_LIR_OPNUM_FORMAL_PUSH R
#define WOORT_LIR_OPNUM_FORMAL_PUSHCS CS
#define WOORT_LIR_OPNUM_FORMAL_POP R
#define WOORT_LIR_OPNUM_FORMAL_POPCS CS
#define WOORT_LIR_OPNUM_FORMAL_CASTITOR R_R
#define WOORT_LIR_OPNUM_FORMAL_CASTITOS R_R
#define WOORT_LIR_OPNUM_FORMAL_CASTRTOI R_R
#define WOORT_LIR_OPNUM_FORMAL_CASTRTOS R_R
#define WOORT_LIR_OPNUM_FORMAL_JMP LABEL
#define WOORT_LIR_OPNUM_FORMAL_JNZ R_LABEL
#define WOORT_LIR_OPNUM_FORMAL_JZ R_LABEL
#define WOORT_LIR_OPNUM_FORMAL_JEQ R_R_LABEL
#define WOORT_LIR_OPNUM_FORMAL_JNEQ R_R_LABEL
#define WOORT_LIR_OPNUM_FORMAL_CALLNWO R_R
#define WOORT_LIR_OPNUM_FORMAL_CALLNFP R_R
#define WOORT_LIR_OPNUM_FORMAL_CALL R_R
#define WOORT_LIR_OPNUM_FORMAL_RET R_COUNT16
#define WOORT_LIR_OPNUM_FORMAL_MKARR R_COUNT16
#define WOORT_LIR_OPNUM_FORMAL_MKMAP R_COUNT16
#define WOORT_LIR_OPNUM_FORMAL_MKSTRUCT R_COUNT16
#define WOORT_LIR_OPNUM_FORMAL_MKCLOSURE R_R_COUNT16
#define WOORT_LIR_OPNUM_FORMAL_ADDI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_SUBI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_MULI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_DIVI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_MODI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_NEGI R_R
#define WOORT_LIR_OPNUM_FORMAL_LTI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_GTI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_ELTI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_EGTI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_EQI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_NEQI R_R_R
#define WOORT_LIR_OPNUM_FORMAL_ADDR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_SUBR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_MULR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_DIVR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_MODR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_NEGR R_R
#define WOORT_LIR_OPNUM_FORMAL_LTR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_GTR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_ELTR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_EGTR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_EQR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_NEQR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_ADDS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_LTS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_GTS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_ELTS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_EGTS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_EQS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_NEQS R_R_R
#define WOORT_LIR_OPNUM_FORMAL_LOR R_R_R
#define WOORT_LIR_OPNUM_FORMAL_LAND R_R_R
#define WOORT_LIR_OPNUM_FORMAL_LNOT R_R

#define _WOORT_LIR_FORMAL_T(FORMAL)\
    woort_LIR_OpnumFormal_##FORMAL
#define WOORT_LIR_FORMAL_T(FORMAL)\
    _WOORT_LIR_FORMAL_T(FORMAL)
#define WOORT_LIR_OP_FORMAL_T(LIROP)\
    WOORT_LIR_FORMAL_T(WOORT_LIR_OPNUM_FORMAL_##LIROP)

#define _WOORT_LIR_FORMAL_KIND(FORMAL)\
    WOORT_LIR_OPNUMFORMAL_##FORMAL
#define WOORT_LIR_FORMAL_KIND(FORMAL)\
    _WOORT_LIR_FORMAL_KIND(FORMAL)
#define WOORT_LIR_OP_FORMAL_KIND(LIROP)\
    WOORT_LIR_FORMAL_KIND(WOORT_LIR_OPNUM_FORMAL_##LIROP)

#define WOORT_LIR_OPNUM_FORMAL_DEFINE(LIROP)\
    WOORT_LIR_OP_FORMAL_T(LIROP) m_##LIROP

typedef union woort_LIR_Opnums
{
    woort_LIR_OpnumFormal_CS m_cs;
    woort_LIR_OpnumFormal_CS_R m_cs_r;
    woort_LIR_OpnumFormal_S_R m_s_r;
    woort_LIR_OpnumFormal_R m_r;
    woort_LIR_OpnumFormal_R_R m_r_r;
    woort_LIR_OpnumFormal_R_R_R m_r_r_r;
    woort_LIR_OpnumFormal_R_R_COUNT16 m_r_r_count16;
    woort_LIR_OpnumFormal_R_COUNT16 m_r_count16;
    woort_LIR_OpnumFormal_R_LABEL m_r_label;
    woort_LIR_OpnumFormal_R_R_LABEL m_r_r_label;
    woort_LIR_OpnumFormal_LABEL m_label;

    WOORT_LIR_OPNUM_FORMAL_DEFINE(LOAD);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(STORE);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(PUSH);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(PUSHCS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(POP);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(POPCS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CASTITOR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CASTITOS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CASTRTOI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CASTRTOS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(JMP);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(JNZ);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(JZ);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(JEQ);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(JNEQ);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CALLNWO);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CALLNFP);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(CALL);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(RET);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MKARR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MKMAP);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MKSTRUCT);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MKCLOSURE);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(ADDI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(SUBI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MULI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(DIVI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MODI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(NEGI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(LTI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(GTI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(ELTI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(EGTI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(EQI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(NEQI);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(ADDR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(SUBR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MULR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(DIVR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(MODR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(NEGR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(LTR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(GTR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(ELTR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(EGTR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(EQR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(NEQR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(ADDS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(LTS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(GTS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(ELTS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(EGTS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(EQS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(NEQS);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(LOR);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(LAND);
    WOORT_LIR_OPNUM_FORMAL_DEFINE(LNOT);

} woort_LIR_Opnums;

#undef WOORT_LIR_OPNUM_FORMAL_DEFINE

typedef struct woort_LIR
{
    woort_LIR_Opcode        m_opcode;
    woort_LIR_OpnumFormal   m_opnum_formal;
    woort_LIR_Opnums        m_opnums;

    /*
    `m_fact_bytecode_offset` is populated as the absolute instruction location (in units of 4 bytes)
    when the function IR is submitted to the code generator. This value is updated during the submission
    process (e.g., when encountering long-range address jumps, additional instructions need to be inserted,
    which may cause changes to this offset).
    */
    size_t                  m_fact_bytecode_offset;

}woort_LIR;

/*
NOTE: Since static storage shares the same addressing mechanism as constants, this method must be used to
    update the offset of the static storage space.
*/
void woort_LIR_update_static_storage(woort_LIR* lir, size_t constant_count);
/*
NOTE: This method is used by the ir-compiler when submitting a function to calculate the IR length required
    for each LIR, but temporarily does not consider the extra length expansion introduced by conditional
    jump instructions during long-range jumps: these will be calculated later.

    Return 0 means bad.
*/
WOORT_NODISCARD /* May 0 if failed. */ size_t woort_LIR_ir_length_exclude_jmp(
    const woort_LIR* lir);

WOORT_NODISCARD bool woort_LIR_emit_to_lir_compiler(
    const woort_LIR* lir, struct woort_LIRCompiler* modifing_compiler);
