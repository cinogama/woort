#include "woort_setting.h"

#ifdef NDEBUG
bool _woort_setting_ENABLE_JIT = true;
#else   
bool _woort_setting_ENABLE_JIT = false;
#endif

bool _woort_setting_HOOK_CTRL_C_BRING_UP_DEBUGGER = true;
_woort_HaltPanicVMMode _woort_setting_HALT_PANIC_VM_MODE = WOORT_HALT_PANIC_VM_MODE_DONOT_HALT;
size_t _woort_setting_MAX_RESERVED_MEMORY_IN_MB = 1024;


void _woort_setting_reset_to_default(void)
{
    _woort_setting_HOOK_CTRL_C_BRING_UP_DEBUGGER = true;
    _woort_setting_HALT_PANIC_VM_MODE = WOORT_HALT_PANIC_VM_MODE_DONOT_HALT;
    _woort_setting_MAX_RESERVED_MEMORY_IN_MB = 1024;
#ifdef NDEBUG
    _woort_setting_ENABLE_JIT = true;
#else
    _woort_setting_ENABLE_JIT = false;
#endif
}
