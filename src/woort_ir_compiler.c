#include "woort_ir_compiler.h"
#include "woort_ir_block.h"
#include "woort_diagnosis.h"

/*
在栈槽分配完成之后，根据块中的 m_operates，生成块内的字节码到 m_bytecodes_in_block 中
*/
WOORT_NODISCARD bool _woort_IRFunction_commit_codes(woort_IRFunction* f)
{
    // 分配栈槽
    if (!_woort_IRFunction_stack_slot_assign())
        return false;

}