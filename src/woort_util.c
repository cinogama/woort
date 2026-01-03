#include <stdint.h>
#include <stddef.h>

#include "woort_util.h"

size_t woort_util_abs_diff(
    size_t a,
    size_t b)
{
    return (a > b) ? (a - b) : (b - a);
}