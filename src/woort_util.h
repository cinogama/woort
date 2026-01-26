#pragma once

/*
woort_util.h
*/

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

WOORT_NODISCARD size_t woort_util_abs_diff(
    size_t a,
    size_t b);

WOORT_NODISCARD size_t woort_util_ptr_hash(
    const void* ptr_addr);

WOORT_NODISCARD bool woort_util_ptr_equal(
    const void* ptr_a_addr, 
    const void* ptr_b_addr);
