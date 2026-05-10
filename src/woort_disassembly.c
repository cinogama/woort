#include "woort_disassembly.h"

#include "woort_opcode.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

const woort_Bytecode* woort_disassembly(
    const woort_Bytecode* c, woort_Disassembly_DumpCallback callback)
{
    woort_Bytecode bc = c[0];

label_reentry_for_debug_trap:;
    const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
    const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

    switch (op6)
    {
    case WOORT_OPCODE_NOP:
    {
        callback("NOP\n");
        return c + 1;
    }

    case WOORT_OPCODE_LOAD:
    {
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        const uint32_t src = WOORT_BYTECODE(MAB18, bc);
        callback("LOAD        [SB %+d] = G[%u]\n", dst, src);
        return c + 1;
    }

    case WOORT_OPCODE_STORE:
    {
        const int8_t src = (int8_t)WOORT_BYTECODE(C8, bc);
        const uint32_t dst = WOORT_BYTECODE(MAB18, bc);
        callback("STORE       G[%u] = [SB %+d]\n", dst, src);
        return c + 1;
    }

    case WOORT_OPCODE_LOADEX:
    {
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t src = (uint32_t)c[1];
        callback("LOADEX      [SB %+d] = G[%u]\n", dst, src);
        return c + 2;
    }

    case WOORT_OPCODE_STOREEX:
    {
        const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t dst = (uint32_t)c[1];
        callback("STOREEX     G[%u] = [SB %+d]\n", dst, src);
        return c + 2;
    }

    case WOORT_OPCODE_MOV:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("MOVLD       [SB %+d] = [SB %+d]\n", dst, src);
            return c + 1;
        }
        case 1:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("MOVST       [SB %+d] = [SB %+d]\n", dst, src);
            return c + 1;
        }
        case 2:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            const int32_t src = (int32_t)c[1];
            callback("MOVLDEXT    [SB %+d] = [SB %+d]\n", dst, src);
            return c + 2;
        }
        case 3:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            const int32_t dst = (int32_t)c[1];
            callback("MOVSTEXT    [SB %+d] = [SB %+d]\n", dst, src);
            return c + 2;
        }
        }
        break;
    }

    case WOORT_OPCODE_PUSHCHK:
    {
        switch (m2)
        {
        case 0:
        {
            const uint32_t n = WOORT_BYTECODE(ABC24, bc);
            callback("PUSHRCHK    %u\n", n);
            return c + 1;
        }
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("PUSHSCHK    [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            callback("PUSHCCHK    G[%u]\n", src);
            return c + 1;
        }
        case 3:
        {
            const uint32_t src = (uint32_t)c[1];
            callback("PUSHCCHKEXT G[%u]\n", src);
            return c + 2;
        }
        }
        break;
    }

    case WOORT_OPCODE_PUSH:
    {
        switch (m2)
        {
        case 0:
        {
            const uint32_t n = WOORT_BYTECODE(ABC24, bc);
            callback("ASSURESSZ   %u\n", n);
            return c + 1;
        }
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("PUSHS       [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            callback("PUSHC       G[%u]\n", src);
            return c + 1;
        }
        case 3:
        {
            const uint32_t src = (uint32_t)c[1];
            callback("PUSHCEXT    G[%u]\n", src);
            return c + 2;
        }
        }
        break;
    }

    case WOORT_OPCODE_POP:
    {
        switch (m2)
        {
        case 0:
        {
            const uint32_t n = WOORT_BYTECODE(ABC24, bc);
            callback("POPR        %u\n", n);
            return c + 1;
        }
        case 1:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("POPS        [SB %+d]\n", dst);
            return c + 1;
        }
        case 2:
        {
            const uint32_t dst = WOORT_BYTECODE(ABC24, bc);
            callback("POPC        G[%u]\n", dst);
            return c + 1;
        }
        case 3:
        {
            const uint32_t dst = (uint32_t)c[1];
            callback("POPCEXT     G[%u]\n", dst);
            return c + 2;
        }
        }
        break;
    }

    case WOORT_OPCODE_CASTI:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("ITORST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("ITORLD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("ITOSST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("ITOSLD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_CASTR:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("RTOIST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("RTOILD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("RTOSST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("RTOSLD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_CASTX:
    {
        switch (m2)
        {
        case 0:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("CASTSTO     T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 1:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("CASTSFROM   T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 2:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("CASTDYN     T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 3:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("ASSERTDYN   T%u, [SB %+d]\n", t, src);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_CALLNWO:
    {
        const uint32_t target = WOORT_BYTECODE(MABC26, bc);
        callback("CALLNWO     G[%u]\n", target);
        return c + 1;
    }

    case WOORT_OPCODE_CALLNFP:
    {
        const uint32_t target = WOORT_BYTECODE(MABC26, bc);
        callback("CALLNFP     G[%u]\n", target);
        return c + 1;
    }

    case WOORT_OPCODE_CALLNJIT:
    {
        const uint32_t target = WOORT_BYTECODE(MABC26, bc);
        callback("CALLNJIT    G[%u]\n", target);
        return c + 1;
    }

    case WOORT_OPCODE_CALL:
    {
        switch (m2)
        {
        case 0:
        {
            const int16_t target = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("CALLS       [SB %+d]\n", target);
            return c + 1;
        }
        case 1:
        {
            const uint32_t target = WOORT_BYTECODE(ABC24, bc);
            callback("CALLC       G[%u]\n", target);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_RET:
    {
        switch (m2)
        {
        case 0:
            callback("RET\n");
            return c + 1;
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("RETVS       [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            callback("RETVC       G[%u]\n", src);
            return c + 1;
        }
        case 3:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("POPRS       [SB %+d]\n", src);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_RESULT:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        callback("RESULT      [SB %+d], POP %u\n", dst, n);
        return c + 1;
    }

    case WOORT_OPCODE_JFWD:
    {
        const uint32_t addr = WOORT_BYTECODE(MABC26, bc);
        callback("JFWD        %u\n", addr);
        return c + 1;
    }

    case WOORT_OPCODE_JBCK:
    {
        const uint32_t addr = WOORT_BYTECODE(MABC26, bc);
        callback("JBCK        %u\n", addr);
        return c + 1;
    }

    case WOORT_OPCODE_JFWDCND:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const uint16_t offset = (uint16_t)WOORT_BYTECODE(BC16, bc);
            callback("JFWDNZ      +%u IF [SB %+d] != 0\n", offset, a);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const uint16_t offset = (uint16_t)WOORT_BYTECODE(BC16, bc);
            callback("JFWDZ       +%u IF [SB %+d] == 0\n", offset, a);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            callback("JFWDEQ      +%u IF [SB %+d] == [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            callback("JFWDNEQ     +%u IF [SB %+d] != [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_JBCKCND:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const uint16_t offset = (uint16_t)WOORT_BYTECODE(BC16, bc);
            callback("JBCKNZ      -%u IF [SB %+d] != 0\n", offset, a);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const uint16_t offset = (uint16_t)WOORT_BYTECODE(BC16, bc);
            callback("JBCKZ       -%u IF [SB %+d] == 0\n", offset, a);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            callback("JBCKEQ      -%u IF [SB %+d] == [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            callback("JBCKNEQ     -%u IF [SB %+d] != [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_JFDCMP:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("JFWDLT      +%u IF [SB %+d] < [SB %+d]\n", offset, a, b);
            return c + 1;
        case 1:
            callback("JFWDGT      +%u IF [SB %+d] > [SB %+d]\n", offset, a, b);
            return c + 1;
        case 2:
            callback("JFWDEL      +%u IF [SB %+d] <= [SB %+d]\n", offset, a, b);
            return c + 1;
        case 3:
            callback("JFWDEG      +%u IF [SB %+d] >= [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_JBCKCMP:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("JBCKLT      -%u IF [SB %+d] < [SB %+d]\n", offset, a, b);
            return c + 1;
        case 1:
            callback("JBCKGT      -%u IF [SB %+d] > [SB %+d]\n", offset, a, b);
            return c + 1;
        case 2:
            callback("JBCKEL      -%u IF [SB %+d] <= [SB %+d]\n", offset, a, b);
            return c + 1;
        case 3:
            callback("JBCKEG      -%u IF [SB %+d] >= [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_CONS:
    {
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
        {
            const uint8_t n = (uint8_t)WOORT_BYTECODE(A8, bc);
            callback("MKVEC       %u -> [SB %+d]\n", n, dst);
            return c + 1;
        }
        case 1:
        {
            const uint8_t n = (uint8_t)WOORT_BYTECODE(A8, bc);
            callback("MKMAP       %u -> [SB %+d]\n", n, dst);
            return c + 1;
        }
        case 2:
        {
            const uint8_t n = (uint8_t)WOORT_BYTECODE(A8, bc);
            callback("MKSTRUCT    %u -> [SB %+d]\n", n, dst);
            return c + 1;
        }
        case 3:
        {
            const uint8_t idx = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst2 = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("MKUNION     %u, [SB %+d] -> [SB %+d]\n", idx, src, dst2);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_CONSEX:
    {
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t n = (uint32_t)c[1];
        switch (m2)
        {
        case 0:
            callback("MKVECEXT    %u -> [SB %+d]\n", n, dst);
            return c + 2;
        case 1:
            callback("MKMAPEXT    %u -> [SB %+d]\n", n, dst);
            return c + 2;
        case 2:
            callback("MKSTRUCTEXT %u -> [SB %+d]\n", n, dst);
            return c + 2;
        case 3:
            callback("MKUNIONEXT  %u, [SB %+d] -> [SB %+d]\n", n, (int8_t)WOORT_BYTECODE(A8, bc), dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_MKCLOSURE:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t func = (uint32_t)c[1];
        callback("MKCLOSURE   %u -> [SB %+d], G[%u]\n", n, dst, func);
        return c + 2;
    }

    case WOORT_OPCODE_DYN:
    {
        switch (m2)
        {
        case 0:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("BOXDYN      T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 1:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("UNBOXDYN    T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 2:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("CHECKDYN    T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 3:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("PUSHBOXDYN  T%u, [SB %+d]\n", t, src);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_OPIASMD:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("ADDI        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("SUBI        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("MULI        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("DIVI        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPIONLG:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("MODI        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("NEGI        -[SB %+d] -> [SB %+d]\n", a, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("LTI         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("GTI         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_OPISREN:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("LEI         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("GEI         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("EQI         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("NEI         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPRASMD:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("ADDR        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("SUBR        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("MULR        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("DIVR        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPRONLG:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("MODR        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("NEGR        -[SB %+d] -> [SB %+d]\n", a, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("LTR         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("GTR         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_OPRSREN:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("LER         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("GER         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("EQR         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("NER         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPSALGS:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("ADDS        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("LTS         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("GTS         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("LES         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPSREN:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("GES         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("EQS         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("NES         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPLAONI:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("LAND        [SB %+d] && [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            callback("LOR         [SB %+d] || [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("LNOT        ![SB %+d] -> [SB %+d]\n", a, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_OPCIASMD:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
            callback("CADDI       [SB %+d] += [SB %+d]\n", dst, a);
            return c + 1;
        case 1:
            callback("CSUBI       [SB %+d] -= [SB %+d]\n", dst, a);
            return c + 1;
        case 2:
            callback("CMULI       [SB %+d] *= [SB %+d]\n", dst, a);
            return c + 1;
        case 3:
            callback("CDIVI       [SB %+d] /= [SB %+d]\n", dst, a);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPCRASMD:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
            callback("CADDR       [SB %+d] += [SB %+d]\n", dst, a);
            return c + 1;
        case 1:
            callback("CSUBR       [SB %+d] -= [SB %+d]\n", dst, a);
            return c + 1;
        case 2:
            callback("CMULR       [SB %+d] *= [SB %+d]\n", dst, a);
            return c + 1;
        case 3:
            callback("CDIVR       [SB %+d] /= [SB %+d]\n", dst, a);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPCSAIOO:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
            callback("CADDS       [SB %+d] += [SB %+d]\n", dst, a);
            return c + 1;
        case 1:
            callback("CVADDS      [SB %+d] = [SB %+d] + [SB %+d]\n", dst, a, dst);
            return c + 1;
        case 2:
            callback("CMODI       [SB %+d] %%= [SB %+d]\n", dst, a);
            return c + 1;
        case 3:
            callback("CMODR       [SB %+d] %%= [SB %+d]\n", dst, a);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_OPCLAON:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("CLAND       [SB %+d] &&= [SB %+d]\n", dst, a);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("CLOR        [SB %+d] ||= [SB %+d]\n", dst, a);
            return c + 1;
        }
        case 2:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("CLNOT       ![SB %+d]\n", dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_LDIDX:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("LDIDXVEC    [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("LDIDXVECX   [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("LDIDSTRUCT  [SB %+d].%u -> [SB %+d]\n", b, (uint8_t)a, dst);
            return c + 1;
        case 3:
            callback("LDIDSTRING  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_LDIDXDICT:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("LDIDXDICTI  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("LDIDXDICTR  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("LDIDXDICTB  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("LDIDXDICTX  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_LDIDXDICTX:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("LDIDXDICTIX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            callback("LDIDXDICTRX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            callback("LDIDXDICTBX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            callback("LDIDXDICTXX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_LDIDXEX:
    {
        const int16_t a = (int16_t)WOORT_BYTECODE(BC16, bc);
        const int16_t b = (int16_t)(c[1] >> 16);
        const int16_t dst = (int16_t)(c[1] & 0xFFFF);
        switch (m2)
        {
        case 0:
            callback("LDIDXVECEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            callback("LDIDXVECXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            callback("LDIDSTRUCTEXT [SB %+d].%u -> [SB %+d]\n", b, (uint32_t)a, dst);
            return c + 2;
        case 3:
            callback("LDIDSTRINGEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_LDIDXDICTEX:
    {
        const int16_t a = (int16_t)WOORT_BYTECODE(BC16, bc);
        const int16_t b = (int16_t)(c[1] >> 16);
        const int16_t dst = (int16_t)(c[1] & 0xFFFF);
        switch (m2)
        {
        case 0:
            callback("LDIDXDICTIEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            callback("LDIDXDICTREXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            callback("LDIDXDICTBEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 3:
            callback("LDIDXDICTXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_LDIDXDICTEXX:
    {
        const int16_t a = (int16_t)WOORT_BYTECODE(BC16, bc);
        const int16_t b = (int16_t)(c[1] >> 16);
        const int16_t dst = (int16_t)(c[1] & 0xFFFF);
        switch (m2)
        {
        case 0:
            callback("LDIDXDICTIXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            callback("LDIDXDICTRXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            callback("LDIDXDICTBXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 3:
            callback("LDIDXDICTXXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_STIDXVEC:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXVECI   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXVECR   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXVECB   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXVECX   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXDICTI:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXDICTII [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXDICTIR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXDICTIB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXDICTIX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXDICTR:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXDICTRI [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXDICTRR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXDICTRB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXDICTRX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXDICTB:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXDICTBI [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXDICTBR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXDICTBB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXDICTBX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXDICTX:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXDICTXI [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXDICTXR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXDICTXB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXDICTXX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXMAPI:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXMAPII  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXMAPIR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXMAPIB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXMAPIX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXMAPR:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXMAPRI  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXMAPRR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXMAPRB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXMAPRX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXMAPB:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXMAPBI  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXMAPBR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXMAPBB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXMAPBX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDXMAPX:
    {
        const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t c8 = (int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0:
            callback("STIDXMAPXI  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            callback("STIDXMAPXR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            callback("STIDXMAPXB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            callback("STIDXMAPXX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDSTRUCT:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int8_t a = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(C8, bc);
        callback("STIDSTRUCT  [SB %+d].%u = [SB %+d]\n", a, n, b);
        return c + 1;
    }

    case WOORT_OPCODE_STIDXEX:
    {
        const int16_t a = (int16_t)WOORT_BYTECODE(BC16, bc);
        const int16_t b = (int16_t)(c[1] >> 16);
        const int16_t dst = (int16_t)(c[1] & 0xFFFF);
        switch (m2)
        {
        case 0:
            callback("STIDXVECEXT [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            callback("STIDXDICTEXT [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            callback("STIDXMAPEXT [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, dst);
            return c + 2;
        case 3:
            callback("STIDSTRUCTEXT [SB %+d].%u = [SB %+d]\n", b, (uint32_t)a, dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_UNPACK:
    {
        switch (m2)
        {
        case 0:
            callback("UNPACKVEC   %u in [SB %+d]\n",
                (uint8_t)WOORT_BYTECODE(A8, bc),
                (int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1:
            callback("UNPACKVECX  %u in [SB %+d]\n",
                (uint8_t)WOORT_BYTECODE(A8, bc),
                (int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2:
            callback("UNPACKVECALL %u in [SB %+d] -> [SB %+d]\n",
                (uint8_t)WOORT_BYTECODE(A8, bc),
                (int8_t)WOORT_BYTECODE(B8, bc),
                (int8_t)WOORT_BYTECODE(C8, bc));
            return c + 1;
        case 3:
            callback("UNPACKVECXALL %u in [SB %+d] -> [SB %+d]\n",
                (uint8_t)WOORT_BYTECODE(A8, bc),
                (int8_t)WOORT_BYTECODE(B8, bc),
                (int8_t)WOORT_BYTECODE(C8, bc));
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_PUSHIDXSTBOX:
    {
        const uint16_t n = (uint16_t)WOORT_BYTECODE(A8, bc);
        const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
            callback("PUSHIDXSTRUCT [SB %+d].%u\n", src, n);
            return c + 1;
        case 1:
            callback("PUSHIDXSTBOXI [SB %+d].%u\n", src, n);
            return c + 1;
        case 2:
            callback("PUSHIDXSTBOXR [SB %+d].%u\n", src, n);
            return c + 1;
        case 3:
            callback("PUSHIDXSTBOXB [SB %+d].%u\n", src, n);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_PACKARG:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        callback("PACKARG     %u -> [SB %+d]\n", n, dst);
        return c + 1;
    }

    case WOORT_OPCODE_ATOMIC:
    {
        switch (m2)
        {
        case 0:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            const uint32_t dst = (uint32_t)c[1];
            callback("ASTORE      G[%u] = [SB %+d]\n", dst, src);
            return c + 2;
        }
        case 1:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            const uint32_t src = (uint32_t)c[1];
            callback("ALOAD       [SB %+d] = G[%u]\n", dst, src);
            return c + 2;
        }
        case 2:
        {
            const int8_t desired = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t expected = (int16_t)WOORT_BYTECODE(BC16, bc);
            const uint32_t addr = (uint32_t)c[1];
            callback("CAS         DESIRED=[SB %+d] EXPECTED=[SB %+d], G[%u]\n", desired, expected, addr);
            return c + 2;
        }
        }
        break;
    }

    case WOORT_OPCODE_JIFINITED:
    {
        const uint32_t addr = WOORT_BYTECODE(MABC26, bc);
        const uint32_t c32 = (uint32_t)c[1];
        callback("JIFINITED   %u, IF ALOAD G[%u] != 0\n", addr, c32);
        return c + 2;
    }

    case WOORT_OPCODE_TRAP:
    {
        switch (m2)
        {
        case 0:
        {
            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(c, &cenv))
            {
                bc = woort_CodeEnv_raw_trap(cenv, c);
                goto label_reentry_for_debug_trap;
            }
            else
            {
                callback("DEBUGTRAP\n");
                return c + 1;
            }
        }
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            callback("PANICS      [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            callback("PANICC      G[%u]\n", src);
            return c + 1;
        }
        }
        break;
    }

    default:
        break;
    }

    callback("UNKNOWN_OPCODE(%u)\n", op6);
    return c + 1;
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
