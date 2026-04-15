#include "woort_disassembly.h"

#include "woort_opcode.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

const woort_Bytecode* woort_disassembly(const woort_Bytecode* c)
{
    const woort_Bytecode bc = c[0];
    const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
    const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

    switch (op6)
    {
    case WOORT_OPCODE_NOP:
    {
        printf("NOP\n");
        return c + 1;
    }

    case WOORT_OPCODE_LOAD:
    {
        const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
        const uint32_t src = WOORT_BYTECODE(MAB18, bc);
        printf("LOAD        [SB %+d] = G[%u]\n", dst, src);
        return c + 1;
    }

    case WOORT_OPCODE_STORE:
    {
        const int8_t src = (int8_t)WOORT_BYTECODE(C8, bc);
        const uint32_t dst = WOORT_BYTECODE(MAB18, bc);
        printf("STORE       G[%u] = [SB %+d]\n", dst, src);
        return c + 1;
    }

    case WOORT_OPCODE_LOADEX:
    {
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t src = (uint32_t)c[1];
        printf("LOADEX      [SB %+d] = G[%u]\n", dst, src);
        return c + 2;
    }

    case WOORT_OPCODE_STOREEX:
    {
        const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t dst = (uint32_t)c[1];
        printf("STOREEX     G[%u] = [SB %+d]\n", dst, src);
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
            printf("MOVLD       [SB %+d] = [SB %+d]\n", dst, src);
            return c + 1;
        }
        case 1:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("MOVST       [SB %+d] = [SB %+d]\n", dst, src);
            return c + 1;
        }
        case 2:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            const int32_t src = (int32_t)c[1];
            printf("MOVLDEXT    [SB %+d] = [SB %+d]\n", dst, src);
            return c + 2;
        }
        case 3:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            const int32_t dst = (int32_t)c[1];
            printf("MOVSTEXT    [SB %+d] = [SB %+d]\n", dst, src);
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
            printf("PUSHRCHK    %u\n", n);
            return c + 1;
        }
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("PUSHSCHK    [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            printf("PUSHCCHK    G[%u]\n", src);
            return c + 1;
        }
        case 3:
        {
            const uint32_t src = (uint32_t)c[1];
            printf("PUSHCCHKEXT G[%u]\n", src);
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
            printf("ASSURESSZ   %u\n", n);
            return c + 1;
        }
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("PUSHS       [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            printf("PUSHC       G[%u]\n", src);
            return c + 1;
        }
        case 3:
        {
            const uint32_t src = (uint32_t)c[1];
            printf("PUSHCEXT    G[%u]\n", src);
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
            printf("POPR        %u\n", n);
            return c + 1;
        }
        case 1:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("POPS        [SB %+d]\n", dst);
            return c + 1;
        }
        case 2:
        {
            const uint32_t dst = WOORT_BYTECODE(ABC24, bc);
            printf("POPC        G[%u]\n", dst);
            return c + 1;
        }
        case 3:
        {
            const uint32_t dst = (uint32_t)c[1];
            printf("POPCEXT     G[%u]\n", dst);
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
            printf("ITORST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("ITORLD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("ITOSST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("ITOSLD      [SB %+d] -> [SB %+d]\n", src, dst);
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
            printf("RTOIST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("RTOILD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("RTOSST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("RTOSLD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_CASTS:
    {
        switch (m2)
        {
        case 0:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("STOIST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("STOILD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t src = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("STORST      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t dst = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("STORLD      [SB %+d] -> [SB %+d]\n", src, dst);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_CALLNWO:
    {
        const uint32_t target = WOORT_BYTECODE(MABC26, bc);
        printf("CALLNWO     G[%u]\n", target);
        return c + 1;
    }

    case WOORT_OPCODE_CALLNFP:
    {
        const uint32_t target = WOORT_BYTECODE(MABC26, bc);
        printf("CALLNFP     G[%u]\n", target);
        return c + 1;
    }

    case WOORT_OPCODE_CALLNJIT:
    {
        const uint32_t target = WOORT_BYTECODE(MABC26, bc);
        printf("CALLNJIT    G[%u]\n", target);
        return c + 1;
    }

    case WOORT_OPCODE_CALL:
    {
        switch (m2)
        {
        case 0:
        {
            const int16_t target = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("CALLS       [SB %+d]\n", target);
            return c + 1;
        }
        case 1:
        {
            const uint32_t target = WOORT_BYTECODE(ABC24, bc);
            printf("CALLC       G[%u]\n", target);
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
            printf("RET\n");
            return c + 1;
        case 1:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("RETVS       [SB %+d]\n", src);
            return c + 1;
        }
        case 2:
        {
            const uint32_t src = WOORT_BYTECODE(ABC24, bc);
            printf("RETVC       G[%u]\n", src);
            return c + 1;
        }
        case 3:
        {
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("POPRS       [SB %+d]\n", src);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_RESULT:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        printf("RESULT      [SB %+d], POP %u\n", dst, n);
        return c + 1;
    }

    case WOORT_OPCODE_JFWD:
    {
        const uint32_t addr = WOORT_BYTECODE(MABC26, bc);
        printf("JFWD        %u\n", addr);
        return c + 1;
    }

    case WOORT_OPCODE_JBCK:
    {
        const uint32_t addr = WOORT_BYTECODE(MABC26, bc);
        printf("JBCK        %u\n", addr);
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
            printf("JFWDNZ      +%u IF [SB %+d] != 0\n", offset, a);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const uint16_t offset = (uint16_t)WOORT_BYTECODE(BC16, bc);
            printf("JFWDZ       +%u IF [SB %+d] == 0\n", offset, a);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            printf("JFWDEQ      +%u IF [SB %+d] == [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            printf("JFWDNEQ     +%u IF [SB %+d] != [SB %+d]\n", offset, a, b);
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
            printf("JBCKNZ      -%u IF [SB %+d] != 0\n", offset, a);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const uint16_t offset = (uint16_t)WOORT_BYTECODE(BC16, bc);
            printf("JBCKZ       -%u IF [SB %+d] == 0\n", offset, a);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            printf("JBCKEQ      -%u IF [SB %+d] == [SB %+d]\n", offset, a, b);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const uint8_t offset = (uint8_t)WOORT_BYTECODE(C8, bc);
            printf("JBCKNEQ     -%u IF [SB %+d] != [SB %+d]\n", offset, a, b);
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
            printf("JFWDLT      +%u IF [SB %+d] < [SB %+d]\n", offset, a, b);
            return c + 1;
        case 1:
            printf("JFWDGT      +%u IF [SB %+d] > [SB %+d]\n", offset, a, b);
            return c + 1;
        case 2:
            printf("JFWDEL      +%u IF [SB %+d] <= [SB %+d]\n", offset, a, b);
            return c + 1;
        case 3:
            printf("JFWDEG      +%u IF [SB %+d] >= [SB %+d]\n", offset, a, b);
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
            printf("JBCKLT      -%u IF [SB %+d] < [SB %+d]\n", offset, a, b);
            return c + 1;
        case 1:
            printf("JBCKGT      -%u IF [SB %+d] > [SB %+d]\n", offset, a, b);
            return c + 1;
        case 2:
            printf("JBCKEL      -%u IF [SB %+d] <= [SB %+d]\n", offset, a, b);
            return c + 1;
        case 3:
            printf("JBCKEG      -%u IF [SB %+d] >= [SB %+d]\n", offset, a, b);
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
            printf("MKVEC       %u -> [SB %+d]\n", n, dst);
            return c + 1;
        }
        case 1:
        {
            const uint8_t n = (uint8_t)WOORT_BYTECODE(A8, bc);
            printf("MKMAP       %u -> [SB %+d]\n", n, dst);
            return c + 1;
        }
        case 2:
        {
            const uint8_t n = (uint8_t)WOORT_BYTECODE(A8, bc);
            printf("MKSTRUCT    %u -> [SB %+d]\n", n, dst);
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
            printf("MKVECEXT    %u -> [SB %+d]\n", n, dst);
            return c + 2;
        case 1:
            printf("MKMAPEXT    %u -> [SB %+d]\n", n, dst);
            return c + 2;
        case 2:
            printf("MKSTRUCTEXT %u -> [SB %+d]\n", n, dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_MKCLOSURE:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        const uint32_t func = (uint32_t)c[1];
        printf("MKCLOSURE   %u -> [SB %+d], G[%u]\n", n, dst, func);
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
            printf("BOXDYN      T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 1:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("UNBOXDYN    T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 2:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int8_t src = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("CHECKDYN    T%u, [SB %+d] -> [SB %+d]\n", t, src, dst);
            return c + 1;
        }
        case 3:
        {
            const uint8_t t = (uint8_t)WOORT_BYTECODE(A8, bc);
            const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("PUSHBOXDYN  T%u, [SB %+d]\n", t, src);
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
            printf("ADDI        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("SUBI        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("MULI        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("DIVI        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("MODI        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("NEGI        -[SB %+d] -> [SB %+d]\n", a, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("LTI         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("GTI         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("LEI         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("GEI         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("EQI         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("NEI         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("ADDR        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("SUBR        [SB %+d] - [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("MULR        [SB %+d] * [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("DIVR        [SB %+d] / [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("MODR        [SB %+d] %% [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("NEGR        -[SB %+d] -> [SB %+d]\n", a, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("LTR         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 3:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("GTR         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("LER         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("GER         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("EQR         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("NER         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("ADDS        [SB %+d] + [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("LTS         [SB %+d] < [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("GTS         [SB %+d] > [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("LES         [SB %+d] <= [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("GES         [SB %+d] >= [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("EQS         [SB %+d] == [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("NES         [SB %+d] != [SB %+d] -> [SB %+d]\n", a, b, dst);
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
            printf("LAND        [SB %+d] && [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int8_t b = (int8_t)WOORT_BYTECODE(B8, bc);
            const int8_t dst = (int8_t)WOORT_BYTECODE(C8, bc);
            printf("LOR         [SB %+d] || [SB %+d] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        }
        case 2:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("LNOT        ![SB %+d] -> [SB %+d]\n", a, dst);
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
            printf("CADDI       [SB %+d] += [SB %+d]\n", dst, a);
            return c + 1;
        case 1:
            printf("CSUBI       [SB %+d] -= [SB %+d]\n", dst, a);
            return c + 1;
        case 2:
            printf("CMULI       [SB %+d] *= [SB %+d]\n", dst, a);
            return c + 1;
        case 3:
            printf("CDIVI       [SB %+d] /= [SB %+d]\n", dst, a);
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
            printf("CADDR       [SB %+d] += [SB %+d]\n", dst, a);
            return c + 1;
        case 1:
            printf("CSUBR       [SB %+d] -= [SB %+d]\n", dst, a);
            return c + 1;
        case 2:
            printf("CMULR       [SB %+d] *= [SB %+d]\n", dst, a);
            return c + 1;
        case 3:
            printf("CDIVR       [SB %+d] /= [SB %+d]\n", dst, a);
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
            printf("CADDS       [SB %+d] += [SB %+d]\n", dst, a);
            return c + 1;
        case 1:
            printf("CVADDS      [SB %+d] = [SB %+d] + [SB %+d]\n", dst, a, dst);
            return c + 1;
        case 2:
            printf("CMODI       [SB %+d] %%= [SB %+d]\n", dst, a);
            return c + 1;
        case 3:
            printf("CMODR       [SB %+d] %%= [SB %+d]\n", dst, a);
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
            printf("CLAND       [SB %+d] &&= [SB %+d]\n", dst, a);
            return c + 1;
        }
        case 1:
        {
            const int8_t a = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("CLOR        [SB %+d] ||= [SB %+d]\n", dst, a);
            return c + 1;
        }
        case 2:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            printf("CLNOT       ![SB %+d]\n", dst);
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
            printf("LDIDXVEC    [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("LDIDXVECX   [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("LDIDSTRUCT  [SB %+d].%u -> [SB %+d]\n", b, (uint8_t)a, dst);
            return c + 1;
        case 3:
            printf("LDIDSTRING  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
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
            printf("LDIDXDICTI  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("LDIDXDICTR  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("LDIDXDICTB  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("LDIDXDICTX  [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
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
            printf("LDIDXDICTIX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 1:
            printf("LDIDXDICTRX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 2:
            printf("LDIDXDICTBX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 1;
        case 3:
            printf("LDIDXDICTXX [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
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
            printf("LDIDXVECEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            printf("LDIDXVECXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            printf("LDIDSTRUCTEXT [SB %+d].%u -> [SB %+d]\n", b, (uint32_t)a, dst);
            return c + 2;
        case 3:
            printf("LDIDSTRINGEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
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
            printf("LDIDXDICTIEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            printf("LDIDXDICTREXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            printf("LDIDXDICTBEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 3:
            printf("LDIDXDICTXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
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
            printf("LDIDXDICTIXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            printf("LDIDXDICTRXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            printf("LDIDXDICTBXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
            return c + 2;
        case 3:
            printf("LDIDXDICTXXEXT [[SB %+d] + [SB %+d]] -> [SB %+d]\n", a, b, dst);
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
            printf("STIDXVECI   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXVECR   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXVECB   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXVECX   [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXDICTII [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXDICTIR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXDICTIB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXDICTIX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXDICTRI [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXDICTRR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXDICTRB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXDICTRX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXDICTBI [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXDICTBR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXDICTBB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXDICTBX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXDICTXI [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXDICTXR [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXDICTXB [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXDICTXX [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXMAPII  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXMAPIR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXMAPIB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXMAPIX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXMAPRI  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXMAPRR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXMAPRB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXMAPRX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXMAPBI  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXMAPBR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXMAPBB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXMAPBX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
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
            printf("STIDXMAPXI  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 1:
            printf("STIDXMAPXR  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 2:
            printf("STIDXMAPXB  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        case 3:
            printf("STIDXMAPXX  [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, c8);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_STIDSTRUCT:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int8_t a = (int8_t)WOORT_BYTECODE(B8, bc);
        const int8_t b = (int8_t)WOORT_BYTECODE(C8, bc);
        printf("STIDSTRUCT  [SB %+d].%u = [SB %+d]\n", a, n, b);
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
            printf("STIDXVECEXT [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, dst);
            return c + 2;
        case 1:
            printf("STIDXDICTEXT [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, dst);
            return c + 2;
        case 2:
            printf("STIDXMAPEXT [[SB %+d] + [SB %+d]] = [SB %+d]\n", a, b, dst);
            return c + 2;
        case 3:
            printf("STIDSTRUCTEXT [SB %+d].%u = [SB %+d]\n", b, (uint32_t)a, dst);
            return c + 2;
        }
        break;
    }

    case WOORT_OPCODE_UNPACK:
    {
        const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
            printf("UNPACKSTRUCT [SB %+d]\n", src);
            return c + 1;
        case 1:
            printf("UNPACKVEC    [SB %+d]\n", src);
            return c + 1;
        case 2:
            printf("UNPACKVECX   [SB %+d]\n", src);
            return c + 1;
        case 3:
        {
            const uint8_t n = (uint8_t)WOORT_BYTECODE(A8, bc);
            printf("PUSHIDXSTRUCT %u, [SB %+d]\n", n, src);
            return c + 1;
        }
        }
        break;
    }

    case WOORT_OPCODE_PUSHIDXSTBOX:
    {
        const uint16_t ma = (uint16_t)WOORT_BYTECODE(MA10, bc);
        const uint8_t n = (uint8_t)(ma >> 8);
        const int16_t src = (int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0:
            printf("PUSHIDXSTBOXI %u, [SB %+d]\n", n, src);
            return c + 1;
        case 1:
            printf("PUSHIDXSTBOXR %u, [SB %+d]\n", n, src);
            return c + 1;
        case 2:
            printf("PUSHIDXSTBOXB %u, [SB %+d]\n", n, src);
            return c + 1;
        case 3:
            printf("PUSHIDXSTBOXX %u, [SB %+d]\n", n, src);
            return c + 1;
        }
        break;
    }

    case WOORT_OPCODE_PACKARG:
    {
        const uint32_t n = WOORT_BYTECODE(MA10, bc);
        const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
        printf("PACKARG     %u -> [SB %+d]\n", n, dst);
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
            printf("ASTORE      G[%u] = [SB %+d]\n", dst, src);
            return c + 2;
        }
        case 1:
        {
            const int16_t dst = (int16_t)WOORT_BYTECODE(BC16, bc);
            const uint32_t src = (uint32_t)c[1];
            printf("ALOAD       [SB %+d] = G[%u]\n", dst, src);
            return c + 2;
        }
        case 2:
        {
            const int8_t desired = (int8_t)WOORT_BYTECODE(A8, bc);
            const int16_t expected = (int16_t)WOORT_BYTECODE(BC16, bc);
            const uint32_t addr = (uint32_t)c[1];
            printf("CAS         DESIRED=[SB %+d] EXPECTED=[SB %+d], G[%u]\n", desired, expected, addr);
            return c + 2;
        }
        }
        break;
    }

    case WOORT_OPCODE_JIFINITED:
    {
        const uint32_t addr = WOORT_BYTECODE(MABC26, bc);
        const uint32_t c32 = (uint32_t)c[1];
        printf("JIFINITED   %u, IF ALOAD G[%u] != 0\n", addr, c32);
        return c + 2;
    }

    default:
        break;
    }

    printf("UNKNOWN_OPCODE(%u)\n", op6);
    return c + 1;
}

void woort_dump_codes(const woort_CodeEnv* code_env)
{
    const woort_Bytecode* pc = code_env->m_code_begin;

    while (pc < code_env->m_code_end)
        pc = woort_disassembly(pc);

    printf("\n");
}
