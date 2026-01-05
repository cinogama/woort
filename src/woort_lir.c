#include <stdlib.h>
#include <assert.h>

#include "woort_lir.h"
#include "woort_log.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"

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
size_t woort_LIR_ir_length_exclude_jmp(const woort_LIR* lir)
{
    const size_t UINT18_MAX = (1 << 18) - 1;
    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_LOAD:
        if (lir->m_opnums.m_LOAD.m_cs.m_is_constant)
        {
            if (lir->m_opnums.m_LOAD.m_cs.m_constant > UINT18_MAX)
                return 2;
        }
        else if (lir->m_opnums.m_LOAD.m_cs.m_static > UINT18_MAX)
            return 2;
        break;
    case WOORT_LIR_OPCODE_STORE:
        if (lir->m_opnums.m_STORE.m_s > UINT18_MAX)
            return 2;
        break;
    default:
        break;
    }
    return 1;
}

#define WOORT_LIR_EMIT_OP6M2_8_I16(opcode, m2, i16_value)   \
    do{                                                     \
        out_bytecode->m_op6m2_8_i16 =                       \
            woort_OpcodeFormal_OP6M2_8_I16_cons(            \
                opcode, m2, i16_value);                     \
    }while(0)

#define WOORT_LIR_EMIT_OP6_U26(opcode, u26_value)           \
    do{                                                     \
        out_bytecode->m_op6_u26 =                           \
            woort_OpcodeFormal_OP6_U26_cons(                \
                opcode, u26_value);                         \
    }while(0)

void woort_LIR_emit(const woort_LIR* lir, woort_Bytecode* out_bytecode)
{
    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_LOAD:
    case WOORT_LIR_OPCODE_STORE:
        abort();
    case WOORT_LIR_OPCODE_PUSH:
    {
        WOORT_LIR_EMIT_OP6M2_8_I16(
            WOORT_OPCODE_PUSH, 2, 
            lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset);

        break;
    }
    case WOORT_LIR_OPCODE_PUSHCS:
    case WOORT_LIR_OPCODE_POP:
    {
        WOORT_LIR_EMIT_OP6M2_8_I16(
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
            WOORT_LIR_EMIT_OP6_U26(
                WOORT_OPCODE_JMPGC,
                lir->m_fact_bytecode_offset - jump_target_offset);
        }
        else
        {
            // Is jump forward.
            WOORT_LIR_EMIT_OP6_U26(
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
}