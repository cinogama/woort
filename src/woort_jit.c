#include "woort_jit.h"
#include "woort_codeenv.h"
#include "woort_hashmap.h"
#include "woort_vector.h"
#include "woort_log.h"

#include <assert.h>

typedef struct woort_JIT_Compile_Context
{
    woort_HashMap /**/ m_set;

} woort_JIT_Compile_Context;

WOORT_NODISCARD bool woort_JIT_compile_function(
    const woort_JIT_Interface* interface,
    const woort_Bytecode* function_entry,
    woort_JitFunction* out_compiled_jit_function)
{
    woort_CodeEnv* env;
    if (!woort_CodeEnv_find(function_entry, &env))
    {
        WOORT_DEBUG("Unable to find function: %p's CodeEnv.", function_entry);
        return false;
    }

    assert(function_entry >= env->m_code_begin 
        && function_entry < env->m_code_end);
    
    
}