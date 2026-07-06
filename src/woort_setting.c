#include "woort_setting.h"

#ifdef NDEBUG
#   define WOORT_SETTING_DEFAULT_ENABLE_JIT true
#else
#   define WOORT_SETTING_DEFAULT_ENABLE_JIT false
#endif

woort_Settings g_woort_settings = {
    .m_enable_jit                   = WOORT_SETTING_DEFAULT_ENABLE_JIT,
    .m_hook_ctrl_c_bring_up_debugger = true,
    .m_halt_panic_vm_mode           = WOORT_HALT_PANIC_VM_MODE_DONOT_HALT,
    .m_max_reserved_memory_in_mb    = 1024,
};

void _woort_setting_reset_to_default(void)
{
    g_woort_settings.m_hook_ctrl_c_bring_up_debugger = true;
    g_woort_settings.m_halt_panic_vm_mode = WOORT_HALT_PANIC_VM_MODE_DONOT_HALT;
    g_woort_settings.m_max_reserved_memory_in_mb = 1024;
    g_woort_settings.m_enable_jit = WOORT_SETTING_DEFAULT_ENABLE_JIT;
}
