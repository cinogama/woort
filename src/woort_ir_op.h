#pragma once

/*
 * woort_ir_op.h
 *
 * IR 指令定义。
 * 新设计：无 SSA/PHI，使用可变虚拟寄存器 + 显式 MOV/JMP/JCC。
 * Label 作为跳转目标，框架在 finish() 时自动切分基本块。
 */

#include "woort.h"

#include "woort_ir_value.h"

#include <stdint.h>
#include <stddef.h>

typedef enum woort_IROp_Kind
{
    WOORT_IROP_KIND_NOP,            /* no-op */
    WOORT_IROP_KIND_EMPTY,          /* no-op, no code generated. */

    /* ============ 数据移动 ============ */
    WOORT_IROP_KIND_MOV,            /* dst = src */
    WOORT_IROP_KIND_LOAD,           /* dst = G[static_idx] */
    WOORT_IROP_KIND_STORE,          /* G[static_idx] = src */
    WOORT_IROP_KIND_LOADPVALUE,     /* dst = *ptr (load through pvalue) */
    WOORT_IROP_KIND_STOREPVALUE,    /* *ptr = src (store through pvalue, write barrier) */
    WOORT_IROP_KIND_MKPVALUE,       /* dst = new box(src); dst.m_pvalue -> box */

    /* ============ 栈操作 ============ */
    WOORT_IROP_KIND_PUSHCHK,        /* push src (with stack check) */
    WOORT_IROP_KIND_PUSHSTATICCHK,  /* push G[static_idx] (with stack check) */
    WOORT_IROP_KIND_POP,            /* dst = pop */
    WOORT_IROP_KIND_POPR,           /* pop n (discard) */
    WOORT_IROP_KIND_POPRS,          /* pop count from src vreg */

    /* ============ 类型转换 ============ */
    WOORT_IROP_KIND_ITOR,
    WOORT_IROP_KIND_ITOS,
    WOORT_IROP_KIND_RTOI,
    WOORT_IROP_KIND_RTOS,

    /* ============ 函数调用 ============ */
    WOORT_IROP_KIND_CALLNWO,        /* call script function */
    WOORT_IROP_KIND_CALLNFP,        /* call native function */
    WOORT_IROP_KIND_CALLNJIT,       /* call jit function */
    WOORT_IROP_KIND_CALL,           /* indirect call via vreg */

    /* ============ 闭包/容器 ============ */
    WOORT_IROP_KIND_MKCLOSURE,
    WOORT_IROP_KIND_MKVEC,
    WOORT_IROP_KIND_MKMAP,
    WOORT_IROP_KIND_MKSTRUCT,
    WOORT_IROP_KIND_MKUNION,

    /* ============ 动态类型 ============ */
    WOORT_IROP_KIND_BOXDYN,
    WOORT_IROP_KIND_UNBOXDYN,
    WOORT_IROP_KIND_CHECKDYN,
    WOORT_IROP_KIND_PUSHBOXDYN,

    /* ============ 字符串/BOXED 转换 ============ */
    WOORT_IROP_KIND_CASTSTO,
    WOORT_IROP_KIND_CASTSFROM,
    WOORT_IROP_KIND_CASTDYN,
    WOORT_IROP_KIND_ASSERTDYN,

    /* ============ 整数算术 ============ */
    WOORT_IROP_KIND_ADDI,
    WOORT_IROP_KIND_SUBI,
    WOORT_IROP_KIND_MULI,
    WOORT_IROP_KIND_DIVI,
    WOORT_IROP_KIND_MODI,
    WOORT_IROP_KIND_NEGI,

    /* ============ 整数除法检查 ============ */
    WOORT_IROP_KIND_CHKDIVIL,
    WOORT_IROP_KIND_CHKDIVIR,
    WOORT_IROP_KIND_CHKDIVIRZ,
    WOORT_IROP_KIND_CHKDIVILR,

    /* ============ 整数比较 ============ */
    WOORT_IROP_KIND_LTI,
    WOORT_IROP_KIND_GTI,
    WOORT_IROP_KIND_LEI,
    WOORT_IROP_KIND_GEI,
    WOORT_IROP_KIND_EQI,
    WOORT_IROP_KIND_NEI,

    /* ============ 实数算术 ============ */
    WOORT_IROP_KIND_ADDR,
    WOORT_IROP_KIND_SUBR,
    WOORT_IROP_KIND_MULR,
    WOORT_IROP_KIND_DIVR,
    WOORT_IROP_KIND_MODR,
    WOORT_IROP_KIND_NEGR,

    /* ============ 实数比较 ============ */
    WOORT_IROP_KIND_LTR,
    WOORT_IROP_KIND_GTR,
    WOORT_IROP_KIND_LER,
    WOORT_IROP_KIND_GER,
    WOORT_IROP_KIND_EQR,
    WOORT_IROP_KIND_NER,

    /* ============ 字符串 ============ */
    WOORT_IROP_KIND_ADDS,
    WOORT_IROP_KIND_LTS,
    WOORT_IROP_KIND_GTS,
    WOORT_IROP_KIND_LES,
    WOORT_IROP_KIND_GES,
    WOORT_IROP_KIND_EQS,
    WOORT_IROP_KIND_NES,

    /* ============ 逻辑 ============ */
    WOORT_IROP_KIND_LAND,
    WOORT_IROP_KIND_LOR,
    WOORT_IROP_KIND_LNOT,

    /* ============ 索引 (加载) ============ */
    WOORT_IROP_KIND_LDIDXVEC,
    WOORT_IROP_KIND_LDIDXVECX,
    WOORT_IROP_KIND_LDIDXSTRUCT,
    WOORT_IROP_KIND_LDIDXSTRING,

    WOORT_IROP_KIND_LDIDXDICTI,
    WOORT_IROP_KIND_LDIDXDICTR,
    WOORT_IROP_KIND_LDIDXDICTB,
    WOORT_IROP_KIND_LDIDXDICTX,
    WOORT_IROP_KIND_LDIDXDICTIX,
    WOORT_IROP_KIND_LDIDXDICTRX,
    WOORT_IROP_KIND_LDIDXDICTBX,
    WOORT_IROP_KIND_LDIDXDICTXX,

    /* ============ 索引 (存储) - vec ============ */
    WOORT_IROP_KIND_SDIDXVECI,
    WOORT_IROP_KIND_SDIDXVECR,
    WOORT_IROP_KIND_SDIDXVECB,
    WOORT_IROP_KIND_SDIDXVECX,

    /* ============ 索引 (存储) - dict ============ */
    WOORT_IROP_KIND_SDIDXDICTII,
    WOORT_IROP_KIND_SDIDXDICTIR,
    WOORT_IROP_KIND_SDIDXDICTIB,
    WOORT_IROP_KIND_SDIDXDICTIX,
    WOORT_IROP_KIND_SDIDXDICTRI,
    WOORT_IROP_KIND_SDIDXDICTRR,
    WOORT_IROP_KIND_SDIDXDICTRB,
    WOORT_IROP_KIND_SDIDXDICTRX,
    WOORT_IROP_KIND_SDIDXDICTBI,
    WOORT_IROP_KIND_SDIDXDICTBR,
    WOORT_IROP_KIND_SDIDXDICTBB,
    WOORT_IROP_KIND_SDIDXDICTBX,
    WOORT_IROP_KIND_SDIDXDICTXI,
    WOORT_IROP_KIND_SDIDXDICTXR,
    WOORT_IROP_KIND_SDIDXDICTXB,
    WOORT_IROP_KIND_SDIDXDICTXX,

    /* ============ 索引 (存储) - map ============ */
    WOORT_IROP_KIND_SDIDXMAPII,
    WOORT_IROP_KIND_SDIDXMAPIR,
    WOORT_IROP_KIND_SDIDXMAPIB,
    WOORT_IROP_KIND_SDIDXMAPIX,
    WOORT_IROP_KIND_SDIDXMAPRI,
    WOORT_IROP_KIND_SDIDXMAPRR,
    WOORT_IROP_KIND_SDIDXMAPRB,
    WOORT_IROP_KIND_SDIDXMAPRX,
    WOORT_IROP_KIND_SDIDXMAPBI,
    WOORT_IROP_KIND_SDIDXMAPBR,
    WOORT_IROP_KIND_SDIDXMAPBB,
    WOORT_IROP_KIND_SDIDXMAPBX,
    WOORT_IROP_KIND_SDIDXMAPXI,
    WOORT_IROP_KIND_SDIDXMAPXR,
    WOORT_IROP_KIND_SDIDXMAPXB,
    WOORT_IROP_KIND_SDIDXMAPXX,

    /* ============ 索引 (存储) - struct ============ */
    WOORT_IROP_KIND_SDIDXSTRUCT,

    /* ============ 解包 ============ */
    WOORT_IROP_KIND_UNPACKVEC,
    WOORT_IROP_KIND_UNPACKVECX,
    WOORT_IROP_KIND_UNPACKVECALL,
    WOORT_IROP_KIND_UNPACKVECXALL,

    /* ============ 结构体字段推栈 ============ */
    WOORT_IROP_KIND_PUSHIDXSTRUCT,
    WOORT_IROP_KIND_PUSHIDXSTBOXI,
    WOORT_IROP_KIND_PUSHIDXSTBOXR,
    WOORT_IROP_KIND_PUSHIDXSTBOXB,

    /* ============ 控制流 ============ */
    WOORT_IROP_KIND_JMP,            /* goto label */
    WOORT_IROP_KIND_JCC,            /* if (src != 0) goto label */
    WOORT_IROP_KIND_JCCZ,           /* if (src == 0) goto label */
    WOORT_IROP_KIND_JCC_LT,         /* if (a < b) goto label */
    WOORT_IROP_KIND_JCC_LE,         /* if (a <= b) goto label */
    WOORT_IROP_KIND_JCC_EQ,         /* if (a == b) goto label */
    WOORT_IROP_KIND_JCC_GT,         /* if (a > b) goto label (sugar: swapped lt) */
    WOORT_IROP_KIND_JCC_GE,         /* if (a >= b) goto label (sugar: swapped le) */
    WOORT_IROP_KIND_JCC_NE,         /* if (a != b) goto label */
    WOORT_IROP_KIND_JIFINITED,      /* once init guard: if G[static]==2 goto label; else CAS 0->1 fallthrough */

    /* ============ 原子操作 ============ */
    WOORT_IROP_KIND_ASTORE,         /* atomic store: G[static_idx] = src (release) */
    WOORT_IROP_KIND_ALOAD,          /* atomic load:  dst = G[static_idx] (acquire) */
    WOORT_IROP_KIND_CAS,            /* compare-and-swap: CAS G[static_idx](expected, desired) */
    WOORT_IROP_KIND_PACKARG,        /* pack remaining arguments for variadic call */

    /* ============ 陷阱/Panic ============ */
    WOORT_IROP_KIND_DEBUGTRAP,      /* debug trap/breakpoint */
    WOORT_IROP_KIND_PANIC,          /* panic with string message */

    /* ============ 返回 ============ */
    WOORT_IROP_KIND_RET,            /* return src */
    WOORT_IROP_KIND_RET_VOID,       /* return void */

    /* ============ Label 绑定 (伪指令) ============ */
    WOORT_IROP_KIND_LABEL,          /* label bind point */

    WOORT_IROP_KIND_count,

} woort_IROp_Kind;

/*
 * IR 指令
 *
 * 三地址形式：dst + src[0..2]
 * 对于写操作，dst 是目标虚拟寄存器。
 * 对于纯读操作（STORE、PUSHCHK、CALL无返回值等），dst 为 NULL。
 */
typedef struct woort_IROp
{
    woort_IROp_Kind m_op;

    /* 源码位置索引（指向 IRFunction 的 m_source_locations 中的条目）。
     * UINT32_MAX (WOORT_SRCLOC_INVALID_INDEX) 表示无源码信息。 */
    uint32_t m_srcloc_index;

    /* 写目标虚拟寄存器 */
    /* OPTIONAL */ woort_IRValue* m_dst;

    /* 读操作数虚拟寄存器 */
    /* OPTIONAL */ const woort_IRValue* m_src[3];

    union
    {
        /* LOAD/STORE */
        woort_IRStaticIndex m_static_index;

        /* MKVEC/MKMAP/MKSTRUCT/MKCLOSURE */
        uint32_t m_count;

        /* LDIDXSTRUCT/SDIDXSTRUCT/PUSHIDXSTRUCT/PUSHIDXSTBOX* */
        uint32_t m_index;

        /* BOXDYN/UNBOXDYN/CHECKDYN/PUSHBOXDYN */
        uint8_t m_type;

        /* CALLNWO/CALLNFP/CALLNJIT */
        struct
        {
            woort_IRConstantIndex m_calln_target;
            uint32_t m_argument_count;
        };

        /* CALL (indirect) */
        uint32_t m_call_argument_count;

        /* POPR */
        uint32_t m_pop_count;

        /* PACKARG */
        uint16_t m_packarg_count;

        /* JMP, JCC, JCCZ, JCC_LT, JCC_LE, JCC_EQ, JCC_GT, JCC_GE, JCC_NE */
        woort_IRLabel* m_jump_target;

        /* LABEL */
        woort_IRLabel* m_label;
    };

    /* JIFINITED: static index of the atomic flag slot */
    woort_IRStaticIndex m_jifinited_static;

} woort_IROp;
