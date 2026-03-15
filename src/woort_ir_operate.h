#pragma once

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
