#pragma once

#include "woort.h"
#include "woort_opcode_dispatcher_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    bool woort_JIT_Backend_x64_prologue(
        const woort_Bytecode* function,
        size_t function_len,
        void** out_emmiter);
    bool woort_JIT_Backend_x64_epilogue(
        void* emmiter,
        woort_JitFunction* out_code);
    bool woort_JIT_Backend_x64_check_state(
        void* emmiter);
    void woort_JIT_Backend_x64_droper(
        woort_JitFunction* code);
    void woort_JIT_Backend_x64_NOP(void* emmiter);
    void woort_JIT_Backend_x64_LOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_STORE(void* emmiter, woort_Opcode_Global dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LOADPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STOREPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_MOVLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_MOVST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHRCHK(void* emmiter, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_PUSHSCHK(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHCCHK(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_ASSURESSZ(void* emmiter, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_PUSHS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHC(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_POPR(void* emmiter, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_POPS(void* emmiter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_x64_POPC(void* emmiter, woort_Opcode_Global dst);
    void woort_JIT_Backend_x64_ITORST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ITORLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ITOSST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ITOSLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RTOIST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RTOILD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RTOSST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RTOSLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CASTSTO(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CASTSFROM(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CASTDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ASSERTDYN(void* emmiter, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CALLNWO(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_CALLNFP(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_CALLNJIT(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_CALLS(void* emmiter, woort_Opcode_Stack func);
    void woort_JIT_Backend_x64_CALLC(void* emmiter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_RET(void* emmiter);
    void woort_JIT_Backend_x64_RETVS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RETVC(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_POPRS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RESULT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_JFWD(void* emmiter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_x64_JBCK(void* emmiter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_x64_JFWDNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_MKVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_MKMAP(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_MKSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_MKUNION(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_MKCLOSURE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl);
    void woort_JIT_Backend_x64_BOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_UNBOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHECKDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHBOXDYN(void* emmiter, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_SUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_DIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NEGI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_EQI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_ADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_SUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_DIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NEGR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_EQR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_ADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_EQS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LNOT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CSUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CDIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CSUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CDIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CVADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CLAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CLOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CLNOT(void* emmiter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_x64_MKPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LDIDXVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXVECX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_LDIDSTRING(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTB(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTIX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTRX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTBX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDXDICTXX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_STIDXVECI(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXVECR(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXVECB(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXVECX(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXDICTXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDXMAPXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDSTRUCT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDSTRUCTEXT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_UNPACKVEC(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_UNPACKVECX(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_UNPACKVECALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_UNPACKVECXALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_PUSHIDXSTRUCT(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PUSHIDXSTBOXI(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PUSHIDXSTBOXR(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PUSHIDXSTBOXB(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PACKARG(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count skip);
    void woort_JIT_Backend_x64_ASTORE(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ALOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global storage);
    void woort_JIT_Backend_x64_CAS(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected);
    void woort_JIT_Backend_x64_JIFINITED(void* emmiter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_x64_DEBUGTRAP(void* emmiter);
    void woort_JIT_Backend_x64_PANICS(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PANICC(void* emmiter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_CHKDIVIL(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHKDIVIR(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHKDIVIRZ(void* emmiter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHKDIVILR(void* emmiter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend);

#ifdef __cplusplus
}
#endif
