#pragma once

#include "woort.h"
#include "woort_opcode_dispatcher_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    bool woort_JIT_Backend_arm64_prologue(
        const woort_CodeEnv* cenv,
        const woort_Bytecode** ip,
        void** out_emmiter);
    bool woort_JIT_Backend_arm64_epilogue(
        void* emmiter,
        woort_JitFunction* out_code);
    bool woort_JIT_Backend_arm64_post_dispatch(
        void* emmiter);
    bool woort_JIT_Backend_arm64_pre_dispatch(
        void* emmiter);
    void woort_JIT_Backend_arm64_dropper(
        woort_JitFunction* code);
    void woort_JIT_Backend_arm64_NOP(void* emmiter);
    void woort_JIT_Backend_arm64_LOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_STORE(void* emmiter, woort_Opcode_Global dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LOADPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STOREPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_MOV(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHRCHK(void* emmiter, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_PUSHSCHK(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHCCHK(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_ASSURESSZ(void* emmiter, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_PUSHS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHC(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_POPR(void* emmiter, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_POPS(void* emmiter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_arm64_POPC(void* emmiter, woort_Opcode_Global dst);
    void woort_JIT_Backend_arm64_ITOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ITOS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RTOI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RTOS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CASTSTO(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CASTSFROM(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CASTDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ASSERTDYN(void* emmiter, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CALLNWO(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_CALLNFP(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_CALLNJIT(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_CALLS(void* emmiter, woort_Opcode_Stack func);
    void woort_JIT_Backend_arm64_CALLC(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_RET(void* emmiter);
    void woort_JIT_Backend_arm64_RETVS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RETVC(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_POPRS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RESULT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_JFWD(void* emmiter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_arm64_JBCK(void* emmiter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_arm64_JFWDNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_MKVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_MKMAP(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_MKSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_MKUNION(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_MKCLOSURE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl);
    void woort_JIT_Backend_arm64_BOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_UNBOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHECKDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHBOXDYN(void* emmiter, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_SUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_DIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NEGI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_EQI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_ADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_SUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_DIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NEGR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_EQR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_ADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_EQS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LNOT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CSUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CDIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CSUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CDIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CVADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CLAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CLOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CLNOT(void* emmiter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_arm64_MKPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LDIDVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDVECX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_LDIDSTRING(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTB(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTIX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTRX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTBX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTXX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_STIDVECI(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDVECR(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDVECB(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDVECX(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDSTRUCT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_UNPACKVEC(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_UNPACKVECX(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_UNPACKVECALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_UNPACKVECXALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_PUSHIDSTRUCT(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PUSHIDSTBOXI(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PUSHIDSTBOXR(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PUSHIDSTBOXB(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PACKARG(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count skip);
    void woort_JIT_Backend_arm64_ASTORE(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ALOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global storage);
    void woort_JIT_Backend_arm64_CAS(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected);
    void woort_JIT_Backend_arm64_JIFINITED(void* emmiter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_arm64_DEBUGTRAP(void* emmiter);
    void woort_JIT_Backend_arm64_PANICS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PANICC(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_CHKDIVIL(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHKDIVIR(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHKDIVIRZ(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHKDIVILR(void* emmiter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend);

#ifdef __cplusplus
}
#endif
