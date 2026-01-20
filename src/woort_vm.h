#pragma once

/*
woort_vm.h
*/

#include "woort_codeenv.h"
#include "woort_value.h"

typedef struct woort_VMRuntime
{
    woort_CodeEnv* m_env;

    // Stack status.
    woort_Value* m_stack;
    woort_Value* m_sb;
    woort_Value* m_sp;



} woort_VMRuntime;

