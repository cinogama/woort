#pragma once

#include "woort.h"
#include "woort_opcode_dispatcher_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    WOORT_NODISCARD bool woort_JIT_Backend_x64_prologue(
        const woort_CodeEnv* cenv,
        const woort_Bytecode** ip,
        void** out_emitter);
    WOORT_NODISCARD bool woort_JIT_Backend_x64_epilogue(
        void* emitter,
        woort_JitFunction* out_code);
    WOORT_NODISCARD bool woort_JIT_Backend_x64_post_dispatch(
        void* emitter);
    WOORT_NODISCARD bool woort_JIT_Backend_x64_pre_dispatch(
        void* emitter);
    void woort_JIT_Backend_x64_droper(
        woort_JitFunction* code);
    void woort_JIT_Backend_x64_NOP(void* emitter);
    void woort_JIT_Backend_x64_LOAD(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_STORE(void* emitter, woort_Opcode_Global dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LOADPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STOREPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_MOV(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHRCHK(void* emitter, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_PUSHSCHK(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHCCHK(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_ASSURESSZ(void* emitter, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_PUSHS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHC(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_POPR(void* emitter, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_POPS(void* emitter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_x64_POPC(void* emitter, woort_Opcode_Global dst);
    void woort_JIT_Backend_x64_ITOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ITOS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RTOI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RTOS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CASTSTO(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CASTSFROM(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CASTDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ASSERTDYN(void* emitter, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CALLNWO(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_CALLNFP(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_CALLNJIT(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_CALLS(void* emitter, woort_Opcode_Stack func);
    void woort_JIT_Backend_x64_CALLC(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_x64_RET(void* emitter);
    void woort_JIT_Backend_x64_RETVS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RETVC(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_POPRS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_RESULT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_JFWD(void* emitter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_x64_JBCK(void* emitter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_x64_JFWDNZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDNEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKNZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKNEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDLT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDGT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDEL(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JFWDEG(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKLT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKGT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKEL(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_JBCKEG(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_x64_MKVEC(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_MKMAP(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_MKSTRUCT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_x64_MKUNION(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_MKCLOSURE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl);
    void woort_JIT_Backend_x64_BOXDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_UNBOXDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHECKDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PUSHBOXDYN(void* emitter, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ADDI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_SUBI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MULI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_DIVI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MODI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NEGI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_EQI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_ADDR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_SUBR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MULR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_DIVR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_MODR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NEGR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_EQR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_ADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LTS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GTS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_GES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_EQS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_NES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LAND(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_x64_LNOT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CADDI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CSUBI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMULI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CDIVI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CADDR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CSUBR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMULR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CDIVR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CVADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMODI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CMODR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CLAND(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CLOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CLNOT(void* emitter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_x64_MKPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_LDIDVEC(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDVECX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDSTRUCT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_LDIDSTRING(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTB(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTIX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTRX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTBX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_LDIDDICTXX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_x64_STIDVECI(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDVECR(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDVECB(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDVECX(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTII(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTIR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTIB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTIX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTRI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTRR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTRB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTRX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTBI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTBR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTBB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTBX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTXI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTXR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTXB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDDICTXX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPII(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPIR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPIB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPIX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPRI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPRR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPRB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPRX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPBI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPBR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPBB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPBX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPXI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPXR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPXB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDMAPXX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_STIDSTRUCT(void* emitter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_UNPACKVEC(void* emitter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_UNPACKVECX(void* emitter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_UNPACKVECALL(void* emitter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_UNPACKVECXALL(void* emitter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_x64_PUSHIDSTRUCT(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PUSHIDSTBOXI(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PUSHIDSTBOXR(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PUSHIDSTBOXB(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_x64_PACKARG(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count skip);
    void woort_JIT_Backend_x64_ASTORE(void* emitter, woort_Opcode_Global storage, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_ALOAD(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Global storage);
    void woort_JIT_Backend_x64_CAS(void* emitter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected);
    void woort_JIT_Backend_x64_JIFINITED(void* emitter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_x64_DEBUGTRAP(void* emitter);
    void woort_JIT_Backend_x64_PANICS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_PANICC(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_x64_CHKDIVIL(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHKDIVIR(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHKDIVIRZ(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_x64_CHKDIVILR(void* emitter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend);

#ifdef __cplusplus
}
#endif
