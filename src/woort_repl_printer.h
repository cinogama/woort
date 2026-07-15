#pragma once

#include "woort.h"

#include "woort_value_types.h"

WOORT_NODISCARD bool woort_REPLPrinter_print_mixed(
    woort_REPLPrinter* printer, woort_DynBox boxed);

WOORT_NODISCARD bool woort_REPLPrinter_print_debug(
    woort_REPLPrinter* printer, woort_DynBox boxed);

WOORT_NODISCARD bool woort_REPLPrinter_print_string(
    woort_REPLPrinter* printer, const char* str);