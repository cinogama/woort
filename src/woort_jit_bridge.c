#include "woort_jit_bridge.h"

#include "woort_vm.h"

const int32_t WOORT_VM_OFFSETOF_JIT_CALL_DEPTH = (int32_t)offsetof(woort_VMRuntime, m_jit_call_depth);
const int32_t WOORT_VM_OFFSETOF_IP = (int32_t)offsetof(woort_VMRuntime, m_ip);
const int32_t WOORT_VM_OFFSETOF_SP = (int32_t)offsetof(woort_VMRuntime, m_sp);
const int32_t WOORT_VM_OFFSETOF_SB = (int32_t)offsetof(woort_VMRuntime, m_sb);
const int32_t WOORT_VM_OFFSETOF_ENV = (int32_t)offsetof(woort_VMRuntime, m_env);
const int32_t WOORT_VM_OFFSETOF_STACK = (int32_t)offsetof(woort_VMRuntime, m_stack);
const int32_t WOORT_VM_OFFSETOF_STACK_END = (int32_t)offsetof(woort_VMRuntime, m_stack_end);

size_t woort_JIT_CodeEnv_constant_count(const woort_CodeEnv* cenv)
{
    return cenv->m_const_records.m_size;
}

woort_Value* woort_JIT_CodeEnv_static_data(const woort_CodeEnv* cenv)
{
    return cenv->m_data_begin;
}
