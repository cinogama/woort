#pragma once

/*
woort_gc_gchandle_types.h
*/

#include "woort_value_types.h"

struct woort_GCHandle
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    union
    {
        woort_Value m_hold_value;
        woort_GCHandle_UserMarkFunction m_user_mark_callback;
    };
    /* OPTIONAL, NULL if closed */ void* m_user_handle;
    woort_GCHandle_UserDestructFunction m_user_destruct_callback;
    /* OPTIONAL */ woort_Dylib* m_dylib;

};
