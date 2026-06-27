#include "woort_jit_bridge.h"

#include "woort_vm.h"

const size_t WOORT_VM_OFFSETOF_JIT_CALL_DEPTH = offsetof(woort_VMRuntime, m_jit_call_depth);
const size_t WOORT_VM_OFFSETOF_IP = offsetof(woort_VMRuntime, m_ip);
const size_t WOORT_VM_OFFSETOF_SP = offsetof(woort_VMRuntime, m_sp);
const size_t WOORT_VM_OFFSETOF_SB = offsetof(woort_VMRuntime, m_sb);
const size_t WOORT_VM_OFFSETOF_ENV = offsetof(woort_VMRuntime, m_env);