#include <stdlib.h>
#include <assert.h>

#include "woort_lir.h"
#include "woort_log.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_vector.h"
#include "woort_lir_compiler.h"

const size_t UINT18_MAX = ((size_t)1 << 18) - 1;
const size_t UINT24_MAX = ((size_t)1 << 24) - 1;
const size_t UINT26_MAX = ((size_t)1 << 26) - 1;
const size_t UINT44_MAX = ((size_t)1 << 44) - 1;
const size_t UINT50_MAX = ((size_t)1 << 50) - 1;

void woort_LIR_update_static_storage(
    woort_LIR* lir, size_t constant_count)
{
    switch (lir->m_opnum_formal)
    {
    case WOORT_LIR_OPNUMFORMAL_CS:
        if (!lir->m_opnums.m_cs.m_cs.m_is_constant)
            lir->m_opnums.m_cs.m_cs.m_static += constant_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_CS_R:
        if (!lir->m_opnums.m_cs_r.m_cs.m_is_constant)
            lir->m_opnums.m_cs_r.m_cs.m_static += constant_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_S_R:
        lir->m_opnums.m_s_r.m_s += constant_count;
        break;
    default:
        // No static storage to update.
        break;
    }
}
WOORT_NODISCARD size_t woort_LIR_ir_length_exclude_jmp(const woort_LIR* lir)
{
    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_LOAD:
    {
        const uint16_t register_stack_offset =
            lir->m_opnums.m_LOAD.m_r->m_assigned_bp_offset;
        const uint64_t data_index =
            lir->m_opnums.m_LOAD.m_cs.m_is_constant
            ? lir->m_opnums.m_LOAD.m_cs.m_constant
            : lir->m_opnums.m_LOAD.m_cs.m_static;

        if (data_index <= UINT18_MAX)
        {
            if (register_stack_offset >= INT8_MIN
                && register_stack_offset <= INT8_MAX)
                // Normal
                break;
        }
        else
        {
            if (register_stack_offset >= INT8_MIN
                && register_stack_offset <= INT8_MAX
                && data_index <= UINT44_MAX)
                // Far way, LOADEXT.
                return 2;
        }

        // Very slow, use push & pop.
        if (data_index <= UINT24_MAX)
            return 2;
        else if (data_index <= UINT50_MAX)
            return 3;
        else
        {
            WOORT_DEBUG("Constant/static addressing out of range.");
            return 0;
        }
        break;
    }
    case WOORT_LIR_OPCODE_STORE:
        if (lir->m_opnums.m_STORE.m_s > UINT18_MAX)
            return 2;
        break;
    default:
        break;
    }
    return 1;
}

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
    case WOORT_LIR_OPCODE_LOAD:
    {
        const uint16_t register_stack_offset =
            lir->m_opnums.m_LOAD.m_r->m_assigned_bp_offset;
        const uint64_t data_index =
            lir->m_opnums.m_LOAD.m_cs.m_is_constant
            ? lir->m_opnums.m_LOAD.m_cs.m_constant
            : lir->m_opnums.m_LOAD.m_cs.m_static;

        if (data_index <= UINT18_MAX)
        {
            if (register_stack_offset >= INT8_MIN
                && register_stack_offset <= INT8_MAX)
            {
                // Fast way.
                WOORT_LIR_EMIT_OP6_MAB18_C8(
                    WOORT_OPCODE_LOAD,
                    data_index,
                    register_stack_offset);

                break;
            }
        }
        else
        {
            if (register_stack_offset >= INT8_MIN
                && register_stack_offset <= INT8_MAX
                && data_index <= UINT44_MAX)
            {
                // Fast way.
                WOORT_LIR_EMIT_OP6_MAB18_C8(
                    WOORT_OPCODE_LOADEX,
                    (data_index & ~LOW_26_BIT_MASK) >> 26,
                    register_stack_offset);
                WOORT_LIR_EMIT_OP6_MABC26(
                    WOORT_OPCODE_NOP,
                    data_index & LOW_26_BIT_MASK);

                break;
            }
        }

        // Slow way.
        if (data_index <= UINT24_MAX)
        {
            WOORT_LIR_EMIT_OP6_M2_ABC24(
                WOORT_OPCODE_PUSH, 1, data_index);
        }
        else if (data_index <= UINT50_MAX)
        {
            WOORT_LIR_EMIT_OP6_M2_ABC24(
                WOORT_OPCODE_PUSH, 3, (data_index & ~LOW_26_BIT_MASK) >> 26);
            WOORT_LIR_EMIT_OP6_MABC26(
                WOORT_OPCODE_NOP,
                data_index & LOW_26_BIT_MASK);
        }
        else
        {
            WOORT_DEBUG("Constant/static addressing out of range.");
            return false;
        }

        WOORT_LIR_EMIT_OP6_M2_BC16(
            WOORT_OPCODE_POP, 2, register_stack_offset);
        break;
    }
    case WOORT_LIR_OPCODE_STORE:
    {
        const uint16_t register_stack_offset =
            lir->m_opnums.m_STORE.m_r->m_assigned_bp_offset;
        const uint64_t data_index = lir->m_opnums.m_STORE.m_s;

        if (data_index <= UINT18_MAX)
        {
            if (register_stack_offset >= INT8_MIN
                && register_stack_offset <= INT8_MAX)
            {
                // Fast way.
                WOORT_LIR_EMIT_OP6_MAB18_C8(
                    WOORT_OPCODE_STORE,
                    data_index,
                    register_stack_offset);

                break;
            }
        }
        else
        {
            if (register_stack_offset >= INT8_MIN
                && register_stack_offset <= INT8_MAX
                && data_index <= UINT44_MAX)
            {
                // Fast way.
                WOORT_LIR_EMIT_OP6_MAB18_C8(
                    WOORT_OPCODE_STOREEX,
                    (data_index & ~LOW_26_BIT_MASK) >> 26,
                    register_stack_offset);
                WOORT_LIR_EMIT_OP6_MABC26(
                    WOORT_OPCODE_NOP,
                    data_index & LOW_26_BIT_MASK);

                break;
            }
        }

        // Slow way.
        WOORT_LIR_EMIT_OP6_M2_BC16(
            WOORT_OPCODE_PUSH, 2, register_stack_offset);

        if (data_index <= UINT24_MAX)
        {
            WOORT_LIR_EMIT_OP6_M2_ABC24(
                WOORT_OPCODE_POP, 1, data_index);
        }
        else if (data_index <= UINT50_MAX)
        {
            WOORT_LIR_EMIT_OP6_M2_ABC24(
                WOORT_OPCODE_POP, 3, (data_index & ~LOW_26_BIT_MASK) >> 26);
            WOORT_LIR_EMIT_OP6_MABC26(
                WOORT_OPCODE_NOP,
                data_index & LOW_26_BIT_MASK);
        }
        else
        {
            WOORT_DEBUG("Constant/static addressing out of range.");
            return false;
        }

        break;
    }
    case WOORT_LIR_OPCODE_PUSH:
    {
        WOORT_LIR_EMIT_OP6_M2_BC16(
            WOORT_OPCODE_PUSH, 2,
            lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset);

        break;
    }
    case WOORT_LIR_OPCODE_PUSHCS:
    case WOORT_LIR_OPCODE_POP:
    {
        WOORT_LIR_EMIT_OP6_M2_BC16(
            WOORT_OPCODE_POP, 2,
            lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset);

        break;
    }
    case WOORT_LIR_OPCODE_POPCS:
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
