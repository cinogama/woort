#pragma once

/*
woort_gc.h
*/
#include "woort_diagnosis.h"
#include "woort_vm.h"

#include <stdbool.h>

void woort_GC_bootup(void);
void woort_GC_shutdown(void);

WOORT_NODISCARD bool woort_GC_register_root_vm(
    struct woort_VMRuntime* vmruntime);
void woort_GC_unregister_root_vm(
    struct woort_VMRuntime* vmruntime);
