#pragma once

/*
 * woort_disassembly.h
 */

#include "woort_opcode_formal.h"
#include "woort_codeenv.h"

const woort_Bytecode* woort_disassembly(const woort_Bytecode* c);
void woort_dump_codes(const woort_CodeEnv* code_env);
