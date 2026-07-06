#include "woort_bitset.h"
#include "woort_log.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WOORT_BITSET_WORD_BITS 64

WOORT_NODISCARD bool woort_bitset_init(woort_Bitset* bitset, size_t bit_count)
{
    bitset->m_bit_count = bit_count;
    bitset->m_word_count = (bit_count + (WOORT_BITSET_WORD_BITS - 1)) / WOORT_BITSET_WORD_BITS;

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
    /* Safe to call even if _init failed (m_data may be NULL). */
    if (bitset->m_data != NULL)
    {
        free(bitset->m_data);
        bitset->m_data = NULL;
    }
    bitset->m_bit_count = 0;
    bitset->m_word_count = 0;
}

WOORT_NODISCARD bool woort_bitset_set(woort_Bitset* bitset, size_t index)
{
    assert(index < bitset->m_bit_count);
    bitset->m_data[index / WOORT_BITSET_WORD_BITS] |= (1ULL << (index % WOORT_BITSET_WORD_BITS));
    return true;
}

WOORT_NODISCARD bool woort_bitset_reset(woort_Bitset* bitset, size_t index)
{
    assert(index < bitset->m_bit_count);
    bitset->m_data[index / WOORT_BITSET_WORD_BITS] &= ~(1ULL << (index % WOORT_BITSET_WORD_BITS));
    return true;
}

WOORT_NODISCARD bool woort_bitset_test(const woort_Bitset* bitset, size_t index)
{
    assert(index < bitset->m_bit_count);
    return (bitset->m_data[index / WOORT_BITSET_WORD_BITS] & (1ULL << (index % WOORT_BITSET_WORD_BITS))) != 0;
}

void woort_bitset_clear(woort_Bitset* bitset)
{
    memset(bitset->m_data, 0, bitset->m_word_count * sizeof(uint64_t));
}

WOORT_NODISCARD size_t woort_bitset_size(const woort_Bitset* bitset)
{
    return bitset->m_bit_count;
}

WOORT_NODISCARD bool woort_bitset_find_first_unset(const woort_Bitset* bitset, size_t* out_index)
{
    for (size_t i = 0; i < bitset->m_word_count; ++i)
    {
        uint64_t word = bitset->m_data[i];
        if (word != UINT64_MAX)
        {
            for (size_t bit = 0; bit < WOORT_BITSET_WORD_BITS; ++bit)
            {
                if (!((word >> bit) & 1))
                {
                    size_t index = i * WOORT_BITSET_WORD_BITS + bit;
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
