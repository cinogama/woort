#pragma once

/*
 * woort_disassembly.h
 */

#include "woort_opcode_formal.h"
#include "woort_codeenv.h"

typedef int (*woort_Disassembly_DumpCallback)(const char*, ...);

WOORT_NODISCARD const woort_Bytecode* woort_disassembly(
    const woort_Bytecode* c, woort_Disassembly_DumpCallback callback);
void woort_dump_codes(
    const woort_CodeEnv* code_env, woort_Disassembly_DumpCallback callback);
