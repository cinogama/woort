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
    const woort_IRValue* v,
    int8_t temp_slot_idx,
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

WOORT_NODISCARD int8_t _woort_IRBlock_get_place_to_store_value_storage8(
    const woort_IRValue* v,
    int8_t temp_slot_idx)
{
    assert(temp_slot_idx == -126 || temp_slot_idx == -127 || temp_slot_idx == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact_value_assigned_stack_offset =
        _woort_IR_get_fact_stack_storage(v->m_assigned_stack_offset);

    if (fact_value_assigned_stack_offset < INT8_MIN
        || fact_value_assigned_stack_offset > INT8_MAX)
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

    if (fact_value_assigned_stack_offset < INT16_MIN
        || fact_value_assigned_stack_offset > INT16_MAX)
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

    if (fact_value_assigned_stack_offset >= INT16_MIN
        && fact_value_assigned_stack_offset <= INT16_MAX)
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

    if ((fact_stack_slot >= INT8_MIN && fact_stack_slot <= INT8_MAX)
        && constant_storage <= WOORT_UINT18_MAX)
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

    if ((fact_stack_slot >= INT8_MIN && fact_stack_slot <= INT8_MAX)
        && constant_storage <= WOORT_UINT18_MAX)
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
        assert(op->m_r[0]->m_source == WOORT_IRVALUE_SOURCE_CONSTANT
            && !op->m_r[0]->m_constant_need_stack_slot);

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

    if (r >= INT8_MIN && r <= INT8_MAX
        && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 ITORST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITORST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX
        && r >= INT16_MIN && r <= INT16_MAX)
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

    if (r >= INT8_MIN && r <= INT8_MAX
        && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 ITOSST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_ITOSST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX
        && r >= INT16_MIN && r <= INT16_MAX)
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

    if (r >= INT8_MIN && r <= INT8_MAX
        && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 RTOIST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOIST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX
        && r >= INT16_MIN && r <= INT16_MAX)
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

    if (r >= INT8_MIN && r <= INT8_MAX
        && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 RTOSST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_RTOSST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX
        && r >= INT16_MIN && r <= INT16_MAX)
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

    if (r >= INT8_MIN && r <= INT8_MAX
        && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 STOIST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STOIST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX
        && r >= INT16_MIN && r <= INT16_MAX)
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

    if (r >= INT8_MIN && r <= INT8_MAX
        && w >= INT16_MIN && w <= INT16_MAX)
    {
        /* Case 1: 源在 S8，目标在 S16，使用 STORST，无需搬运 */
        return _woort_IRBlock_emit_bytecode(
            b, woort_OpCode_STORST((int8_t)r, (int16_t)w));
    }

    if (w >= INT8_MIN && w <= INT8_MAX
        && r >= INT16_MIN && r <= INT16_MAX)
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

        return op->m_argument_count == 0
            || _woort_IRBlock_emit_bytecode(
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
        assert(op->m_r[0]->m_source == WOORT_IRVALUE_SOURCE_CONSTANT
            && !op->m_r[0]->m_constant_need_stack_slot);

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

        return op->m_argument_count == 0
            || _woort_IRBlock_emit_bytecode(
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
    CSUBI a8, bc16:  [SB + a8] - [SB + bc16] -> [SB + bc16] (复合减法)

    注意：减法不满足交换律，CSUBI 只在目标与右操作数相同时可优化。
    */
    (void)c;

    const int32_t write_aim = _woort_IR_get_fact_stack_storage(op->m_w->m_assigned_stack_offset);
    if (write_aim >= INT16_MIN && write_aim <= INT16_MAX)
    {
        if (op->m_w->m_assigned_stack_offset == op->m_r[1]->m_assigned_stack_offset)
        {
            int8_t r;
            if (!_woort_IRBlock_load_value_storage8(b, op->m_r[0], -128, &r))
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
#ifndef _NDEBUG
    /*
    STEP 0: 检查 PHI 节点的输入节点是否共享相同的栈槽
    */
    for (woort_IRPhi* phi = woort_linklist_iter(&b->m_phis);
        phi != NULL;
        phi = woort_linklist_next(&b->m_phis))
    {
        assert(phi->m_phi_value->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);
        for (woort_IRPhi_ReentryRecord* entry_record = woort_linklist_iter(&phi->m_records);
            entry_record != NULL;
            entry_record = woort_linklist_next(&phi->m_records))
        {
            assert(phi->m_phi_value->m_assigned_stack_offset
                == entry_record->m_value->m_constant_need_stack_slot);
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

        assert(loading_constant->m_source == WOORT_IRVALUE_SOURCE_CONSTANT
            && loading_constant->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

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
    if (used_function_stack_slot_count != 0
        && !_woort_IRBlock_emit_bytecode(
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

    // Ok, 当前函数的所有块已经提交，我们开始准备块的跳转


    // todo
    abort();
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