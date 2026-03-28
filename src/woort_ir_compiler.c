#include "woort_ir_compiler.h"
#include "woort_ir_block.h"
#include "woort_ir_function.h"
#include "woort_diagnosis.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_opcode_builder.h"

#include <assert.h>
#include <stdlib.h>

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
    return woort_vector_push_back(&b->m_bytecodes_in_block, 1, &c);
}
WOORT_NODISCARD bool _woort_IRBlock_emit_bytecode_ext(woort_IRBlock* b, woort_Bytecode c, uint32_t ex)
{
    const uint32_t cs[2] = { c, ex };
    return woort_vector_push_back(&b->m_bytecodes_in_block, 2, cs);
}

WOORT_NODISCARD bool _woort_IRBlock_load_value_storage8(
    woort_IRBlock* b,
    const woort_IRValue* v,
    int8_t temp_slot_idx,
    int8_t* storage_8)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT8_MIN || fact_value_assigned_stack_offset > INT8_MAX)
    {
        // Need use extra temp storage.
        if (fact_value_assigned_stack_offset >= INT16_MIN && fact_value_assigned_stack_offset <= INT16_MAX)
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
    const woort_IRValue* v,
    int8_t temp_slot_idx,
    int16_t* storage_16)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT16_MIN || fact_value_assigned_stack_offset > INT16_MAX)
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

WOORT_NODISCARD int8_t _woort_IRBlock_get_place_to_store_value_storage8(
    const woort_IRValue* v,
    int8_t temp_slot_idx)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT8_MIN || fact_value_assigned_stack_offset > INT8_MAX)
    {
        return temp_slot_idx;
    }
    return fact_value_assigned_stack_offset;
}

WOORT_NODISCARD int16_t _woort_IRBlock_get_place_to_store_value_storage16(
    const woort_IRValue* v,
    int8_t temp_slot_idx)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT16_MIN || fact_value_assigned_stack_offset > INT16_MAX)
    {
        return temp_slot_idx;
    }
    return fact_value_assigned_stack_offset;
}

WOORT_NODISCARD bool _woort_IRBlock_apply_store_value(
    woort_IRBlock* b,
    const woort_IRValue* v,
    int32_t storage)
{
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    /*
    如果 storage 和实际栈偏移相同，说明指令已经直接写入了目标位置，
    无需额外搬运。
    */
    if (fact_value_assigned_stack_offset == storage)
        return true;

    /*
    storage 是临时槽（-126/-127/-128，一定在 S8 范围内），
    需要将值从临时槽搬运到 v 的实际栈位置。
    */
    assert(storage == -126 || storage == -127 || storage == -128);

    if (fact_value_assigned_stack_offset >= INT16_MIN && fact_value_assigned_stack_offset <= INT16_MAX)
    {
        /* 目标在 S16 范围内，使用 MOVST [SB + bc16] = [SB + a8] */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_MOVST((int8_t)storage, (int16_t)fact_value_assigned_stack_offset));
    }
    else
    {
        /* 目标超出 S16 范围，使用 MOVSTEXT [SB + ex32] = [SB + bc16] */
        return _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MOVSTEXT((int16_t)storage), (uint32_t)fact_value_assigned_stack_offset);
    }
}

typedef bool (*_woort_IRBlock_CommitCallback)(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c);

#define WOORT_UINT10_MAX ((1u << 10) - 1)
#define WOORT_UINT18_MAX ((1u << 18) - 1)
#define WOORT_UINT24_MAX ((1u << 24) - 1)
#define WOORT_UINT26_MAX ((1u << 26) - 1)
#define WOORT_UINT8_MAX ((1u << 8) - 1)

WOORT_NODISCARD bool _woort_IRBlock_commit_LOAD_op(
    woort_IRBlock* b,
    int32_t fact_stack_slot,
    uint32_t constant_storage)
{
    bool loading_result = false;

    if ((fact_stack_slot >= INT8_MIN && fact_stack_slot <= INT8_MAX) && constant_storage <= WOORT_UINT18_MAX)
    {
        /*
        Case 1: 栈槽在 S8 范围内，常量索引在 U18 范围内
        使用 LOAD [SB + c8] = G[mab18] 单条紧凑指令完成加载
        */
        loading_result = _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_LOAD(constant_storage, fact_stack_slot));
    }
    else if (fact_stack_slot >= INT16_MIN && fact_stack_slot <= INT16_MAX)
    {
        /*
        Case 2: 栈槽在 S16 范围内（包括 S8 但常量索引超过 U18 的情况）
        使用 LOADEX [SB + bc16] = G[ex32] 扩展指令完成加载
        */
        loading_result = _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_LOADEX(fact_stack_slot), (uint32_t)constant_storage);
    }
    else if (constant_storage <= WOORT_UINT18_MAX)
    {
        /*
        Case 3: 栈槽超出 S16 范围，但常量索引在 U18 范围内
        先用 LOAD [SB + -128] = G[mab18] 加载到临时槽
        再用 MOVSTEXT [SB + ex32] = [SB + -128] 移动到目标槽
        */
        loading_result = _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_LOAD(constant_storage, -128));
        loading_result = loading_result && _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MOVSTEXT(-128), (uint32_t)fact_stack_slot);
    }
    else
    {
        /*
        Case 4: 栈槽超出 S16 范围，常量索引也超出 U18 范围
        先用 LOADEX [SB + -128] = G[ex32] 加载到临时槽
        再用 MOVSTEXT [SB + ex32] = [SB + -128] 移动到目标槽
        */
        loading_result = _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_LOADEX(-128), constant_storage);
        loading_result = loading_result && _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MOVSTEXT(-128), (uint32_t)fact_stack_slot);
    }

    return loading_result;
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STORE_op(
    woort_IRBlock* b,
    int32_t fact_stack_slot,
    uint32_t constant_storage)
{
    bool storing_result = false;

    if ((fact_stack_slot >= INT8_MIN && fact_stack_slot <= INT8_MAX) && constant_storage <= WOORT_UINT18_MAX)
    {
        /*
        Case 1: 栈槽在 S8 范围内，常量索引在 U18 范围内
        使用 STORE G[mab18] = [SB + c8] 单条紧凑指令完成存储
        */
        storing_result = _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORE(constant_storage, fact_stack_slot));
    }
    else if (fact_stack_slot >= INT16_MIN && fact_stack_slot <= INT16_MAX)
    {
        /*
        Case 2: 栈槽在 S16 范围内（包括 S8 但常量索引超过 U18 的情况）
        使用 STOREEX G[ex32] = [SB + bc16] 扩展指令完成存储
        */
        storing_result = _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_STOREEX(fact_stack_slot), constant_storage);
    }
    else if (constant_storage <= WOORT_UINT18_MAX)
    {
        /*
        Case 3: 栈槽超出 S16 范围，但常量索引在 U18 范围内
        先用 MOVLDEXT [SB + -128] = [SB + ex32] 将源值移动到临时槽
        再用 STORE G[mab18] = [SB + -128] 从临时槽存储到全局
        */
        storing_result = _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MOVLDEXT(-128), (uint32_t)fact_stack_slot);
        storing_result = storing_result && _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORE(constant_storage, -128));
    }
    else
    {
        /*
        Case 4: 栈槽超出 S16 范围，常量索引也超出 U18 范围
        先用 MOVLDEXT [SB + -128] = [SB + ex32] 将源值移动到临时槽
        再用 STOREEX G[ex32] = [SB + -128] 从临时槽存储到全局
        */
        storing_result = _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MOVLDEXT(-128), (uint32_t)fact_stack_slot);
        storing_result = storing_result && _woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_STOREEX(-128), constant_storage);
    }

    return storing_result;
}

WOORT_NODISCARD bool _woort_IRBlock_commit_LOAD(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    NOTE: IR-Operate 的 LOAD & STORE 是用于处理静态存储的，不需要考虑常量
    */
    const uint32_t storage_place = op->m_static_index + c->m_constant_alloc_count;
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    return _woort_IRBlock_commit_LOAD_op(b, w, storage_place);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STORE(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    NOTE: IR-Operate 的 LOAD & STORE 是用于处理静态存储的，不需要考虑常量
    STORE 从栈槽 (m_r[0]) 读取值，写入静态存储 (m_static_index)
    */
    const uint32_t storage_place = op->m_static_index + c->m_constant_alloc_count;
    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);

    return _woort_IRBlock_commit_STORE_op(b, r, storage_place);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHCHK(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    if (op->m_r[0]->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
    {
        // Have stack slot, use PUSHSCHK
        int16_t r;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
            return false;

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHSCHK(r)))
            return false;
    }
    else
    {
        // Use PUSHCCHK
        assert(op->m_r[0]->m_source == WOORT_IRVALUE_SOURCE_CONSTANT && !op->m_r[0]->m_constant_need_stack_slot);

        if (op->m_r[0]->m_constant <= WOORT_UINT24_MAX)
        {
            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_PUSHCCHK(op->m_r[0]->m_constant)))
                return false;
        }
        else
        {
            if (!_woort_IRBlock_emit_bytecode_ext(
                b, woort_OpCode_PUSHCCHKEXT(), (uint32_t)op->m_r[0]->m_constant))
                return false;
        }
    }
    return true;
}
WOORT_NODISCARD bool _woort_IRBlock_commit_POP(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    const int16_t w =
        _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -128);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_POPS(w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_POPR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    assert(op->m_count <= WOORT_UINT24_MAX);

    return _woort_IRBlock_emit_bytecode(
        b, woort_OpCode_POPR(op->m_count));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_POPRS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;

    return _woort_IRBlock_emit_bytecode(
        b, woort_OpCode_POPRS(r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ITOR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    ITOR: 整数转实数
    m_r[0] = 源值（整数），m_w = 目标值（实数）

    ITORST (mode=0): [SB + a8(源)] -> [SB + bc16(目标)]   源S8, 目标S16
    ITORLD (mode=1): [SB + bc16(源)] -> [SB + a8(目标)]   源S16, 目标S8

    优先根据原始操作数范围选择变体，避免不必要的 MOV 搬运。
    */

    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 ITORST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITORST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX)
    {
        /* Case 2: 目标在 S8，源在 S16，使用 ITORLD，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITORLD((int8_t)w, (int16_t)r));
    }

    /*
    Case 3: ITORST / ITORLD 均无法直接编码两个操作数，
    需要根据各操作数的实际范围细分处理。
    */

    if (r >= INT8_MIN && r <= INT8_MAX)
    {
        /*
        Case 3a: 源在 S8，目标超出 S16
        源天然满足 ITORST 的 a8 要求，只需将目标搬入临时 S16 槽。
        */
        const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ITORST((int8_t)r, w16)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }

    if (w >= INT8_MIN && w <= INT8_MAX)
    {
        /*
        Case 3b: 目标在 S8，源超出 S16
        目标天然满足 ITORLD 的 a8 要求，只需将源搬入临时 S16 槽。
        */
        int16_t r16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r16))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITORLD((int8_t)w, r16));
    }

    if (w >= INT16_MIN && w <= INT16_MAX)
    {
        /*
        Case 3c: 目标在 S16（但不在 S8），源不在 S8（也不在 S16 或更大）
        目标天然满足 ITORST 的 bc16 要求，只需将源搬入临时 S8 槽。
        */
        int8_t r8;
        if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITORST(r8, (int16_t)w));
    }

    if (r >= INT16_MIN && r <= INT16_MAX)
    {
        /*
        Case 3d: 源在 S16（但不在 S8），目标超出 S16（也不在 S8）
        源天然满足 ITORLD 的 bc16 要求，只需将目标搬入临时 S8 槽。
        */
        const int8_t w8 = _woort_IRBlock_get_place_to_store_value_storage8(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ITORLD(w8, (int16_t)r)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w8);
    }

    /*
    Case 3e: 源和目标均超出 S16
    两个操作数都需要搬运，将源搬入临时 S8 槽，目标使用临时 S16 槽，
    使用 ITORST 完成转换后再将结果搬回目标实际位置。
    */
    int8_t r8;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
        return false;

    const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ITORST(r8, w16)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ITOS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    ITOS: 整数转字符串
    m_r[0] = 源值（整数），m_w = 目标值（字符串）

    ITOSST (mode=2): [SB + a8(源)] -> [SB + bc16(目标)]   源S8, 目标S16
    ITOSLD (mode=3): [SB + bc16(源)] -> [SB + a8(目标)]   源S16, 目标S8

    优先根据原始操作数范围选择变体，避免不必要的 MOV 搬运。
    */

    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 ITOSST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITOSST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX)
    {
        /* Case 2: 目标在 S8，源在 S16，使用 ITOSLD，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITOSLD((int8_t)w, (int16_t)r));
    }

    /*
    Case 3: ITOSST / ITOSLD 均无法直接编码两个操作数，
    需要根据各操作数的实际范围细分处理。
    */

    if (r >= INT8_MIN && r <= INT8_MAX)
    {
        /*
        Case 3a: 源在 S8，目标超出 S16
        源天然满足 ITOSST 的 a8 要求，只需将目标搬入临时 S16 槽。
        */
        const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ITOSST((int8_t)r, w16)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }

    if (w >= INT8_MIN && w <= INT8_MAX)
    {
        /*
        Case 3b: 目标在 S8，源超出 S16
        目标天然满足 ITOSLD 的 a8 要求，只需将源搬入临时 S16 槽。
        */
        int16_t r16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r16))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITOSLD((int8_t)w, r16));
    }

    if (w >= INT16_MIN && w <= INT16_MAX)
    {
        /*
        Case 3c: 目标在 S16（但不在 S8），源不在 S8
        目标天然满足 ITOSST 的 bc16 要求，只需将源搬入临时 S8 槽。
        */
        int8_t r8;
        if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITOSST(r8, (int16_t)w));
    }

    if (r >= INT16_MIN && r <= INT16_MAX)
    {
        /*
        Case 3d: 源在 S16（但不在 S8），目标超出 S16
        源天然满足 ITOSLD 的 bc16 要求，只需将目标搬入临时 S8 槽。
        */
        const int8_t w8 = _woort_IRBlock_get_place_to_store_value_storage8(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ITOSLD(w8, (int16_t)r)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w8);
    }

    /*
    Case 3e: 源和目标均超出 S16
    将源搬入临时 S8 槽，目标使用临时 S16 槽，
    使用 ITOSST 完成转换后再将结果搬回目标实际位置。
    */
    int8_t r8;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
        return false;

    const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ITOSST(r8, w16)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_RTOI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    RTOI: 实数转整数
    m_r[0] = 源值（实数），m_w = 目标值（整数）

    RTOIST (mode=0): [SB + a8(源)] -> [SB + bc16(目标)]   源S8, 目标S16
    RTOILD (mode=1): [SB + bc16(源)] -> [SB + a8(目标)]   源S16, 目标S8

    优先根据原始操作数范围选择变体，避免不必要的 MOV 搬运。
    */

    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 RTOIST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOIST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX)
    {
        /* Case 2: 目标在 S8，源在 S16，使用 RTOILD，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOILD((int8_t)w, (int16_t)r));
    }

    /*
    Case 3: RTOIST / RTOILD 均无法直接编码两个操作数，
    需要根据各操作数的实际范围细分处理。
    */

    if (r >= INT8_MIN && r <= INT8_MAX)
    {
        /*
        Case 3a: 源在 S8，目标超出 S16
        源天然满足 RTOIST 的 a8 要求，只需将目标搬入临时 S16 槽。
        */
        const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RTOIST((int8_t)r, w16)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }

    if (w >= INT8_MIN && w <= INT8_MAX)
    {
        /*
        Case 3b: 目标在 S8，源超出 S16
        目标天然满足 RTOILD 的 a8 要求，只需将源搬入临时 S16 槽。
        */
        int16_t r16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r16))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOILD((int8_t)w, r16));
    }

    if (w >= INT16_MIN && w <= INT16_MAX)
    {
        /*
        Case 3c: 目标在 S16（但不在 S8），源不在 S8
        目标天然满足 RTOIST 的 bc16 要求，只需将源搬入临时 S8 槽。
        */
        int8_t r8;
        if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOIST(r8, (int16_t)w));
    }

    if (r >= INT16_MIN && r <= INT16_MAX)
    {
        /*
        Case 3d: 源在 S16（但不在 S8），目标超出 S16
        源天然满足 RTOILD 的 bc16 要求，只需将目标搬入临时 S8 槽。
        */
        const int8_t w8 = _woort_IRBlock_get_place_to_store_value_storage8(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RTOILD(w8, (int16_t)r)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w8);
    }

    /*
    Case 3e: 源和目标均超出 S16
    将源搬入临时 S8 槽，目标使用临时 S16 槽，
    使用 RTOIST 完成转换后再将结果搬回目标实际位置。
    */
    int8_t r8;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
        return false;

    const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RTOIST(r8, w16)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_RTOS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    RTOS: 实数转字符串
    m_r[0] = 源值（实数），m_w = 目标值（字符串）

    RTOSST (mode=2): [SB + a8(源)] -> [SB + bc16(目标)]   源S8, 目标S16
    RTOSLD (mode=3): [SB + bc16(源)] -> [SB + a8(目标)]   源S16, 目标S8

    优先根据原始操作数范围选择变体，避免不必要的 MOV 搬运。
    */

    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 RTOSST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOSST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX)
    {
        /* Case 2: 目标在 S8，源在 S16，使用 RTOSLD，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOSLD((int8_t)w, (int16_t)r));
    }

    /*
    Case 3: RTOSST / RTOSLD 均无法直接编码两个操作数，
    需要根据各操作数的实际范围细分处理。
    */

    if (r >= INT8_MIN && r <= INT8_MAX)
    {
        /*
        Case 3a: 源在 S8，目标超出 S16
        源天然满足 RTOSST 的 a8 要求，只需将目标搬入临时 S16 槽。
        */
        const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RTOSST((int8_t)r, w16)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }

    if (w >= INT8_MIN && w <= INT8_MAX)
    {
        /*
        Case 3b: 目标在 S8，源超出 S16
        目标天然满足 RTOSLD 的 a8 要求，只需将源搬入临时 S16 槽。
        */
        int16_t r16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r16))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOSLD((int8_t)w, r16));
    }

    if (w >= INT16_MIN && w <= INT16_MAX)
    {
        /*
        Case 3c: 目标在 S16（但不在 S8），源不在 S8
        目标天然满足 RTOSST 的 bc16 要求，只需将源搬入临时 S8 槽。
        */
        int8_t r8;
        if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOSST(r8, (int16_t)w));
    }

    if (r >= INT16_MIN && r <= INT16_MAX)
    {
        /*
        Case 3d: 源在 S16（但不在 S8），目标超出 S16
        源天然满足 RTOSLD 的 bc16 要求，只需将目标搬入临时 S8 槽。
        */
        const int8_t w8 = _woort_IRBlock_get_place_to_store_value_storage8(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RTOSLD(w8, (int16_t)r)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w8);
    }

    /*
    Case 3e: 源和目标均超出 S16
    将源搬入临时 S8 槽，目标使用临时 S16 槽，
    使用 RTOSST 完成转换后再将结果搬回目标实际位置。
    */
    int8_t r8;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
        return false;

    const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RTOSST(r8, w16)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STOI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    STOI: 字符串转整数
    m_r[0] = 源值（字符串），m_w = 目标值（整数）

    STOIST (mode=0): [SB + a8(源)] -> [SB + bc16(目标)]   源S8, 目标S16
    STOILD (mode=1): [SB + bc16(源)] -> [SB + a8(目标)]   源S16, 目标S8

    优先根据原始操作数范围选择变体，避免不必要的 MOV 搬运。
    */

    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 STOIST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STOIST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX)
    {
        /* Case 2: 目标在 S8，源在 S16，使用 STOILD，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STOILD((int8_t)w, (int16_t)r));
    }

    /*
    Case 3: STOIST / STOILD 均无法直接编码两个操作数，
    需要根据各操作数的实际范围细分处理。
    */

    if (r >= INT8_MIN && r <= INT8_MAX)
    {
        /*
        Case 3a: 源在 S8，目标超出 S16
        源天然满足 STOIST 的 a8 要求，只需将目标搬入临时 S16 槽。
        */
        const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_STOIST((int8_t)r, w16)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }

    if (w >= INT8_MIN && w <= INT8_MAX)
    {
        /*
        Case 3b: 目标在 S8，源超出 S16
        目标天然满足 STOILD 的 a8 要求，只需将源搬入临时 S16 槽。
        */
        int16_t r16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r16))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STOILD((int8_t)w, r16));
    }

    if (w >= INT16_MIN && w <= INT16_MAX)
    {
        /*
        Case 3c: 目标在 S16（但不在 S8），源不在 S8
        目标天然满足 STOIST 的 bc16 要求，只需将源搬入临时 S8 槽。
        */
        int8_t r8;
        if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STOIST(r8, (int16_t)w));
    }

    if (r >= INT16_MIN && r <= INT16_MAX)
    {
        /*
        Case 3d: 源在 S16（但不在 S8），目标超出 S16
        源天然满足 STOILD 的 bc16 要求，只需将目标搬入临时 S8 槽。
        */
        const int8_t w8 = _woort_IRBlock_get_place_to_store_value_storage8(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_STOILD(w8, (int16_t)r)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w8);
    }

    /*
    Case 3e: 源和目标均超出 S16
    将源搬入临时 S8 槽，目标使用临时 S16 槽，
    使用 STOIST 完成转换后再将结果搬回目标实际位置。
    */
    int8_t r8;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
        return false;

    const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_STOIST(r8, w16)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_STOR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    STOR: 字符串转实数
    m_r[0] = 源值（字符串），m_w = 目标值（实数）

    STORST (mode=2): [SB + a8(源)] -> [SB + bc16(目标)]   源S8, 目标S16
    STORLD (mode=3): [SB + bc16(源)] -> [SB + a8(目标)]   源S16, 目标S8

    优先根据原始操作数范围选择变体，避免不必要的 MOV 搬运。
    */

    const int32_t r = _woort_IR_get_fact_stack_storage(op->m_r[0]->m_assigned_stack_offset);
    const int32_t w = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);

    if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 STORST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX)
    {
        /* Case 2: 目标在 S8，源在 S16，使用 STORLD，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORLD((int8_t)w, (int16_t)r));
    }

    /*
    Case 3: STORST / STORLD 均无法直接编码两个操作数，
    需要根据各操作数的实际范围细分处理。
    */

    if (r >= INT8_MIN && r <= INT8_MAX)
    {
        /*
        Case 3a: 源在 S8，目标超出 S16
        源天然满足 STORST 的 a8 要求，只需将目标搬入临时 S16 槽。
        */
        const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_STORST((int8_t)r, w16)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }

    if (w >= INT8_MIN && w <= INT8_MAX)
    {
        /*
        Case 3b: 目标在 S8，源超出 S16
        目标天然满足 STORLD 的 a8 要求，只需将源搬入临时 S16 槽。
        */
        int16_t r16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r16))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORLD((int8_t)w, r16));
    }

    if (w >= INT16_MIN && w <= INT16_MAX)
    {
        /*
        Case 3c: 目标在 S16（但不在 S8），源不在 S8
        目标天然满足 STORST 的 bc16 要求，只需将源搬入临时 S8 槽。
        */
        int8_t r8;
        if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
            return false;

        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORST(r8, (int16_t)w));
    }

    if (r >= INT16_MIN && r <= INT16_MAX)
    {
        /*
        Case 3d: 源在 S16（但不在 S8），目标超出 S16
        源天然满足 STORLD 的 bc16 要求，只需将目标搬入临时 S8 槽。
        */
        const int8_t w8 = _woort_IRBlock_get_place_to_store_value_storage8(
            op->m_w, -127);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_STORLD(w8, (int16_t)r)))
            return false;

        return _woort_IRBlock_apply_store_value(b, op->m_w, w8);
    }

    /*
    Case 3e: 源和目标均超出 S16
    将源搬入临时 S8 槽，目标使用临时 S16 槽，
    使用 STORST 完成转换后再将结果搬回目标实际位置。
    */
    int8_t r8;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r8))
        return false;

    const int16_t w16 = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_STORST(r8, w16)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD static bool _woort_IRBlock_commit_CALLN_common(
    woort_IRBlock* b,
    woort_IROp* op,
    uint32_t call_opcode)
{
    if (!_woort_IRBlock_emit_bytecode(b, call_opcode))
        return false;

    if (op->m_w != NULL)
    {
        /*
        有返回值：使用 RESULT 指令弹出参数并将返回值存入目标栈槽。
        RESULT [SB + bc16], POP n10
        n10 = 参数数量 (10位, 最大1023)
        bc16 = 目标栈槽 (S16)
        */
        const int16_t w16 =
            _woort_IRBlock_get_place_to_store_value_storage16(
                op->m_w, -128);

        if (op->m_argument_count <= WOORT_UINT10_MAX)
        {
            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_RESULT(op->m_argument_count, w16)))
                return false;
        }
        else
        {
            assert(op->m_argument_count <= WOORT_UINT24_MAX);

            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_RESULT(0, w16)))
                return false;

            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_POPR(op->m_argument_count)))
                return false;
        }

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }
    else
    {
        /*
        无返回值：使用 POPR 指令仅弹出参数。
        POPR n24
        */
        assert(op->m_argument_count <= WOORT_UINT24_MAX);

        return op->m_argument_count == 0 || _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_POPR(op->m_argument_count));
    }
}

WOORT_NODISCARD bool _woort_IRBlock_commit_CALLNWO(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    CALLNWO: 调用脚本函数（NEAR 调用）
    m_calln_target = 函数在常量区的索引 G[u26]
    m_argument_count = 调用后需弹出的参数数量
    m_w = 返回值接收（OPTIONAL，为 NULL 时不接收返回值）

    生成字节码序列：
    1. CALLNWO G[target]
    2a. 有返回值: RESULT [SB + bc16], POP n10
    2b. 无返回值: POPR n24
    */
    (void)c;

    const uint32_t target = op->m_calln_target;
    assert(target <= WOORT_UINT26_MAX);

    return _woort_IRBlock_commit_CALLN_common(b, op, woort_OpCode_CALLNWO(target));
}

WOORT_NODISCARD bool _woort_IRBlock_commit_CALLNFP(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    CALLNFP: 调用原生函数指针（NEAR 调用）
    m_calln_target = 函数在常量区的索引 G[u26]
    m_argument_count = 调用后需弹出的参数数量
    m_w = 返回值接收（OPTIONAL，为 NULL 时不接收返回值）

    生成字节码序列：
    1. CALLNFP G[target]
    2a. 有返回值: RESULT [SB + bc16], POP n10
    2b. 无返回值: POPR n24
    */
    (void)c;

    const uint32_t target = op->m_calln_target;
    assert(target <= WOORT_UINT26_MAX);

    return _woort_IRBlock_commit_CALLN_common(b, op, woort_OpCode_CALLNFP(target));
}

WOORT_NODISCARD bool _woort_IRBlock_commit_CALLNJIT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    CALLNJIT: 调用 JIT 编译函数（NEAR 调用）
    m_calln_target = 函数在常量区的索引 G[u26]
    m_argument_count = 调用后需弹出的参数数量
    m_w = 返回值接收（OPTIONAL，为 NULL 时不接收返回值）

    生成字节码序列：
    1. CALLNJIT G[target]
    2a. 有返回值: RESULT [SB + bc16], POP n10
    2b. 无返回值: POPR n24
    */
    (void)c;

    const uint32_t target = op->m_calln_target;
    assert(target <= WOORT_UINT26_MAX);

    return _woort_IRBlock_commit_CALLN_common(b, op, woort_OpCode_CALLNJIT(target));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CALL(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    CALL: 间接调用函数
    m_r[0] = 函数值（存储在栈槽中或常量）
    m_argument_count = 调用后需弹出的参数数量
    m_w = 返回值接收（OPTIONAL，为 NULL 时不接收返回值）

    生成字节码序列：
    1a. 有栈槽: CALLS [SB + bc16]
    1b. 常量: CALLC G[abc24]
    2a. 有返回值: RESULT [SB + bc16], POP n10
    2b. 无返回值: POPR n24
    */
    (void)c;

    if (op->m_r[0]->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
    {
        int16_t f16;
        if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &f16))
            return false;

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_CALLS(f16)))
            return false;
    }
    else
    {
        assert(op->m_r[0]->m_source == WOORT_IRVALUE_SOURCE_CONSTANT && !op->m_r[0]->m_constant_need_stack_slot);

        const uint32_t target = op->m_r[0]->m_constant;
        assert(target <= WOORT_UINT24_MAX);

        if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_CALLC(target)))
            return false;
    }

    if (op->m_w != NULL)
    {
        const int16_t w16 =
            _woort_IRBlock_get_place_to_store_value_storage16(
                op->m_w, -128);

        if (op->m_argument_count <= WOORT_UINT10_MAX)
        {
            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_RESULT(op->m_argument_count, w16)))
                return false;
        }
        else
        {
            assert(op->m_argument_count <= WOORT_UINT24_MAX);

            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_RESULT(0, w16)))
                return false;

            if (!_woort_IRBlock_emit_bytecode(
                b, woort_OpCode_POPR(op->m_argument_count)))
                return false;
        }

        return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
    }
    else
    {
        assert(op->m_argument_count <= WOORT_UINT24_MAX);

        return op->m_argument_count == 0 || _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_POPR(op->m_argument_count));
    }
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKCLOSURE(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    MKCLOSURE: 创建闭包
    m_calln_target = 函数在常量区的索引 G[ex32]
    m_argument_count = 捕获的值数量 (n10, 最大1023)
    m_w = 返回值

    生成字节码：
    MKCLOSURE n10, bc16; ex32
    */
    (void)c;

    assert(op->m_argument_count <= WOORT_UINT10_MAX);

    const int16_t w16 =
        _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -128);

    const uint32_t target = op->m_calln_target;

    if (!_woort_IRBlock_emit_bytecode_ext(
        b, woort_OpCode_MKCLOSURE(op->m_argument_count, w16), target))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKVEC(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    MKVEC: 构造向量
    m_count = 元素数量 (n8, 最大255; 超过则使用 MKVECEXT + ex32)
    m_w = 返回值

    生成字节码：
    MKVEC n8, bc16
    或
    MKVECEXT bc16; ex32
    */
    (void)c;

    const int16_t w16 =
        _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -128);

    if (op->m_count <= WOORT_UINT8_MAX)
    {
        if (!_woort_IRBlock_emit_bytecode(
            b, woort_OpCode_MKVEC(op->m_count, w16)))
            return false;
    }
    else
    {
        if (!_woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MKVECEXT(w16), op->m_count))
            return false;
    }

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKMAP(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    MKMAP: 构造字典
    m_count = 键值对数量 (n8, 最大255; 超过则使用 MKMAPEXT + ex32)
    m_w = 返回值

    生成字节码：
    MKMAP n8, bc16
    或
    MKMAPEXT bc16; ex32
    */
    (void)c;

    const int16_t w16 =
        _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -128);

    if (op->m_count <= WOORT_UINT8_MAX)
    {
        if (!_woort_IRBlock_emit_bytecode(
            b, woort_OpCode_MKMAP(op->m_count, w16)))
            return false;
    }
    else
    {
        if (!_woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MKMAPEXT(w16), op->m_count))
            return false;
    }

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MKSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    MKSTRUCT: 构造结构体
    m_count = 字段数量 (n8, 最大255; 超过则使用 MKSTRUCTEXT + ex32)
    m_w = 返回值

    生成字节码：
    MKSTRUCT n8, bc16
    或
    MKSTRUCTEXT bc16; ex32
    */
    (void)c;

    const int16_t w16 =
        _woort_IRBlock_get_place_to_store_value_storage16(
            op->m_w, -128);

    if (op->m_count <= WOORT_UINT8_MAX)
    {
        if (!_woort_IRBlock_emit_bytecode(
            b, woort_OpCode_MKSTRUCT(op->m_count, w16)))
            return false;
    }
    else
    {
        if (!_woort_IRBlock_emit_bytecode_ext(
            b, woort_OpCode_MKSTRUCTEXT(w16), op->m_count))
            return false;
    }

    return _woort_IRBlock_apply_store_value(b, op->m_w, w16);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_BOXDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    BOXDYN: 将值装箱为动态类型
    m_r[0] = 源值，m_w = 目标值，m_type = 类型

    BOXDYN t8, b8, c8: [SB + b8] -> [SB + c8]
    源和目标都必须在 S8 范围内
    */
    (void)c;

    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_BOXDYN(op->m_type, r, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNBOXDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    UNBOXDYN: 将动态类型拆箱为指定类型
    m_r[0] = 源值，m_w = 目标值，m_type = 类型

    UNBOXDYN t8, b8, c8: [SB + b8] -> [SB + c8]
    源和目标都必须在 S8 范围内
    */
    (void)c;

    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_UNBOXDYN(op->m_type, r, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_CHECKDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    CHECKDYN: 检查动态类型是否为指定类型
    m_r[0] = 源值，m_w = 目标值，m_type = 类型

    CHECKDYN t8, b8, c8: [SB + b8] -> [SB + c8]
    源和目标都必须在 S8 范围内
    */
    (void)c;

    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_CHECKDYN(op->m_type, r, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHBOXDYN(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    PUSHBOXDYN: 将值装箱为动态类型并压栈
    m_r[0] = 源值，m_type = 类型

    PUSHBOXDYN t8, bc16: [SB + bc16] 装箱并压栈
    */
    (void)c;

    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;

    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHBOXDYN(op->m_type, r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ADDI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    ADDI: 整数加法
    m_r[0] = 左操作数，m_r[1] = 右操作数，m_w = 目标

    ADDI a8, b8, c8: [SB + a8] + [SB + b8] -> [SB + c8]
    CADDI a8, bc16:  [SB + a8] + [SB + bc16] -> [SB + bc16] (复合加法)

    如果目标与右操作数相同，且右操作数在 S16 范围内，使用 CADDI 优化。
    */
    (void)c;

    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;

            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CADDI(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;

            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CADDI(r, write_aim));
        }
    }

    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ADDI(r1, r2, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SUBI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    SUBI: 整数减法
    m_r[0] = 左操作数，m_r[1] = 右操作数，m_w = 目标

    SUBI a8, b8, c8: [SB + a8] - [SB + b8] -> [SB + c8]
    CSUBI a8, bc16:  [SB + bc16] - [SB + a8] -> [SB + bc16] (复合减法)

    注意：减法不满足交换律，CSUBI 只在目标与左操作数相同时可优化。
    即 w = r[0] - r[1]，当 w == r[0] 时: [w] -= [r[1]]
    */
    (void)c;

    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;

            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CSUBI(r, write_aim));
        }
    }

    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_SUBI(r1, r2, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MULI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    MULI: 整数乘法
    m_r[0] = 左操作数，m_r[1] = 右操作数，m_w = 目标
    MULI a8, b8, c8: [SB + a8] * [SB + b8] -> [SB + c8]
    CMULI a8, bc16:  [SB + a8] * [SB + bc16] -> [SB + bc16] (复合乘法)
    */
    (void)c;

    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CMULI(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CMULI(r, write_aim));
        }
    }

    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_MULI(r1, r2, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_DIVI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    DIVI: 整数除法
    DIVI a8, b8, c8: [SB + a8] / [SB + b8] -> [SB + c8]
    CDIVI a8, bc16:  [SB + bc16] / [SB + a8] -> [SB + bc16]
    除法不满足交换律，CDIVI 只在目标与左操作数相同时可优化。
    即 w = r[0] / r[1]，当 w == r[0] 时: [w] /= [r[1]]
    */
    (void)c;

    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CDIVI(r, write_aim));
        }
    }

    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_DIVI(r1, r2, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MODI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    MODI: 整数取模
    MODI a8, b8, c8: [SB + a8] % [SB + b8] -> [SB + c8]
    CMODI a8, bc16:  [SB + bc16] % [SB + a8] -> [SB + bc16]
    取模不满足交换律，CMODI 只在目标与左操作数相同时可优化。
    即 w = r[0] % r[1]，当 w == r[0] 时: [w] %= [r[1]]
    */
    (void)c;

    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CMODI(r, write_aim));
        }
    }

    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_MODI(r1, r2, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NEGI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    NEGI: 整数取负
    m_r[0] = 源操作数，m_w = 目标
    NEGI a8, bc16: -[SB + a8] -> [SB + bc16]
    */
    (void)c;

    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;

    const int16_t w = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_NEGI(r, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LTI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    LTI: 整数小于比较
    m_r[0] = 左操作数，m_r[1] = 右操作数，m_w = 目标

    LTI a8, b8, c8: [SB + a8] < [SB + b8] -> [SB + c8]
    */
    (void)c;

    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;

    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);

    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LTI(r1, r2, w)))
        return false;

    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GTI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_GTI(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LEI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LEI(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GEI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_GEI(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_EQI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_EQI(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NEI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_NEI(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ADDR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* ADDR: 满足交换律，可使用 CADDR 复合优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CADDR(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CADDR(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ADDR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SUBR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* SUBR: 不满足交换律，CSUBR 只在目标与左操作数相同时可优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CSUBR(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_SUBR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MULR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* MULR: 满足交换律，可使用 CMULR 复合优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CMULR(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CMULR(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_MULR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_DIVR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* DIVR: 不满足交换律，CDIVR 只在目标与左操作数相同时可优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CDIVR(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_DIVR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_MODR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* MODR: 不满足交换律，CMODR 只在目标与左操作数相同时可优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CMODR(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_MODR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NEGR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;
    const int16_t w = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_NEGR(r, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LTR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LTR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GTR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_GTR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LER(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LER(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GER(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_GER(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_EQR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_EQR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NER(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_NER(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_ADDS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    ADDS: 字符串连接  w = r[0] + r[1]  (r[0] 为前缀，r[1] 为后缀)

    CADDS(a8, bc16):  [bc16] = concat([bc16], [a8])  —— bc16 为前缀，a8 为后缀
        可用条件: w == r[0]（前缀留在原地，追加 r[1]）

    CVADDS(a8, bc16): [bc16] = concat([a8], [bc16])  —— a8 为前缀，bc16 为后缀
        可用条件: w == r[1]（后缀留在原地，在前面拼接 r[0]）
    */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            /* w == r[0]（前缀），使用 CADDS: [w] = concat([w], [r[1]]) */
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CADDS(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            /* w == r[1]（后缀），使用 CVADDS: [w] = concat([r[0]], [w]) */
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CVADDS(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_ADDS(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LTS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LTS(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GTS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_GTS(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LES(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LES(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_GES(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_GES(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_EQS(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_EQS(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_NES(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_NES(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LAND(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* LAND: 满足交换律，可使用 CLAND 复合优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CLAND(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CLAND(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LAND(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LOR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* LOR: 满足交换律，可使用 CLOR 复合优化 */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CLOR(r, write_aim));
        }
        else if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
                return false;
            return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CLOR(r, write_aim));
        }
    }
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LOR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LNOT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /*
    LNOT: 逻辑取反
    LNOT a8, bc16: ![SB + a8] -> [SB + bc16]
    CLNOT bc16:    ![SB + bc16] -> [SB + bc16] (原地取反)
    */
    (void)c;
    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX && op->m_w->m_assigned_stack_offset == op->m_r[0]->m_assigned_stack_offset)
    {
        return _woort_IRBlock_emit_bytecode(b, woort_OpCode_CLNOT(write_aim));
    }
    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;
    const int16_t w = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LNOT(r, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXVEC(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* LDIDXVEC a8, b8, c8: vec=[SB+a8], idx=[SB+b8] -> [SB+c8] */
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDXVEC(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXVECX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* LDIDXVECX a8, b8, c8: vec=[SB+a8], idx=[SB+b8] -> [SB+c8] (dynamic) */
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDXVECX(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* LDIDSTRUCT n8, b8, c8: struct=[SB+b8], field=n8 -> [SB+c8] */
    (void)c;
    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -127);
    assert(op->m_index <= WOORT_UINT8_MAX);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDSTRUCT((uint8_t)op->m_index, r, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXSTRING(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* LDIDSTRING a8, b8, c8: str=[SB+a8], idx=[SB+b8] -> [SB+c8] */
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDSTRING(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDXDICTI(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDXDICTR(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDXDICTB(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_LDIDXDICTX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    const int8_t w =
        _woort_IRBlock_get_place_to_store_value_storage8(op->m_w, -126);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_LDIDXDICTX(r1, r2, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* STIDXVEC_I a8, b8, c8: vec=[SB+a8], idx=[SB+b8], val=[SB+c8] */
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXVEC_I(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXVEC_R(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXVEC_B(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXVECX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXVEC_X(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTII(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTII(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTIR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTIR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTIB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTIB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTIX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTIX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTRI(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTRR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTRB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTRX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTRX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTBI(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTBR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTBB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTBX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTBX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTXI(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTXR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTXB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXDICTXX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXDICTXX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPII(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPII(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPIR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPIR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPIB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPIB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPIX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPIX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPRI(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPRR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPRB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPRX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPRX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPBI(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPBR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPBB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPBX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPBX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPXI(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPXR(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPXB(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXMAPXX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int8_t r1, r2, r3;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[2], -126, &r3))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDXMAPXX(r1, r2, r3));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_SDIDXSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* STIDSTRUCT n10, a8, b8: struct=[SB+a8], val=[SB+b8], field=n10 */
    (void)c;
    int8_t r1, r2;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r1))
        return false;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[1], -127, &r2))
        return false;
    assert(op->m_index <= WOORT_UINT10_MAX);
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_STIDSTRUCT((uint16_t)op->m_index, r1, r2));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNPACKSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* UNPACKSTRUCT bc16: 解包结构体到 [SB + bc16] */
    (void)c;
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_UNPACKSTRUCT(r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNPACKVEC(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* UNPACKVEC a8, bc16: 解包向量到 [SB + bc16]，展开数量写入 [SB + a8] */
    (void)c;
    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;
    const int16_t w = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_UNPACKVEC(r, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_UNPACKVECX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* UNPACKVECX a8, bc16: 解包向量（动态）到 [SB + bc16]，展开数量写入 [SB + a8] */
    (void)c;
    int8_t r;
    if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
        return false;
    const int16_t w = _woort_IRBlock_get_place_to_store_value_storage16(
        op->m_w, -127);
    if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_UNPACKVECX(r, w)))
        return false;
    return _woort_IRBlock_apply_store_value(b, op->m_w, w);
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTRUCT(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* PUSHIDXSTRUCT n8, bc16: struct=[SB+bc16], 压入 field n8 */
    (void)c;
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;
    assert(op->m_index <= WOORT_UINT8_MAX);
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHIDXSTRUCT((uint8_t)op->m_index, r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXI(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    /* PUSHIDXSTBOXI n8, bc16: struct=[SB+bc16], 压入 field n8 的 int box 引用 */
    (void)c;
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;
    assert(op->m_index <= WOORT_UINT8_MAX);
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHIDXSTBOXI((uint8_t)op->m_index, r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXR(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;
    assert(op->m_index <= WOORT_UINT8_MAX);
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHIDXSTBOXR((uint8_t)op->m_index, r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXB(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;
    assert(op->m_index <= WOORT_UINT8_MAX);
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHIDXSTBOXB((uint8_t)op->m_index, r));
}
WOORT_NODISCARD bool _woort_IRBlock_commit_PUSHIDXSTBOXX(woort_IRBlock* b, woort_IROp* op, woort_IRCompiler* c)
{
    (void)c;
    int16_t r;
    if (!_woort_IRBlock_load_value_storage16(b, op->m_r[0], -128, &r))
        return false;
    assert(op->m_index <= WOORT_UINT8_MAX);
    return _woort_IRBlock_emit_bytecode(b, woort_OpCode_PUSHIDXSTBOXX((uint8_t)op->m_index, r));
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
    WOORT_IROP_KIND_count * sizeof(_woort_IRBlock_CommitCallback) == sizeof(_ir_op_commit_callbacks),
    "All case must been covered.");

WOORT_NODISCARD bool _woort_IRBlock_commit_codes(woort_IRBlock* b, woort_IRCompiler* c)
{
#ifndef _NDEBUG
    /*
    STEP 0: 检查 PHI 节点的输入节点是否共享相同的栈槽
    */
    for (woort_IRPhi* phi = woort_linklist_iter(&b->m_phis);
        phi != NULL;
        phi = woort_linklist_next(phi))
    {
        assert(phi->m_phi_value->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);
        for (woort_IRPhi_ReentryRecord* entry_record = woort_linklist_iter(&phi->m_records);
            entry_record != NULL;
            entry_record = woort_linklist_next(entry_record))
        {
            assert(phi->m_phi_value->m_assigned_stack_offset 
                == entry_record->m_value->m_assigned_stack_offset);
        }
    }
#endif

    /*
    STEP 1: 块起始时，先加载当前块所需载入的常量
    */
    for (size_t i = 0; i < b->m_loading_constants.m_size; ++i)
    {
        woort_IRValue* const loading_constant =
            *(woort_IRValue**)woort_vector_at(&b->m_loading_constants, i);

        assert(loading_constant->m_source == WOORT_IRVALUE_SOURCE_CONSTANT && loading_constant->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

        const int32_t constant_fact_stack_slot =
            _woort_IR_get_fact_stack_storage(loading_constant->m_assigned_stack_offset);

        if (!_woort_IRBlock_commit_LOAD_op(b, constant_fact_stack_slot, loading_constant->m_constant))
            return false;
    }

    /*
    STEP 2: 提交块的本体指令
    */
    for (woort_IROp* ir_op = woort_linklist_iter(&b->m_operates);
        ir_op != NULL;
        ir_op = woort_linklist_next(ir_op))
    {
        assert(ir_op->m_op >= 0 && ir_op->m_op < WOORT_IROP_KIND_count);

        if (!_ir_op_commit_callbacks[ir_op->m_op](b, ir_op, c))
            return false;
    }

    /*
    STEP 3: 处理返回指令
    */
    if (b->m_cond_type == WOORT_IRBLOCK_ENDWAY_RET)
    {
        if (b->m_ret_value_may_null == NULL)
        {
            if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RET()))
                return false;
        }
        else if (b->m_ret_value_may_null->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
        {
            int16_t r;
            if (!_woort_IRBlock_load_value_storage16(b, b->m_ret_value_may_null, -128, &r))
                return false;

            if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RETVS(r)))
                return false;
        }
        else
        {
            assert(b->m_ret_value_may_null->m_constant <= WOORT_UINT24_MAX);
            if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_RETVC(b->m_ret_value_may_null->m_constant)))
                return false;
        }
    }

    /*
    完成，块的跳转将在所有块的 _woort_IRBlock_commit_codes 完成之后执行。
    */
    b->m_body_codes_len = b->m_bytecodes_in_block.m_size;

    return true;
}

static size_t _block_ptr_hash(const void* key)
{
    return (size_t)(*(const void* const*)key);
}

static bool _block_ptr_equal(const void* key1, const void* key2)
{
    return *(const void* const*)key1 == *(const void* const*)key2;
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
    if (used_function_stack_slot_count != 0 && !_woort_IRBlock_emit_bytecode(
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

    /*
    Ok, 当前函数的所有块已经提交，我们开始准备块的跳转。

    步骤：
    1. 为每个块根据 m_cond_type 发射跳转字节码（占位符，偏移量为0）
       - 支持 fall-through 优化：如果跳转目标是布局中紧随其后的块，则省略跳转
    2. 计算每个块在最终代码数组中的起始偏移
    3. 修正跳转目标地址
       - 无条件跳转使用绝对地址（JFWD/JBCK）
       - 条件跳转使用相对地址（从当前指令算起）
       - 如果条件跳转偏移溢出，使用反转条件+无条件跳转的方式处理
    4. 将所有块的字节码拼接到 c->m_commited_codes 中
    */

    /*
    STEP 1: 发射跳转指令占位符
    记录需要修正的跳转信息
    */
    typedef struct _JumpPatch
    {
        woort_IRBlock* m_source_block;
        size_t m_bytecode_index; /* 在块的 m_bytecodes_in_block 中的索引 */
        woort_IRBlock* m_target_block;
        bool m_is_unconditional; /* true = JFWD/JBCK, false = 条件跳转 */
        bool m_is_nz_or_z;       /* true = NZ/Z 模式(U16), false = EQ/NEQ/CMP(U8) */
    } _JumpPatch;

    woort_Vector /* _JumpPatch */ jump_patches;
    woort_vector_init(&jump_patches, sizeof(_JumpPatch));

    for (woort_IRBlock* b = woort_linklist_iter(&f->m_ir_blocks);
        b != NULL;
        b = woort_linklist_next(b))
    {
        woort_IRBlock* next_block = (woort_IRBlock*)woort_linklist_next(b);

        switch (b->m_cond_type)
        {
        case WOORT_IRBLOCK_ENDWAY_RET:
            /* 已经在 _woort_IRBlock_commit_codes 中处理 */
            break;

        case WOORT_IRBLOCK_ENDWAY_NOT_FINISHED:
            assert(false && "Block not finished");
            woort_vector_deinit(&jump_patches);
            return false;

        case WOORT_IRBLOCK_ENDWAY_BR:
        {
            if (b->m_br_next_block == next_block)
            {
                /* Fall-through, 不需要跳转指令 */
                break;
            }

            /* 发射无条件跳转占位符 JFWD(0) */
            size_t jmp_idx = b->m_bytecodes_in_block.m_size;
            if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWD(0)))
            {
                woort_vector_deinit(&jump_patches);
                return false;
            }
            _JumpPatch patch;
            patch.m_source_block = b;
            patch.m_bytecode_index = jmp_idx;
            patch.m_target_block = b->m_br_next_block;
            patch.m_is_unconditional = true;
            patch.m_is_nz_or_z = false;
            if (!woort_vector_push_back(&jump_patches, 1, &patch))
            {
                woort_vector_deinit(&jump_patches);
                return false;
            }
            break;
        }

        case WOORT_IRBLOCK_ENDWAY_BR_COND:
        {
            int8_t cond_s8;
            if (!_woort_IRBlock_load_value_storage8(
                b, b->m_br_cond_value, -126, &cond_s8))
            {
                woort_vector_deinit(&jump_patches);
                return false;
            }

            if (b->m_br_next_block_cond_false == next_block)
            {
                /*
                false 分支 fall-through，只需条件跳转到 true 分支。
                JFWDNZ(cond, offset_to_true) —— 条件非零时跳转
                */
                size_t jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWDNZ(cond_s8, 0)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch patch;
                patch.m_source_block = b;
                patch.m_bytecode_index = jmp_idx;
                patch.m_target_block = b->m_br_next_block_cond_true;
                patch.m_is_unconditional = false;
                patch.m_is_nz_or_z = true;
                if (!woort_vector_push_back(&jump_patches, 1, &patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
            }
            else if (b->m_br_next_block_cond_true == next_block)
            {
                /*
                true 分支 fall-through，只需条件跳转到 false 分支。
                JFWDZ(cond, offset_to_false) —— 条件为零时跳转
                */
                size_t jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWDZ(cond_s8, 0)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch patch;
                patch.m_source_block = b;
                patch.m_bytecode_index = jmp_idx;
                patch.m_target_block = b->m_br_next_block_cond_false;
                patch.m_is_unconditional = false;
                patch.m_is_nz_or_z = true;
                if (!woort_vector_push_back(&jump_patches, 1, &patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
            }
            else
            {
                /*
                两个分支都不是 fall-through。
                发射：
                    JFWDNZ(cond, 2)   —— 如果条件非零，跳过下一条 JFWD
                    JFWD(0)            —— 无条件跳转到 false 分支
                    JFWD(0)            —— 无条件跳转到 true 分支

                注意: JFWDNZ 的偏移 2 表示跳过1条指令（当前指令 +2 = 后面第二条指令）
                */
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWDNZ(cond_s8, 2)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }

                /* JFWD(0) 到 false 分支 */
                size_t false_jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWD(0)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch false_patch;
                false_patch.m_source_block = b;
                false_patch.m_bytecode_index = false_jmp_idx;
                false_patch.m_target_block = b->m_br_next_block_cond_false;
                false_patch.m_is_unconditional = true;
                false_patch.m_is_nz_or_z = false;
                if (!woort_vector_push_back(&jump_patches, 1, &false_patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }

                /* JFWD(0) 到 true 分支 */
                size_t true_jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWD(0)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch true_patch;
                true_patch.m_source_block = b;
                true_patch.m_bytecode_index = true_jmp_idx;
                true_patch.m_target_block = b->m_br_next_block_cond_true;
                true_patch.m_is_unconditional = true;
                true_patch.m_is_nz_or_z = false;
                if (!woort_vector_push_back(&jump_patches, 1, &true_patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
            }
            break;
        }

        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_EQ:
        {
            int8_t a_s8, b_s8;
            if (!_woort_IRBlock_load_value_storage8(
                b, b->m_br_compare_values[0], -126, &a_s8))
            {
                woort_vector_deinit(&jump_patches);
                return false;
            }
            if (!_woort_IRBlock_load_value_storage8(
                b, b->m_br_compare_values[1], -127, &b_s8))
            {
                woort_vector_deinit(&jump_patches);
                return false;
            }

            if (b->m_br_next_block_compare_false == next_block)
            {
                /*
                false 分支 fall-through，条件跳转到 true 分支。
                根据比较类型选择相应的跳转指令：
                    LT -> JFWDLT
                    LE -> JFWDEL
                    EQ -> JFWDEQ
                */
                woort_Bytecode jmp_code;
                bool is_nz_or_z = false;
                switch (b->m_cond_type)
                {
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
                    jmp_code = woort_OpCode_JFWDLT(a_s8, b_s8, 0);
                    break;
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
                    jmp_code = woort_OpCode_JFWDEL(a_s8, b_s8, 0);
                    break;
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_EQ:
                    jmp_code = woort_OpCode_JFWDEQ(a_s8, b_s8, 0);
                    is_nz_or_z = false;
                    break;
                default:
                    assert(false);
                    jmp_code = 0;
                    break;
                }

                size_t jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, jmp_code))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch patch;
                patch.m_source_block = b;
                patch.m_bytecode_index = jmp_idx;
                patch.m_target_block = b->m_br_next_block_compare_true;
                patch.m_is_unconditional = false;
                patch.m_is_nz_or_z = is_nz_or_z;
                if (!woort_vector_push_back(&jump_patches, 1, &patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
            }
            else if (b->m_br_next_block_compare_true == next_block)
            {
                /*
                true 分支 fall-through，需要在条件不满足时跳转到 false 分支。
                使用反转条件：
                    LT -> GE (JFWDEG)
                    LE -> GT (JFWDGT)
                    EQ -> NEQ (JFWDNEQ)
                */
                woort_Bytecode jmp_code;
                bool is_nz_or_z = false;
                switch (b->m_cond_type)
                {
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
                    jmp_code = woort_OpCode_JFWDEG(a_s8, b_s8, 0);
                    break;
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
                    jmp_code = woort_OpCode_JFWDGT(a_s8, b_s8, 0);
                    break;
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_EQ:
                    jmp_code = woort_OpCode_JFWDNEQ(a_s8, b_s8, 0);
                    is_nz_or_z = false;
                    break;
                default:
                    assert(false);
                    jmp_code = 0;
                    break;
                }

                size_t jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, jmp_code))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch patch;
                patch.m_source_block = b;
                patch.m_bytecode_index = jmp_idx;
                patch.m_target_block = b->m_br_next_block_compare_false;
                patch.m_is_unconditional = false;
                patch.m_is_nz_or_z = is_nz_or_z;
                if (!woort_vector_push_back(&jump_patches, 1, &patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
            }
            else
            {
                /*
                两个分支都不是 fall-through。
                使用比较跳转 + 2条无条件跳转。
                    JFWD<cmp>(a, b, 2)   —— 条件满足时跳过下面的 JFWD
                    JFWD(0)              —— 无条件跳转到 false 分支
                    JFWD(0)              —— 无条件跳转到 true 分支
                */
                woort_Bytecode cmp_code;
                switch (b->m_cond_type)
                {
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
                    cmp_code = woort_OpCode_JFWDLT(a_s8, b_s8, 2);
                    break;
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
                    cmp_code = woort_OpCode_JFWDEL(a_s8, b_s8, 2);
                    break;
                case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_EQ:
                    cmp_code = woort_OpCode_JFWDEQ(a_s8, b_s8, 2);
                    break;
                default:
                    assert(false);
                    cmp_code = 0;
                    break;
                }

                if (!_woort_IRBlock_emit_bytecode(b, cmp_code))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }

                /* JFWD(0) 到 false 分支 */
                size_t false_jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWD(0)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch false_patch;
                false_patch.m_source_block = b;
                false_patch.m_bytecode_index = false_jmp_idx;
                false_patch.m_target_block = b->m_br_next_block_compare_false;
                false_patch.m_is_unconditional = true;
                false_patch.m_is_nz_or_z = false;
                if (!woort_vector_push_back(&jump_patches, 1, &false_patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }

                /* JFWD(0) 到 true 分支 */
                size_t true_jmp_idx = b->m_bytecodes_in_block.m_size;
                if (!_woort_IRBlock_emit_bytecode(b, woort_OpCode_JFWD(0)))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
                _JumpPatch true_patch;
                true_patch.m_source_block = b;
                true_patch.m_bytecode_index = true_jmp_idx;
                true_patch.m_target_block = b->m_br_next_block_compare_true;
                true_patch.m_is_unconditional = true;
                true_patch.m_is_nz_or_z = false;
                if (!woort_vector_push_back(&jump_patches, 1, &true_patch))
                {
                    woort_vector_deinit(&jump_patches);
                    return false;
                }
            }
            break;
        }

        default:
            assert(false && "Unknown block end way");
            woort_vector_deinit(&jump_patches);
            return false;
        }
    }

    /*
    STEP 2 & 3: 计算块偏移量并修正跳转目标地址。

    使用迭代方法：如果条件跳转偏移溢出，则展开为反转条件跳转 + 无条件跳转，
    展开会改变块大小，需要重新计算偏移。循环直到所有偏移稳定。
    */

    /* 建立 IRBlock* -> 偏移量 的映射 */
    woort_HashMap block_offset_map;
    woort_hashmap_init(
        &block_offset_map,
        sizeof(woort_IRBlock*),
        sizeof(size_t),
        _block_ptr_hash,
        _block_ptr_equal);

    bool need_recalc = true;
    while (need_recalc)
    {
        need_recalc = false;

        /* 计算每个块的起始偏移量 */
        woort_hashmap_clear(&block_offset_map);
        size_t offset = 0;
        for (woort_IRBlock* b = woort_linklist_iter(&f->m_ir_blocks);
            b != NULL;
            b = woort_linklist_next(b))
        {
            woort_hashmap_Result r = woort_hashmap_insert(
                &block_offset_map, &b, &offset);
            if (r == WOORT_HASHMAP_RESULT_ALREADY_EXIST)
            {
                /* 重新计算时块已存在，更新偏移 */
                void* val_addr;
                woort_hashmap_find(&block_offset_map, &b, &val_addr);
                *(size_t*)val_addr = offset;
            }
            else if (r == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
            {
                woort_hashmap_deinit(&block_offset_map);
                woort_vector_deinit(&jump_patches);
                return false;
            }
            offset += b->m_bytecodes_in_block.m_size;
        }

        /* 修正每条跳转指令的目标地址 */
        for (size_t i = 0; i < jump_patches.m_size; ++i)
        {
            _JumpPatch* patch = (_JumpPatch*)woort_vector_at(&jump_patches, i);

            void* src_offset_addr;
            void* tgt_offset_addr;
            woort_hashmap_find(&block_offset_map, &patch->m_source_block, &src_offset_addr);
            woort_hashmap_find(&block_offset_map, &patch->m_target_block, &tgt_offset_addr);

            size_t src_block_offset = *(size_t*)src_offset_addr;
            size_t tgt_block_offset = *(size_t*)tgt_offset_addr;

            size_t jmp_abs_pos = src_block_offset + patch->m_bytecode_index;
            size_t target_abs_pos = tgt_block_offset;

            woort_Bytecode* bytecode_ptr =
                (woort_Bytecode*)woort_vector_at(
                    &patch->m_source_block->m_bytecodes_in_block,
                    patch->m_bytecode_index);

            if (patch->m_is_unconditional)
            {
                /* 无条件跳转：绝对地址，JFWD 或 JBCK */
                assert(target_abs_pos <= WOORT_UINT26_MAX);

                if (target_abs_pos <= jmp_abs_pos)
                {
                    /* 向后跳转 */
                    *bytecode_ptr = woort_OpCode_JBCK((uint32_t)target_abs_pos);
                }
                else
                {
                    /* 向前跳转 */
                    *bytecode_ptr = woort_OpCode_JFWD((uint32_t)target_abs_pos);
                }
            }
            else
            {
                /* 条件跳转：相对地址 */
                bool is_forward = (target_abs_pos >= jmp_abs_pos);
                size_t rel_offset = is_forward
                    ? (target_abs_pos - jmp_abs_pos)
                    : (jmp_abs_pos - target_abs_pos);

                /* 获取当前指令的操作数信息（寄存器等），保留操作码和模式 */
                woort_Bytecode old_code = *bytecode_ptr;
                uint32_t op6 = WOORT_BYTECODE(OP6, old_code);
                uint32_t m2 = WOORT_BYTECODE(M2, old_code);
                uint32_t a8 = WOORT_BYTECODE(A8, old_code);
                uint32_t b8_val = WOORT_BYTECODE(B8, old_code);

                if (patch->m_is_nz_or_z)
                {
                    /* NZ/Z 模式: 最大偏移 U16 (65535) */
                    if (rel_offset > UINT16_MAX)
                    {
                        /*
                        偏移溢出！展开为：
                            反转条件(NZ<->Z) 跳过2条指令（offset=2）
                            JFWD/JBCK(target_abs) —— 无条件跳转到原目标

                        需要在当前位置后面插入一条 JFWD/JBCK 指令。
                        */
                        uint32_t inv_m2 = (m2 == 0) ? 1u : 0u; /* NZ <-> Z */

                        /* 将当前条件跳转改为反转条件，跳过2条指令 */
                        *bytecode_ptr = woort_OpcodeFormal_OP6_M2_A8_BC16_cons(
                            WOORT_OPCODE_JFWDCND, inv_m2, a8, 2);

                        /*
                        在当前指令后面插入一条无条件跳转到原目标。
                        注意：这里需要在 m_bytecodes_in_block 中插入一条指令，
                        会导致后续所有偏移失效，需要重新计算。
                        */
                        woort_Bytecode uncond_jmp = woort_OpCode_JFWD(0);
                        size_t insert_pos = patch->m_bytecode_index + 1;

                        /* 先扩容 */
                        woort_Bytecode placeholder = 0;
                        if (!woort_vector_push_back(
                            &patch->m_source_block->m_bytecodes_in_block, 1, &placeholder))
                        {
                            woort_hashmap_deinit(&block_offset_map);
                            woort_vector_deinit(&jump_patches);
                            return false;
                        }

                        /* 将 insert_pos 之后的数据后移一位 */
                        woort_Bytecode* codes_data =
                            (woort_Bytecode*)patch->m_source_block->m_bytecodes_in_block.m_data;
                        size_t total_size = patch->m_source_block->m_bytecodes_in_block.m_size;
                        for (size_t j = total_size - 1; j > insert_pos; --j)
                        {
                            codes_data[j] = codes_data[j - 1];
                        }
                        codes_data[insert_pos] = uncond_jmp;

                        /*
                        更新同一块中后续的 patch 记录的 m_bytecode_index，
                        因为插入了一条指令，后面的索引都要 +1。
                        */
                        for (size_t j = 0; j < jump_patches.m_size; ++j)
                        {
                            _JumpPatch* other = (_JumpPatch*)woort_vector_at(&jump_patches, j);
                            if (other->m_source_block == patch->m_source_block && other->m_bytecode_index > patch->m_bytecode_index && other != patch)
                            {
                                other->m_bytecode_index++;
                            }
                        }

                        /* 将当前 patch 改为无条件跳转 */
                        patch->m_bytecode_index = insert_pos;
                        patch->m_is_unconditional = true;
                        patch->m_is_nz_or_z = false;

                        need_recalc = true;
                        break; /* 跳出 patch 循环，重新计算偏移 */
                    }
                    else
                    {
                        /* 偏移在范围内，直接修正 */
                        if (is_forward)
                        {
                            *bytecode_ptr = woort_OpcodeFormal_OP6_M2_A8_BC16_cons(
                                WOORT_OPCODE_JFWDCND, m2, a8, (uint16_t)rel_offset);
                        }
                        else
                        {
                            *bytecode_ptr = woort_OpcodeFormal_OP6_M2_A8_BC16_cons(
                                WOORT_OPCODE_JBCKCND, m2, a8, (uint16_t)rel_offset);
                        }
                    }
                }
                else
                {
                    /* EQ/NEQ/CMP 模式: 最大偏移 U8 (255) */
                    if (rel_offset > UINT8_MAX)
                    {
                        /*
                        偏移溢出！展开为反转条件 + 无条件跳转。

                        反转方式：
                        - JFWDCND mode=2 (EQ)  <-> mode=3 (NEQ)
                        - JFDCMP  mode=0 (LT)  <-> mode=2 (LE, 取反为 GE -> JFWDEG mode=3)
                                  mode=1 (GT)  <-> mode=3 (GE, 取反为 LE -> JFWDEL mode=2)

                        实际上对 CMP:
                            LT (mode=0) 反转 -> GE (mode=3)
                            GT (mode=1) 反转 -> LE (mode=2)
                            LE (mode=2) 反转 -> GT (mode=1)
                            GE (mode=3) 反转 -> LT (mode=0)

                        对 JFWDCND:
                            EQ (mode=2) 反转 -> NEQ (mode=3)
                            NEQ (mode=3) 反转 -> EQ (mode=2)
                        */
                        uint32_t inv_m2;
                        uint32_t inv_op6 = op6;

                        if (op6 == WOORT_OPCODE_JFWDCND || op6 == WOORT_OPCODE_JBCKCND)
                        {
                            /* EQ <-> NEQ */
                            inv_m2 = (m2 == 2) ? 3u : 2u;
                            inv_op6 = WOORT_OPCODE_JFWDCND;
                        }
                        else
                        {
                            /* CMP: LT<->GE, GT<->LE */
                            inv_op6 = WOORT_OPCODE_JFDCMP;
                            switch (m2)
                            {
                            case 0:
                                inv_m2 = 3;
                                break; /* LT -> GE */
                            case 1:
                                inv_m2 = 2;
                                break; /* GT -> LE */
                            case 2:
                                inv_m2 = 1;
                                break; /* LE -> GT */
                            case 3:
                                inv_m2 = 0;
                                break; /* GE -> LT */
                            default:
                                inv_m2 = m2;
                                assert(false);
                                break;
                            }
                        }

                        /* 反转条件跳过2条指令 */
                        *bytecode_ptr = woort_OpcodeFormal_OP6_M2_A8_B8_C8_cons(
                            inv_op6, inv_m2, a8, b8_val, 2);

                        /* 插入无条件跳转 */
                        woort_Bytecode uncond_jmp = woort_OpCode_JFWD(0);
                        size_t insert_pos = patch->m_bytecode_index + 1;

                        woort_Bytecode placeholder = 0;
                        if (!woort_vector_push_back(
                            &patch->m_source_block->m_bytecodes_in_block, 1, &placeholder))
                        {
                            woort_hashmap_deinit(&block_offset_map);
                            woort_vector_deinit(&jump_patches);
                            return false;
                        }

                        woort_Bytecode* codes_data =
                            (woort_Bytecode*)patch->m_source_block->m_bytecodes_in_block.m_data;
                        size_t total_size = patch->m_source_block->m_bytecodes_in_block.m_size;
                        for (size_t j = total_size - 1; j > insert_pos; --j)
                        {
                            codes_data[j] = codes_data[j - 1];
                        }
                        codes_data[insert_pos] = uncond_jmp;

                        /* 更新同一块中后续 patch 记录的索引 */
                        for (size_t j = 0; j < jump_patches.m_size; ++j)
                        {
                            _JumpPatch* other = (_JumpPatch*)woort_vector_at(&jump_patches, j);
                            if (other->m_source_block == patch->m_source_block && other->m_bytecode_index > patch->m_bytecode_index && other != patch)
                            {
                                other->m_bytecode_index++;
                            }
                        }

                        /* 将当前 patch 改为无条件跳转 */
                        patch->m_bytecode_index = insert_pos;
                        patch->m_is_unconditional = true;
                        patch->m_is_nz_or_z = false;

                        need_recalc = true;
                        break; /* 重新计算偏移 */
                    }
                    else
                    {
                        /* 偏移在范围内，直接修正 */
                        if (is_forward)
                        {
                            *bytecode_ptr = woort_OpcodeFormal_OP6_M2_A8_B8_C8_cons(
                                op6 == WOORT_OPCODE_JBCKCND ? WOORT_OPCODE_JFWDCND
                                : op6 == WOORT_OPCODE_JBCKCMP ? WOORT_OPCODE_JFDCMP
                                : op6,
                                m2, a8, b8_val, (uint8_t)rel_offset);
                        }
                        else
                        {
                            uint32_t bck_op6;
                            if (op6 == WOORT_OPCODE_JFWDCND || op6 == WOORT_OPCODE_JBCKCND)
                                bck_op6 = WOORT_OPCODE_JBCKCND;
                            else
                                bck_op6 = WOORT_OPCODE_JBCKCMP;

                            *bytecode_ptr = woort_OpcodeFormal_OP6_M2_A8_B8_C8_cons(
                                bck_op6, m2, a8, b8_val, (uint8_t)rel_offset);
                        }
                    }
                }
            }
        }
    }

    /*
    STEP 4: 将所有块的字节码拼接到 c->m_commited_codes 中
    */
    for (woort_IRBlock* b = woort_linklist_iter(&f->m_ir_blocks);
        b != NULL;
        b = woort_linklist_next(b))
    {
        if (b->m_bytecodes_in_block.m_size > 0)
        {
            if (!woort_vector_push_back(
                &c->m_commited_codes,
                b->m_bytecodes_in_block.m_size,
                b->m_bytecodes_in_block.m_data))
            {
                woort_hashmap_deinit(&block_offset_map);
                woort_vector_deinit(&jump_patches);
                return false;
            }
        }
    }

    woort_hashmap_deinit(&block_offset_map);
    woort_vector_deinit(&jump_patches);
    return true;
}

void woort_IRCompiler_init(woort_IRCompiler* c)
{
    woort_linklist_init(&c->m_ir_functions, sizeof(woort_IRFunction));
    c->m_constant_alloc_count = 0;
    c->m_static_storage_alloc_count = 0;

    woort_vector_init(&c->m_commited_codes, sizeof(woort_Bytecode));
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
    woort_vector_deinit(&c->m_commited_codes);
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

WOORT_NODISCARD bool woort_IRCompiler_finish(woort_IRCompiler* c, woort_CodeEnv** out_cenv)
{
    /* 提交所有函数的字节码到 m_commited_codes */
    for (woort_IRFunction* f = woort_linklist_iter(&c->m_ir_functions);
        f != NULL;
        f = woort_linklist_next(f))
    {
        if (!_woort_IRFunction_commit_codes(f, c))
            return false;
    }

    /* 创建 CodeEnv */
    size_t constant_and_static_count =
        (size_t)c->m_constant_alloc_count + (size_t)c->m_static_storage_alloc_count;

    return woort_CodeEnv_create(
        (const woort_Bytecode*)c->m_commited_codes.m_data,
        c->m_commited_codes.m_size,
        constant_and_static_count,
        out_cenv);
}