#pragma once

/*
woort_gc_gchandle.h
*/

#include "woort.h"

#include "woort_gc_units.h"
#include "woort_value.h"

typedef struct woort_GCHandle
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    union
    {
        woort_Value m_hold_value;
        woort_GCHandle_UserMarkFunction m_user_mark_callback;
    };
    void* m_user_handle;
    woort_GCHandle_UserDestructFunction m_user_destruct_callback;

} woort_GCHandle;

extern const woort_GCUnitProxy g_gchandle_unit_proxy;

woort_GCHandle* woort_GCHandle_new(
    void* addr, 
    /* OPTIONAL */woort_Value* holding,  
    woort_GCHandle_UserDestructFunction destructor);

woort_GCHandle* woort_GCHandle_new_with_marker(
    void* addr,
    woort_GCHandle_UserMarkFunction marker,
    woort_GCHandle_UserDestructFunction destructor);
