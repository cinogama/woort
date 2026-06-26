#pragma once

#include "woort_value.h"
#include "woort_opcode_dispatcher.h"
#include "woort_codeenv.h"

#include <stdbool.h>

typedef void* woort_JIT_Backend_Emmiter;

typedef bool (*woort_JIT_Backend_EmitPrologue)(
    const woort_FunctionBoundary*, 
    woort_JIT_Backend_Emmiter* out_emmiter);

typedef bool (*woort_JIT_Backend_EmitEpilogue)(
    woort_JIT_Backend_Emmiter, 
    woort_JitFunction* out_code);

typedef bool (*woort_JIT_Backend_DropCode)(
    woort_JitFunction*);

typedef struct woort_JIT_Backend
{
    woort_JIT_Backend_EmitPrologue m_emit_prologue;
    woort_JIT_Backend_EmitEpilogue m_emit_epilogue;

    const woort_OpcodeDispatchers* m_dispatchers;

    woort_JIT_Backend_DropCode m_code_dropper;

} woort_JIT_Backend;

void woort_JIT_bootup(void);
void woort_JIT_shutdown(void);
