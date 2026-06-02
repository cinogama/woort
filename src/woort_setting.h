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

}_woort_HaltPanicVMMode;

extern bool _woort_setting_HOOK_CTRL_C_BRING_UP_DEBUGGER;
extern _woort_HaltPanicVMMode _woort_setting_HALT_PANIC_VM_MODE;
extern size_t _woort_setting_MAX_RESERVED_MEMORY_IN_MB;

void _woort_setting_reset_to_default(void);
