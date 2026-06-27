#include "woort_jit_arm64.h"


static bool woort_JIT_Backend_arm64_prologue(
    const woort_Bytecode* function, 
    size_t function_len, 
    void** out_emmiter)
{
    (void)function;
    (void)function_len;
    
    *out_emmiter = NULL;
    return false;
}

static bool woort_JIT_Backend_arm64_epilogue(
    void* emmiter,
    woort_JitFunction* out_code)
{
    (void)emmiter;

    *out_code = NULL;
    return false;
}

static bool woort_JIT_Backend_arm64_check_state(
    void* emmiter)
{
    (void)emmiter;

    return false;
}

static void woort_JIT_Backend_arm64_dropper(
    woort_JitFunction* code)
{
    (void)code;
}

/* -------------------------------------------------------------------------- */
/* 指令派发接口：以下均为占位实现，暂无实际代码生成。                          */
/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_NOP(void* emmiter)
{
    (void)emmiter;
}

static void woort_JIT_Backend_arm64_LOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_STORE(void* emmiter, woort_Opcode_Global dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_LOADPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_STOREPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_MOVLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_MOVST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_PUSHRCHK(void* emmiter, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)n;
}

static void woort_JIT_Backend_arm64_PUSHSCHK(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_PUSHCCHK(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_ASSURESSZ(void* emmiter, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)n;
}

static void woort_JIT_Backend_arm64_PUSHS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_PUSHC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_POPR(void* emmiter, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)n;
}

static void woort_JIT_Backend_arm64_POPS(void* emmiter, woort_Opcode_Stack dst)
{
    (void)emmiter;
    (void)dst;
}

static void woort_JIT_Backend_arm64_POPC(void* emmiter, woort_Opcode_Global dst)
{
    (void)emmiter;
    (void)dst;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_ITORST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_ITORLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_ITOSST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_ITOSLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_RTOIST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_RTOILD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_RTOSST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_RTOSLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_CASTSTO(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)target;
    (void)src;
}

static void woort_JIT_Backend_arm64_CASTSFROM(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)srctype;
    (void)src;
}

static void woort_JIT_Backend_arm64_CASTDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)target;
    (void)src;
}

static void woort_JIT_Backend_arm64_ASSERTDYN(void* emmiter, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)target;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_CALLNWO(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

static void woort_JIT_Backend_arm64_CALLNFP(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

static void woort_JIT_Backend_arm64_CALLNJIT(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

static void woort_JIT_Backend_arm64_CALLS(void* emmiter, woort_Opcode_Stack func)
{
    (void)emmiter;
    (void)func;
}

static void woort_JIT_Backend_arm64_CALLC(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

static void woort_JIT_Backend_arm64_RET(void* emmiter)
{
    (void)emmiter;
}

static void woort_JIT_Backend_arm64_RETVS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_RETVC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_POPRS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_RESULT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_JFWD(void* emmiter, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)target;
}

static void woort_JIT_Backend_arm64_JBCK(void* emmiter, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)target;
}

static void woort_JIT_Backend_arm64_JFWDNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JFWDEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

static void woort_JIT_Backend_arm64_JBCKEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_MKVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

static void woort_JIT_Backend_arm64_MKMAP(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

static void woort_JIT_Backend_arm64_MKSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

static void woort_JIT_Backend_arm64_MKUNION(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_MKCLOSURE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{
    (void)emmiter;
    (void)dst;
    (void)n;
    (void)tmpl;
}

static void woort_JIT_Backend_arm64_BOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
}

static void woort_JIT_Backend_arm64_UNBOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
}

static void woort_JIT_Backend_arm64_CHECKDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
}

static void woort_JIT_Backend_arm64_PUSHBOXDYN(void* emmiter, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)type;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_ADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_SUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_MULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_DIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_MODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_NEGI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_LTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_GTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_GEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_EQI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_NEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_ADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_SUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_MULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_DIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_MODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_NEGR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_LTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_GTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_GER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_EQR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_NER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_ADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_GTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_GES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_EQS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_NES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

static void woort_JIT_Backend_arm64_LNOT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_CADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CSUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CMULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CDIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CSUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CMULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CDIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CVADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CMODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CMODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CLAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CLOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

static void woort_JIT_Backend_arm64_CLNOT(void* emmiter, woort_Opcode_Stack dst)
{
    (void)emmiter;
    (void)dst;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_MKPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_LDIDXVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)vec;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXVECX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)vec;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)dst;
    (void)idx;
    (void)obj;
}

static void woort_JIT_Backend_arm64_LDIDSTRING(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)str;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTB(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTIX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTRX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTBX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

static void woort_JIT_Backend_arm64_LDIDXDICTXX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_STIDXVECI(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXVECR(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXVECB(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXVECX(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXDICTXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDXMAPXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_STIDSTRUCT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)obj;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_STIDSTRUCTEXT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)obj;
    (void)idx;
    (void)src;
}

static void woort_JIT_Backend_arm64_UNPACKVEC(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)n;
    (void)vec;
}

static void woort_JIT_Backend_arm64_UNPACKVECX(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)n;
    (void)vec;
}

static void woort_JIT_Backend_arm64_UNPACKVECALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)count_dst;
    (void)n;
    (void)vec;
}

static void woort_JIT_Backend_arm64_UNPACKVECXALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)count_dst;
    (void)n;
    (void)vec;
}

static void woort_JIT_Backend_arm64_PUSHIDXSTRUCT(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

static void woort_JIT_Backend_arm64_PUSHIDXSTBOXI(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

static void woort_JIT_Backend_arm64_PUSHIDXSTBOXR(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

static void woort_JIT_Backend_arm64_PUSHIDXSTBOXB(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

static void woort_JIT_Backend_arm64_PACKARG(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count skip)
{
    (void)emmiter;
    (void)dst;
    (void)skip;
}

/* -------------------------------------------------------------------------- */

static void woort_JIT_Backend_arm64_ASTORE(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)storage;
    (void)src;
}

static void woort_JIT_Backend_arm64_ALOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{
    (void)emmiter;
    (void)dst;
    (void)storage;
}

static void woort_JIT_Backend_arm64_CAS(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{
    (void)emmiter;
    (void)storage;
    (void)desired;
    (void)expected;
}

static void woort_JIT_Backend_arm64_JIFINITED(void* emmiter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)flag;
    (void)target;
}

static void woort_JIT_Backend_arm64_DEBUGTRAP(void* emmiter)
{
    (void)emmiter;
}

static void woort_JIT_Backend_arm64_PANICS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_PANICC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_CHKDIVIL(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_CHKDIVIR(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_CHKDIVIRZ(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

static void woort_JIT_Backend_arm64_CHKDIVILR(void* emmiter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{
    (void)emmiter;
    (void)divisor;
    (void)dividend;
}

/* -------------------------------------------------------------------------- */

static const woort_OpcodeDispatchers _WOORT_JIT_BACKEND_CODE_DISPATCHERS_IMPL_ARM64 = {
    .m_NOP = woort_JIT_Backend_arm64_NOP,
    .m_LOAD = woort_JIT_Backend_arm64_LOAD,
    .m_STORE = woort_JIT_Backend_arm64_STORE,
    .m_LOADPVALUE = woort_JIT_Backend_arm64_LOADPVALUE,
    .m_STOREPVALUE = woort_JIT_Backend_arm64_STOREPVALUE,
    .m_MOVLD = woort_JIT_Backend_arm64_MOVLD,
    .m_MOVST = woort_JIT_Backend_arm64_MOVST,
    .m_PUSHRCHK = woort_JIT_Backend_arm64_PUSHRCHK,
    .m_PUSHSCHK = woort_JIT_Backend_arm64_PUSHSCHK,
    .m_PUSHCCHK = woort_JIT_Backend_arm64_PUSHCCHK,
    .m_ASSURESSZ = woort_JIT_Backend_arm64_ASSURESSZ,
    .m_PUSHS = woort_JIT_Backend_arm64_PUSHS,
    .m_PUSHC = woort_JIT_Backend_arm64_PUSHC,
    .m_POPR = woort_JIT_Backend_arm64_POPR,
    .m_POPS = woort_JIT_Backend_arm64_POPS,
    .m_POPC = woort_JIT_Backend_arm64_POPC,
    .m_ITORST = woort_JIT_Backend_arm64_ITORST,
    .m_ITORLD = woort_JIT_Backend_arm64_ITORLD,
    .m_ITOSST = woort_JIT_Backend_arm64_ITOSST,
    .m_ITOSLD = woort_JIT_Backend_arm64_ITOSLD,
    .m_RTOIST = woort_JIT_Backend_arm64_RTOIST,
    .m_RTOILD = woort_JIT_Backend_arm64_RTOILD,
    .m_RTOSST = woort_JIT_Backend_arm64_RTOSST,
    .m_RTOSLD = woort_JIT_Backend_arm64_RTOSLD,
    .m_CASTSTO = woort_JIT_Backend_arm64_CASTSTO,
    .m_CASTSFROM = woort_JIT_Backend_arm64_CASTSFROM,
    .m_CASTDYN = woort_JIT_Backend_arm64_CASTDYN,
    .m_ASSERTDYN = woort_JIT_Backend_arm64_ASSERTDYN,
    .m_CALLNWO = woort_JIT_Backend_arm64_CALLNWO,
    .m_CALLNFP = woort_JIT_Backend_arm64_CALLNFP,
    .m_CALLNJIT = woort_JIT_Backend_arm64_CALLNJIT,
    .m_CALLS = woort_JIT_Backend_arm64_CALLS,
    .m_CALLC = woort_JIT_Backend_arm64_CALLC,
    .m_RET = woort_JIT_Backend_arm64_RET,
    .m_RETVS = woort_JIT_Backend_arm64_RETVS,
    .m_RETVC = woort_JIT_Backend_arm64_RETVC,
    .m_POPRS = woort_JIT_Backend_arm64_POPRS,
    .m_RESULT = woort_JIT_Backend_arm64_RESULT,
    .m_JFWD = woort_JIT_Backend_arm64_JFWD,
    .m_JBCK = woort_JIT_Backend_arm64_JBCK,
    .m_JFWDNZ = woort_JIT_Backend_arm64_JFWDNZ,
    .m_JFWDZ = woort_JIT_Backend_arm64_JFWDZ,
    .m_JFWDEQ = woort_JIT_Backend_arm64_JFWDEQ,
    .m_JFWDNEQ = woort_JIT_Backend_arm64_JFWDNEQ,
    .m_JBCKNZ = woort_JIT_Backend_arm64_JBCKNZ,
    .m_JBCKZ = woort_JIT_Backend_arm64_JBCKZ,
    .m_JBCKEQ = woort_JIT_Backend_arm64_JBCKEQ,
    .m_JBCKNEQ = woort_JIT_Backend_arm64_JBCKNEQ,
    .m_JFWDLT = woort_JIT_Backend_arm64_JFWDLT,
    .m_JFWDGT = woort_JIT_Backend_arm64_JFWDGT,
    .m_JFWDEL = woort_JIT_Backend_arm64_JFWDEL,
    .m_JFWDEG = woort_JIT_Backend_arm64_JFWDEG,
    .m_JBCKLT = woort_JIT_Backend_arm64_JBCKLT,
    .m_JBCKGT = woort_JIT_Backend_arm64_JBCKGT,
    .m_JBCKEL = woort_JIT_Backend_arm64_JBCKEL,
    .m_JBCKEG = woort_JIT_Backend_arm64_JBCKEG,
    .m_MKVEC = woort_JIT_Backend_arm64_MKVEC,
    .m_MKMAP = woort_JIT_Backend_arm64_MKMAP,
    .m_MKSTRUCT = woort_JIT_Backend_arm64_MKSTRUCT,
    .m_MKUNION = woort_JIT_Backend_arm64_MKUNION,
    .m_MKCLOSURE = woort_JIT_Backend_arm64_MKCLOSURE,
    .m_BOXDYN = woort_JIT_Backend_arm64_BOXDYN,
    .m_UNBOXDYN = woort_JIT_Backend_arm64_UNBOXDYN,
    .m_CHECKDYN = woort_JIT_Backend_arm64_CHECKDYN,
    .m_PUSHBOXDYN = woort_JIT_Backend_arm64_PUSHBOXDYN,
    .m_ADDI = woort_JIT_Backend_arm64_ADDI,
    .m_SUBI = woort_JIT_Backend_arm64_SUBI,
    .m_MULI = woort_JIT_Backend_arm64_MULI,
    .m_DIVI = woort_JIT_Backend_arm64_DIVI,
    .m_MODI = woort_JIT_Backend_arm64_MODI,
    .m_NEGI = woort_JIT_Backend_arm64_NEGI,
    .m_LTI = woort_JIT_Backend_arm64_LTI,
    .m_GTI = woort_JIT_Backend_arm64_GTI,
    .m_LEI = woort_JIT_Backend_arm64_LEI,
    .m_GEI = woort_JIT_Backend_arm64_GEI,
    .m_EQI = woort_JIT_Backend_arm64_EQI,
    .m_NEI = woort_JIT_Backend_arm64_NEI,
    .m_ADDR = woort_JIT_Backend_arm64_ADDR,
    .m_SUBR = woort_JIT_Backend_arm64_SUBR,
    .m_MULR = woort_JIT_Backend_arm64_MULR,
    .m_DIVR = woort_JIT_Backend_arm64_DIVR,
    .m_MODR = woort_JIT_Backend_arm64_MODR,
    .m_NEGR = woort_JIT_Backend_arm64_NEGR,
    .m_LTR = woort_JIT_Backend_arm64_LTR,
    .m_GTR = woort_JIT_Backend_arm64_GTR,
    .m_LER = woort_JIT_Backend_arm64_LER,
    .m_GER = woort_JIT_Backend_arm64_GER,
    .m_EQR = woort_JIT_Backend_arm64_EQR,
    .m_NER = woort_JIT_Backend_arm64_NER,
    .m_ADDS = woort_JIT_Backend_arm64_ADDS,
    .m_LTS = woort_JIT_Backend_arm64_LTS,
    .m_GTS = woort_JIT_Backend_arm64_GTS,
    .m_LES = woort_JIT_Backend_arm64_LES,
    .m_GES = woort_JIT_Backend_arm64_GES,
    .m_EQS = woort_JIT_Backend_arm64_EQS,
    .m_NES = woort_JIT_Backend_arm64_NES,
    .m_LAND = woort_JIT_Backend_arm64_LAND,
    .m_LOR = woort_JIT_Backend_arm64_LOR,
    .m_LNOT = woort_JIT_Backend_arm64_LNOT,
    .m_CADDI = woort_JIT_Backend_arm64_CADDI,
    .m_CSUBI = woort_JIT_Backend_arm64_CSUBI,
    .m_CMULI = woort_JIT_Backend_arm64_CMULI,
    .m_CDIVI = woort_JIT_Backend_arm64_CDIVI,
    .m_CADDR = woort_JIT_Backend_arm64_CADDR,
    .m_CSUBR = woort_JIT_Backend_arm64_CSUBR,
    .m_CMULR = woort_JIT_Backend_arm64_CMULR,
    .m_CDIVR = woort_JIT_Backend_arm64_CDIVR,
    .m_CADDS = woort_JIT_Backend_arm64_CADDS,
    .m_CVADDS = woort_JIT_Backend_arm64_CVADDS,
    .m_CMODI = woort_JIT_Backend_arm64_CMODI,
    .m_CMODR = woort_JIT_Backend_arm64_CMODR,
    .m_CLAND = woort_JIT_Backend_arm64_CLAND,
    .m_CLOR = woort_JIT_Backend_arm64_CLOR,
    .m_CLNOT = woort_JIT_Backend_arm64_CLNOT,
    .m_MKPVALUE = woort_JIT_Backend_arm64_MKPVALUE,
    .m_LDIDXVEC = woort_JIT_Backend_arm64_LDIDXVEC,
    .m_LDIDXVECX = woort_JIT_Backend_arm64_LDIDXVECX,
    .m_LDIDSTRUCT = woort_JIT_Backend_arm64_LDIDSTRUCT,
    .m_LDIDSTRING = woort_JIT_Backend_arm64_LDIDSTRING,
    .m_LDIDXDICTI = woort_JIT_Backend_arm64_LDIDXDICTI,
    .m_LDIDXDICTR = woort_JIT_Backend_arm64_LDIDXDICTR,
    .m_LDIDXDICTB = woort_JIT_Backend_arm64_LDIDXDICTB,
    .m_LDIDXDICTX = woort_JIT_Backend_arm64_LDIDXDICTX,
    .m_LDIDXDICTIX = woort_JIT_Backend_arm64_LDIDXDICTIX,
    .m_LDIDXDICTRX = woort_JIT_Backend_arm64_LDIDXDICTRX,
    .m_LDIDXDICTBX = woort_JIT_Backend_arm64_LDIDXDICTBX,
    .m_LDIDXDICTXX = woort_JIT_Backend_arm64_LDIDXDICTXX,
    .m_STIDXVECI = woort_JIT_Backend_arm64_STIDXVECI,
    .m_STIDXVECR = woort_JIT_Backend_arm64_STIDXVECR,
    .m_STIDXVECB = woort_JIT_Backend_arm64_STIDXVECB,
    .m_STIDXVECX = woort_JIT_Backend_arm64_STIDXVECX,
    .m_STIDXDICTII = woort_JIT_Backend_arm64_STIDXDICTII,
    .m_STIDXDICTIR = woort_JIT_Backend_arm64_STIDXDICTIR,
    .m_STIDXDICTIB = woort_JIT_Backend_arm64_STIDXDICTIB,
    .m_STIDXDICTIX = woort_JIT_Backend_arm64_STIDXDICTIX,
    .m_STIDXDICTRI = woort_JIT_Backend_arm64_STIDXDICTRI,
    .m_STIDXDICTRR = woort_JIT_Backend_arm64_STIDXDICTRR,
    .m_STIDXDICTRB = woort_JIT_Backend_arm64_STIDXDICTRB,
    .m_STIDXDICTRX = woort_JIT_Backend_arm64_STIDXDICTRX,
    .m_STIDXDICTBI = woort_JIT_Backend_arm64_STIDXDICTBI,
    .m_STIDXDICTBR = woort_JIT_Backend_arm64_STIDXDICTBR,
    .m_STIDXDICTBB = woort_JIT_Backend_arm64_STIDXDICTBB,
    .m_STIDXDICTBX = woort_JIT_Backend_arm64_STIDXDICTBX,
    .m_STIDXDICTXI = woort_JIT_Backend_arm64_STIDXDICTXI,
    .m_STIDXDICTXR = woort_JIT_Backend_arm64_STIDXDICTXR,
    .m_STIDXDICTXB = woort_JIT_Backend_arm64_STIDXDICTXB,
    .m_STIDXDICTXX = woort_JIT_Backend_arm64_STIDXDICTXX,
    .m_STIDXMAPII = woort_JIT_Backend_arm64_STIDXMAPII,
    .m_STIDXMAPIR = woort_JIT_Backend_arm64_STIDXMAPIR,
    .m_STIDXMAPIB = woort_JIT_Backend_arm64_STIDXMAPIB,
    .m_STIDXMAPIX = woort_JIT_Backend_arm64_STIDXMAPIX,
    .m_STIDXMAPRI = woort_JIT_Backend_arm64_STIDXMAPRI,
    .m_STIDXMAPRR = woort_JIT_Backend_arm64_STIDXMAPRR,
    .m_STIDXMAPRB = woort_JIT_Backend_arm64_STIDXMAPRB,
    .m_STIDXMAPRX = woort_JIT_Backend_arm64_STIDXMAPRX,
    .m_STIDXMAPBI = woort_JIT_Backend_arm64_STIDXMAPBI,
    .m_STIDXMAPBR = woort_JIT_Backend_arm64_STIDXMAPBR,
    .m_STIDXMAPBB = woort_JIT_Backend_arm64_STIDXMAPBB,
    .m_STIDXMAPBX = woort_JIT_Backend_arm64_STIDXMAPBX,
    .m_STIDXMAPXI = woort_JIT_Backend_arm64_STIDXMAPXI,
    .m_STIDXMAPXR = woort_JIT_Backend_arm64_STIDXMAPXR,
    .m_STIDXMAPXB = woort_JIT_Backend_arm64_STIDXMAPXB,
    .m_STIDXMAPXX = woort_JIT_Backend_arm64_STIDXMAPXX,
    .m_STIDSTRUCT = woort_JIT_Backend_arm64_STIDSTRUCT,
    .m_STIDSTRUCTEXT = woort_JIT_Backend_arm64_STIDSTRUCTEXT,
    .m_UNPACKVEC = woort_JIT_Backend_arm64_UNPACKVEC,
    .m_UNPACKVECX = woort_JIT_Backend_arm64_UNPACKVECX,
    .m_UNPACKVECALL = woort_JIT_Backend_arm64_UNPACKVECALL,
    .m_UNPACKVECXALL = woort_JIT_Backend_arm64_UNPACKVECXALL,
    .m_PUSHIDXSTRUCT = woort_JIT_Backend_arm64_PUSHIDXSTRUCT,
    .m_PUSHIDXSTBOXI = woort_JIT_Backend_arm64_PUSHIDXSTBOXI,
    .m_PUSHIDXSTBOXR = woort_JIT_Backend_arm64_PUSHIDXSTBOXR,
    .m_PUSHIDXSTBOXB = woort_JIT_Backend_arm64_PUSHIDXSTBOXB,
    .m_PACKARG = woort_JIT_Backend_arm64_PACKARG,
    .m_ASTORE = woort_JIT_Backend_arm64_ASTORE,
    .m_ALOAD = woort_JIT_Backend_arm64_ALOAD,
    .m_CAS = woort_JIT_Backend_arm64_CAS,
    .m_JIFINITED = woort_JIT_Backend_arm64_JIFINITED,
    .m_DEBUGTRAP = woort_JIT_Backend_arm64_DEBUGTRAP,
    .m_PANICS = woort_JIT_Backend_arm64_PANICS,
    .m_PANICC = woort_JIT_Backend_arm64_PANICC,
    .m_CHKDIVIL = woort_JIT_Backend_arm64_CHKDIVIL,
    .m_CHKDIVIR = woort_JIT_Backend_arm64_CHKDIVIR,
    .m_CHKDIVIRZ = woort_JIT_Backend_arm64_CHKDIVIRZ,
    .m_CHKDIVILR = woort_JIT_Backend_arm64_CHKDIVILR,
};

const woort_JIT_Backend WOORT_JIT_BACKEND_IMPL_ARM64 = {
    .m_emit_prologue = woort_JIT_Backend_arm64_prologue,
    .m_emit_epilogue = woort_JIT_Backend_arm64_epilogue,
    .m_check_state = woort_JIT_Backend_arm64_check_state,
    .m_dispatchers = &_WOORT_JIT_BACKEND_CODE_DISPATCHERS_IMPL_ARM64,
    .m_drop_code = woort_JIT_Backend_arm64_dropper,
};
