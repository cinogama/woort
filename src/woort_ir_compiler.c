#include "woort_ir_compiler.h"
#include "woort_ir_block.h"
#include "woort_ir_function.h"
#include "woort_diagnosis.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_opcode_builder.h"

#include <assert.h>

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

    /*
    NOTE: 考虑到对于超出 S8 索引范围的当前栈帧槽或者参数的访问，
        我们总是需要预留三个槽位(-126 -127 -128)以备使用。
    */
    int32_t fact_value_assigned_stack_offset = v->m_assigned_stack_offset;
    if (fact_value_assigned_stack_offset <= -126)
        // Make shift.
        fact_value_assigned_stack_offset -= 3;

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
}

/*
在栈槽分配完成之后，根据块中的 m_operates，生成块内的字节码到 m_bytecodes_in_block 中
*/
WOORT_NODISCARD bool _woort_IRFunction_commit_codes(woort_IRFunction* f)
{
    // 分配栈槽
    size_t used_function_stack_slot_count;
    if (!_woort_IRFunction_stack_slot_assign(f, &used_function_stack_slot_count))
        return false;


}