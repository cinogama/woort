#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum _woort_HaltPanicVMMode
{
    WOORT_HALT_PANIC_VM_MODE_DONOT_HALT,
    WOORT_HALT_PANIC_VM_MODE_VM_ITSELF,
    WOORT_HALT_PANIC_VM_MODE_PROCESS,
    WOORT_HALT_PANIC_VM_MODE_SIGNAL,

} _woort_HaltPanicVMMode;

/*
 * Centralised runtime settings.  Grouping the individual knobs into one
 * struct makes _woort_setting_reset_to_default a single assignment and
 * prevents the "forgot to reset one field" class of bug.
 */
typedef struct woort_Settings
{
    bool                    m_enable_jit;
    bool                    m_hook_ctrl_c_bring_up_debugger;
    _woort_HaltPanicVMMode  m_halt_panic_vm_mode;
    size_t                  m_max_reserved_memory_in_mb;
} woort_Settings;

extern woort_Settings g_woort_settings;

void _woort_setting_reset_to_default(void);
