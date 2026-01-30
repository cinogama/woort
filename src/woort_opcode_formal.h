#pragma once

#include <stdint.h>
#include <stddef.h>

/*
woort_opcode_formal.h
*/

typedef uint32_t woort_Bytecode;

#define WOORT_BYTECODE_OP6_MASK 0xfc000000u
#define WOORT_BYTECODE_M2_MASK 0x03000000u
#define WOORT_BYTECODE_A8_MASK 0x00ff0000u
#define WOORT_BYTECODE_B8_MASK 0x0000ff00u
#define WOORT_BYTECODE_C8_MASK 0x000000ffu
#define WOORT_BYTECODE_OPM8_MASK    \
    (WOORT_BYTECODE_OP6_MASK        \
    | WOORT_BYTECODE_M2_MASK)
#define WOORT_BYTECODE_BC16_MASK    \
    (WOORT_BYTECODE_B8_MASK         \
    | WOORT_BYTECODE_C8_MASK)
#define WOORT_BYTECODE_ABC24_MASK   \
    (WOORT_BYTECODE_A8_MASK         \
    | WOORT_BYTECODE_B8_MASK        \
    | WOORT_BYTECODE_C8_MASK)
#define WOORT_BYTECODE_MA10_MASK    \
    (WOORT_BYTECODE_M2_MASK         \
    | WOORT_BYTECODE_A8_MASK)
#define WOORT_BYTECODE_MAB18_MASK   \
    (WOORT_BYTECODE_M2_MASK         \
    | WOORT_BYTECODE_A8_MASK        \
    | WOORT_BYTECODE_B8_MASK)
#define WOORT_BYTECODE_MABC26_MASK  \
    (WOORT_BYTECODE_M2_MASK         \
    | WOORT_BYTECODE_A8_MASK        \
    | WOORT_BYTECODE_B8_MASK        \
    | WOORT_BYTECODE_C8_MASK)

#define WOORT_BYTECODE_OP6_SHIFT 26
#define WOORT_BYTECODE_M2_SHIFT 24
#define WOORT_BYTECODE_A8_SHIFT 16
#define WOORT_BYTECODE_B8_SHIFT 8
#define WOORT_BYTECODE_C8_SHIFT 0
#define WOORT_BYTECODE_OPM8_SHIFT 24
#define WOORT_BYTECODE_BC16_SHIFT 0
#define WOORT_BYTECODE_ABC24_SHIFT 0
#define WOORT_BYTECODE_MA10_SHIFT 16
#define WOORT_BYTECODE_MAB18_SHIFT 8
#define WOORT_BYTECODE_MABC26_SHIFT 0

#define WOORT_MAKE_BYTECODE(FORMAL, V) (woort_Bytecode)(    \
    (((uint32_t)(V) << WOORT_BYTECODE_##FORMAL##_SHIFT) & WOORT_BYTECODE_##FORMAL##_MASK))

#define WOORT_BYTECODE(FORMAL, BYTECODE) (uint32_t)(        \
    ((woort_Bytecode)(BYTECODE) & WOORT_BYTECODE_##FORMAL##_MASK) >> WOORT_BYTECODE_##FORMAL##_SHIFT)

#define woort_OpcodeFormal_OP6_cons(op6)                    \
    WOORT_MAKE_BYTECODE(OP6, op6)

#define woort_OpcodeFormal_OP6_M2_cons(op6, m2)             \
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2))

#define woort_OpcodeFormal_OP6_MABC26_cons(op6, u26)        \
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(MABC26, u26))

#define woort_OpcodeFormal_OP6_MA10_BC16_cons(op6, ma10, bc16)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(MA10, ma10)                       \
    | WOORT_MAKE_BYTECODE(BC16, bc16))

#define woort_OpcodeFormal_OP6_MAB18_C8_cons(op6, mab18, c8)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(MAB18, mab18)                     \
    | WOORT_MAKE_BYTECODE(C8, c8))

#define woort_OpcodeFormal_OP6_M2_ABC24_cons(op6, m2, abc24)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(ABC24, abc24))

#define woort_OpcodeFormal_OP6_M2_C8_cons(op6, m2, c8)      \
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(C8, c8))

#define woort_OpcodeFormal_OP6_M2_B8_C8_cons(op6, m2, b8, c8)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(B8, b8)                           \
    | WOORT_MAKE_BYTECODE(C8, c8))

#define woort_OpcodeFormal_OP6_M2_A8_BC16_cons(op6, m2, a8, bc16)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(A8, a8)                           \
    | WOORT_MAKE_BYTECODE(BC16, bc16))

#define woort_OpcodeFormal_OP6_M2_BC16_cons(op6, m2, bc16)  \
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(BC16, bc16))

#define woort_OpcodeFormal_OP6_M2_B8_C8_cons(op6, m2, b8, c8)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(B8, b8)                           \
    | WOORT_MAKE_BYTECODE(C8, c8))           

#define woort_OpcodeFormal_OP6_M2_A8_B8_C8_cons(op6, m2, a8, b8, c8)\
    (WOORT_MAKE_BYTECODE(OP6, op6)                          \
    | WOORT_MAKE_BYTECODE(M2, m2)                           \
    | WOORT_MAKE_BYTECODE(A8, a8)                           \
    | WOORT_MAKE_BYTECODE(B8, b8)                           \
    | WOORT_MAKE_BYTECODE(C8, c8))                           

#define woort_OpCodeFormal_cons(FORMAL, ...) \
    woort_OpcodeFormal_##FORMAL##_cons(__VA_ARGS__)
