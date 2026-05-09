#include "woort.h"
#include "woort_vm_debugger_api.h"
#include "woort_threads.h"
#include "woort_hashmap.h"

/*
Watch And Inspect Program Operation
*/

typedef struct woort_WAIPO_BreakpointCollection
{
    woort_HashMap /* woort_Bytecode*, size_t */ m_breakpoints;

}woort_WAIPO_BreakpointCollection;

static void _woort_WAIPO_BreakpointCollection_init(woort_WAIPO_BreakpointCollection* collection)
{}
static void _woort_WAIPO_BreakpointCollection_deinit(woort_WAIPO_BreakpointCollection* collection)
{}

typedef struct woort_WAIPO_VMLocalContext
{

    
}woort_WAIPO_VMLocalContext;

static void _woort_WAIPO_VMLocalContext_init(woort_WAIPO_VMLocalContext* vmcontext)
{}
static void _woort_WAIPO_VMLocalContext_deinit(woort_WAIPO_VMLocalContext* vmcontext)
{}

typedef struct woort_WAIPO_Debugger
{
    /* OPTIONAL */ woort_HashMap /* woort_VMRuntime* */ m_focusing_vms;
    
}woort_WAIPO_Debugger;

static void woort_WAIPO_Debugger_active(woort_VMRuntime* vm, void* instance)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;

    if (woort_hashmap_is_empty(&debugger_instance->m_focusing_vms))
        woort_hashmap_insert(&debugger_instance->m_focusing_vms, vm, NULL);

    if (woort_hashmap_contains(&debugger_instance->m_focusing_vms, vm))
    {

    }
}

static void _woort_WAIPO_Debugger_close(void* instance)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;
    free(debugger_instance);
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_attach(void)
{
    woort_WAIPO_Debugger* const debugger_instance = 
        malloc(sizeof(woort_WAIPO_Debugger));

    if (debugger_instance == NULL)
        return false;

    debugger_instance->m_focusing_vm = NULL;

    return woort_VMRuntime_Debugger_attach(
        &woort_WAIPO_Debugger_active,
        debugger_instance,
        &_woort_WAIPO_Debugger_close);
}
