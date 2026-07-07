#pragma once

#ifdef WOORT_BUILD_WITH_ASMJIT

#include "woort.h"
#include "woort_opcode_dispatcher_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    WOORT_NODISCARD bool woort_JIT_Backend_arm64_prologue(
        const woort_CodeEnv* cenv,
        const woort_Bytecode** ip,
        void** out_emitter);
    WOORT_NODISCARD bool woort_JIT_Backend_arm64_epilogue(
        void* emitter,
        woort_JitFunction* out_code);
    WOORT_NODISCARD bool woort_JIT_Backend_arm64_post_dispatch(
        void* emitter);
    WOORT_NODISCARD bool woort_JIT_Backend_arm64_pre_dispatch(
        void* emitter);
    void woort_JIT_Backend_arm64_dropper(
        woort_JitFunction* code);
    void woort_JIT_Backend_arm64_NOP(void* emitter);
    void woort_JIT_Backend_arm64_LOAD(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_STORE(void* emitter, woort_Opcode_Global dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LOADPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STOREPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_MOV(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHRCHK(void* emitter, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_PUSHSCHK(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHCCHK(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_ASSURESSZ(void* emitter, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_PUSHS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHC(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_POPR(void* emitter, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_POPS(void* emitter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_arm64_POPC(void* emitter, woort_Opcode_Global dst);
    void woort_JIT_Backend_arm64_ITOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ITOS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RTOI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RTOS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CASTSTO(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CASTSFROM(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CASTDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ASSERTDYN(void* emitter, woort_BoxValueType target, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CALLNWO(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_CALLNFP(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_CALLNJIT(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_CALLS(void* emitter, woort_Opcode_Stack func);
    void woort_JIT_Backend_arm64_CALLC(void* emitter, woort_Opcode_Global func);
    void woort_JIT_Backend_arm64_RET(void* emitter);
    void woort_JIT_Backend_arm64_RETVS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RETVC(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_POPRS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_RESULT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_JFWD(void* emitter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_arm64_JBCK(void* emitter, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_arm64_JFWDNZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDNEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKNZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKNEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDLT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDGT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDEL(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JFWDEG(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKLT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKGT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKEL(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_JBCKEG(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off);
    void woort_JIT_Backend_arm64_MKVEC(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_MKMAP(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_MKSTRUCT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n);
    void woort_JIT_Backend_arm64_MKUNION(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_MKCLOSURE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl);
    void woort_JIT_Backend_arm64_BOXDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_UNBOXDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHECKDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PUSHBOXDYN(void* emitter, woort_BoxValueType type, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ADDI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_SUBI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MULI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_DIVI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MODI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NEGI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_EQI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_ADDR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_SUBR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MULR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_DIVR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_MODR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NEGR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_EQR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_ADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LTS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GTS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_GES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_EQS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_NES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LAND(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b);
    void woort_JIT_Backend_arm64_LNOT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CADDI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CSUBI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMULI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CDIVI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CADDR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CSUBR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMULR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CDIVR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CVADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMODI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CMODR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CLAND(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CLOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CLNOT(void* emitter, woort_Opcode_Stack dst);
    void woort_JIT_Backend_arm64_MKPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_LDIDVEC(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDVECX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDSTRUCT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_LDIDSTRING(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTB(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTIX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTRX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTBX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_LDIDDICTXX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx);
    void woort_JIT_Backend_arm64_STIDVECI(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDVECR(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDVECB(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDVECX(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTII(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTIR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTIB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTIX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTRX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTBX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDDICTXX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPII(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPIR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPIB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPIX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPRX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPBX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDMAPXX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_STIDSTRUCT(void* emitter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_UNPACKVEC(void* emitter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_UNPACKVECX(void* emitter, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_UNPACKVECALL(void* emitter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_UNPACKVECXALL(void* emitter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec);
    void woort_JIT_Backend_arm64_PUSHIDSTRUCT(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PUSHIDSTBOXI(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PUSHIDSTBOXR(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PUSHIDSTBOXB(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj);
    void woort_JIT_Backend_arm64_PACKARG(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count skip);
    void woort_JIT_Backend_arm64_ASTORE(void* emitter, woort_Opcode_Global storage, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_ALOAD(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Global storage);
    void woort_JIT_Backend_arm64_CAS(void* emitter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected);
    void woort_JIT_Backend_arm64_JIFINITED(void* emitter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target);
    void woort_JIT_Backend_arm64_DEBUGTRAP(void* emitter);
    void woort_JIT_Backend_arm64_PANICS(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_PANICC(void* emitter, woort_Opcode_Global src);
    void woort_JIT_Backend_arm64_CHKDIVIL(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHKDIVIR(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHKDIVIRZ(void* emitter, woort_Opcode_Stack src);
    void woort_JIT_Backend_arm64_CHKDIVILR(void* emitter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend);

#ifdef __cplusplus
}
#endif

#endif