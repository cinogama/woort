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
const size_t UINT36_MAX = ((size_t)1 << 36) - 1;
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

WOORT_NODISCARD bool _woort_LIR_is_near_stack(woort_RegisterStorageId storage)
{
    return storage >= INT8_MIN
        && storage <= INT8_MAX;
}

typedef enum _woort_LIR_ir_extern_formal
{
    WOOIR_LIR_IR_EXTERN_FORMAL_BAD,
    WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL,
    WOOIR_LIR_IR_EXTERN_FORMAL_EXTERN_COMMAND,
    WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_2,
    WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_3,
    WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_4,
}_woort_LIR_ir_extern_formal;

_woort_LIR_ir_extern_formal _woort_LIR_ir_get_cmd_extern_formal(
    const woort_LIR* lir)
{
    /*
    Special command.
    */
    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_LOAD:
    {
        const woort_RegisterStorageId register_stack_offset =
            lir->m_opnums.m_LOAD.m_r->m_assigned_bp_offset;
        const uint64_t data_index =
            lir->m_opnums.m_LOAD.m_cs.m_is_constant
            ? lir->m_opnums.m_LOAD.m_cs.m_constant
            : lir->m_opnums.m_LOAD.m_cs.m_static;

        if (data_index <= UINT18_MAX
            && register_stack_offset >= INT8_MIN
            && register_stack_offset <= INT8_MAX)
            // Use fast normal formal.
            return WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL;

        // Use extern formal.
        return WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL;
    }
    case WOORT_LIR_OPCODE_STORE:
    {
        const woort_RegisterStorageId register_stack_offset =
            lir->m_opnums.m_STORE.m_r->m_assigned_bp_offset;
        const uint64_t data_index = lir->m_opnums.m_STORE.m_s;

        if (data_index <= UINT18_MAX
            && register_stack_offset >= INT8_MIN
            && register_stack_offset <= INT8_MAX)
            // Use fast normal formal.
            return WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL;

        // Use extern formal.
        return WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL;
    }
    default:
        break;
    }

    /*
    Far register load.
    */
    size_t far_register_count = 0;
    switch (lir->m_opnum_formal)
    {
    case WOORT_LIR_OPNUMFORMAL_CS_R:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_cs_r.m_r->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_S_R:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_s_r.m_r->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r.m_r->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R:
        if (!_woort_LIR_is_near_stack(lir->m_opnums.m_r_r.m_r1->m_assigned_bp_offset))
            ++far_register_count;
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r.m_r2->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R_R:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_r.m_r1->m_assigned_bp_offset))
            ++far_register_count;
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_r.m_r2->m_assigned_bp_offset))
            ++far_register_count;
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_r.m_r3->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_COUNT16:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_count16.m_r->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R_COUNT16:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_count16.m_r1->m_assigned_bp_offset))
            ++far_register_count;
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_count16.m_r2->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_LABEL:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_label.m_r->m_assigned_bp_offset))
            ++far_register_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R_LABEL:
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_label.m_r1->m_assigned_bp_offset))
            ++far_register_count;
        if (!_woort_LIR_is_near_stack(
            lir->m_opnums.m_r_r_label.m_r2->m_assigned_bp_offset))
            ++far_register_count;
        break;
    default:
        break;
    }

    switch (far_register_count)
    {
    case 0:
        return WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL;
    case 1:
        return WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_2;
    case 2:
        return WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_3;
    case 3:
        return WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_4;
    default:
        WOORT_DEBUG("Too much far register count: %d.", (int)far_register_count);
        return WOOIR_LIR_IR_EXTERN_FORMAL_BAD;
    }

    // Cannot be here.
    abort();
}

WOORT_NODISCARD size_t woort_LIR_ir_length_exclude_jmp(const woort_LIR* lir)
{
    const _woort_LIR_ir_extern_formal f =
        _woort_LIR_ir_get_cmd_extern_formal(lir);

    switch (f)
    {
    case WOOIR_LIR_IR_EXTERN_FORMAL_BAD:
        break;
    case WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL:
        return 1;
    case WOOIR_LIR_IR_EXTERN_FORMAL_EXTERN_COMMAND:
    case WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_2:
        return 2;
    case WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_3:
        return 3;
    case WOOIR_LIR_IR_EXTERN_FORMAL_COMMAND_4:
        return 4;
    default:
        WOORT_DEBUG("Bad ir extern formal: %d.", (int)f);
        break;
    }
    return 0;
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

//WOORT_NODISCARD woort_RegisterStorageId woort_LIR_preload_register_to_read(
//    struct woort_LIRCompiler* modifing_compiler,
//    const woort_LIRRegister* r,
//    woort_RegisterStorageId regid)
//{
//    if (_woort_LIR_is_near_stack(r->m_assigned_bp_offset))
//        return r->m_assigned_bp_offset;
//    else
//    {
//        WOORT_LIR_EMIT_BYTECODE_TO_LIST(
//            woort_OpCodeFormal_cons(
//                OP6_M2_A8_BC16,
//                WOORT_OPCODE_MOV,
//                1,
//                regid,
//                r->m_assigned_bp_offset));
//
//        return regid;
//    }
//}
//
//WOORT_NODISCARD woort_RegisterStorageId woort_LIR_preload_register_to_write(
//    struct woort_LIRCompiler* modifing_compiler,
//    const woort_LIRRegister* r,
//    woort_RegisterStorageId regid)
//{
//    if (_woort_LIR_is_near_stack(r->m_assigned_bp_offset))
//        return r->m_assigned_bp_offset;
//    else
//        return regid;
//}
//
//WOORT_NODISCARD bool woort_LIR_apply_register_to_write(
//    struct woort_LIRCompiler* modifing_compiler,
//    const woort_LIRRegister* r,
//    woort_RegisterStorageId regid)
//{
//    if (!_woort_LIR_is_near_stack(r->m_assigned_bp_offset))
//    {
//        WOORT_LIR_EMIT_BYTECODE_TO_LIST(
//            woort_OpCodeFormal_cons(
//                OP6_M2_A8_BC16,
//                WOORT_OPCODE_MOV,
//                0,
//                regid,
//                r->m_assigned_bp_offset));
//    }
//}

WOORT_NODISCARD bool woort_LIR_emit_to_lir_compiler(
    const woort_LIR* lir, struct woort_LIRCompiler* modifing_compiler)
{
    const uint64_t LOW_26_BIT_MASK = 0x3ffffffu;

    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_LOAD:
    {
        const int16_t register_stack_offset =
            lir->m_opnums.m_LOAD.m_r->m_assigned_bp_offset;
        const uint64_t data_index =
            lir->m_opnums.m_LOAD.m_cs.m_is_constant
            ? lir->m_opnums.m_LOAD.m_cs.m_constant
            : lir->m_opnums.m_LOAD.m_cs.m_static;

        const _woort_LIR_ir_extern_formal f =
            _woort_LIR_ir_get_cmd_extern_formal(lir);

        switch (f)
        {
        case WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL:
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MAB18_C8,
                    WOORT_OPCODE_LOAD,
                    data_index,
                    register_stack_offset));
            break;
        case WOOIR_LIR_IR_EXTERN_FORMAL_EXTERN_COMMAND:
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MA10_BC16,
                    WOORT_OPCODE_LOADEX,
                    (data_index & ~LOW_26_BIT_MASK) >> 26,
                    register_stack_offset));
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MABC26,
                    WOORT_OPCODE_NOP,
                    data_index & LOW_26_BIT_MASK));
            break;
        default:
            WOORT_DEBUG("Bad ir extern formal: %d for lir LOAD.", (int)f);
            break;
        }
        break;
    }
    case WOORT_LIR_OPCODE_STORE:
    {
        const int16_t register_stack_offset =
            lir->m_opnums.m_STORE.m_r->m_assigned_bp_offset;
        const uint64_t data_index = lir->m_opnums.m_STORE.m_s;

        const _woort_LIR_ir_extern_formal f =
            _woort_LIR_ir_get_cmd_extern_formal(lir);

        switch (f)
        {
        case WOOIR_LIR_IR_EXTERN_FORMAL_NORMAL:
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MAB18_C8,
                    WOORT_OPCODE_STORE,
                    data_index,
                    register_stack_offset));
            break;
        case WOOIR_LIR_IR_EXTERN_FORMAL_EXTERN_COMMAND:
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MA10_BC16,
                    WOORT_OPCODE_STOREEX,
                    (data_index & ~LOW_26_BIT_MASK) >> 26,
                    register_stack_offset));
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MABC26,
                    WOORT_OPCODE_NOP,
                    data_index & LOW_26_BIT_MASK));
            break;
        default:
            WOORT_DEBUG("Bad ir extern formal: %d for lir STORE.", (int)f);
            break;
        }

        break;
    }
    case WOORT_LIR_OPCODE_PUSH:
    {
        WOORT_LIR_EMIT_BYTECODE_TO_LIST(
            woort_OpCodeFormal_cons(
                OP6_M2_BC16,
                WOORT_OPCODE_PUSH, 2,
                lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset));

        break;
    }
    case WOORT_LIR_OPCODE_PUSHCS:
    case WOORT_LIR_OPCODE_POP:
    {
        WOORT_LIR_EMIT_BYTECODE_TO_LIST(
            woort_OpCodeFormal_cons(
                OP6_M2_BC16,
                WOORT_OPCODE_POP, 2,
                lir->m_opnums.m_PUSH.m_r->m_assigned_bp_offset));

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
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MABC26,
                    WOORT_OPCODE_JMPGC,
                    lir->m_fact_bytecode_offset - jump_target_offset));
        }
        else
        {
            // Is jump forward.
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_MABC26,
                    WOORT_OPCODE_JMP,
                    jump_target_offset - lir->m_fact_bytecode_offset));
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
        abort();
    case WOORT_LIR_OPCODE_ADDI:
    {
       /* _WOORT_PREFETCH_OPNUM_ABT(lir->m_opnums.m_ADDI);

        if (t == a)
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_M2_B8_C8,
                    WOORT_OPCODE_OPCIASMD, 0, b, t));
        else if (t == b)
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_M2_B8_C8,
                    WOORT_OPCODE_OPCIASMD, 0, a, t));
        else
            WOORT_LIR_EMIT_BYTECODE_TO_LIST(
                woort_OpCodeFormal_cons(
                    OP6_M2_A8_B8_C8,
                    WOORT_OPCODE_OPIASMD, 0, a, b, t));

        _WOORT_APPLY_OPNUM_T(lir->m_opnums.m_ADDI);*/

        break;
    }
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
        abort();
    default:
        WOORT_DEBUG("Unsupported LIR opcode in emit: %d", (int)lir->m_opcode);
        abort();
    }

    return true;
}
