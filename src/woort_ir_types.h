#pragma once

/*
 * woort_ir_types.h
 */

#include <stdint.h>
#include <stddef.h>

typedef struct woort_IRCompiler woort_IRCompiler;
typedef struct woort_IRFunction woort_IRFunction;
typedef struct woort_IRBlock woort_IRBlock;
typedef struct woort_IRValue woort_IRValue;
typedef struct woort_IRPHI woort_IRPHI;
typedef struct woort_CodeEnv woort_CodeEnv;

typedef uint32_t woort_IRGlobalIndex;

#define WOORT_IR_GLOBAL_INDEX_INVALID ((woort_IRGlobalIndex)UINT32_MAX)
