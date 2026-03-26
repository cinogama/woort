#include "woort_ir_compiler.h"
#include "woort_ir_block.h"

/*
在栈槽分配完成之后，根据块中的 m_operates，生成块内的字节码到 m_bytecodes_in_block 中
*/
bool _woort_IRCompiler_commit_codes_in_block(woort_IRBlock* block)
{
}