#include "woort.h"

#include "woort_disassembly.h"
#include "woort_opcode.h"
#include "woort_opcode_dispatcher.h"

#include <stdint.h>

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

static void _woort_dis_MOV(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("MOV         [SB %+d] = [SB %+d]\n", dst, src); CTX->printed = 1; }

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

static void _woort_dis_ITOR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITOR        [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_ITOS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("ITOS        [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_RTOI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOI        [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

static void _woort_dis_RTOS(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{ CTX->callback("RTOS        [SB %+d] -> [SB %+d]\n", src, dst); CTX->printed = 1; }

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
{ CTX->callback("CALLNJIT    JIT[%u]\n", func); CTX->printed = 1; }

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

static void _woort_dis_LDIDVEC(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{ CTX->callback("LDIDVEC    [SB %+d].[SB %+d] -> [SB %+d]\n", vec, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDVECX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{ CTX->callback("LDIDVECX   [SB %+d].[SB %+d] -> [SB %+d]\n", vec, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDSTRUCT(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("LDIDSTRUCT  [SB %+d].%u -> [SB %+d]\n", obj, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDSTRING(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{ CTX->callback("LDIDSTRING  [[SB %+d].[SB %+d]] -> [SB %+d]\n", str, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTI(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTI  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTR(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTR  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTB(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTB  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTX  [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTIX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTIX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTRX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTRX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTBX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTBX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_LDIDDICTXX(void* userdata, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{ CTX->callback("LDIDDICTXX [SB %+d].[SB %+d] -> [SB %+d]\n", map, idx, dst); CTX->printed = 1; }

static void _woort_dis_STIDVECI(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDVECI   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDVECR(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDVECR   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDVECB(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDVECB   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDVECX(void* userdata, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{ CTX->callback("STIDVECX   [SB %+d].[SB %+d] = [SB %+d]\n", vec, idx, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTII(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTII [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTIR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTIR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTIB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTIB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTIX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTIX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTRI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTRI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTRR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTRR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTRB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTRB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTRX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTRX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTBI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTBI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTBR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTBR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTBB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTBB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTBX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTBX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTXI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTXI [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTXR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTXR [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTXB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTXB [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDDICTXX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDDICTXX [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPII(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPII  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPIR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPIR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPIB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPIB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPIX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPIX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPRI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPRI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPRR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPRR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPRB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPRB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPRX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPRX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPBI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPBI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPBR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPBR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPBB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPBB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPBX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPBX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPXI(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPXI  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPXR(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPXR  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPXB(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPXB  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDMAPXX(void* userdata, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{ CTX->callback("STIDMAPXX  [SB %+d].[SB %+d] = [SB %+d]\n", map, key, src); CTX->printed = 1; }

static void _woort_dis_STIDSTRUCT(void* userdata, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{ CTX->callback("STIDSTRUCT  [SB %+d].%u = [SB %+d]\n", obj, idx, src); CTX->printed = 1; }

static void _woort_dis_UNPACKVEC(void* userdata, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVEC   %u in [SB %+d]\n", n, vec); CTX->printed = 1; }

static void _woort_dis_UNPACKVECX(void* userdata, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECX  %u in [SB %+d]\n", n, vec); CTX->printed = 1; }

static void _woort_dis_UNPACKVECALL(void* userdata, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECALL %u in [SB %+d] -> [SB %+d]\n", n, vec, count_dst); CTX->printed = 1; }

static void _woort_dis_UNPACKVECXALL(void* userdata, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{ CTX->callback("UNPACKVECXALL %u in [SB %+d] -> [SB %+d]\n", n, vec, count_dst); CTX->printed = 1; }

static void _woort_dis_PUSHIDSTRUCT(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDSTRUCT [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PUSHIDSTBOXI(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDSTBOXI [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PUSHIDSTBOXR(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDSTBOXR [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

static void _woort_dis_PUSHIDSTBOXB(void* userdata, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{ CTX->callback("PUSHIDSTBOXB [SB %+d].%u\n", obj, idx); CTX->printed = 1; }

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
    .m_MOV = _woort_dis_MOV,
    .m_PUSHRCHK = _woort_dis_PUSHRCHK,
    .m_PUSHSCHK = _woort_dis_PUSHSCHK,
    .m_PUSHCCHK = _woort_dis_PUSHCCHK,
    .m_ASSURESSZ = _woort_dis_ASSURESSZ,
    .m_PUSHS = _woort_dis_PUSHS,
    .m_PUSHC = _woort_dis_PUSHC,
    .m_POPR = _woort_dis_POPR,
    .m_POPS = _woort_dis_POPS,
    .m_POPC = _woort_dis_POPC,
    .m_ITOR = _woort_dis_ITOR,
    .m_ITOS = _woort_dis_ITOS,
    .m_RTOI = _woort_dis_RTOI,
    .m_RTOS = _woort_dis_RTOS,
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
    .m_LDIDVEC = _woort_dis_LDIDVEC,
    .m_LDIDVECX = _woort_dis_LDIDVECX,
    .m_LDIDSTRUCT = _woort_dis_LDIDSTRUCT,
    .m_LDIDSTRING = _woort_dis_LDIDSTRING,
    .m_LDIDDICTI = _woort_dis_LDIDDICTI,
    .m_LDIDDICTR = _woort_dis_LDIDDICTR,
    .m_LDIDDICTB = _woort_dis_LDIDDICTB,
    .m_LDIDDICTX = _woort_dis_LDIDDICTX,
    .m_LDIDDICTIX = _woort_dis_LDIDDICTIX,
    .m_LDIDDICTRX = _woort_dis_LDIDDICTRX,
    .m_LDIDDICTBX = _woort_dis_LDIDDICTBX,
    .m_LDIDDICTXX = _woort_dis_LDIDDICTXX,
    .m_STIDVECI = _woort_dis_STIDVECI,
    .m_STIDVECR = _woort_dis_STIDVECR,
    .m_STIDVECB = _woort_dis_STIDVECB,
    .m_STIDVECX = _woort_dis_STIDVECX,
    .m_STIDDICTII = _woort_dis_STIDDICTII,
    .m_STIDDICTIR = _woort_dis_STIDDICTIR,
    .m_STIDDICTIB = _woort_dis_STIDDICTIB,
    .m_STIDDICTIX = _woort_dis_STIDDICTIX,
    .m_STIDDICTRI = _woort_dis_STIDDICTRI,
    .m_STIDDICTRR = _woort_dis_STIDDICTRR,
    .m_STIDDICTRB = _woort_dis_STIDDICTRB,
    .m_STIDDICTRX = _woort_dis_STIDDICTRX,
    .m_STIDDICTBI = _woort_dis_STIDDICTBI,
    .m_STIDDICTBR = _woort_dis_STIDDICTBR,
    .m_STIDDICTBB = _woort_dis_STIDDICTBB,
    .m_STIDDICTBX = _woort_dis_STIDDICTBX,
    .m_STIDDICTXI = _woort_dis_STIDDICTXI,
    .m_STIDDICTXR = _woort_dis_STIDDICTXR,
    .m_STIDDICTXB = _woort_dis_STIDDICTXB,
    .m_STIDDICTXX = _woort_dis_STIDDICTXX,
    .m_STIDMAPII = _woort_dis_STIDMAPII,
    .m_STIDMAPIR = _woort_dis_STIDMAPIR,
    .m_STIDMAPIB = _woort_dis_STIDMAPIB,
    .m_STIDMAPIX = _woort_dis_STIDMAPIX,
    .m_STIDMAPRI = _woort_dis_STIDMAPRI,
    .m_STIDMAPRR = _woort_dis_STIDMAPRR,
    .m_STIDMAPRB = _woort_dis_STIDMAPRB,
    .m_STIDMAPRX = _woort_dis_STIDMAPRX,
    .m_STIDMAPBI = _woort_dis_STIDMAPBI,
    .m_STIDMAPBR = _woort_dis_STIDMAPBR,
    .m_STIDMAPBB = _woort_dis_STIDMAPBB,
    .m_STIDMAPBX = _woort_dis_STIDMAPBX,
    .m_STIDMAPXI = _woort_dis_STIDMAPXI,
    .m_STIDMAPXR = _woort_dis_STIDMAPXR,
    .m_STIDMAPXB = _woort_dis_STIDMAPXB,
    .m_STIDMAPXX = _woort_dis_STIDMAPXX,
    .m_STIDSTRUCT = _woort_dis_STIDSTRUCT,
    .m_UNPACKVEC = _woort_dis_UNPACKVEC,
    .m_UNPACKVECX = _woort_dis_UNPACKVECX,
    .m_UNPACKVECALL = _woort_dis_UNPACKVECALL,
    .m_UNPACKVECXALL = _woort_dis_UNPACKVECXALL,
    .m_PUSHIDSTRUCT = _woort_dis_PUSHIDSTRUCT,
    .m_PUSHIDSTBOXI = _woort_dis_PUSHIDSTBOXI,
    .m_PUSHIDSTBOXR = _woort_dis_PUSHIDSTBOXR,
    .m_PUSHIDSTBOXB = _woort_dis_PUSHIDSTBOXB,
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

WOORT_NODISCARD const woort_Bytecode* woort_disassembly(
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
