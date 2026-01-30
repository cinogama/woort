#include "woort_bitset.h"
#include "woort_log.h"

#include <stdlib.h>
#include <string.h>

WOORT_NODISCARD bool woort_bitset_init(woort_Bitset* bitset, size_t bit_count)
{
    bitset->m_bit_count = bit_count;
    bitset->m_word_count = (bit_count + 63) / 64;

    bitset->m_data = (uint64_t*)calloc(bitset->m_word_count, sizeof(uint64_t));
    if (bitset->m_data == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }
    return true;
}

void woort_bitset_deinit(woort_Bitset* bitset)
{
    if (bitset->m_data)
    {
        free(bitset->m_data);
        bitset->m_data = NULL;
    }
    bitset->m_bit_count = 0;
    bitset->m_word_count = 0;
}

WOORT_NODISCARD bool woort_bitset_set(woort_Bitset* bitset, size_t index)
{
    if (index >= bitset->m_bit_count)
        return false;

    bitset->m_data[index / 64] |= (1ULL << (index % 64));
    return true;
}

WOORT_NODISCARD bool woort_bitset_reset(woort_Bitset* bitset, size_t index)
{
    if (index >= bitset->m_bit_count)
        return false;

    bitset->m_data[index / 64] &= ~(1ULL << (index % 64));
    return true;
}

WOORT_NODISCARD bool woort_bitset_test(const woort_Bitset* bitset, size_t index)
{
    if (index >= bitset->m_bit_count)
        return false;
    return (bitset->m_data[index / 64] & (1ULL << (index % 64))) != 0;
}

WOORT_NODISCARD bool woort_bitset_find_first_unset(const woort_Bitset* bitset, size_t* out_index)
{
    for (size_t i = 0; i < bitset->m_word_count; ++i)
    {
        uint64_t word = bitset->m_data[i];
        if (word != UINT64_MAX)
        {
            for (size_t bit = 0; bit < 64; ++bit)
            {
                if (!((word >> bit) & 1))
                {
                    size_t index = i * 64 + bit;
                    if (index < bitset->m_bit_count)
                    {
                        *out_index = index;
                        return true;
                    }
                    return false;
                }
            }
        }
    }
    return false;
}
