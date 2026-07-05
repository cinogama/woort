#include "woort_jit_bridge.h"
#include "asmjit/x86.h"

#include <cassert>
#include <new>

using namespace asmjit;
using namespace asmjit::x86;

static JitRuntime* _woort_asmjit_runtime = nullptr;

WOORT_NODISCARD bool woort_JIT_Asmjit_bootup(void)
{
    assert(_woort_asmjit_runtime == nullptr);
    _woort_asmjit_runtime = new (std::nothrow) JitRuntime();

    if (_woort_asmjit_runtime == nullptr)
        return false;

    return true;
}
void woort_JIT_Asmjit_shutdown(void)
{
    delete _woort_asmjit_runtime;
    _woort_asmjit_runtime = nullptr;
}

void* woort_JIT_Asmjit_get_runtime(void)
{
    assert(_woort_asmjit_runtime != nullptr);
    return _woort_asmjit_runtime;
}
