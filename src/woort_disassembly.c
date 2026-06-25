#include "woort_disassembly.h"

#include "woort_opcode.h"
#include "woort_opcode_dispatcher.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct woort_DisassemblyCtx
{
    woort_Disassembly_DumpCallback callback;
    const woort_Bytecode* pc;
    int printed;
    const woort_OpcodeDispatchers* dispatchers;
};

#define CTX ((struct woort_DisassemblyCtx*)userdata)

static void dis_NOP(void* userdata)
{ CTX->callback("NOP\n"); CTX->printed = 1; }

static void dis_LOAD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Global src)
{ CTX->callback("LOAD        [SB %+d] = G[%u]\n", dst, src); CTX->printed = 1; }

static void dis_STORE(void* userdata, woort_Opcode_Global dst, woort_Opcode_Stack src)
{ CTX->callback("STORE       G[%u] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_LOADPVALUE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("LOADPVALUE  [SB %+d] = *[SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_STOREPVALUE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("STOREPVALUE *[SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_MOVLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MOVLD       [SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_MOVST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MOVST       [SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_PUSHRCHK(void* userdata, woort_Opcode_Count n)
{ CTX->callback("PUSHRCHK    %u\n", n); CTX->printed = 1; }

static void dis_PUSHSCHK(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("PUSHSCHK    [SB %+d]\n", src); CTX->printed = 1; }

static void dis_PUSHCCHK(void* userdata, woort_Opcode_Global src)
{ CTX->callback("PUSHCCHK    G[%u]\n", src); CTX->printed = 1; }

static void dis_ASSURESSZ(void* userdata, woort_Opcode_Count n)
{ CTX->callback("ASSURESSZ   %u\n", n); CTX->printed = 1; }

static void dis_PUSHS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("PUSHS       [SB %+d]\n", src); CTX->printed = 1; }

static void dis_PUSHC(void* userdata, woort_Opcode_Global src)
{ CTX->callback("PUSHC       G[%u]\n", src); CTX->printed = 1; }

static void dis_POPR(void* userdata, woort_Opcode_Count n)
{ CTX->callback("POPR        %u\n", n); CTX->printed = 1; }

static void dis_POPS(void* userdata, woort_Opcode_Stack dst)
{ CTX->callback("POPS        [SB %+d]\n", dst); CTX->printed = 1; }

static void dis_POPC(void* userdata, woort_Opcode_Global dst)
{ CTX->callback("POPC        G[%u]\n", dst); CTX->printed = 1; }

static void dis_ITORST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITORST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_ITORLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITORLD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_ITOSST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITOSST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_ITOSLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITOSLD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_RTOIST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOIST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_RTOILD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOILD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_RTOSST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOSST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_RTOSLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOSLD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_CASTSTO(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{ CTX->callback("CASTSTO     T%u, [SB %+d] -> [SB %+d]\n", target, src, dst); CTX->printed = 1; }

static void dis_CASTSFROM(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src)
{ CTX->callback("CASTSFROM   T%u, [SB %+d] -> [SB %+d]\n", srctype, src, dst); CTX->printed = 1; }

static void dis_CASTDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{ CTX->callback("CASTDYN     T%u, [SB %+d] -> [SB %+d]\n", target, src, dst); CTX->printed = 1; }

static void dis_ASSERTDYN(void* userdata, woort_BoxValueType target, woort_Opcode_Stack src)
{ CTX->callback("ASSERTDYN   T%u, [SB %+d]\n", target, src); CTX->printed = 1; }

static void dis_CALLNWO(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLNWO     G[%u]\n", func); CTX->printed = 1; }

static void dis_CALLNFP(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLNFP     G[%u]\n", func); CTX->printed = 1; }

static void dis_CALLNJIT(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLNJIT    G[%u]\n", func); CTX->printed = 1; }

static void dis_CALLS(void* userdata, woort_Opcode_Stack func)
{ CTX->callback("CALLS       [SB %+d]\n", func); CTX->printed = 1; }

static void dis_CALLC(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLC       G[%u]\n", func); CTX->printed = 1; }

static void dis_RET(void* userdata)
{ CTX->callback("RET\n"); CTX->printed = 1; }

static void dis_RETVS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("RETVS       [SB %+d]\n", src); CTX->printed = 1; }

static void dis_RETVC(void* userdata, woort_Opcode_Global src)
{ CTX->callback("RETVC       G[%u]\n", src); CTX->printed = 1; }

static void dis_POPRS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("POPRS       [SB %+d]\n", src); CTX->printed = 1; }

static void dis_RESULT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("RESULT      [SB %+d], POP %u\n", dst, n); CTX->printed = 1; }

static void dis_JFWD(void* userdata, woort_Opcode_CodeAbs target)
{ CTX->callback("JFWD        %u\n", target); CTX->printed = 1; }

static void dis_JBCK(void* userdata, woort_Opcode_CodeAbs target)
{ CTX->callback("JBCK        %u\n", target); CTX->printed = 1; }

static void dis_JFWDNZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDNZ      +%u IF [SB %+d] != 0\n", off, cond); CTX->printed = 1; }

static void dis_JFWDZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDZ       +%u IF [SB %+d] == 0\n", off, cond); CTX->printed = 1; }

static void dis_JFWDEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDEQ      +%u IF [SB %+d] == [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JFWDNEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDNEQ     +%u IF [SB %+d] != [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JBCKNZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKNZ      -%u IF [SB %+d] != 0\n", off, cond); CTX->printed = 1; }

static void dis_JBCKZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKZ       -%u IF [SB %+d] == 0\n", off, cond); CTX->printed = 1; }

static void dis_JBCKEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKEQ      -%u IF [SB %+d] == [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JBCKNEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKNEQ     -%u IF [SB %+d] != [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JFWDLT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDLT      +%u IF [SB %+d] < [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JFWDGT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDGT      +%u IF [SB %+d] > [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JFWDEL(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDEL      +%u IF [SB %+d] <= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JFWDEG(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDEG      +%u IF [SB %+d] >= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JBCKLT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKLT      -%u IF [SB %+d] < [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JBCKGT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKGT      -%u IF [SB %+d] > [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JBCKEL(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKEL      -%u IF [SB %+d] <= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_JBCKEG(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKEG      -%u IF [SB %+d] >= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void dis_MKVEC(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("MKVEC       %u -> [SB %+d]\n", n, dst); CTX->printed = 1; }

static void dis_MKMAP(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("MKMAP       %u -> [SB %+d]\n", n, dst); CTX->printed = 1; }

static void dis_MKSTRUCT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("MKSTRUCT    %u -> [SB %+d]\n", n, dst); CTX->printed = 1; }

static void dis_MKUNION(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("MKUNION     %u, [SB %+d] -> [SB %+d]\n", idx, src, dst); CTX->printed = 1; }

static void dis_MKCLOSURE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{ CTX->callback("MKCLOSURE   %u -> [SB %+d], G[%u]\n", n, dst, tmpl); CTX->printed = 1; }

static void dis_BOXDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("BOXDYN      T%u, [SB %+d] -> [SB %+d]\n", type, src, dst); CTX->printed = 1; }

static void dis_UNBOXDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("UNBOXDYN    T%u, [SB %+d] -> [SB %+d]\n", type, src, dst); CTX->printed = 1; }

static void dis_CHECKDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("CHECKDYN    T%u, [SB %+d] -> [SB %+d]\n", type, src, dst); CTX->printed = 1; }

static void dis_PUSHBOXDYN(void* userdata, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("PUSHBOXDYN  T%u, [SB %+d]\n", type, src); CTX->printed = 1; }

static void dis_ADDI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("ADDI        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_SUBI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("SUBI        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_MULI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MULI        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_DIVI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("DIVI        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_MODI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MODI        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_NEGI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("NEGI        -[SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_LTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LTI         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_GTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GTI         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LEI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LEI         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_GEI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GEI         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_EQI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("EQI         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_NEI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("NEI         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_ADDR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("ADDR        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_SUBR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("SUBR        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_MULR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MULR        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_DIVR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("DIVR        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_MODR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MODR        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_NEGR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("NEGR        -[SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_LTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LTR         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_GTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GTR         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LER(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LER         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_GER(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GER         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_EQR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("EQR         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_NER(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("NER         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_ADDS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("ADDS        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LTS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LTS         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_GTS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GTS         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LES(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LES         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_GES(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GES         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_EQS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("EQS         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_NES(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("NES         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LAND(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LAND        [SB %+d] && [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LOR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LOR         [SB %+d] || [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void dis_LNOT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("LNOT        ![SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void dis_CADDI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CADDI       [SB %+d] += [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CSUBI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CSUBI       [SB %+d] -= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CMULI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMULI       [SB %+d] *= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CDIVI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CDIVI       [SB %+d] /= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CADDR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CADDR       [SB %+d] += [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CSUBR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CSUBR       [SB %+d] -= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CMULR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMULR       [SB %+d] *= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CDIVR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CDIVR       [SB %+d] /= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CADDS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CADDS       [SB %+d] += [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CVADDS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CVADDS      [SB %+d] = [SB %+d] + [SB %+d]\n", dst, src, dst); CTX->printed = 1; }

static void dis_CMODI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMODI       [SB %+d] %%= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CMODR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMODR       [SB %+d] %%= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CLAND(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CLAND       [SB %+d] &&= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CLOR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CLOR        [SB %+d] ||= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_CLNOT(void* userdata, woort_Opcode_Stack dst)
{ CTX->callback("CLNOT       ![SB %+d]\n", dst); CTX->printed = 1; }

static void dis_MKPVALUE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MKPVALUE    *[SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void dis_LDIDXVEC(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXVEC    [SB %+d].[SB %+d] -> [SB %+d]\n", vec, idx, dst); CTX->printed = 1; }

static void dis_LDIDXVECX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXVECX   [SB %+d].[SB %+d] -> [SB %+d]\n", vec, idx, dst); CTX->printed = 1; }

static void dis_LDIDSTRUCT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("LDIDSTRUCT  [SB %+d].%u -> [SB %+d]\n", obj, idx, dst); CTX->printed = 1; }

static void dis_LDIDSTRING(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{ CTX->callback("LDIDSTRING  [[SB %+d].[SB %+d]] -> [SB %+d]\n", str, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTI  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTR  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTB(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTB  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTX  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTIX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTIX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTRX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTRX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTBX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTBX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_LDIDXDICTXX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTXX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void dis_STIDXVECI(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECI   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void dis_STIDXVECR(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECR   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void dis_STIDXVECB(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECB   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void dis_STIDXVECX(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECX   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void dis_STIDXDICTII(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTII [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTIR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTIR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTIB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTIB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTIX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTIX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTRI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTRR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTRB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTRX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTBI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTBR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTBB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTBX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTXI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTXR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTXB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXDICTXX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPII(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPII  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPIR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPIR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPIB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPIB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPIX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPIX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPRI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPRR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPRB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPRX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPBI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPBR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPBB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPBX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPXI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPXR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPXB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPXX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDSTRUCT(void* userdata, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("STIDSTRUCT  [SB %+d].%u = [SB %+d]\n", obj, idx, src); CTX->printed = 1; }

static void dis_STIDXVECEXT(void* userdata, woort_Opcode_Stack vec, woort_BoxValueType valtype, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ (void)valtype; CTX->callback("STIDXVECEXT [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void dis_STIDXDICTEXT(void* userdata, woort_Opcode_Stack map, woort_BoxValueType keytype, woort_BoxValueType valtype, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ (void)keytype; (void)valtype; CTX->callback("STIDXDICTEXT [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDXMAPEXT(void* userdata, woort_Opcode_Stack map, woort_BoxValueType keytype, woort_BoxValueType valtype, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ (void)keytype; (void)valtype; CTX->callback("STIDXMAPEXT [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void dis_STIDSTRUCTEXT(void* userdata, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("STIDSTRUCTEXT [SB %+d].%u = [SB %+d]\n", obj, idx, src); CTX->printed = 1; }

static void dis_UNPACKVEC(void* userdata, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVEC   %u in [SB %+d]\n", n, vec); CTX->printed = 1; }

static void dis_UNPACKVECX(void* userdata, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECX  %u in [SB %+d]\n", n, vec); CTX->printed = 1; }

static void dis_UNPACKVECALL(void* userdata, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECALL %u in [SB %+d] -> [SB %+d]\n", n, vec, count_dst); CTX->printed = 1; }

static void dis_UNPACKVECXALL(void* userdata, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECXALL %u in [SB %+d] -> [SB %+d]\n", n, vec, count_dst); CTX->printed = 1; }

static void dis_PUSHIDXSTRUCT(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTRUCT [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void dis_PUSHIDXSTBOXI(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTBOXI [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void dis_PUSHIDXSTBOXR(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTBOXR [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void dis_PUSHIDXSTBOXB(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTBOXB [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void dis_PACKARG(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count skip)
{ CTX->callback("PACKARG     %u -> [SB %+d]\n", skip, dst); CTX->printed = 1; }

static void dis_ASTORE(void* userdata, woort_Opcode_Global storage, woort_Opcode_Stack src)
{ CTX->callback("ASTORE      G[%u] = [SB %+d]\n", storage, src); CTX->printed = 1; }

static void dis_ALOAD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{ CTX->callback("ALOAD       [SB %+d] = G[%u]\n", dst, storage); CTX->printed = 1; }

static void dis_CAS(void* userdata, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{ CTX->callback("CAS         DESIRED=[SB %+d] EXPECTED=[SB %+d], G[%u]\n", desired, expected, storage); CTX->printed = 1; }

static void dis_JIFINITED(void* userdata, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{ CTX->callback("JIFINITED   %u, IF ALOAD G[%u] != 0\n", target, flag); CTX->printed = 1; }

static void dis_PANICS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("PANICS      [SB %+d]\n", src); CTX->printed = 1; }

static void dis_PANICC(void* userdata, woort_Opcode_Global src)
{ CTX->callback("PANICC      G[%u]\n", src); CTX->printed = 1; }

static void dis_CHKDIVIL(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("CHKDIVIL    [SB %+d]\n", src); CTX->printed = 1; }

static void dis_CHKDIVIR(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("CHKDIVIR    [SB %+d]\n", src); CTX->printed = 1; }

static void dis_CHKDIVIRZ(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("CHKDIVIRZ   [SB %+d]\n", src); CTX->printed = 1; }

static void dis_CHKDIVILR(void* userdata, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{ CTX->callback("CHKDIVILR   [SB %+d], [SB %+d]\n", divisor, dividend); CTX->printed = 1; }

static void dis_DEBUGTRAP(void* userdata)
{
    struct woort_DisassemblyCtx* ctx = CTX;
    woort_CodeEnv* cenv;
    if (woort_CodeEnv_find(ctx->pc, &cenv))
    {
        const woort_Bytecode buf[2] = {
            woort_CodeEnv_raw_trap(cenv, ctx->pc), ctx->pc[1] };
        (void)woort_OpcodeDispatcher_decode(buf, ctx->dispatchers, ctx);
    }
    else
    {
        ctx->callback("DEBUGTRAP\n");
    }
    ctx->printed = 1;
}

#undef CTX

static const woort_OpcodeDispatchers g_disasm_dispatchers = {
    .m_NOP = dis_NOP,
    .m_LOAD = dis_LOAD,
    .m_STORE = dis_STORE,
    .m_LOADPVALUE = dis_LOADPVALUE,
    .m_STOREPVALUE = dis_STOREPVALUE,
    .m_MOVLD = dis_MOVLD,
    .m_MOVST = dis_MOVST,
    .m_PUSHRCHK = dis_PUSHRCHK,
    .m_PUSHSCHK = dis_PUSHSCHK,
    .m_PUSHCCHK = dis_PUSHCCHK,
    .m_ASSURESSZ = dis_ASSURESSZ,
    .m_PUSHS = dis_PUSHS,
    .m_PUSHC = dis_PUSHC,
    .m_POPR = dis_POPR,
    .m_POPS = dis_POPS,
    .m_POPC = dis_POPC,
    .m_ITORST = dis_ITORST,
    .m_ITORLD = dis_ITORLD,
    .m_ITOSST = dis_ITOSST,
    .m_ITOSLD = dis_ITOSLD,
    .m_RTOIST = dis_RTOIST,
    .m_RTOILD = dis_RTOILD,
    .m_RTOSST = dis_RTOSST,
    .m_RTOSLD = dis_RTOSLD,
    .m_CASTSTO = dis_CASTSTO,
    .m_CASTSFROM = dis_CASTSFROM,
    .m_CASTDYN = dis_CASTDYN,
    .m_ASSERTDYN = dis_ASSERTDYN,
    .m_CALLNWO = dis_CALLNWO,
    .m_CALLNFP = dis_CALLNFP,
    .m_CALLNJIT = dis_CALLNJIT,
    .m_CALLS = dis_CALLS,
    .m_CALLC = dis_CALLC,
    .m_RET = dis_RET,
    .m_RETVS = dis_RETVS,
    .m_RETVC = dis_RETVC,
    .m_POPRS = dis_POPRS,
    .m_RESULT = dis_RESULT,
    .m_JFWD = dis_JFWD,
    .m_JBCK = dis_JBCK,
    .m_JFWDNZ = dis_JFWDNZ,
    .m_JFWDZ = dis_JFWDZ,
    .m_JFWDEQ = dis_JFWDEQ,
    .m_JFWDNEQ = dis_JFWDNEQ,
    .m_JBCKNZ = dis_JBCKNZ,
    .m_JBCKZ = dis_JBCKZ,
    .m_JBCKEQ = dis_JBCKEQ,
    .m_JBCKNEQ = dis_JBCKNEQ,
    .m_JFWDLT = dis_JFWDLT,
    .m_JFWDGT = dis_JFWDGT,
    .m_JFWDEL = dis_JFWDEL,
    .m_JFWDEG = dis_JFWDEG,
    .m_JBCKLT = dis_JBCKLT,
    .m_JBCKGT = dis_JBCKGT,
    .m_JBCKEL = dis_JBCKEL,
    .m_JBCKEG = dis_JBCKEG,
    .m_MKVEC = dis_MKVEC,
    .m_MKMAP = dis_MKMAP,
    .m_MKSTRUCT = dis_MKSTRUCT,
    .m_MKUNION = dis_MKUNION,
    .m_MKCLOSURE = dis_MKCLOSURE,
    .m_BOXDYN = dis_BOXDYN,
    .m_UNBOXDYN = dis_UNBOXDYN,
    .m_CHECKDYN = dis_CHECKDYN,
    .m_PUSHBOXDYN = dis_PUSHBOXDYN,
    .m_ADDI = dis_ADDI,
    .m_SUBI = dis_SUBI,
    .m_MULI = dis_MULI,
    .m_DIVI = dis_DIVI,
    .m_MODI = dis_MODI,
    .m_NEGI = dis_NEGI,
    .m_LTI = dis_LTI,
    .m_GTI = dis_GTI,
    .m_LEI = dis_LEI,
    .m_GEI = dis_GEI,
    .m_EQI = dis_EQI,
    .m_NEI = dis_NEI,
    .m_ADDR = dis_ADDR,
    .m_SUBR = dis_SUBR,
    .m_MULR = dis_MULR,
    .m_DIVR = dis_DIVR,
    .m_MODR = dis_MODR,
    .m_NEGR = dis_NEGR,
    .m_LTR = dis_LTR,
    .m_GTR = dis_GTR,
    .m_LER = dis_LER,
    .m_GER = dis_GER,
    .m_EQR = dis_EQR,
    .m_NER = dis_NER,
    .m_ADDS = dis_ADDS,
    .m_LTS = dis_LTS,
    .m_GTS = dis_GTS,
    .m_LES = dis_LES,
    .m_GES = dis_GES,
    .m_EQS = dis_EQS,
    .m_NES = dis_NES,
    .m_LAND = dis_LAND,
    .m_LOR = dis_LOR,
    .m_LNOT = dis_LNOT,
    .m_CADDI = dis_CADDI,
    .m_CSUBI = dis_CSUBI,
    .m_CMULI = dis_CMULI,
    .m_CDIVI = dis_CDIVI,
    .m_CADDR = dis_CADDR,
    .m_CSUBR = dis_CSUBR,
    .m_CMULR = dis_CMULR,
    .m_CDIVR = dis_CDIVR,
    .m_CADDS = dis_CADDS,
    .m_CVADDS = dis_CVADDS,
    .m_CMODI = dis_CMODI,
    .m_CMODR = dis_CMODR,
    .m_CLAND = dis_CLAND,
    .m_CLOR = dis_CLOR,
    .m_CLNOT = dis_CLNOT,
    .m_MKPVALUE = dis_MKPVALUE,
    .m_LDIDXVEC = dis_LDIDXVEC,
    .m_LDIDXVECX = dis_LDIDXVECX,
    .m_LDIDSTRUCT = dis_LDIDSTRUCT,
    .m_LDIDSTRING = dis_LDIDSTRING,
    .m_LDIDXDICTI = dis_LDIDXDICTI,
    .m_LDIDXDICTR = dis_LDIDXDICTR,
    .m_LDIDXDICTB = dis_LDIDXDICTB,
    .m_LDIDXDICTX = dis_LDIDXDICTX,
    .m_LDIDXDICTIX = dis_LDIDXDICTIX,
    .m_LDIDXDICTRX = dis_LDIDXDICTRX,
    .m_LDIDXDICTBX = dis_LDIDXDICTBX,
    .m_LDIDXDICTXX = dis_LDIDXDICTXX,
    .m_STIDXVECI = dis_STIDXVECI,
    .m_STIDXVECR = dis_STIDXVECR,
    .m_STIDXVECB = dis_STIDXVECB,
    .m_STIDXVECX = dis_STIDXVECX,
    .m_STIDXDICTII = dis_STIDXDICTII,
    .m_STIDXDICTIR = dis_STIDXDICTIR,
    .m_STIDXDICTIB = dis_STIDXDICTIB,
    .m_STIDXDICTIX = dis_STIDXDICTIX,
    .m_STIDXDICTRI = dis_STIDXDICTRI,
    .m_STIDXDICTRR = dis_STIDXDICTRR,
    .m_STIDXDICTRB = dis_STIDXDICTRB,
    .m_STIDXDICTRX = dis_STIDXDICTRX,
    .m_STIDXDICTBI = dis_STIDXDICTBI,
    .m_STIDXDICTBR = dis_STIDXDICTBR,
    .m_STIDXDICTBB = dis_STIDXDICTBB,
    .m_STIDXDICTBX = dis_STIDXDICTBX,
    .m_STIDXDICTXI = dis_STIDXDICTXI,
    .m_STIDXDICTXR = dis_STIDXDICTXR,
    .m_STIDXDICTXB = dis_STIDXDICTXB,
    .m_STIDXDICTXX = dis_STIDXDICTXX,
    .m_STIDXMAPII = dis_STIDXMAPII,
    .m_STIDXMAPIR = dis_STIDXMAPIR,
    .m_STIDXMAPIB = dis_STIDXMAPIB,
    .m_STIDXMAPIX = dis_STIDXMAPIX,
    .m_STIDXMAPRI = dis_STIDXMAPRI,
    .m_STIDXMAPRR = dis_STIDXMAPRR,
    .m_STIDXMAPRB = dis_STIDXMAPRB,
    .m_STIDXMAPRX = dis_STIDXMAPRX,
    .m_STIDXMAPBI = dis_STIDXMAPBI,
    .m_STIDXMAPBR = dis_STIDXMAPBR,
    .m_STIDXMAPBB = dis_STIDXMAPBB,
    .m_STIDXMAPBX = dis_STIDXMAPBX,
    .m_STIDXMAPXI = dis_STIDXMAPXI,
    .m_STIDXMAPXR = dis_STIDXMAPXR,
    .m_STIDXMAPXB = dis_STIDXMAPXB,
    .m_STIDXMAPXX = dis_STIDXMAPXX,
    .m_STIDSTRUCT = dis_STIDSTRUCT,
    .m_STIDXVECEXT = dis_STIDXVECEXT,
    .m_STIDXDICTEXT = dis_STIDXDICTEXT,
    .m_STIDXMAPEXT = dis_STIDXMAPEXT,
    .m_STIDSTRUCTEXT = dis_STIDSTRUCTEXT,
    .m_UNPACKVEC = dis_UNPACKVEC,
    .m_UNPACKVECX = dis_UNPACKVECX,
    .m_UNPACKVECALL = dis_UNPACKVECALL,
    .m_UNPACKVECXALL = dis_UNPACKVECXALL,
    .m_PUSHIDXSTRUCT = dis_PUSHIDXSTRUCT,
    .m_PUSHIDXSTBOXI = dis_PUSHIDXSTBOXI,
    .m_PUSHIDXSTBOXR = dis_PUSHIDXSTBOXR,
    .m_PUSHIDXSTBOXB = dis_PUSHIDXSTBOXB,
    .m_PACKARG = dis_PACKARG,
    .m_ASTORE = dis_ASTORE,
    .m_ALOAD = dis_ALOAD,
    .m_CAS = dis_CAS,
    .m_JIFINITED = dis_JIFINITED,
    .m_DEBUGTRAP = dis_DEBUGTRAP,
    .m_PANICS = dis_PANICS,
    .m_PANICC = dis_PANICC,
    .m_CHKDIVIL = dis_CHKDIVIL,
    .m_CHKDIVIR = dis_CHKDIVIR,
    .m_CHKDIVIRZ = dis_CHKDIVIRZ,
    .m_CHKDIVILR = dis_CHKDIVILR,
};

const woort_Bytecode* woort_disassembly(
    const woort_Bytecode* c, woort_Disassembly_DumpCallback callback)
{
    struct woort_DisassemblyCtx ctx = { callback, c, 0, &g_disasm_dispatchers };

    const woort_Bytecode* next =
        woort_OpcodeDispatcher_decode(c, &g_disasm_dispatchers, &ctx);

    if (!ctx.printed)
        callback("UNKNOWN_OPCODE(%u)\n", (uint8_t)WOORT_BYTECODE(OP6, c[0]));

    return next;
}

void woort_dump_codes(
    const woort_CodeEnv* code_env, woort_Disassembly_DumpCallback callback)
{
    const woort_Bytecode* pc = code_env->m_code_begin;
    const woort_Bytecode* next_bc = pc;

    while (pc < code_env->m_code_end)
    {
        callback("%04zu:\t", (size_t)(pc - code_env->m_code_begin));

        if (pc == next_bc)
            next_bc = woort_disassembly(pc, callback);
        else
            callback("\n");

        ++pc;
    }

    callback("\n");
}
