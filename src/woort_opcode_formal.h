#pragma once

#include <stdint.h>
#include <stddef.h>

/*
woort_opcode_formal.h
*/

typedef struct woort_OpcodeFormal_OP6_26
{
    _Alignas(4) uint8_t     m_op6_2;
    char                    m_24[3];

} woort_OpcodeFormal_OP6_26;
#define woort_OpcodeFormal_OP6_26_cons(op6) \
    (woort_Bytecode){.m_op6_26 = (const woort_OpcodeFormal_OP6_26){ .m_op6_2 = ((uint8_t)(op6)) << 2 }}

typedef struct woort_OpcodeFormal_OP6_U26
{
    _Alignas(4) uint8_t     m_op6_u26l2;
    uint8_t                 m_u26h8;
    uint16_t                m_u26m16;

} woort_OpcodeFormal_OP6_U26;
#define _woort_OpcodeFormal_OP6_U26_cons(op6, u26)                      \
    (woort_Bytecode){                                                   \
        .m_op6_u26 = (const woort_OpcodeFormal_OP6_U26){                \
            .m_op6_u26l2 = ((op6) << 2) | (uint8_t)((u26) & 0b011u),    \
            .m_u26h8 = (uint8_t)(((u26) >> 18) & 0xffu),                \
            .m_u26m16 = (uint16_t)(((u26) >> 2) & 0xffffu),             \
        }                                                               \
    }
#define woort_OpcodeFormal_OP6_U26_cons(op6, u26)                       \
    _woort_OpcodeFormal_OP6_U26_cons((uint8_t)(op6), (uint32_t)(u26))

typedef struct woort_OpcodeFormal_OP6_U18_I8
{
    _Alignas(4) uint8_t     m_op6_u18h2;
    int8_t                  m_i8;
    uint16_t                m_u18l16;

} woort_OpcodeFormal_OP6_U18_I8;
#define _woort_OpcodeFormal_OP6_U18_I8_cons(op6, u18, i8)                   \
    (woort_Bytecode){                                                       \
        .m_op6_u18_i8 = (const woort_OpcodeFormal_OP6_U18_I8){              \
            .m_op6_u18h2 = ((op6) << 2) | (uint8_t)(((u18) >> 16) & 0b11u), \
            .m_i8 = (i8),                                                   \
            .m_u18l16 = (uint16_t)((u18) & 0xffffu),                        \
        }                                                                   \
    }
#define woort_OpcodeFormal_OP6_U18_I8_cons(op6, u18, i8)                    \
    _woort_OpcodeFormal_OP6_U18_I8_cons((uint8_t)(op6), (uint32_t)(u18), (int8_t)(i8))

typedef struct woort_OpcodeFormal_OP6M2_U24
{
    _Alignas(4) uint8_t     m_op6_m2;
    uint8_t                 m_u24h8;
    uint16_t                m_u24l16;

} woort_OpcodeFormal_OP6M2_U24;
#define _woort_OpcodeFormal_OP6M2_U24_cons(op6, m2, u24)        \
    (woort_Bytecode){                                           \
        .m_op6m2_u24 = (const woort_OpcodeFormal_OP6M2_U24){    \
            .m_op6_m2 = ((op6) << 2) | (m2),                    \
            .m_u24h8 = (uint8_t)(((u24) >> 16) & 0xffu),        \
            .m_u24l16 = (uint16_t)((u24) & 0xffffu),            \
        }                                                       \
    }
#define woort_OpcodeFormal_OP6M2_U24_cons(op6, m2, u24)         \
    _woort_OpcodeFormal_OP6M2_U24_cons((uint8_t)(op6), (uint8_t)(m2), (uint32_t)(u24))

typedef struct woort_OpcodeFormal_OP6M2_I8_16
{
    _Alignas(4) uint8_t     m_op6_m2;
    int8_t                  m_i8;
    char                    m_16[2];

} woort_OpcodeFormal_OP6M2_I8_16;
#define _woort_OpcodeFormal_OP6M2_I8_16_cons(op6, m2, i8)           \
    (woort_Bytecode){                                               \
        .m_op6m2_i8_16 = (const woort_OpcodeFormal_OP6M2_I8_16){    \
            .m_op6_m2 = ((op6) << 2) | (m2),                        \
            .m_i8 = (i8),                                           \
        }                                                           \
    }
#define woort_OpcodeFormal_OP6M2_I8_16_cons(op6, m2, i8)            \
    _woort_OpcodeFormal_OP6M2_I8_16_cons((uint8_t)(op6), (uint8_t)(m2), (int8_t)(i8))

typedef struct woort_OpcodeFormal_OP6M2_I8_I8_8
{
    _Alignas(4) uint8_t     m_op6_m2;
    int8_t                  m_i8_1;
    int8_t                  m_i8_2;
    char                    m_8[1];

} woort_OpcodeFormal_OP6M2_I8_I8_8;
#define _woort_OpcodeFormal_OP6M2_I8_I8_8_cons(op6, m2, i8_1, i8_2)     \
    (woort_Bytecode){                                                   \
        .m_op6m2_i8_i8_8 = (const woort_OpcodeFormal_OP6M2_I8_I8_8){    \
            .m_op6_m2 = ((op6) << 2) | (m2),                            \
            .m_i8_1 = (i8_1),                                           \
            .m_i8_2 = (i8_2),                                           \
        }                                                               \
    }
#define woort_OpcodeFormal_OP6M2_I8_I8_8_cons(op6, m2, i8_1, i8_2)      \
    _woort_OpcodeFormal_OP6M2_I8_I8_8_cons((uint8_t)(op6), (uint8_t)(m2), (int8_t)(i8_1), (int8_t)(i8_2))

typedef struct woort_OpcodeFormal_OP6M2_I8_I16
{
    _Alignas(4) uint8_t     m_op6_m2;
    int8_t                  m_i8;
    int16_t                 m_i16;

} woort_OpcodeFormal_OP6M2_I8_I16;
#define _woort_OpcodeFormal_OP6M2_I8_I16_cons(op6, m2, i8, i16)     \
    (woort_Bytecode){                                               \
        .m_op6m2_i8_i16 = (const woort_OpcodeFormal_OP6M2_I8_I16){  \
            .m_op6_m2 = ((op6) << 2) | (m2),                        \
            .m_i8 = (i8),                                           \
            .m_i16 = (i16),                                         \
        }                                                           \
    }
#define woort_OpcodeFormal_OP6M2_I8_I16_cons(op6, m2, i8, i16)      \
    _woort_OpcodeFormal_OP6M2_I8_I16_cons((uint8_t)(op6), (uint8_t)(m2), (int8_t)(i8), (int16_t)(i16))

typedef struct woort_OpcodeFormal_OP6M2_8_I16
{
    _Alignas(4) uint8_t     m_op6_m2;
    char                    m_8[1];
    int16_t                 m_i16;

} woort_OpcodeFormal_OP6M2_8_I16;
#define _woort_OpcodeFormal_OP6M2_8_I16_cons(op6, m2, i16)          \
    (woort_Bytecode){                                               \
        .m_op6m2_8_i16 = (const woort_OpcodeFormal_OP6M2_8_I16){    \
            .m_op6_m2 = ((op6) << 2) | (m2),                        \
            .m_i16 = (i16),                                         \
        }                                                           \
    }
#define woort_OpcodeFormal_OP6M2_8_I16_cons(op6, m2, i16)           \
    _woort_OpcodeFormal_OP6M2_8_I16_cons((uint8_t)(op6), (uint8_t)(m2), (int16_t)(i16))

typedef struct woort_OpcodeFormal_OP6M2_I8_U16
{
    _Alignas(4) uint8_t     m_op6_m2;
    int8_t                  m_i8;
    uint16_t                m_u16;

} woort_OpcodeFormal_OP6M2_I8_U16;
#define _woort_OpcodeFormal_OP6M2_I8_U16_cons(op6, m2, i8, u16)     \
    (woort_Bytecode){                                               \
        .m_op6m2_i8_u16 = (const woort_OpcodeFormal_OP6M2_I8_U16){  \
            .m_op6_m2 = ((op6) << 2) | (m2),                        \
            .m_i8 = (i8),                                           \
            .m_u16 = (u16),                                         \
        }                                                           \
    }
#define woort_OpcodeFormal_OP6M2_I8_U16_cons(op6, m2, i8, u16)      \
    _woort_OpcodeFormal_OP6M2_I8_U16_cons((uint8_t)(op6), (uint8_t)(m2), (int8_t)(i8), (uint16_t)(u16))

typedef struct woort_OpcodeFormal_OP6M2_I8_I8_I8
{
    _Alignas(4) uint8_t     m_op6_m2;
    int8_t                  m_i8_1;
    int8_t                  m_i8_2;
    int8_t                  m_i8_3;

} woort_OpcodeFormal_OP6M2_I8_I8_I8;
#define _woort_OpcodeFormal_OP6M2_I8_I8_I8_cons(op6, m2, i8_1, i8_2, i8_3)  \
    (woort_Bytecode){                                                       \
        .m_op6m2_i8_i8_i8 = (const woort_OpcodeFormal_OP6M2_I8_I8_I8){      \
            .m_op6_m2 = ((op6) << 2) | (m2),                                \
            .m_i8_1 = (i8_1),                                               \
            .m_i8_2 = (i8_2),                                               \
            .m_i8_3 = (i8_3),                                               \
        }                                                                   \
    }
#define woort_OpcodeFormal_OP6M2_I8_I8_I8_cons(op6, m2, i8_1, i8_2, i8_3)   \
    _woort_OpcodeFormal_OP6M2_I8_I8_I8_cons((uint8_t)(op6), (uint8_t)(m2), (int8_t)(i8_1), (int8_t)(i8_2), (int8_t)(i8_3))

typedef struct woort_OpcodeFormal_OP6M2_I8_I8_U8
{
    _Alignas(4) uint8_t     m_op6_m2;
    int8_t                  m_i8_1;
    int8_t                  m_i8_2;
    uint8_t                 m_u8_3;

} woort_OpcodeFormal_OP6M2_I8_I8_U8;
#define _woort_OpcodeFormal_OP6M2_I8_I8_U8_cons(op6, m2, i8_1, i8_2, u8_3)  \
    (woort_Bytecode){                                                       \
        .m_op6m2_i8_i8_u8 = (const woort_OpcodeFormal_OP6M2_I8_I8_U8){      \
            .m_op6_m2 = ((op6) << 2) | (m2),                                \
            .m_i8_1 = (i8_1),                                               \
            .m_i8_2 = (i8_2),                                               \
            .m_u8_3 = (u8_3),                                               \
        }                                                                   \
    }
#define woort_OpcodeFormal_OP6M2_I8_I8_U8_cons(op6, m2, i8_1, i8_2, u8_3)   \
    _woort_OpcodeFormal_OP6M2_I8_I8_U8_cons((uint8_t)(op6), (uint8_t)(m2), (int8_t)(i8_1), (int8_t)(i8_2), (uint8_t)(u8_3))

#define woort_OpCodeFormal_cons(FORMAL, ...) \
    woort_OpcodeFormal_FORMAL##_cons(__VA_ARGS__)

typedef union woort_Bytecode
{
    uint8_t                             m_op6m2;

    woort_OpcodeFormal_OP6_26           m_op6_26;
    woort_OpcodeFormal_OP6_U26          m_op6_u26;
    woort_OpcodeFormal_OP6_U18_I8       m_op6_u18_i8;

    woort_OpcodeFormal_OP6M2_U24        m_op6m2_u24;
    woort_OpcodeFormal_OP6M2_8_I16      m_op6m2_8_i16;
    woort_OpcodeFormal_OP6M2_I8_16      m_op6m2_i8_16;
    woort_OpcodeFormal_OP6M2_I8_U16     m_op6m2_i8_u16;
    woort_OpcodeFormal_OP6M2_I8_I16     m_op6m2_i8_i16;
    woort_OpcodeFormal_OP6M2_I8_I8_8    m_op6m2_i8_i8_8;
    woort_OpcodeFormal_OP6M2_I8_I8_U8   m_op6m2_i8_i8_u8;
    woort_OpcodeFormal_OP6M2_I8_I8_I8   m_op6m2_i8_i8_i8;

}woort_Bytecode;