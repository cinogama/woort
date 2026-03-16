#pragma once

#include <stdint.h>

/*
woort_ir_op.h
*/

typedef enum woort_IR_Operate_kind
{
    WOORT_IR_OPERATE_KIND_NOP,
    WOORT_IR_OPERATE_KIND_MOV,
    WOORT_IR_OPERATE_KIND_ITOR,
    WOORT_IR_OPERATE_KIND_ITOS,
    WOORT_IR_OPERATE_KIND_RTOI,
    WOORT_IR_OPERATE_KIND_RTOS,
    WOORT_IR_OPERATE_KIND_STOI,
    WOORT_IR_OPERATE_KIND_STOR,
    WOORT_IR_OPERATE_KIND_RET,
    WOORT_IR_OPERATE_KIND_RETV,

    WOORT_IR_OPERATE_KIND_ADDI,
    WOORT_IR_OPERATE_KIND_SUBI,
    WOORT_IR_OPERATE_KIND_MULI,
    WOORT_IR_OPERATE_KIND_DIVI,
    WOORT_IR_OPERATE_KIND_MODI,
    WOORT_IR_OPERATE_KIND_NEGI,
    WOORT_IR_OPERATE_KIND_LTI,
    WOORT_IR_OPERATE_KIND_GTI,
    WOORT_IR_OPERATE_KIND_LEI,
    WOORT_IR_OPERATE_KIND_GEI,
    WOORT_IR_OPERATE_KIND_EQI,
    WOORT_IR_OPERATE_KIND_NEI,

    WOORT_IR_OPERATE_KIND_ADDR,
    WOORT_IR_OPERATE_KIND_SUBR,
    WOORT_IR_OPERATE_KIND_MULR,
    WOORT_IR_OPERATE_KIND_DIVR,
    WOORT_IR_OPERATE_KIND_MODR,
    WOORT_IR_OPERATE_KIND_NEGR,
    WOORT_IR_OPERATE_KIND_LTR,
    WOORT_IR_OPERATE_KIND_GTR,
    WOORT_IR_OPERATE_KIND_LER,
    WOORT_IR_OPERATE_KIND_GER,
    WOORT_IR_OPERATE_KIND_EQR,
    WOORT_IR_OPERATE_KIND_NER,

    WOORT_IR_OPERATE_KIND_ADDS,
    WOORT_IR_OPERATE_KIND_LTS,
    WOORT_IR_OPERATE_KIND_GTS,
    WOORT_IR_OPERATE_KIND_LES,
    WOORT_IR_OPERATE_KIND_GES,
    WOORT_IR_OPERATE_KIND_EQS,
    WOORT_IR_OPERATE_KIND_NES,

    WOORT_IR_OPERATE_KIND_LAND,
    WOORT_IR_OPERATE_KIND_LOR,
    WOORT_IR_OPERATE_KIND_LNOT,

    WOORT_IR_OPERATE_KIND_LDIDXVEC,
    WOORT_IR_OPERATE_KIND_LDIDXVECX,
    WOORT_IR_OPERATE_KIND_LDIDXSTRUCT,
    WOORT_IR_OPERATE_KIND_LDIDXSTRING,

    WOORT_IR_OPERATE_KIND_LDIDXDICTI,
    WOORT_IR_OPERATE_KIND_LDIDXDICTR,
    WOORT_IR_OPERATE_KIND_LDIDXDICTB,
    WOORT_IR_OPERATE_KIND_LDIDXDICTX,

    WOORT_IR_OPERATE_KIND_STIDXVECI,
    WOORT_IR_OPERATE_KIND_STIDXVECR,
    WOORT_IR_OPERATE_KIND_STIDXVECB,
    WOORT_IR_OPERATE_KIND_STIDXVECX,

    WOORT_IR_OPERATE_KIND_STIDXDICTII,
    WOORT_IR_OPERATE_KIND_STIDXDICTIR,
    WOORT_IR_OPERATE_KIND_STIDXDICTIB,
    WOORT_IR_OPERATE_KIND_STIDXDICTIX,

    WOORT_IR_OPERATE_KIND_STIDXDICTRI,
    WOORT_IR_OPERATE_KIND_STIDXDICTRR,
    WOORT_IR_OPERATE_KIND_STIDXDICTRB,
    WOORT_IR_OPERATE_KIND_STIDXDICTRX,

    WOORT_IR_OPERATE_KIND_STIDXDICTBI,
    WOORT_IR_OPERATE_KIND_STIDXDICTBR,
    WOORT_IR_OPERATE_KIND_STIDXDICTBB,
    WOORT_IR_OPERATE_KIND_STIDXDICTBX,

    WOORT_IR_OPERATE_KIND_STIDXDICTXI,
    WOORT_IR_OPERATE_KIND_STIDXDICTXR,
    WOORT_IR_OPERATE_KIND_STIDXDICTXB,
    WOORT_IR_OPERATE_KIND_STIDXDICTXX,

    WOORT_IR_OPERATE_KIND_STIDXMAPII,
    WOORT_IR_OPERATE_KIND_STIDXMAPIR,
    WOORT_IR_OPERATE_KIND_STIDXMAPIB,
    WOORT_IR_OPERATE_KIND_STIDXMAPIX,

    WOORT_IR_OPERATE_KIND_STIDXMAPRI,
    WOORT_IR_OPERATE_KIND_STIDXMAPRR,
    WOORT_IR_OPERATE_KIND_STIDXMAPRB,
    WOORT_IR_OPERATE_KIND_STIDXMAPRX,

    WOORT_IR_OPERATE_KIND_STIDXMAPBI,
    WOORT_IR_OPERATE_KIND_STIDXMAPBR,
    WOORT_IR_OPERATE_KIND_STIDXMAPBB,
    WOORT_IR_OPERATE_KIND_STIDXMAPBX,

    WOORT_IR_OPERATE_KIND_STIDXMAPXI,
    WOORT_IR_OPERATE_KIND_STIDXMAPXR,
    WOORT_IR_OPERATE_KIND_STIDXMAPXB,
    WOORT_IR_OPERATE_KIND_STIDXMAPXX,

    WOORT_IR_OPERATE_KIND_STIDSTRUCT,

    WOORT_IR_OPERATE_KIND_CALLNWO,
    WOORT_IR_OPERATE_KIND_CALLNNATIVE,
    WOORT_IR_OPERATE_KIND_CALL,
    WOORT_IR_OPERATE_KIND_MKVEC,
    WOORT_IR_OPERATE_KIND_MKMAP,
    WOORT_IR_OPERATE_KIND_MKSTRUCT,
    WOORT_IR_OPERATE_KIND_MKCLOSURE,

}woort_IR_Operate_kind;

typedef struct woort_IR_Register woort_IR_Register;

typedef struct woort_IR_Operate_base
{
    woort_IR_Operate_kind m_op;

    // 指令的寄存器操作数均储存在此，方便后续做寄存器分配
    /* OPTIONAL */ woort_IR_Register* m_read_r0;
    /* OPTIONAL */ woort_IR_Register* m_read_r1;
    /* OPTIONAL */ woort_IR_Register* m_read_r2;
    /* OPTIONAL */ woort_IR_Register* m_write_r;

} woort_IR_Operate_base;
#define _woort_IR_Operate_base_init(op, r0, r1, r2, wr) \
    {                                                   \
        .m_op = op,                                     \
        .m_read_r0 = r0,                                \
        .m_read_r1 = r1,                                \
        .m_read_r2 = r2,                                \
        .m_write_r = wr,                                \
    }

typedef struct woort_IR_Operate_NOP
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_NOP;
#define woort_IR_Operate_NOP_init()                 \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_NOP,              \
            NULL, NULL, NULL, NULL),                \
    }

typedef struct woort_IR_Operate_MOV
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_MOV;
#define woort_IR_Operate_MOV_init(SRC, DST)         \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_MOV,              \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_ITOR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_ITOR;
#define woort_IR_Operate_ITOR_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_ITOR,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_ITOS
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_ITOS;
#define woort_IR_Operate_ITOS_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_ITOS,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_RTOI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_RTOI;
#define woort_IR_Operate_RTOI_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_RTOI,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_RTOS
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_RTOS;
#define woort_IR_Operate_RTOS_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_RTOS,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_STOI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STOI;
#define woort_IR_Operate_STOI_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_STOI,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_STOR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STOR;
#define woort_IR_Operate_STOR_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_STOR,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_RET
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_RET;
#define woort_IR_Operate_RET_init()                 \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_RET,              \
            NULL, NULL, NULL, NULL),                \
    }

typedef struct woort_IR_Operate_RETV
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_RETV;
#define woort_IR_Operate_RETV_init(SRC)             \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_RETV,             \
            SRC, NULL, NULL, NULL),                 \
    }

// ============================================================================
// 整数算术运算
// ============================================================================

typedef struct woort_IR_Operate_ADDI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_ADDI;
#define woort_IR_Operate_ADDI_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_ADDI,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_SUBI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_SUBI;
#define woort_IR_Operate_SUBI_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_SUBI,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_MULI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_MULI;
#define woort_IR_Operate_MULI_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_MULI,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_DIVI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_DIVI;
#define woort_IR_Operate_DIVI_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_DIVI,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_MODI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_MODI;
#define woort_IR_Operate_MODI_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_MODI,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_NEGI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_NEGI;
#define woort_IR_Operate_NEGI_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_NEGI,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_LTI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LTI;
#define woort_IR_Operate_LTI_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LTI,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_GTI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_GTI;
#define woort_IR_Operate_GTI_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_GTI,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_LEI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LEI;
#define woort_IR_Operate_LEI_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LEI,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_GEI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_GEI;
#define woort_IR_Operate_GEI_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_GEI,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_EQI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_EQI;
#define woort_IR_Operate_EQI_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_EQI,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_NEI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_NEI;
#define woort_IR_Operate_NEI_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_NEI,              \
            LHS, RHS, NULL, DST),                   \
    }

// ============================================================================
// 实数算术运算
// ============================================================================

typedef struct woort_IR_Operate_ADDR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_ADDR;
#define woort_IR_Operate_ADDR_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_ADDR,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_SUBR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_SUBR;
#define woort_IR_Operate_SUBR_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_SUBR,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_MULR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_MULR;
#define woort_IR_Operate_MULR_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_MULR,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_DIVR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_DIVR;
#define woort_IR_Operate_DIVR_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_DIVR,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_MODR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_MODR;
#define woort_IR_Operate_MODR_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_MODR,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_NEGR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_NEGR;
#define woort_IR_Operate_NEGR_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_NEGR,             \
            SRC, NULL, NULL, DST),                  \
    }

typedef struct woort_IR_Operate_LTR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LTR;
#define woort_IR_Operate_LTR_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LTR,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_GTR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_GTR;
#define woort_IR_Operate_GTR_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_GTR,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_LER
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LER;
#define woort_IR_Operate_LER_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LER,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_GER
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_GER;
#define woort_IR_Operate_GER_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_GER,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_EQR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_EQR;
#define woort_IR_Operate_EQR_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_EQR,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_NER
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_NER;
#define woort_IR_Operate_NER_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_NER,              \
            LHS, RHS, NULL, DST),                   \
    }

// ============================================================================
// 字符串操作
// ============================================================================

typedef struct woort_IR_Operate_ADDS
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_ADDS;
#define woort_IR_Operate_ADDS_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_ADDS,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_LTS
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LTS;
#define woort_IR_Operate_LTS_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LTS,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_GTS
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_GTS;
#define woort_IR_Operate_GTS_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_GTS,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_LES
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LES;
#define woort_IR_Operate_LES_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LES,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_GES
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_GES;
#define woort_IR_Operate_GES_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_GES,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_EQS
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_EQS;
#define woort_IR_Operate_EQS_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_EQS,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_NES
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_NES;
#define woort_IR_Operate_NES_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_NES,              \
            LHS, RHS, NULL, DST),                   \
    }

// ============================================================================
// 逻辑运算
// ============================================================================

typedef struct woort_IR_Operate_LAND
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LAND;
#define woort_IR_Operate_LAND_init(LHS, RHS, DST)   \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LAND,             \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_LOR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LOR;
#define woort_IR_Operate_LOR_init(LHS, RHS, DST)    \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LOR,              \
            LHS, RHS, NULL, DST),                   \
    }

typedef struct woort_IR_Operate_LNOT
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LNOT;
#define woort_IR_Operate_LNOT_init(SRC, DST)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_LNOT,             \
            SRC, NULL, NULL, DST),                  \
    }

// ============================================================================
// 索引加载操作
// r0 = 索引, r1 = 容器, wr = 目标
// ============================================================================

typedef struct woort_IR_Operate_LDIDXVEC
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXVEC;
#define woort_IR_Operate_LDIDXVEC_init(IDX, VEC, DST)   \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_LDIDXVEC,             \
            IDX, VEC, NULL, DST),                       \
    }

typedef struct woort_IR_Operate_LDIDXVECX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXVECX;
#define woort_IR_Operate_LDIDXVECX_init(IDX, VEC, DST)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_LDIDXVECX,            \
            IDX, VEC, NULL, DST),                       \
    }

typedef struct woort_IR_Operate_LDIDXSTRUCT
{
    woort_IR_Operate_base m_op_base;
    uint32_t m_field_idx;  // 字段索引（常量）

}woort_IR_Operate_LDIDXSTRUCT;
#define woort_IR_Operate_LDIDXSTRUCT_init(FIELD_IDX, STRUCT, DST)    \
    {                                                               \
        .m_op_base = _woort_IR_Operate_base_init(                   \
            WOORT_IR_OPERATE_KIND_LDIDXSTRUCT,                      \
            NULL, STRUCT, NULL, DST),                               \
        .m_field_idx = FIELD_IDX,                                   \
    }

typedef struct woort_IR_Operate_LDIDXSTRING
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXSTRING;
#define woort_IR_Operate_LDIDXSTRING_init(IDX, STR, DST) \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_LDIDXSTRING,          \
            IDX, STR, NULL, DST),                       \
    }

typedef struct woort_IR_Operate_LDIDXDICTI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXDICTI;
#define woort_IR_Operate_LDIDXDICTI_init(KEY, DICT, DST)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_LDIDXDICTI,               \
            KEY, DICT, NULL, DST),                          \
    }

typedef struct woort_IR_Operate_LDIDXDICTR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXDICTR;
#define woort_IR_Operate_LDIDXDICTR_init(KEY, DICT, DST)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_LDIDXDICTR,               \
            KEY, DICT, NULL, DST),                          \
    }

typedef struct woort_IR_Operate_LDIDXDICTB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXDICTB;
#define woort_IR_Operate_LDIDXDICTB_init(KEY, DICT, DST)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_LDIDXDICTB,               \
            KEY, DICT, NULL, DST),                          \
    }

typedef struct woort_IR_Operate_LDIDXDICTX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_LDIDXDICTX;
#define woort_IR_Operate_LDIDXDICTX_init(KEY, DICT, DST)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_LDIDXDICTX,               \
            KEY, DICT, NULL, DST),                          \
    }

// ============================================================================
// 向量索引存储操作
// r0 = 向量, r1 = 索引, r2 = 值
// ============================================================================

typedef struct woort_IR_Operate_STIDXVECI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXVECI;
#define woort_IR_Operate_STIDXVECI_init(VEC, IDX, VAL)   \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXVECI,            \
            VEC, IDX, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXVECR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXVECR;
#define woort_IR_Operate_STIDXVECR_init(VEC, IDX, VAL)   \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXVECR,            \
            VEC, IDX, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXVECB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXVECB;
#define woort_IR_Operate_STIDXVECB_init(VEC, IDX, VAL)   \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXVECB,            \
            VEC, IDX, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXVECX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXVECX;
#define woort_IR_Operate_STIDXVECX_init(VEC, IDX, VAL)   \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXVECX,            \
            VEC, IDX, VAL, NULL),                       \
    }

// ============================================================================
// 字典索引存储操作 - 整数键
// r0 = 字典, r1 = 键, r2 = 值
// ============================================================================

typedef struct woort_IR_Operate_STIDXDICTII
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTII;
#define woort_IR_Operate_STIDXDICTII_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTII,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTIR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTIR;
#define woort_IR_Operate_STIDXDICTIR_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTIR,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTIB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTIB;
#define woort_IR_Operate_STIDXDICTIB_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTIB,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTIX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTIX;
#define woort_IR_Operate_STIDXDICTIX_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTIX,              \
            DICT, KEY, VAL, NULL),                          \
    }

// ============================================================================
// 字典索引存储操作 - 实数键
// ============================================================================

typedef struct woort_IR_Operate_STIDXDICTRI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTRI;
#define woort_IR_Operate_STIDXDICTRI_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTRI,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTRR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTRR;
#define woort_IR_Operate_STIDXDICTRR_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTRR,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTRB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTRB;
#define woort_IR_Operate_STIDXDICTRB_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTRB,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTRX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTRX;
#define woort_IR_Operate_STIDXDICTRX_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTRX,              \
            DICT, KEY, VAL, NULL),                          \
    }

// ============================================================================
// 字典索引存储操作 - 布尔键
// ============================================================================

typedef struct woort_IR_Operate_STIDXDICTBI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTBI;
#define woort_IR_Operate_STIDXDICTBI_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTBI,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTBR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTBR;
#define woort_IR_Operate_STIDXDICTBR_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTBR,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTBB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTBB;
#define woort_IR_Operate_STIDXDICTBB_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTBB,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTBX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTBX;
#define woort_IR_Operate_STIDXDICTBX_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTBX,              \
            DICT, KEY, VAL, NULL),                          \
    }

// ============================================================================
// 字典索引存储操作 - 动态类型键
// ============================================================================

typedef struct woort_IR_Operate_STIDXDICTXI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTXI;
#define woort_IR_Operate_STIDXDICTXI_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTXI,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTXR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTXR;
#define woort_IR_Operate_STIDXDICTXR_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTXR,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTXB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTXB;
#define woort_IR_Operate_STIDXDICTXB_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTXB,              \
            DICT, KEY, VAL, NULL),                          \
    }

typedef struct woort_IR_Operate_STIDXDICTXX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXDICTXX;
#define woort_IR_Operate_STIDXDICTXX_init(DICT, KEY, VAL)    \
    {                                                       \
        .m_op_base = _woort_IR_Operate_base_init(           \
            WOORT_IR_OPERATE_KIND_STIDXDICTXX,              \
            DICT, KEY, VAL, NULL),                          \
    }

// ============================================================================
// 映射索引存储操作 - 整数键
// ============================================================================

typedef struct woort_IR_Operate_STIDXMAPII
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPII;
#define woort_IR_Operate_STIDXMAPII_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPII,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPIR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPIR;
#define woort_IR_Operate_STIDXMAPIR_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPIR,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPIB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPIB;
#define woort_IR_Operate_STIDXMAPIB_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPIB,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPIX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPIX;
#define woort_IR_Operate_STIDXMAPIX_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPIX,           \
            MAP, KEY, VAL, NULL),                       \
    }

// ============================================================================
// 映射索引存储操作 - 实数键
// ============================================================================

typedef struct woort_IR_Operate_STIDXMAPRI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPRI;
#define woort_IR_Operate_STIDXMAPRI_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPRI,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPRR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPRR;
#define woort_IR_Operate_STIDXMAPRR_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPRR,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPRB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPRB;
#define woort_IR_Operate_STIDXMAPRB_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPRB,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPRX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPRX;
#define woort_IR_Operate_STIDXMAPRX_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPRX,           \
            MAP, KEY, VAL, NULL),                       \
    }

// ============================================================================
// 映射索引存储操作 - 布尔键
// ============================================================================

typedef struct woort_IR_Operate_STIDXMAPBI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPBI;
#define woort_IR_Operate_STIDXMAPBI_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPBI,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPBR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPBR;
#define woort_IR_Operate_STIDXMAPBR_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPBR,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPBB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPBB;
#define woort_IR_Operate_STIDXMAPBB_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPBB,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPBX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPBX;
#define woort_IR_Operate_STIDXMAPBX_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPBX,           \
            MAP, KEY, VAL, NULL),                       \
    }

// ============================================================================
// 映射索引存储操作 - 动态类型键
// ============================================================================

typedef struct woort_IR_Operate_STIDXMAPXI
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPXI;
#define woort_IR_Operate_STIDXMAPXI_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPXI,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPXR
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPXR;
#define woort_IR_Operate_STIDXMAPXR_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPXR,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPXB
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPXB;
#define woort_IR_Operate_STIDXMAPXB_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPXB,           \
            MAP, KEY, VAL, NULL),                       \
    }

typedef struct woort_IR_Operate_STIDXMAPXX
{
    woort_IR_Operate_base m_op_base;

}woort_IR_Operate_STIDXMAPXX;
#define woort_IR_Operate_STIDXMAPXX_init(MAP, KEY, VAL)  \
    {                                                   \
        .m_op_base = _woort_IR_Operate_base_init(       \
            WOORT_IR_OPERATE_KIND_STIDXMAPXX,           \
            MAP, KEY, VAL, NULL),                       \
    }

// ============================================================================
// 结构体字段存储操作
// ============================================================================

typedef struct woort_IR_Operate_STIDSTRUCT
{
    woort_IR_Operate_base m_op_base;
    uint32_t m_field_idx;  // 字段索引（常量）

}woort_IR_Operate_STIDSTRUCT;
#define woort_IR_Operate_STIDSTRUCT_init(FIELD_IDX, STRUCT, VAL)   \
    {                                                             \
        .m_op_base = _woort_IR_Operate_base_init(                 \
            WOORT_IR_OPERATE_KIND_STIDSTRUCT,                     \
            NULL, STRUCT, VAL, NULL),                             \
        .m_field_idx = FIELD_IDX,                                 \
    }

// ============================================================================
// 函数调用及结构体打包（非常规操作数结构）
// ============================================================================


typedef struct woort_IR_Operate_STIDSTRUCT
{
    woort_IR_Operate_base m_op_base;
    uint32_t m_field_idx;  // 字段索引（常量）

}woort_IR_Operate_STIDSTRUCT;

WOORT_IR_OPERATE_KIND_CALLNWO,
WOORT_IR_OPERATE_KIND_CALLNNATIVE,
WOORT_IR_OPERATE_KIND_CALL,
WOORT_IR_OPERATE_KIND_MKVEC,
WOORT_IR_OPERATE_KIND_MKMAP,
WOORT_IR_OPERATE_KIND_MKSTRUCT,
WOORT_IR_OPERATE_KIND_MKCLOSURE,