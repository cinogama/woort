#pragma once

/*
woort_bitset.h
*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct woort_Bitset
{
    uint64_t*   m_data;
    size_t      m_bit_count;
    size_t      m_word_count;

} woort_Bitset;

bool woort_bitset_init(woort_Bitset* bitset, size_t bit_count);
void woort_bitset_deinit(woort_Bitset* bitset);

void woort_bitset_set(woort_Bitset* bitset, size_t index);
void woort_bitset_reset(woort_Bitset* bitset, size_t index);
bool woort_bitset_test(const woort_Bitset* bitset, size_t index);

bool woort_bitset_find_first_unset(const woort_Bitset* bitset, size_t* out_index);
