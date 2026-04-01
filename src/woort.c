#include "woort.h"
#include "woort_codeenv.h"
#include "woort_log.h"
#include "woort_gc.h"
#include "woort_vm.h"
#include "woort_ir_compiler.h"

#include <stdlib.h>
#include <assert.h>

void woort_init(void)
{
    if (!woort_CodeEnv_bootup())
    {
        WOORT_DEBUG("Failed to bootup code env.");
        abort();
    }

    woort_GC_bootup();
}
void woort_shutdown(void)
{
    woort_GC_shutdown();

    woort_CodeEnv_shutdown();
}

/* OPTIONAL */ woort_vm woort_vm_create(void)
{
    woort_VMRuntime* vm;
    if (!woort_VMRuntime_create(&vm))
        return NULL;
    return vm;
}

void woort_vm_close(woort_vm vm)
{
    assert(vm != NULL);
    woort_VMRuntime_destroy(vm);
}

woort_vm woort_vm_swap_running(/* OPTIONAL */ woort_vm vm)
{
    return woort_VMRuntime_swap_running_vm(vm);
}

/* OPTIONAL */ woort_ircompiler woort_ircompiler_create(void)
{
    woort_IRCompiler* c = malloc(sizeof(woort_IRCompiler));
    if (!c)
        return NULL;
    woort_IRCompiler_init(c);
    return c;
}

void woort_ircompiler_close(woort_ircompiler c)
{
    assert(c != NULL);

    woort_IRCompiler_deinit(c);
    free(c);
}