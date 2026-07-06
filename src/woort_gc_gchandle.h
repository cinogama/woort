#pragma once

/*
woort_gc_gchandle.h
*/

#include "woort_gc_gchandle_types.h"

#include "woort_gc_units.h"
#include "woort_value.h"

extern const woort_GCUnitProxy WOORT_GCHANDLE_UNIT_PROXY;

WOORT_NODISCARD const woort_GCHandle* woort_GCHandle_new(
    void* addr,
    /* OPTIONAL */woort_Value* holding,
    woort_GCHandle_UserDestructFunction destructor,
    /* OPTIONAL */ woort_Dylib* dylib);

WOORT_NODISCARD const woort_GCHandle* woort_GCHandle_new_with_marker(
    void* addr,
    woort_GCHandle_UserMarkFunction marker,
    woort_GCHandle_UserDestructFunction destructor,
    /* OPTIONAL */ woort_Dylib* dylib);

WOORT_NODISCARD bool woort_GCHandle_close(woort_GCHandle* gchandle);
