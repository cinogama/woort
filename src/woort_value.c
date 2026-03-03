#include "woort_value.h"
#include <math.h>
#include <string.h>

/*
 * 62-bit Floating Point Format:
 * ==================================
 * Format: | S (1 bit) | E (9 bits) | M (52 bits) |
 * 
 * S: Sign bit (0 = positive, 1 = negative)
 * E: Exponent (9 bits, bias = 255)
 *    - Valid exponent range: [-254, 255]
 *    - Special values: E=0 with M=0 -> ±0
 * M: Mantissa (52 bits, implicit leading 1)
 *
 * Conversion from IEEE 754 double (64-bit):
 *   - Original format: S (1) | E (11, bias=1023) | M (52)
 *   - New exponent = Original exponent - 768 (1023 - 255)
 *   - Range check: original exponent must be in [-254+1023, 255+1023] = [769, 1278]
 *   - But we also need to handle denormals and special values
 */

#define WOORT_F62_EXP_BITS       9
#define WOORT_F61_MANT_BITS      52
#define WOORT_F62_BIAS           255
#define WOORT_F64_BIAS           1023
#define WOORT_F62_EXP_MASK       0x1FF
#define WOORT_F62_MANT_MASK      0xFFFFFFFFFFFFF
#define WOORT_F62_SIGN_SHIFT     61
#define WOORT_F62_EXP_SHIFT      52

/* IEEE 754 double precision constants */
#define WOORT_F64_EXP_MASK       0x7FF0000000000000ULL
#define WOORT_F64_MANT_MASK      0x000FFFFFFFFFFFFFULL
#define WOORT_F64_SIGN_MASK      0x8000000000000000ULL
#define WOORT_F64_SIGN_SHIFT     63
#define WOORT_F64_EXP_SHIFT      52

/* Special 62-bit encodings */
#define WOORT_F62_POS_ZERO       0x0000000000000000ULL
#define WOORT_F62_NEG_ZERO       0x2000000000000000ULL

bool _woort_try_pack_f64_to_f62(double val, uint64_t* out_val)
{
    /* Get the bit representation of the double */
    uint64_t bits;
    memcpy(&bits, &val, sizeof(double));
    
    /* Extract sign */
    uint64_t sign = (bits >> WOORT_F64_SIGN_SHIFT) & 1;
    
    /* Extract exponent (11 bits) */
    uint16_t exp64 = (uint16_t)((bits & WOORT_F64_EXP_MASK) >> WOORT_F64_EXP_SHIFT);
    
    /* Extract mantissa (52 bits) */
    uint64_t mant = bits & WOORT_F64_MANT_MASK;
    
    /* Handle special cases */
    
    /* Case 1: Zero (+0.0 or -0.0) */
    if (exp64 == 0 && mant == 0) {
        *out_val = sign << WOORT_F62_SIGN_SHIFT;
        return true;
    }
    
    /* Case 2: Infinity - cannot be represented in 62-bit format */
    if (exp64 == 0x7FF && mant == 0) {
        return false;
    }
    
    /* Case 3: NaN - cannot be represented in 62-bit format */
    if (exp64 == 0x7FF && mant != 0) {
        return false;
    }
    
    /* Case 4: Denormalized numbers */
    if (exp64 == 0 && mant != 0) {
        /* Denormalized numbers have exponent = -1022 in IEEE 754
         * They cannot be exactly represented in our 62-bit format
         * because we don't support denormals */
        return false;
    }
    
    /* Case 5: Normal numbers */
    /* Calculate the actual exponent value */
    int32_t actual_exp = (int32_t)exp64 - WOORT_F64_BIAS;
    
    /* Calculate the new exponent for 62-bit format */
    int32_t new_exp = actual_exp + WOORT_F62_BIAS;
    
    /* Check if the exponent fits in 9 bits (must be > 0 and < 512)
     * Note: We don't support denormals, so exp must be at least 1
     * Maximum valid exponent is 511, but we reserve 0 for zero
     * So valid range is [1, 511], which means actual_exp in [-254, 255] */
    if (new_exp < 1 || new_exp > 511) {
        return false;
    }
    
    /* Pack the 62-bit float:
     * Bit 61: sign
     * Bits 60-52: exponent (9 bits)
     * Bits 51-0: mantissa (52 bits)
     */
    *out_val = (sign << WOORT_F62_SIGN_SHIFT)
             | ((uint64_t)new_exp << WOORT_F62_EXP_SHIFT)
             | mant;
    
    return true;
}

double _woort_unpack_f62_to_f64(uint64_t val)
{
    /* Extract components from 62-bit format */
    uint64_t sign = (val >> WOORT_F62_SIGN_SHIFT) & 1;
    uint16_t exp62 = (uint16_t)((val >> WOORT_F62_EXP_SHIFT) & WOORT_F62_EXP_MASK);
    uint64_t mant = val & WOORT_F62_MANT_MASK;
    
    /* Handle zero */
    if (exp62 == 0 && mant == 0) {
        /* Return ±0.0 */
        uint64_t result = sign << WOORT_F64_SIGN_SHIFT;
        double d;
        memcpy(&d, &result, sizeof(double));
        return d;
    }
    
    /* Calculate IEEE 754 exponent
     * exp64 = exp62 - WOORT_F62_BIAS + WOORT_F64_BIAS
     * exp64 = exp62 + 768
     */
    uint16_t exp64 = exp62 + (WOORT_F64_BIAS - WOORT_F62_BIAS);
    
    /* Pack the 64-bit IEEE 754 double */
    uint64_t result = (sign << WOORT_F64_SIGN_SHIFT)
                    | ((uint64_t)exp64 << WOORT_F64_EXP_SHIFT)
                    | mant;
    
    double d;
    memcpy(&d, &result, sizeof(double));
    return d;
}

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
