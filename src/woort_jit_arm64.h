#pragma once

#ifdef WOORT_BUILD_WITH_ASMJIT
#   define WOORT_JIT_SUPPORT_ARM64
#include "woort_jit.h"

/* JIT 后端实例（实现位于 woort_jit_arm64_impl.cpp）。 */
extern const woort_JIT_Backend WOORT_JIT_BACKEND_IMPL_ARM64;
#endif