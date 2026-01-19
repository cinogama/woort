#include "woort.h"
#include "woomem.h"
#include "woort_codeenv.h"
#include "woort_log.h"

#include <stdlib.h>

void woort_init(void)
{
    woomem_init(NULL, NULL, NULL);

    if (!woort_CodeEnv_bootup())
    {
        WOORT_DEBUG("Failed to bootup code env.");
        abort();
    }
}
void woort_shutdown(void)
{
    woomem_shutdown();

    woort_CodeEnv_shutdown();
}