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
#define woort_IR_Operate_ITOR_init(DST, SRC)        \
    {                                               \
        .m_op_base = _woort_IR_Operate_base_init(   \
            WOORT_IR_OPERATE_KIND_ITOR,             \
            SRC, NULL, NULL, ITOR),                 \
    }
