#pragma once

/*
woort_codeenv.h
*/
#if __STDC_NO_ATOMICS__
#   define woort_atom
#else
#   include <stdatomic.h>
#endif

#include "woort_opcode_formal.h"
#include "woort_Value.h"

bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

typedef struct woort_CodeEnv woort_CodeEnv;

