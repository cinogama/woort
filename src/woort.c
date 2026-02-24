#include "woort.h"
#include "woort_codeenv.h"
#include "woort_log.h"
#include "woort_gc.h"

#include <stdlib.h>

void woort_init(void)
{
    woort_GC_bootup();

    if (!woort_CodeEnv_bootup())
    {
        WOORT_DEBUG("Failed to bootup code env.");
        abort();
    }
}
void woort_shutdown(void)
{
    woort_CodeEnv_shutdown();

    woort_GC_shutdown();
}