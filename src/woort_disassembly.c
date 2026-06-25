#include "woort_disassembly.h"

#include "woort_opcode.h"
#include "woort_opcode_dispatcher.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct woort_DisassemblyCtx
{
    woort_Disassembly_DumpCallback callback;
    int printed;
};

#define CTX ((struct woort_DisassemblyCtx*)userdata)

static void _woort_dis_NOP(void* userdata)
{ CTX->callback("NOP\n"); CTX->printed = 1; }

static void _woort_dis_LOAD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Global src)
{ CTX->callback("LOAD        [SB %+d] = G[%u]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_STORE(void* userdata, woort_Opcode_Global dst, woort_Opcode_Stack src)
{ CTX->callback("STORE       G[%u] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_LOADPVALUE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("LOADPVALUE  [SB %+d] = *[SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_STOREPVALUE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("STOREPVALUE *[SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_MOVLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MOVLD       [SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_MOVST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MOVST       [SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_PUSHRCHK(void* userdata, woort_Opcode_Count n)
{ CTX->callback("PUSHRCHK    %u\n", n); CTX->printed = 1; }

static void _woort_dis_PUSHSCHK(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("PUSHSCHK    [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_PUSHCCHK(void* userdata, woort_Opcode_Global src)
{ CTX->callback("PUSHCCHK    G[%u]\n", src); CTX->printed = 1; }

static void _woort_dis_ASSURESSZ(void* userdata, woort_Opcode_Count n)
{ CTX->callback("ASSURESSZ   %u\n", n); CTX->printed = 1; }

static void _woort_dis_PUSHS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("PUSHS       [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_PUSHC(void* userdata, woort_Opcode_Global src)
{ CTX->callback("PUSHC       G[%u]\n", src); CTX->printed = 1; }

static void _woort_dis_POPR(void* userdata, woort_Opcode_Count n)
{ CTX->callback("POPR        %u\n", n); CTX->printed = 1; }

static void _woort_dis_POPS(void* userdata, woort_Opcode_Stack dst)
{ CTX->callback("POPS        [SB %+d]\n", dst); CTX->printed = 1; }

static void _woort_dis_POPC(void* userdata, woort_Opcode_Global dst)
{ CTX->callback("POPC        G[%u]\n", dst); CTX->printed = 1; }

static void _woort_dis_ITORST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITORST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_ITORLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITORLD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_ITOSST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITOSST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_ITOSLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITOSLD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_RTOIST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOIST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_RTOILD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOILD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_RTOSST(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOSST      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_RTOSLD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOSLD      [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_CASTSTO(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{ CTX->callback("CASTSTO     T%u, [SB %+d] -> [SB %+d]\n", target, src, dst); CTX->printed = 1; }

static void _woort_dis_CASTSFROM(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src)
{ CTX->callback("CASTSFROM   T%u, [SB %+d] -> [SB %+d]\n", srctype, src, dst); CTX->printed = 1; }

static void _woort_dis_CASTDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{ CTX->callback("CASTDYN     T%u, [SB %+d] -> [SB %+d]\n", target, src, dst); CTX->printed = 1; }

static void _woort_dis_ASSERTDYN(void* userdata, woort_BoxValueType target, woort_Opcode_Stack src)
{ CTX->callback("ASSERTDYN   T%u, [SB %+d]\n", target, src); CTX->printed = 1; }

static void _woort_dis_CALLNWO(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLNWO     G[%u]\n", func); CTX->printed = 1; }

static void _woort_dis_CALLNFP(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLNFP     G[%u]\n", func); CTX->printed = 1; }

static void _woort_dis_CALLNJIT(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLNJIT    G[%u]\n", func); CTX->printed = 1; }

static void _woort_dis_CALLS(void* userdata, woort_Opcode_Stack func)
{ CTX->callback("CALLS       [SB %+d]\n", func); CTX->printed = 1; }

static void _woort_dis_CALLC(void* userdata, woort_Opcode_Global func)
{ CTX->callback("CALLC       G[%u]\n", func); CTX->printed = 1; }

static void _woort_dis_RET(void* userdata)
{ CTX->callback("RET\n"); CTX->printed = 1; }

static void _woort_dis_RETVS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("RETVS       [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_RETVC(void* userdata, woort_Opcode_Global src)
{ CTX->callback("RETVC       G[%u]\n", src); CTX->printed = 1; }

static void _woort_dis_POPRS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("POPRS       [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_RESULT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("RESULT      [SB %+d], POP %u\n", dst, n); CTX->printed = 1; }

static void _woort_dis_JFWD(void* userdata, woort_Opcode_CodeAbs target)
{ CTX->callback("JFWD        %u\n", target); CTX->printed = 1; }

static void _woort_dis_JBCK(void* userdata, woort_Opcode_CodeAbs target)
{ CTX->callback("JBCK        %u\n", target); CTX->printed = 1; }

static void _woort_dis_JFWDNZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDNZ      +%u IF [SB %+d] != 0\n", off, cond); CTX->printed = 1; }

static void _woort_dis_JFWDZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDZ       +%u IF [SB %+d] == 0\n", off, cond); CTX->printed = 1; }

static void _woort_dis_JFWDEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDEQ      +%u IF [SB %+d] == [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JFWDNEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDNEQ     +%u IF [SB %+d] != [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JBCKNZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKNZ      -%u IF [SB %+d] != 0\n", off, cond); CTX->printed = 1; }

static void _woort_dis_JBCKZ(void* userdata, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKZ       -%u IF [SB %+d] == 0\n", off, cond); CTX->printed = 1; }

static void _woort_dis_JBCKEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKEQ      -%u IF [SB %+d] == [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JBCKNEQ(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKNEQ     -%u IF [SB %+d] != [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JFWDLT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDLT      +%u IF [SB %+d] < [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JFWDGT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDGT      +%u IF [SB %+d] > [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JFWDEL(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDEL      +%u IF [SB %+d] <= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JFWDEG(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JFWDEG      +%u IF [SB %+d] >= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JBCKLT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKLT      -%u IF [SB %+d] < [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JBCKGT(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKGT      -%u IF [SB %+d] > [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JBCKEL(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKEL      -%u IF [SB %+d] <= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_JBCKEG(void* userdata, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{ CTX->callback("JBCKEG      -%u IF [SB %+d] >= [SB %+d]\n", off, a, b); CTX->printed = 1; }

static void _woort_dis_MKVEC(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("MKVEC       %u -> [SB %+d]\n", n, dst); CTX->printed = 1; }

static void _woort_dis_MKMAP(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("MKMAP       %u -> [SB %+d]\n", n, dst); CTX->printed = 1; }

static void _woort_dis_MKSTRUCT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n)
{ CTX->callback("MKSTRUCT    %u -> [SB %+d]\n", n, dst); CTX->printed = 1; }

static void _woort_dis_MKUNION(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("MKUNION     %u, [SB %+d] -> [SB %+d]\n", idx, src, dst); CTX->printed = 1; }

static void _woort_dis_MKCLOSURE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{ CTX->callback("MKCLOSURE   %u -> [SB %+d], G[%u]\n", n, dst, tmpl); CTX->printed = 1; }

static void _woort_dis_BOXDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("BOXDYN      T%u, [SB %+d] -> [SB %+d]\n", type, src, dst); CTX->printed = 1; }

static void _woort_dis_UNBOXDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("UNBOXDYN    T%u, [SB %+d] -> [SB %+d]\n", type, src, dst); CTX->printed = 1; }

static void _woort_dis_CHECKDYN(void* userdata, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("CHECKDYN    T%u, [SB %+d] -> [SB %+d]\n", type, src, dst); CTX->printed = 1; }

static void _woort_dis_PUSHBOXDYN(void* userdata, woort_BoxValueType type, woort_Opcode_Stack src)
{ CTX->callback("PUSHBOXDYN  T%u, [SB %+d]\n", type, src); CTX->printed = 1; }

static void _woort_dis_ADDI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("ADDI        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_SUBI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("SUBI        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_MULI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MULI        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_DIVI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("DIVI        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_MODI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MODI        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_NEGI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("NEGI        -[SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_LTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LTI         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_GTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GTI         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LEI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LEI         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_GEI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GEI         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_EQI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("EQI         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_NEI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("NEI         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_ADDR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("ADDR        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_SUBR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("SUBR        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_MULR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MULR        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_DIVR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("DIVR        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_MODR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("MODR        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_NEGR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("NEGR        -[SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_LTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LTR         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_GTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GTR         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LER(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LER         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_GER(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GER         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_EQR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("EQR         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_NER(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("NER         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_ADDS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("ADDS        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LTS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LTS         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_GTS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GTS         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LES(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LES         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_GES(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("GES         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_EQS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("EQS         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_NES(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("NES         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LAND(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LAND        [SB %+d] && [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LOR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{ CTX->callback("LOR         [SB %+d] || [SB %+d] -> [SB %+d]\n", a, b, dst); CTX->printed = 1; }

static void _woort_dis_LNOT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("LNOT        ![SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_CADDI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CADDI       [SB %+d] += [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CSUBI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CSUBI       [SB %+d] -= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CMULI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMULI       [SB %+d] *= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CDIVI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CDIVI       [SB %+d] /= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CADDR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CADDR       [SB %+d] += [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CSUBR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CSUBR       [SB %+d] -= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CMULR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMULR       [SB %+d] *= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CDIVR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CDIVR       [SB %+d] /= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CADDS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CADDS       [SB %+d] += [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CVADDS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CVADDS      [SB %+d] = [SB %+d] + [SB %+d]\n", dst, src, dst); CTX->printed = 1; }

static void _woort_dis_CMODI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMODI       [SB %+d] %%= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CMODR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CMODR       [SB %+d] %%= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CLAND(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CLAND       [SB %+d] &&= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CLOR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("CLOR        [SB %+d] ||= [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_CLNOT(void* userdata, woort_Opcode_Stack dst)
{ CTX->callback("CLNOT       ![SB %+d]\n", dst); CTX->printed = 1; }

static void _woort_dis_MKPVALUE(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MKPVALUE    *[SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

static void _woort_dis_LDIDXVEC(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXVEC    [SB %+d].[SB %+d] -> [SB %+d]\n", vec, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXVECX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXVECX   [SB %+d].[SB %+d] -> [SB %+d]\n", vec, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDSTRUCT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("LDIDSTRUCT  [SB %+d].%u -> [SB %+d]\n", obj, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDSTRING(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{ CTX->callback("LDIDSTRING  [[SB %+d].[SB %+d]] -> [SB %+d]\n", str, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTI  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTR  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTB(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTB  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTX  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTIX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTIX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTRX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTRX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTBX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTBX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDXDICTXX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDXDICTXX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_STIDXVECI(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECI   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDXVECR(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECR   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDXVECB(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECB   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDXVECX(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDXVECX   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTII(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTII [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTIR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTIR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTIB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTIB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTIX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTIX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTRI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTRR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTRB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTRX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTRX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTBI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTBR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTBB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTBX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTBX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTXI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTXR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTXB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXDICTXX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXDICTXX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPII(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPII  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPIR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPIR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPIB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPIB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPIX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPIX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPRI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPRR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPRB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPRX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPRX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPBI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPBR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPBB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPBX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPBX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPXI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPXR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPXB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDXMAPXX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDXMAPXX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDSTRUCT(void* userdata, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("STIDSTRUCT  [SB %+d].%u = [SB %+d]\n", obj, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDSTRUCTEXT(void* userdata, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("STIDSTRUCTEXT [SB %+d].%u = [SB %+d]\n", obj, idx, src); CTX->printed = 1; }

static void _woort_dis_UNPACKVEC(void* userdata, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVEC   %u in [SB %+d]\n", n, vec); CTX->printed = 1; }

static void _woort_dis_UNPACKVECX(void* userdata, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECX  %u in [SB %+d]\n", n, vec); CTX->printed = 1; }

static void _woort_dis_UNPACKVECALL(void* userdata, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECALL %u in [SB %+d] -> [SB %+d]\n", n, vec, count_dst); CTX->printed = 1; }

static void _woort_dis_UNPACKVECXALL(void* userdata, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECXALL %u in [SB %+d] -> [SB %+d]\n", n, vec, count_dst); CTX->printed = 1; }

static void _woort_dis_PUSHIDXSTRUCT(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTRUCT [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PUSHIDXSTBOXI(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTBOXI [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PUSHIDXSTBOXR(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTBOXR [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PUSHIDXSTBOXB(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDXSTBOXB [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PACKARG(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count skip)
{ CTX->callback("PACKARG     %u -> [SB %+d]\n", skip, dst); CTX->printed = 1; }

static void _woort_dis_ASTORE(void* userdata, woort_Opcode_Global storage, woort_Opcode_Stack src)
{ CTX->callback("ASTORE      G[%u] = [SB %+d]\n", storage, src); CTX->printed = 1; }

static void _woort_dis_ALOAD(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{ CTX->callback("ALOAD       [SB %+d] = G[%u]\n", dst, storage); CTX->printed = 1; }

static void _woort_dis_CAS(void* userdata, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{ CTX->callback("CAS         DESIRED=[SB %+d] EXPECTED=[SB %+d], G[%u]\n", desired, expected, storage); CTX->printed = 1; }

static void _woort_dis_JIFINITED(void* userdata, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{ CTX->callback("JIFINITED   %u, IF ALOAD G[%u] != 0\n", target, flag); CTX->printed = 1; }

static void _woort_dis_PANICS(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("PANICS      [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_PANICC(void* userdata, woort_Opcode_Global src)
{ CTX->callback("PANICC      G[%u]\n", src); CTX->printed = 1; }

static void _woort_dis_CHKDIVIL(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("CHKDIVIL    [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_CHKDIVIR(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("CHKDIVIR    [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_CHKDIVIRZ(void* userdata, woort_Opcode_Stack src)
{ CTX->callback("CHKDIVIRZ   [SB %+d]\n", src); CTX->printed = 1; }

static void _woort_dis_CHKDIVILR(void* userdata, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{ CTX->callback("CHKDIVILR   [SB %+d], [SB %+d]\n", divisor, dividend); CTX->printed = 1; }

static void _woort_dis_DEBUGTRAP(void* userdata)
{ CTX->callback("DEBUGTRAP\n"); CTX->printed = 1; }

#undef CTX

static const woort_OpcodeDispatchers g_disasm_dispatchers = {
    .m_NOP = _woort_dis_NOP,
    .m_LOAD = _woort_dis_LOAD,
    .m_STORE = _woort_dis_STORE,
    .m_LOADPVALUE = _woort_dis_LOADPVALUE,
    .m_STOREPVALUE = _woort_dis_STOREPVALUE,
    .m_MOVLD = _woort_dis_MOVLD,
    .m_MOVST = _woort_dis_MOVST,
    .m_PUSHRCHK = _woort_dis_PUSHRCHK,
    .m_PUSHSCHK = _woort_dis_PUSHSCHK,
    .m_PUSHCCHK = _woort_dis_PUSHCCHK,
    .m_ASSURESSZ = _woort_dis_ASSURESSZ,
    .m_PUSHS = _woort_dis_PUSHS,
    .m_PUSHC = _woort_dis_PUSHC,
    .m_POPR = _woort_dis_POPR,
    .m_POPS = _woort_dis_POPS,
    .m_POPC = _woort_dis_POPC,
    .m_ITORST = _woort_dis_ITORST,
    .m_ITORLD = _woort_dis_ITORLD,
    .m_ITOSST = _woort_dis_ITOSST,
    .m_ITOSLD = _woort_dis_ITOSLD,
    .m_RTOIST = _woort_dis_RTOIST,
    .m_RTOILD = _woort_dis_RTOILD,
    .m_RTOSST = _woort_dis_RTOSST,
    .m_RTOSLD = _woort_dis_RTOSLD,
    .m_CASTSTO = _woort_dis_CASTSTO,
    .m_CASTSFROM = _woort_dis_CASTSFROM,
    .m_CASTDYN = _woort_dis_CASTDYN,
    .m_ASSERTDYN = _woort_dis_ASSERTDYN,
    .m_CALLNWO = _woort_dis_CALLNWO,
    .m_CALLNFP = _woort_dis_CALLNFP,
    .m_CALLNJIT = _woort_dis_CALLNJIT,
    .m_CALLS = _woort_dis_CALLS,
    .m_CALLC = _woort_dis_CALLC,
    .m_RET = _woort_dis_RET,
    .m_RETVS = _woort_dis_RETVS,
    .m_RETVC = _woort_dis_RETVC,
    .m_POPRS = _woort_dis_POPRS,
    .m_RESULT = _woort_dis_RESULT,
    .m_JFWD = _woort_dis_JFWD,
    .m_JBCK = _woort_dis_JBCK,
    .m_JFWDNZ = _woort_dis_JFWDNZ,
    .m_JFWDZ = _woort_dis_JFWDZ,
    .m_JFWDEQ = _woort_dis_JFWDEQ,
    .m_JFWDNEQ = _woort_dis_JFWDNEQ,
    .m_JBCKNZ = _woort_dis_JBCKNZ,
    .m_JBCKZ = _woort_dis_JBCKZ,
    .m_JBCKEQ = _woort_dis_JBCKEQ,
    .m_JBCKNEQ = _woort_dis_JBCKNEQ,
    .m_JFWDLT = _woort_dis_JFWDLT,
    .m_JFWDGT = _woort_dis_JFWDGT,
    .m_JFWDEL = _woort_dis_JFWDEL,
    .m_JFWDEG = _woort_dis_JFWDEG,
    .m_JBCKLT = _woort_dis_JBCKLT,
    .m_JBCKGT = _woort_dis_JBCKGT,
    .m_JBCKEL = _woort_dis_JBCKEL,
    .m_JBCKEG = _woort_dis_JBCKEG,
    .m_MKVEC = _woort_dis_MKVEC,
    .m_MKMAP = _woort_dis_MKMAP,
    .m_MKSTRUCT = _woort_dis_MKSTRUCT,
    .m_MKUNION = _woort_dis_MKUNION,
    .m_MKCLOSURE = _woort_dis_MKCLOSURE,
    .m_BOXDYN = _woort_dis_BOXDYN,
    .m_UNBOXDYN = _woort_dis_UNBOXDYN,
    .m_CHECKDYN = _woort_dis_CHECKDYN,
    .m_PUSHBOXDYN = _woort_dis_PUSHBOXDYN,
    .m_ADDI = _woort_dis_ADDI,
    .m_SUBI = _woort_dis_SUBI,
    .m_MULI = _woort_dis_MULI,
    .m_DIVI = _woort_dis_DIVI,
    .m_MODI = _woort_dis_MODI,
    .m_NEGI = _woort_dis_NEGI,
    .m_LTI = _woort_dis_LTI,
    .m_GTI = _woort_dis_GTI,
    .m_LEI = _woort_dis_LEI,
    .m_GEI = _woort_dis_GEI,
    .m_EQI = _woort_dis_EQI,
    .m_NEI = _woort_dis_NEI,
    .m_ADDR = _woort_dis_ADDR,
    .m_SUBR = _woort_dis_SUBR,
    .m_MULR = _woort_dis_MULR,
    .m_DIVR = _woort_dis_DIVR,
    .m_MODR = _woort_dis_MODR,
    .m_NEGR = _woort_dis_NEGR,
    .m_LTR = _woort_dis_LTR,
    .m_GTR = _woort_dis_GTR,
    .m_LER = _woort_dis_LER,
    .m_GER = _woort_dis_GER,
    .m_EQR = _woort_dis_EQR,
    .m_NER = _woort_dis_NER,
    .m_ADDS = _woort_dis_ADDS,
    .m_LTS = _woort_dis_LTS,
    .m_GTS = _woort_dis_GTS,
    .m_LES = _woort_dis_LES,
    .m_GES = _woort_dis_GES,
    .m_EQS = _woort_dis_EQS,
    .m_NES = _woort_dis_NES,
    .m_LAND = _woort_dis_LAND,
    .m_LOR = _woort_dis_LOR,
    .m_LNOT = _woort_dis_LNOT,
    .m_CADDI = _woort_dis_CADDI,
    .m_CSUBI = _woort_dis_CSUBI,
    .m_CMULI = _woort_dis_CMULI,
    .m_CDIVI = _woort_dis_CDIVI,
    .m_CADDR = _woort_dis_CADDR,
    .m_CSUBR = _woort_dis_CSUBR,
    .m_CMULR = _woort_dis_CMULR,
    .m_CDIVR = _woort_dis_CDIVR,
    .m_CADDS = _woort_dis_CADDS,
    .m_CVADDS = _woort_dis_CVADDS,
    .m_CMODI = _woort_dis_CMODI,
    .m_CMODR = _woort_dis_CMODR,
    .m_CLAND = _woort_dis_CLAND,
    .m_CLOR = _woort_dis_CLOR,
    .m_CLNOT = _woort_dis_CLNOT,
    .m_MKPVALUE = _woort_dis_MKPVALUE,
    .m_LDIDXVEC = _woort_dis_LDIDXVEC,
    .m_LDIDXVECX = _woort_dis_LDIDXVECX,
    .m_LDIDSTRUCT = _woort_dis_LDIDSTRUCT,
    .m_LDIDSTRING = _woort_dis_LDIDSTRING,
    .m_LDIDXDICTI = _woort_dis_LDIDXDICTI,
    .m_LDIDXDICTR = _woort_dis_LDIDXDICTR,
    .m_LDIDXDICTB = _woort_dis_LDIDXDICTB,
    .m_LDIDXDICTX = _woort_dis_LDIDXDICTX,
    .m_LDIDXDICTIX = _woort_dis_LDIDXDICTIX,
    .m_LDIDXDICTRX = _woort_dis_LDIDXDICTRX,
    .m_LDIDXDICTBX = _woort_dis_LDIDXDICTBX,
    .m_LDIDXDICTXX = _woort_dis_LDIDXDICTXX,
    .m_STIDXVECI = _woort_dis_STIDXVECI,
    .m_STIDXVECR = _woort_dis_STIDXVECR,
    .m_STIDXVECB = _woort_dis_STIDXVECB,
    .m_STIDXVECX = _woort_dis_STIDXVECX,
    .m_STIDXDICTII = _woort_dis_STIDXDICTII,
    .m_STIDXDICTIR = _woort_dis_STIDXDICTIR,
    .m_STIDXDICTIB = _woort_dis_STIDXDICTIB,
    .m_STIDXDICTIX = _woort_dis_STIDXDICTIX,
    .m_STIDXDICTRI = _woort_dis_STIDXDICTRI,
    .m_STIDXDICTRR = _woort_dis_STIDXDICTRR,
    .m_STIDXDICTRB = _woort_dis_STIDXDICTRB,
    .m_STIDXDICTRX = _woort_dis_STIDXDICTRX,
    .m_STIDXDICTBI = _woort_dis_STIDXDICTBI,
    .m_STIDXDICTBR = _woort_dis_STIDXDICTBR,
    .m_STIDXDICTBB = _woort_dis_STIDXDICTBB,
    .m_STIDXDICTBX = _woort_dis_STIDXDICTBX,
    .m_STIDXDICTXI = _woort_dis_STIDXDICTXI,
    .m_STIDXDICTXR = _woort_dis_STIDXDICTXR,
    .m_STIDXDICTXB = _woort_dis_STIDXDICTXB,
    .m_STIDXDICTXX = _woort_dis_STIDXDICTXX,
    .m_STIDXMAPII = _woort_dis_STIDXMAPII,
    .m_STIDXMAPIR = _woort_dis_STIDXMAPIR,
    .m_STIDXMAPIB = _woort_dis_STIDXMAPIB,
    .m_STIDXMAPIX = _woort_dis_STIDXMAPIX,
    .m_STIDXMAPRI = _woort_dis_STIDXMAPRI,
    .m_STIDXMAPRR = _woort_dis_STIDXMAPRR,
    .m_STIDXMAPRB = _woort_dis_STIDXMAPRB,
    .m_STIDXMAPRX = _woort_dis_STIDXMAPRX,
    .m_STIDXMAPBI = _woort_dis_STIDXMAPBI,
    .m_STIDXMAPBR = _woort_dis_STIDXMAPBR,
    .m_STIDXMAPBB = _woort_dis_STIDXMAPBB,
    .m_STIDXMAPBX = _woort_dis_STIDXMAPBX,
    .m_STIDXMAPXI = _woort_dis_STIDXMAPXI,
    .m_STIDXMAPXR = _woort_dis_STIDXMAPXR,
    .m_STIDXMAPXB = _woort_dis_STIDXMAPXB,
    .m_STIDXMAPXX = _woort_dis_STIDXMAPXX,
    .m_STIDSTRUCT = _woort_dis_STIDSTRUCT,
    .m_STIDSTRUCTEXT = _woort_dis_STIDSTRUCTEXT,
    .m_UNPACKVEC = _woort_dis_UNPACKVEC,
    .m_UNPACKVECX = _woort_dis_UNPACKVECX,
    .m_UNPACKVECALL = _woort_dis_UNPACKVECALL,
    .m_UNPACKVECXALL = _woort_dis_UNPACKVECXALL,
    .m_PUSHIDXSTRUCT = _woort_dis_PUSHIDXSTRUCT,
    .m_PUSHIDXSTBOXI = _woort_dis_PUSHIDXSTBOXI,
    .m_PUSHIDXSTBOXR = _woort_dis_PUSHIDXSTBOXR,
    .m_PUSHIDXSTBOXB = _woort_dis_PUSHIDXSTBOXB,
    .m_PACKARG = _woort_dis_PACKARG,
    .m_ASTORE = _woort_dis_ASTORE,
    .m_ALOAD = _woort_dis_ALOAD,
    .m_CAS = _woort_dis_CAS,
    .m_JIFINITED = _woort_dis_JIFINITED,
    .m_DEBUGTRAP = _woort_dis_DEBUGTRAP,
    .m_PANICS = _woort_dis_PANICS,
    .m_PANICC = _woort_dis_PANICC,
    .m_CHKDIVIL = _woort_dis_CHKDIVIL,
    .m_CHKDIVIR = _woort_dis_CHKDIVIR,
    .m_CHKDIVIRZ = _woort_dis_CHKDIVIRZ,
    .m_CHKDIVILR = _woort_dis_CHKDIVILR,
};

const woort_Bytecode* woort_disassembly(
    const woort_Bytecode* c, woort_Disassembly_DumpCallback callback)
{
    struct woort_DisassemblyCtx ctx = { callback, 0 };

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
