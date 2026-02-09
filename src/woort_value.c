#include "woort_value.h"

void woort_DynBox_box(
    woort_DynBox_ValueType type,
    woort_Value val,
    woort_DynBox* modifing_box);

WOORT_NODISCARD bool woort_DynBox_check(
    woort_DynBox_ValueType expected_type,
    woort_DynBox box);

WOORT_NODISCARD bool woort_DynBox_try_unbox(
    woort_DynBox_ValueType expected_type,
    woort_DynBox box,
    woort_Value* out_val);
