#include "woort.h"

#include "woort_codeenv.h"
#include "woort_log.h"
#include "woort_gc.h"
#include "woort_vm.h"
#include "woort_ir_compiler.h"
#include "woort_value.h"

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

WOORT_NODISCARD /* OPTIONAL */ woort_IRCompiler* woort_IRCompiler_create(void)
{
    woort_IRCompiler* c = malloc(sizeof(woort_IRCompiler));
    if (!c)
        return NULL;
    woort_IRCompiler_init(c);
    return c;
}

void woort_IRCompiler_close(woort_IRCompiler* c)
{
    assert(c != NULL);

    woort_IRCompiler_deinit(c);
    free(c);
}

// Runtime API

void woort_set_value(woort_value* dst, const woort_value* val)
{
    *dst = *val;
}
void woort_set_int(woort_value* dst, woort_Int val)
{
    dst->m_integer = val;
}
void woort_set_real(woort_value* dst, woort_Real val)
{
    dst->m_real = val;
}
void woort_set_float(woort_value* dst, float val)
{
    dst->m_real = (woort_Real)val;
}
void woort_set_buffer(woort_value* dst, const void* val, size_t len);
void woort_set_string(woort_value* dst, woort_U8CString val);
void woort_set_vec(woort_value* dst, size_t reserved_size);
void woort_set_map(woort_value* dst, size_t reserved_size);
void woort_set_struct(woort_value* dst, size_t size);
void woort_set_gchandle(
    woort_value* dst,
    void* ptr,
    /* OPTIONAL */ const woort_value* holding,
    woort_GCHandle_UserDestructFunction destructor);
void woort_set_gcstruct(
    woort_value* dst,
    void* ptr,
    woort_GCHandle_UserMarkFunction marker,
    woort_GCHandle_UserDestructFunction destructor);