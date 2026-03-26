#include "woort_ir_compiler.h"
#include "woort_ir_block.h"
#include "woort_ir_function.h"
#include "woort_diagnosis.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_opcode_builder.h"

#include <assert.h>

WOORT_NODISCARD int32_t _woort_IR_get_fact_stack_storage(int32_t place)
{
    /*
    NOTE: 考虑到对于超出 S8 索引范围的当前栈帧槽或者参数的访问，
        我们总是需要预留三个槽位(-126 -127 -128)以备使用。
    */
    if (place <= -126)
        // Make shift.
        return place - 3;

    return place;
}

WOORT_NODISCARD bool _woort_IRBlock_emit_bytecode(woort_IRBlock* b, woort_Bytecode c)
{
    return woort_vector_push_back(b, 1, &c);
}
WOORT_NODISCARD bool _woort_IRBlock_emit_bytecode_ext(woort_IRBlock* b, woort_Bytecode c, uint32_t ex)
{
    const uint32_t cs[2] = { c, ex };
    return woort_vector_push_back(b, 2, cs);
}

WOORT_NODISCARD bool _woort_IRBlock_load_value_storage8(
    woort_IRBlock* b,
    woort_IRValue* v,
    int8_t temp_slot_idx,
    int8_t* storage_8)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT8_MIN
        || fact_value_assigned_stack_offset > INT8_MAX)
    {
        // Need use extra temp storage.
        if (fact_value_assigned_stack_offset >= INT16_MIN
            && fact_value_assigned_stack_offset <= INT16_MAX)
        {
            // Use normal mov command.
            if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_MOVLD(temp_slot_idx, fact_value_assigned_stack_offset)))
                return false;
        }
        else
        {
            // Use EX mov command.
            if (!_woort_IRBlock_emit_bytecode_ext(b,
                woort_OpCode_MOVLDEXT(temp_slot_idx),
                (uint32_t)fact_value_assigned_stack_offset))
            {
                return false;
            }
        }
        *storage_8 = temp_slot_idx;
    }
    else
        *storage_8 = fact_value_assigned_stack_offset;

    return true;
}

WOORT_NODISCARD bool _woort_IRBlock_load_value_storage16(
    woort_IRBlock* b,
    woort_IRValue* v,
    int16_t temp_slot_idx,
    int16_t* storage_16)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT16_MIN
        || fact_value_assigned_stack_offset > INT16_MAX)
    {
        if (!_woort_IRBlock_emit_bytecode_ext(b,
            woort_OpCode_MOVLDEXT(temp_slot_idx),
            (uint32_t)fact_value_assigned_stack_offset))
        {
            return false;
        }
        *storage_16 = temp_slot_idx;
    }
    else
        *storage_16 = fact_value_assigned_stack_offset;

    return true;
}

typedef bool (*_woort_IRBlock_CommitCallback)(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c);

#define WOORT_UINT18_MAX ((1u << 18) - 1)
#define WOORT_UINT24_MAX ((1u << 24) - 1)

WOORT_NODISCARD bool _woort_IRBlock_commit_LOAD(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    NOTE:
    */
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STORE(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHCHK(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_POP(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_POPR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_POPRS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ITOR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ITOS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_RTOI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_RTOS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STOI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STOR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CALLNWO(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CALLNFP(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CALLNJIT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CALL(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKCLOSURE(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKVEC(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKMAP(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_BOXDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNBOXDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CHECKDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHBOXDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ADDI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SUBI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MULI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_DIVI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MODI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NEGI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LTI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GTI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LEI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GEI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_EQI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NEI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ADDR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SUBR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MULR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_DIVR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MODR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NEGR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LTR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GTR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LER(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GER(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_EQR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NER(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ADDS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LTS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GTS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LES(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GES(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_EQS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NES(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LAND(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LOR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LNOT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXVEC(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXVECX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXSTRING(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTII(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTIR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTIB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTIX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPII(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPIR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPIB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPIX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNPACKSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNPACKVEC(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNPACKVECX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    abort();
}

const _woort_IRBlock_CommitCallback _ir_op_commit_callbacks[] =
{
    _woort_IRBlock_commit_LOAD,
    _woort_IRBlock_commit_STORE,
    _woort_IRBlock_commit_PUSHCHK,
    _woort_IRBlock_commit_POP,
    _woort_IRBlock_commit_POPR,
    _woort_IRBlock_commit_POPRS,
    _woort_IRBlock_commit_ITOR,
    _woort_IRBlock_commit_ITOS,
    _woort_IRBlock_commit_RTOI,
    _woort_IRBlock_commit_RTOS,
    _woort_IRBlock_commit_STOI,
    _woort_IRBlock_commit_STOR,
    _woort_IRBlock_commit_CALLNWO,
    _woort_IRBlock_commit_CALLNFP,
    _woort_IRBlock_commit_CALLNJIT,
    _woort_IRBlock_commit_CALL,
    _woort_IRBlock_commit_MKCLOSURE,
    _woort_IRBlock_commit_MKVEC,
    _woort_IRBlock_commit_MKMAP,
    _woort_IRBlock_commit_MKSTRUCT,
    _woort_IRBlock_commit_BOXDYN,
    _woort_IRBlock_commit_UNBOXDYN,
    _woort_IRBlock_commit_CHECKDYN,
    _woort_IRBlock_commit_PUSHBOXDYN,
    _woort_IRBlock_commit_ADDI,
    _woort_IRBlock_commit_SUBI,
    _woort_IRBlock_commit_MULI,
    _woort_IRBlock_commit_DIVI,
    _woort_IRBlock_commit_MODI,
    _woort_IRBlock_commit_NEGI,
    _woort_IRBlock_commit_LTI,
    _woort_IRBlock_commit_GTI,
    _woort_IRBlock_commit_LEI,
    _woort_IRBlock_commit_GEI,
    _woort_IRBlock_commit_EQI,
    _woort_IRBlock_commit_NEI,
    _woort_IRBlock_commit_ADDR,
    _woort_IRBlock_commit_SUBR,
    _woort_IRBlock_commit_MULR,
    _woort_IRBlock_commit_DIVR,
    _woort_IRBlock_commit_MODR,
    _woort_IRBlock_commit_NEGR,
    _woort_IRBlock_commit_LTR,
    _woort_IRBlock_commit_GTR,
    _woort_IRBlock_commit_LER,
    _woort_IRBlock_commit_GER,
    _woort_IRBlock_commit_EQR,
    _woort_IRBlock_commit_NER,
    _woort_IRBlock_commit_ADDS,
    _woort_IRBlock_commit_LTS,
    _woort_IRBlock_commit_GTS,
    _woort_IRBlock_commit_LES,
    _woort_IRBlock_commit_GES,
    _woort_IRBlock_commit_EQS,
    _woort_IRBlock_commit_NES,
    _woort_IRBlock_commit_LAND,
    _woort_IRBlock_commit_LOR,
    _woort_IRBlock_commit_LNOT,
    _woort_IRBlock_commit_LDIDXVEC,
    _woort_IRBlock_commit_LDIDXVECX,
    _woort_IRBlock_commit_LDIDXSTRUCT,
    _woort_IRBlock_commit_LDIDXSTRING,
    _woort_IRBlock_commit_LDIDXDICTI,
    _woort_IRBlock_commit_LDIDXDICTR,
    _woort_IRBlock_commit_LDIDXDICTB,
    _woort_IRBlock_commit_LDIDXDICTX,
    _woort_IRBlock_commit_SDIDXVECI,
    _woort_IRBlock_commit_SDIDXVECR,
    _woort_IRBlock_commit_SDIDXVECB,
    _woort_IRBlock_commit_SDIDXVECX,
    _woort_IRBlock_commit_SDIDXDICTII,
    _woort_IRBlock_commit_SDIDXDICTIR,
    _woort_IRBlock_commit_SDIDXDICTIB,
    _woort_IRBlock_commit_SDIDXDICTIX,
    _woort_IRBlock_commit_SDIDXDICTRI,
    _woort_IRBlock_commit_SDIDXDICTRR,
    _woort_IRBlock_commit_SDIDXDICTRB,
    _woort_IRBlock_commit_SDIDXDICTRX,
    _woort_IRBlock_commit_SDIDXDICTBI,
    _woort_IRBlock_commit_SDIDXDICTBR,
    _woort_IRBlock_commit_SDIDXDICTBB,
    _woort_IRBlock_commit_SDIDXDICTBX,
    _woort_IRBlock_commit_SDIDXDICTXI,
    _woort_IRBlock_commit_SDIDXDICTXR,
    _woort_IRBlock_commit_SDIDXDICTXB,
    _woort_IRBlock_commit_SDIDXDICTXX,
    _woort_IRBlock_commit_SDIDXMAPII,
    _woort_IRBlock_commit_SDIDXMAPIR,
    _woort_IRBlock_commit_SDIDXMAPIB,
    _woort_IRBlock_commit_SDIDXMAPIX,
    _woort_IRBlock_commit_SDIDXMAPRI,
    _woort_IRBlock_commit_SDIDXMAPRR,
    _woort_IRBlock_commit_SDIDXMAPRB,
    _woort_IRBlock_commit_SDIDXMAPRX,
    _woort_IRBlock_commit_SDIDXMAPBI,
    _woort_IRBlock_commit_SDIDXMAPBR,
    _woort_IRBlock_commit_SDIDXMAPBB,
    _woort_IRBlock_commit_SDIDXMAPBX,
    _woort_IRBlock_commit_SDIDXMAPXI,
    _woort_IRBlock_commit_SDIDXMAPXR,
    _woort_IRBlock_commit_SDIDXMAPXB,
    _woort_IRBlock_commit_SDIDXMAPXX,
    _woort_IRBlock_commit_SDIDXSTRUCT,
    _woort_IRBlock_commit_UNPACKSTRUCT,
    _woort_IRBlock_commit_UNPACKVEC,
    _woort_IRBlock_commit_UNPACKVECX,
    _woort_IRBlock_commit_PUSHIDXSTRUCT,
    _woort_IRBlock_commit_PUSHIDXSTBOXI,
    _woort_IRBlock_commit_PUSHIDXSTBOXR,
    _woort_IRBlock_commit_PUSHIDXSTBOXB,
    _woort_IRBlock_commit_PUSHIDXSTBOXX,
};

_Static_assert(
    WOORT_IROP_KIND_count * sizeof(_woort_IRBlock_CommitCallback)
    == sizeof(_ir_op_commit_callbacks),
    "All case must been covered.");

WOORT_NODISCARD bool _woort_IRBlock_commit_codes(woort_IRBlock* b, woort_IRCompiler* c)
{
    /*
    STEP 1: 块起始时，先加载当前块所需载入的常量
    */
    for (size_t i = 0; i < b->m_loading_constants.m_size; ++i)
    {
        woort_IRValue* const loading_constant =
            *(woort_IRValue**)woort_vector_at(&b->m_loading_constants, i);

        assert(loading_constant->m_source == WOORT_IRVALUE_SOURCE_CONSTANT
            && loading_constant->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

        const int32_t constant_fact_stack_slot =
            _woort_IR_get_fact_stack_storage(loading_constant->m_assigned_stack_offset);

        bool loading_result = false;

        if ((constant_fact_stack_slot >= INT8_MIN && constant_fact_stack_slot <= INT8_MAX)
            && loading_constant->m_constant <= WOORT_UINT18_MAX)
        {
            loading_result = _woort_IRBlock_emit_bytecode(
                b, woort_OpCode_LOAD(loading_constant->m_constant, constant_fact_stack_slot));
        }
        else if (constant_fact_stack_slot >= INT16_MIN && constant_fact_stack_slot <= INT16_MAX)
        {
            loading_result = _woort_IRBlock_emit_bytecode_ext(
                b, woort_OpCode_LOADEX(constant_fact_stack_slot), loading_constant->m_constant);
        }
        else if (loading_constant->m_constant <= WOORT_UINT18_MAX)
        {
            loading_result = _woort_IRBlock_emit_bytecode(
                b, woort_OpCode_LOAD(loading_constant->m_constant, -128));
            loading_result = loading_result&& _woort_IRBlock_emit_bytecode_ext(
                b, woort_OpCode_MOVSTEXT(-128), (uint32_t)constant_fact_stack_slot);
        }
        else
        {
            loading_result = _woort_IRBlock_emit_bytecode_ext(
                b, woort_OpCode_LOADEX(-128), loading_constant->m_constant);
            loading_result = loading_result && _woort_IRBlock_emit_bytecode_ext(
                b, woort_OpCode_MOVSTEXT(-128), (uint32_t)constant_fact_stack_slot);
        }

        if (!loading_result)
            return false;
    }

    for (woort_IROp* ir_op = woort_linklist_iter(&b->m_operates);
        ir_op != NULL;
        ir_op = woort_linklist_next(ir_op))
    {
        assert(ir_op->m_op >= 0 && ir_op->m_op < WOORT_IROP_KIND_count);

        if (!_ir_op_commit_callbacks[ir_op->m_op](b, ir_op, c))
            return false;
    }
}

/*
在栈槽分配完成之后，根据块中的 m_operates，生成块内的字节码到 m_bytecodes_in_block 中
*/
WOORT_NODISCARD bool _woort_IRFunction_commit_codes(woort_IRFunction* f, woort_IRCompiler* c)
{
    // 分配栈槽
    size_t used_function_stack_slot_count;
    if (!_woort_IRFunction_stack_slot_assign(f, &used_function_stack_slot_count))
        return false;

    // 开始提交代码
    // 向函数的起始块开头提交 PUSHRCHK 指令
    woort_IRBlock* const entry_block = woort_IRFunction_entry_block(f);

    // TODO: 应该……用不到 U24 那么多栈空间罢
    if (!_woort_IRBlock_emit_bytecode(
        entry_block, woort_OpCode_PUSHRCHK(used_function_stack_slot_count)))
    {
        return false;
    }

    for (woort_IRBlock* b = woort_linklist_iter(&f->m_ir_blocks);
        b != NULL;
        b = woort_linklist_next(b))
    {
        if (!_woort_IRBlock_commit_codes(b, c))
        {
            return false;
        }
    }
}

void woort_IRCompiler_init(woort_IRCompiler* c)
{
    woort_linklist_init(&c->m_ir_functions, sizeof(woort_IRFunction));
    c->m_constant_alloc_count = 0;
    c->m_static_storage_alloc_count = 0;
}

void woort_IRCompiler_deinit(woort_IRCompiler* c)
{
    for (woort_IRFunction* f = woort_linklist_iter(&c->m_ir_functions);
        f != NULL;
        f = woort_linklist_next(f))
    {
        woort_IRFunction_deinit(f);
    }
    woort_linklist_deinit(&c->m_ir_functions);
}

WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* c, uint32_t param_count, woort_IRFunction** out_f)
{
    void* storage;
    if (!woort_linklist_emplace_back(&c->m_ir_functions, &storage))
        return false;

    woort_IRFunction* f = (woort_IRFunction*)storage;
    woort_IRFunction_init(f, param_count);
    *out_f = f;
    return true;
}

WOORT_NODISCARD woort_IRConstantIndex woort_IRCompiler_add_constant(woort_IRCompiler* c)
{
    return c->m_constant_alloc_count++;
}

WOORT_NODISCARD woort_IRStaticIndex woort_IRCompiler_add_static(woort_IRCompiler* c)
{
    return c->m_static_storage_alloc_count++;
}