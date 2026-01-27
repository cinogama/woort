#include <stdlib.h>
#include <assert.h>

#include "woort_lir.h"
#include "woort_log.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_vector.h"
#include "woort_lir_compiler.h"

#define WOORT_LIR_EMIT_BYTECODE_TO_LIST(BC)     \
    do {                                        \
        if (!woort_LIRCompiler_emit_code(       \
            modifing_compiler,                  \
            BC))                                \
        {                                       \
            return false;                       \
        }                                       \
    } while (0)

#define WOORT_LIR_EMIT_OP6_MABC26(op6, mabc26)              \
    WOORT_LIR_EMIT_BYTECODE_TO_LIST(                        \
            woort_OpcodeFormal_OP6_MABC26_cons(             \
                op6, mabc26))            

#define WOORT_LIR_EMIT_OP6_MAB18_C8(op6, mab18, c8)         \
    WOORT_LIR_EMIT_BYTECODE_TO_LIST(                        \
            woort_OpcodeFormal_OP6_MAB18_C8_cons(           \
                op6, mab18, c8))            

#define WOORT_LIR_EMIT_OP6_M2_BC16(op6, m2, bc16)           \
    WOORT_LIR_EMIT_BYTECODE_TO_LIST(                        \
        woort_OpcodeFormal_OP6_M2_BC16_cons(                \
                op6, m2, bc16))

#define WOORT_LIR_EMIT_OP6_M2_ABC24(op6, m2, abc24)         \
    WOORT_LIR_EMIT_BYTECODE_TO_LIST(                        \
        woort_OpcodeFormal_OP6_M2_ABC24_cons(               \
                op6, m2, abc24))

WOORT_NODISCARD bool woort_LIR_emit_to_lir_compiler(
    const woort_LIR* lir, struct woort_LIRCompiler* modifing_compiler)
{
    const uint64_t LOW_26_BIT_MASK = 0x3ffffffu;

    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_PUSH:
    {
        WOORT_LIR_EMIT_OP6_M2_BC16(
            WOORT_OPCODE_PUSH, 2,
            lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset);

        break;
    }
    case WOORT_LIR_OPCODE_POP:
    {
        WOORT_LIR_EMIT_OP6_M2_BC16(
            WOORT_OPCODE_POP, 2,
            lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset);

        break;
    }
    case WOORT_LIR_OPCODE_CASTITOR:
    case WOORT_LIR_OPCODE_CASTITOS:
    case WOORT_LIR_OPCODE_CASTRTOI:
    case WOORT_LIR_OPCODE_CASTRTOS:
        abort();
    case WOORT_LIR_OPCODE_JMP:
    {
        assert(lir->m_opnums.m_JMP.m_label->m_binded_lir != NULL);

        const size_t jump_target_offset =
            lir->m_opnums.m_JMP.m_label->m_binded_lir->m_fact_bytecode_offset;

        if (jump_target_offset <= lir->m_fact_bytecode_offset)
        {
            // Is jump back.
            WOORT_LIR_EMIT_OP6_MABC26(
                WOORT_OPCODE_JMPGC,
                lir->m_fact_bytecode_offset - jump_target_offset);
        }
        else
        {
            // Is jump forward.
            WOORT_LIR_EMIT_OP6_MABC26(
                WOORT_OPCODE_JMP,
                jump_target_offset - lir->m_fact_bytecode_offset);
        }
        break;
    }
    case WOORT_LIR_OPCODE_JNZ:
    case WOORT_LIR_OPCODE_JZ:
    case WOORT_LIR_OPCODE_JEQ:
    case WOORT_LIR_OPCODE_JNEQ:
    case WOORT_LIR_OPCODE_CALLNWO:
    case WOORT_LIR_OPCODE_CALLNFP:
    case WOORT_LIR_OPCODE_CALL:
    case WOORT_LIR_OPCODE_RET:
    case WOORT_LIR_OPCODE_MKARR:
    case WOORT_LIR_OPCODE_MKMAP:
    case WOORT_LIR_OPCODE_MKSTRUCT:
    case WOORT_LIR_OPCODE_MKCLOSURE:
    case WOORT_LIR_OPCODE_ADDI:
    case WOORT_LIR_OPCODE_SUBI:
    case WOORT_LIR_OPCODE_MULI:
    case WOORT_LIR_OPCODE_DIVI:
    case WOORT_LIR_OPCODE_MODI:
    case WOORT_LIR_OPCODE_NEGI:
    case WOORT_LIR_OPCODE_LTI:
    case WOORT_LIR_OPCODE_GTI:
    case WOORT_LIR_OPCODE_ELTI:
    case WOORT_LIR_OPCODE_EGTI:
    case WOORT_LIR_OPCODE_EQI:
    case WOORT_LIR_OPCODE_NEQI:
    case WOORT_LIR_OPCODE_ADDR:
    case WOORT_LIR_OPCODE_SUBR:
    case WOORT_LIR_OPCODE_MULR:
    case WOORT_LIR_OPCODE_DIVR:
    case WOORT_LIR_OPCODE_MODR:
    case WOORT_LIR_OPCODE_NEGR:
    case WOORT_LIR_OPCODE_LTR:
    case WOORT_LIR_OPCODE_GTR:
    case WOORT_LIR_OPCODE_ELTR:
    case WOORT_LIR_OPCODE_EGTR:
    case WOORT_LIR_OPCODE_EQR:
    case WOORT_LIR_OPCODE_NEQR:
    case WOORT_LIR_OPCODE_ADDS:
    case WOORT_LIR_OPCODE_LTS:
    case WOORT_LIR_OPCODE_GTS:
    case WOORT_LIR_OPCODE_ELTS:
    case WOORT_LIR_OPCODE_EGTS:
    case WOORT_LIR_OPCODE_EQS:
    case WOORT_LIR_OPCODE_NEQS:
    case WOORT_LIR_OPCODE_LOR:
    case WOORT_LIR_OPCODE_LAND:
    case WOORT_LIR_OPCODE_LNOT:
    default:
        WOORT_DEBUG("Unsupported LIR opcode in emit: %d", (int)lir->m_opcode);
        abort();
    }

    return true;
}
