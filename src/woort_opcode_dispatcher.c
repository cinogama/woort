#include "woort_opcode_dispatcher.h"

#include "woort_opcode.h"
#include "woort_opcode_formal.h"
#include "woort_opcode_builder.h"
#include "woort_codeenv.h"

#define DISPATCH(FIELD, ...)                    \
    do                                          \
    {                                           \
        if (d != NULL)                          \
            d->FIELD(userdata,##__VA_ARGS__);   \
    }while(0)       

WOORT_NODISCARD const woort_Bytecode* woort_OpcodeDispatcher_decode(
    const woort_Bytecode* c, 
    /* OPTIONAL */ const woort_OpcodeDispatchers* d, 
    /* OPTIONAL */ void* userdata)
{
    woort_Bytecode bc = c[0];
_label_retry_entry:
    ;
    const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
    const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

    switch (op6)
    {
    case WOORT_OPCODE_NOP:
        DISPATCH(m_NOP);
        return c + 1;

    case WOORT_OPCODE_LOAD:
        DISPATCH(m_LOAD,
            (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
            (woort_Opcode_Global)WOORT_BYTECODE(MAB18, bc));
        return c + 1;

    case WOORT_OPCODE_STORE:
        DISPATCH(m_STORE,
            (woort_Opcode_Global)WOORT_BYTECODE(MAB18, bc),
            (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc));
        return c + 1;

    case WOORT_OPCODE_LDSTEX:
        switch (m2)
        {
        case 0: /* LOADEX -> LOAD */
            DISPATCH(m_LOAD,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Global)(uint32_t)c[1]);
            return c + 2;
        case 1: /* STOREEX -> STORE */
            DISPATCH(m_STORE,
                (woort_Opcode_Global)(uint32_t)c[1],
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 2;
        case 2: /* LOADPVALUE */
            DISPATCH(m_LOADPVALUE,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 3: /* STOREPVALUE */
            DISPATCH(m_STOREPVALUE,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_MOV:
        switch (m2)
        {
        case 0: /* MOVLD */
            DISPATCH(m_MOV,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1: /* MOVST */
            DISPATCH(m_MOV,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 2: /* MOVLDEXT -> MOVLD */
            DISPATCH(m_MOV,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int32_t)c[1]);
            return c + 2;
        case 3: /* MOVSTEXT -> MOVST */
            DISPATCH(m_MOV,
                (woort_Opcode_Stack)(int32_t)c[1],
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 2;
        }
        return c + 1;

    case WOORT_OPCODE_PUSHCHK:
        switch (m2)
        {
        case 0: /* PUSHRCHK */
            DISPATCH(m_PUSHRCHK, (woort_Opcode_Count)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 1: /* PUSHSCHK */
            DISPATCH(m_PUSHSCHK, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* PUSHCCHK */
            DISPATCH(m_PUSHCCHK, (woort_Opcode_Global)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 3: /* PUSHCCHKEXT -> PUSHCCHK */
            DISPATCH(m_PUSHCCHK, (woort_Opcode_Global)(uint32_t)c[1]);
            return c + 2;
        }
        return c + 1;

    case WOORT_OPCODE_PUSH:
        switch (m2)
        {
        case 0: /* ASSURESSZ */
            DISPATCH(m_ASSURESSZ, (woort_Opcode_Count)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 1: /* PUSHS */
            DISPATCH(m_PUSHS, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* PUSHC */
            DISPATCH(m_PUSHC, (woort_Opcode_Global)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 3: /* PUSHCEXT -> PUSHC */
            DISPATCH(m_PUSHC, (woort_Opcode_Global)(uint32_t)c[1]);
            return c + 2;
        }
        return c + 1;

    case WOORT_OPCODE_POP:
        switch (m2)
        {
        case 0: /* POPR */
            DISPATCH(m_POPR, (woort_Opcode_Count)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 1: /* POPS */
            DISPATCH(m_POPS, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* POPC */
            DISPATCH(m_POPC, (woort_Opcode_Global)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 3: /* POPCEXT -> POPC */
            DISPATCH(m_POPC, (woort_Opcode_Global)(uint32_t)c[1]);
            return c + 2;
        }
        return c + 1;

    case WOORT_OPCODE_CASTI:
        switch (m2)
        {
        case 0: /* ITORST */
            DISPATCH(m_ITOR,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 1: /* ITORLD */
            DISPATCH(m_ITOR,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* ITOSST */
            DISPATCH(m_ITOS,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 3: /* ITOSLD */
            DISPATCH(m_ITOS,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_CASTR:
        switch (m2)
        {
        case 0: /* RTOIST */
            DISPATCH(m_RTOI,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 1: /* RTOILD */
            DISPATCH(m_RTOI,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* RTOSST */
            DISPATCH(m_RTOS,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 3: /* RTOSLD */
            DISPATCH(m_RTOS,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_CASTX:
        switch (m2)
        {
        case 0: /* CASTSTO */
            DISPATCH(m_CASTSTO,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 1: /* CASTSFROM */
            DISPATCH(m_CASTSFROM,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 2: /* CASTDYN */
            DISPATCH(m_CASTDYN,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 3: /* ASSERTDYN */
            DISPATCH(m_ASSERTDYN,
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_CALLNWO:
        DISPATCH(m_CALLNWO, (woort_Opcode_Global)WOORT_BYTECODE(MABC26, bc));
        return c + 1;

    case WOORT_OPCODE_CALLNFP:
        DISPATCH(m_CALLNFP, (woort_Opcode_Global)WOORT_BYTECODE(MABC26, bc));
        return c + 1;

    case WOORT_OPCODE_CALLNJIT:
        DISPATCH(m_CALLNJIT, (woort_Opcode_Global)WOORT_BYTECODE(MABC26, bc));
        return c + 1;

    case WOORT_OPCODE_CALL:
        switch (m2)
        {
        case 0: /* CALLS */
            DISPATCH(m_CALLS, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1: /* CALLC */
            DISPATCH(m_CALLC, (woort_Opcode_Global)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_RET:
        switch (m2)
        {
        case 0: /* RET */
            DISPATCH(m_RET);
            return c + 1;
        case 1: /* RETVS */
            DISPATCH(m_RETVS, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* RETVC */
            DISPATCH(m_RETVC, (woort_Opcode_Global)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        case 3: /* POPRS */
            DISPATCH(m_POPRS, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_RESULT:
        DISPATCH(m_RESULT,
            (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
            (woort_Opcode_Count)WOORT_BYTECODE(MA10, bc));
        return c + 1;

    case WOORT_OPCODE_JFWD:
        DISPATCH(m_JFWD, (woort_Opcode_CodeAbs)WOORT_BYTECODE(MABC26, bc));
        return c + 1;

    case WOORT_OPCODE_JBCK:
        DISPATCH(m_JBCK, (woort_Opcode_CodeAbs)WOORT_BYTECODE(MABC26, bc));
        return c + 1;

    case WOORT_OPCODE_JFWDCND:
        switch (m2)
        {
        case 0: /* JFWDNZ */
            DISPATCH(m_JFWDNZ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_CodeDiff)(uint16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1: /* JFWDZ */
            DISPATCH(m_JFWDZ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_CodeDiff)(uint16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* JFWDEQ */
            DISPATCH(m_JFWDEQ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc),
                (woort_Opcode_CodeDiff)(uint8_t)WOORT_BYTECODE(C8, bc));
            return c + 1;
        case 3: /* JFWDNEQ */
            DISPATCH(m_JFWDNEQ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc),
                (woort_Opcode_CodeDiff)(uint8_t)WOORT_BYTECODE(C8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_JBCKCND:
        switch (m2)
        {
        case 0: /* JBCKNZ */
            DISPATCH(m_JBCKNZ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_CodeDiff)(uint16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1: /* JBCKZ */
            DISPATCH(m_JBCKZ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_CodeDiff)(uint16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* JBCKEQ */
            DISPATCH(m_JBCKEQ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc),
                (woort_Opcode_CodeDiff)(uint8_t)WOORT_BYTECODE(C8, bc));
            return c + 1;
        case 3: /* JBCKNEQ */
            DISPATCH(m_JBCKNEQ,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc),
                (woort_Opcode_CodeDiff)(uint8_t)WOORT_BYTECODE(C8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_JFDCMP:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_CodeDiff off = (woort_Opcode_CodeDiff)(uint8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_JFWDLT, a, b, off); return c + 1;
        case 1: DISPATCH(m_JFWDGT, a, b, off); return c + 1;
        case 2: DISPATCH(m_JFWDEL, a, b, off); return c + 1;
        case 3: DISPATCH(m_JFWDEG, a, b, off); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_JBCKCMP:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_CodeDiff off = (woort_Opcode_CodeDiff)(uint8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_JBCKLT, a, b, off); return c + 1;
        case 1: DISPATCH(m_JBCKGT, a, b, off); return c + 1;
        case 2: DISPATCH(m_JBCKEL, a, b, off); return c + 1;
        case 3: DISPATCH(m_JBCKEG, a, b, off); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_CONS:
    {
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0: /* MKVEC */
            DISPATCH(m_MKVEC, dst, (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 1: /* MKMAP */
            DISPATCH(m_MKMAP, dst, (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 2: /* MKSTRUCT */
            DISPATCH(m_MKSTRUCT, dst, (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 3: /* MKUNION */
            DISPATCH(m_MKUNION,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_CONSEX:
    {
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        const woort_Opcode_Count n = (woort_Opcode_Count)(uint32_t)c[1];
        switch (m2)
        {
        case 0: /* MKVECEXT -> MKVEC */
            DISPATCH(m_MKVEC, dst, n);
            return c + 2;
        case 1: /* MKMAPEXT -> MKMAP */
            DISPATCH(m_MKMAP, dst, n);
            return c + 2;
        case 2: /* MKSTRUCTEXT -> MKSTRUCT */
            DISPATCH(m_MKSTRUCT, dst, n);
            return c + 2;
        case 3: /* MKUNIONEXT -> MKUNION */
            DISPATCH(m_MKUNION,
                dst,
                n,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 2;
        }
        return c + 2;
    }

    case WOORT_OPCODE_MKCLOSURE:
        DISPATCH(m_MKCLOSURE,
            (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
            (woort_Opcode_Count)WOORT_BYTECODE(MA10, bc),
            (woort_Opcode_Global)(uint32_t)c[1]);
        return c + 2;

    case WOORT_OPCODE_DYN:
        switch (m2)
        {
        case 0: /* BOXDYN */
            DISPATCH(m_BOXDYN,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 1: /* UNBOXDYN */
            DISPATCH(m_UNBOXDYN,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 2: /* CHECKDYN */
            DISPATCH(m_CHECKDYN,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 3: /* PUSHBOXDYN */
            DISPATCH(m_PUSHBOXDYN,
                (woort_BoxValueType)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_OPIASMD:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_ADDI, dst, a, b); return c + 1;
        case 1: DISPATCH(m_SUBI, dst, a, b); return c + 1;
        case 2: DISPATCH(m_MULI, dst, a, b); return c + 1;
        case 3: DISPATCH(m_DIVI, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPIONLG:
        switch (m2)
        {
        case 0: /* MODI */
            DISPATCH(m_MODI,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 1: /* NEGI */
            DISPATCH(m_NEGI,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 2: /* LTI */
            DISPATCH(m_LTI,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 3: /* GTI */
            DISPATCH(m_GTI,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_OPISREN:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_LEI, dst, a, b); return c + 1;
        case 1: DISPATCH(m_GEI, dst, a, b); return c + 1;
        case 2: DISPATCH(m_EQI, dst, a, b); return c + 1;
        case 3: DISPATCH(m_NEI, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPRASMD:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_ADDR, dst, a, b); return c + 1;
        case 1: DISPATCH(m_SUBR, dst, a, b); return c + 1;
        case 2: DISPATCH(m_MULR, dst, a, b); return c + 1;
        case 3: DISPATCH(m_DIVR, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPRONLG:
        switch (m2)
        {
        case 0: /* MODR */
            DISPATCH(m_MODR,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 1: /* NEGR */
            DISPATCH(m_NEGR,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 2: /* LTR */
            DISPATCH(m_LTR,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 3: /* GTR */
            DISPATCH(m_GTR,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_OPRSREN:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_LER, dst, a, b); return c + 1;
        case 1: DISPATCH(m_GER, dst, a, b); return c + 1;
        case 2: DISPATCH(m_EQR, dst, a, b); return c + 1;
        case 3: DISPATCH(m_NER, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPSALGS:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_ADDS, dst, a, b); return c + 1;
        case 1: DISPATCH(m_LTS, dst, a, b); return c + 1;
        case 2: DISPATCH(m_GTS, dst, a, b); return c + 1;
        case 3: DISPATCH(m_LES, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPSREN:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_GES, dst, a, b); return c + 1;
        case 1: DISPATCH(m_EQS, dst, a, b); return c + 1;
        case 2: DISPATCH(m_NES, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPLAONI:
        switch (m2)
        {
        case 0: /* LAND */
            DISPATCH(m_LAND,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 1: /* LOR */
            DISPATCH(m_LOR,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 2: /* LNOT */
            DISPATCH(m_LNOT,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_OPCIASMD:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_CADDI, dst, a); return c + 1;
        case 1: DISPATCH(m_CSUBI, dst, a); return c + 1;
        case 2: DISPATCH(m_CMULI, dst, a); return c + 1;
        case 3: DISPATCH(m_CDIVI, dst, a); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_CHKDIVI:
        switch (m2)
        {
        case 0: /* CHKDIVIL */
            DISPATCH(m_CHKDIVIL, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1: /* CHKDIVIR */
            DISPATCH(m_CHKDIVIR, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* CHKDIVIRZ */
            DISPATCH(m_CHKDIVIRZ, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 3: /* CHKDIVILR */
            DISPATCH(m_CHKDIVILR,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_OPCRASMD:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_CADDR, dst, a); return c + 1;
        case 1: DISPATCH(m_CSUBR, dst, a); return c + 1;
        case 2: DISPATCH(m_CMULR, dst, a); return c + 1;
        case 3: DISPATCH(m_CDIVR, dst, a); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPCSAIOO:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_CADDS, dst, a); return c + 1;
        case 1: DISPATCH(m_CVADDS, dst, a); return c + 1;
        case 2: DISPATCH(m_CMODI, dst, a); return c + 1;
        case 3: DISPATCH(m_CMODR, dst, a); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_OPCLAON:
        switch (m2)
        {
        case 0: /* CLAND */
            DISPATCH(m_CLAND,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 1: /* CLOR */
            DISPATCH(m_CLOR,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        case 2: /* CLNOT */
            DISPATCH(m_CLNOT, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 3: /* MKPVALUE */
            DISPATCH(m_MKPVALUE,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_LDID:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: /* LDIDVEC */
            DISPATCH(m_LDIDVEC, dst, a, b);
            return c + 1;
        case 1: /* LDIDVECX */
            DISPATCH(m_LDIDVECX, dst, a, b);
            return c + 1;
        case 2: /* LDIDSTRUCT */
            DISPATCH(m_LDIDSTRUCT, dst, (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc), b);
            return c + 1;
        case 3: /* LDIDSTRING */
            DISPATCH(m_LDIDSTRING, dst, a, b);
            return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_LDIDDICT:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_LDIDDICTI, dst, a, b); return c + 1;
        case 1: DISPATCH(m_LDIDDICTR, dst, a, b); return c + 1;
        case 2: DISPATCH(m_LDIDDICTB, dst, a, b); return c + 1;
        case 3: DISPATCH(m_LDIDDICTX, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_LDIDDICTX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_LDIDDICTIX, dst, a, b); return c + 1;
        case 1: DISPATCH(m_LDIDDICTRX, dst, a, b); return c + 1;
        case 2: DISPATCH(m_LDIDDICTBX, dst, a, b); return c + 1;
        case 3: DISPATCH(m_LDIDDICTXX, dst, a, b); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_LDIDEX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int16_t)(c[1] >> 16);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)(c[1] & 0xFFFFu);
        switch (m2)
        {
        case 0: /* LDIDVECEXT -> LDIDVEC */
            DISPATCH(m_LDIDVEC, dst, a, b);
            return c + 2;
        case 1: /* LDIDVECXEXT -> LDIDVECX */
            DISPATCH(m_LDIDVECX, dst, a, b);
            return c + 2;
        case 2: /* LDIDSTRUCTEXT -> LDIDSTRUCT */
            DISPATCH(m_LDIDSTRUCT, dst, (woort_Opcode_Count)WOORT_BYTECODE(ABC24, bc), b);
            return c + 2;
        case 3: /* LDIDSTRINGEXT -> LDIDSTRING */
            DISPATCH(m_LDIDSTRING, dst, a, b);
            return c + 2;
        }
        return c + 2;
    }

    case WOORT_OPCODE_LDIDDICTEX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int16_t)(c[1] >> 16);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)(c[1] & 0xFFFFu);
        switch (m2)
        {
        case 0: DISPATCH(m_LDIDDICTI, dst, a, b); return c + 2;
        case 1: DISPATCH(m_LDIDDICTR, dst, a, b); return c + 2;
        case 2: DISPATCH(m_LDIDDICTB, dst, a, b); return c + 2;
        case 3: DISPATCH(m_LDIDDICTX, dst, a, b); return c + 2;
        }
        return c + 2;
    }

    case WOORT_OPCODE_LDIDDICTEXX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int16_t)(c[1] >> 16);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)(c[1] & 0xFFFFu);
        switch (m2)
        {
        case 0: DISPATCH(m_LDIDDICTIX, dst, a, b); return c + 2;
        case 1: DISPATCH(m_LDIDDICTRX, dst, a, b); return c + 2;
        case 2: DISPATCH(m_LDIDDICTBX, dst, a, b); return c + 2;
        case 3: DISPATCH(m_LDIDDICTXX, dst, a, b); return c + 2;
        }
        return c + 2;
    }

    case WOORT_OPCODE_STIDVEC:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDVECI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDVECR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDVECB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDVECX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDDICTI:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDDICTII, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDDICTIR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDDICTIB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDDICTIX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDDICTR:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDDICTRI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDDICTRR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDDICTRB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDDICTRX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDDICTB:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDDICTBI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDDICTBR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDDICTBB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDDICTBX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDDICTX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDDICTXI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDDICTXR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDDICTXB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDDICTXX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDMAPI:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDMAPII, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDMAPIR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDMAPIB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDMAPIX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDMAPR:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDMAPRI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDMAPRR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDMAPRB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDMAPRX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDMAPB:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDMAPBI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDMAPBR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDMAPBB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDMAPBX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDMAPX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc);
        const woort_Opcode_Stack c8 = (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_STIDMAPXI, a, b, c8); return c + 1;
        case 1: DISPATCH(m_STIDMAPXR, a, b, c8); return c + 1;
        case 2: DISPATCH(m_STIDMAPXB, a, b, c8); return c + 1;
        case 3: DISPATCH(m_STIDMAPXX, a, b, c8); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_STIDSTRUCT:
        DISPATCH(m_STIDSTRUCT,
            (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc),
            (woort_Opcode_Count)WOORT_BYTECODE(MA10, bc),
            (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc));
        return c + 1;

    case WOORT_OPCODE_STIDEX:
    {
        const woort_Opcode_Stack a = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        const woort_Opcode_Stack b = (woort_Opcode_Stack)(int16_t)(c[1] >> 16);
        const woort_Opcode_Stack dst = (woort_Opcode_Stack)(int16_t)(c[1] & 0xFFFFu);
        switch (m2)
        {
        case 0: /* STIDVECEXT -> STIDVEC{I,R,B,X} */
        {
            const uint8_t vt = (uint8_t)WOORT_BYTECODE(A8, bc);
            switch (vt)
            {
            case 0: DISPATCH(m_STIDVECI, a, b, dst); break;
            case 1: DISPATCH(m_STIDVECR, a, b, dst); break;
            case 2: DISPATCH(m_STIDVECB, a, b, dst); break;
            case 3: DISPATCH(m_STIDVECX, a, b, dst); break;
            }
            return c + 2;
        }
        case 1: /* STIDDICTEXT -> STIDDICT{K}{V} */
        {
            const uint8_t kt = (uint8_t)((bc >> 20) & 0xFu);
            const uint8_t vt = (uint8_t)((bc >> 16) & 0xFu);
            switch (kt)
            {
            case 0:
                switch (vt) {
                case 0: DISPATCH(m_STIDDICTII, a, b, dst); break;
                case 1: DISPATCH(m_STIDDICTIR, a, b, dst); break;
                case 2: DISPATCH(m_STIDDICTIB, a, b, dst); break;
                case 3: DISPATCH(m_STIDDICTIX, a, b, dst); break;
                } break;
            case 1:
                switch (vt) {
                case 0: DISPATCH(m_STIDDICTRI, a, b, dst); break;
                case 1: DISPATCH(m_STIDDICTRR, a, b, dst); break;
                case 2: DISPATCH(m_STIDDICTRB, a, b, dst); break;
                case 3: DISPATCH(m_STIDDICTRX, a, b, dst); break;
                } break;
            case 2:
                switch (vt) {
                case 0: DISPATCH(m_STIDDICTBI, a, b, dst); break;
                case 1: DISPATCH(m_STIDDICTBR, a, b, dst); break;
                case 2: DISPATCH(m_STIDDICTBB, a, b, dst); break;
                case 3: DISPATCH(m_STIDDICTBX, a, b, dst); break;
                } break;
            case 3:
                switch (vt) {
                case 0: DISPATCH(m_STIDDICTXI, a, b, dst); break;
                case 1: DISPATCH(m_STIDDICTXR, a, b, dst); break;
                case 2: DISPATCH(m_STIDDICTXB, a, b, dst); break;
                case 3: DISPATCH(m_STIDDICTXX, a, b, dst); break;
                } break;
            }
            return c + 2;
        }
        case 2: /* STIDMAPEXT -> STIDMAP{K}{V} */
        {
            const uint8_t kt = (uint8_t)((bc >> 20) & 0xFu);
            const uint8_t vt = (uint8_t)((bc >> 16) & 0xFu);
            switch (kt)
            {
            case 0:
                switch (vt) {
                case 0: DISPATCH(m_STIDMAPII, a, b, dst); break;
                case 1: DISPATCH(m_STIDMAPIR, a, b, dst); break;
                case 2: DISPATCH(m_STIDMAPIB, a, b, dst); break;
                case 3: DISPATCH(m_STIDMAPIX, a, b, dst); break;
                } break;
            case 1:
                switch (vt) {
                case 0: DISPATCH(m_STIDMAPRI, a, b, dst); break;
                case 1: DISPATCH(m_STIDMAPRR, a, b, dst); break;
                case 2: DISPATCH(m_STIDMAPRB, a, b, dst); break;
                case 3: DISPATCH(m_STIDMAPRX, a, b, dst); break;
                } break;
            case 2:
                switch (vt) {
                case 0: DISPATCH(m_STIDMAPBI, a, b, dst); break;
                case 1: DISPATCH(m_STIDMAPBR, a, b, dst); break;
                case 2: DISPATCH(m_STIDMAPBB, a, b, dst); break;
                case 3: DISPATCH(m_STIDMAPBX, a, b, dst); break;
                } break;
            case 3:
                switch (vt) {
                case 0: DISPATCH(m_STIDMAPXI, a, b, dst); break;
                case 1: DISPATCH(m_STIDMAPXR, a, b, dst); break;
                case 2: DISPATCH(m_STIDMAPXB, a, b, dst); break;
                case 3: DISPATCH(m_STIDMAPXX, a, b, dst); break;
                } break;
            }
            return c + 2;
        }
        case 3: /* STIDSTRUCTEXT */
            DISPATCH(m_STIDSTRUCT,
                b,
                (woort_Opcode_Count)WOORT_BYTECODE(ABC24, bc),
                dst);
            return c + 2;
        }
        return c + 2;
    }

    case WOORT_OPCODE_UNPACK:
        switch (m2)
        {
        case 0: /* UNPACKVEC */
            DISPATCH(m_UNPACKVEC,
                (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 1: /* UNPACKVECX */
            DISPATCH(m_UNPACKVECX,
                (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* UNPACKVECALL */
            DISPATCH(m_UNPACKVECALL,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        case 3: /* UNPACKVECXALL */
            DISPATCH(m_UNPACKVECXALL,
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(C8, bc),
                (woort_Opcode_Count)(uint8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(B8, bc));
            return c + 1;
        }
        return c + 1;

    case WOORT_OPCODE_PUSHIDSTBOX:
    {
        const woort_Opcode_Count n = (woort_Opcode_Count)(uint16_t)WOORT_BYTECODE(A8, bc);
        const woort_Opcode_Stack src = (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc);
        switch (m2)
        {
        case 0: DISPATCH(m_PUSHIDSTRUCT, n, src); return c + 1;
        case 1: DISPATCH(m_PUSHIDSTBOXI, n, src); return c + 1;
        case 2: DISPATCH(m_PUSHIDSTBOXR, n, src); return c + 1;
        case 3: DISPATCH(m_PUSHIDSTBOXB, n, src); return c + 1;
        }
        return c + 1;
    }

    case WOORT_OPCODE_PACKARG:
        DISPATCH(m_PACKARG,
            (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
            (woort_Opcode_Count)WOORT_BYTECODE(MA10, bc));
        return c + 1;

    case WOORT_OPCODE_ATOMIC:
        switch (m2)
        {
        case 0: /* ASTORE */
            DISPATCH(m_ASTORE,
                (woort_Opcode_Global)(uint32_t)c[1],
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 2;
        case 1: /* ALOAD */
            DISPATCH(m_ALOAD,
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc),
                (woort_Opcode_Global)(uint32_t)c[1]);
            return c + 2;
        case 2: /* CAS */
            DISPATCH(m_CAS,
                (woort_Opcode_Global)(uint32_t)c[1],
                (woort_Opcode_Stack)(int8_t)WOORT_BYTECODE(A8, bc),
                (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 2;
        }
        return c + 1;

    case WOORT_OPCODE_JIFINITED:
        DISPATCH(m_JIFINITED,
            (woort_Opcode_Global)(uint32_t)c[1],
            (woort_Opcode_CodeAbs)WOORT_BYTECODE(MABC26, bc));
        return c + 2;

    case WOORT_OPCODE_TRAP:
        switch (m2)
        {
        case 0: /* DEBUGTRAP */
        {
            /*
             * 透明解析断点：DEBUGTRAP 是 set_trap 覆盖原指令后留下的占位
             * 指令。查找其所属 CodeEnv，取回被覆盖的原始指令并重新解码；
             * 仅当它是没有后备指令的、由 IR 直接发射的 DEBUGTRAP 时，
             * 才回调 m_DEBUGTRAP。
             */
            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(c, &cenv))
            {
                bc = woort_CodeEnv_raw_trap(cenv, c);
                if (bc != woort_OpCode_DEBUGTRAP())
                    goto _label_retry_entry;
            }
            DISPATCH(m_DEBUGTRAP);
            return c + 1;
        }
        case 1: /* PANICS */
            DISPATCH(m_PANICS, (woort_Opcode_Stack)(int16_t)WOORT_BYTECODE(BC16, bc));
            return c + 1;
        case 2: /* PANICC */
            DISPATCH(m_PANICC, (woort_Opcode_Global)WOORT_BYTECODE(ABC24, bc));
            return c + 1;
        }
        return c + 1;

    default:
        break;
    }

    return c + 1;
}

#undef DISPATCH
