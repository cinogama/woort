#pragma once

/*
woort_opcode.h
*/

typedef enum woort_Opcode
{
    WOORT_OPCODE_NOP /* EXTDATA */,  // OP6 _MABC26

    WOORT_OPCODE_LOAD,               // OP6 CMAB18 SC8
    WOORT_OPCODE_STORE,              // OP6 C18 SC8
    WOORT_OPCODE_LOADEX,             // OP6 _10 S16   EX_C32
    WOORT_OPCODE_STOREEX,            // OP6 _10 S16   EX_C32

    WOORT_OPCODE_MOV,                
    // OP6 M2
    //      0(MOV):         S8 S16
    //      1(MOVINV):      S8 <=< S16
    //      2(MOVEXT):      _8 S16 EX_S16
    //      3(RESERVED)

    WOORT_OPCODE_PUSHCHK,
    // OP6 M2 
    //     0(PUSHRCHK):     COUNT24
    //     1(PUSHSCHK):     _8 S16
    //     2(PUSHCCHK):     C24
    //     3(PUSHCEXTCHK):  _24 EX_C32
    WOORT_OPCODE_PUSH,
    // OP6 M2 
    //     0(MKSURESZ):     COUNT24
    //     1(PUSHS):        _8 S16
    //     2(PUSHC):        C24
    //     3(PUSHCEXT):     _24 EX_C32
    WOORT_OPCODE_POP,
    // OP6 M2 
    //      0:  COUNT24
    //      1:  _8 S16
    //      2:  C24
    //      3:  _24 EX_C32

    WOORT_OPCODE_CASTI,
    // OP6 M2
    //     0(ITOR):     S8, S16
    //     1(ITOS):     S8, S16
    //     2(ITORINV):  S8, <=< S16
    //     3(ITOSINV):  S8, <=< S16
    WOORT_OPCODE_CASTR,
    // OP6 M2
    //     0(RTOI):     S8, S16
    //     1(RTOS):     S8, S16
    //     2(RTOIINV):  S8, <=< S16
    //     3(RTOSINV):  S8, <=< S16
    WOORT_OPCODE_CASTS,
    // OP6 M2
    //     0(STOI):     S8, S16
    //     1(STOR):     S8, S16
    //     2(STOIINV):  S8, <=< S16
    //     3(STORINV):  S8, <=< S16

    WOORT_OPCODE_RESULT,            // OP6 _2 S8 COUNT16

    /*
    WooRT Stack model:

    sp      |                        | < Next value push storage.
    |       |~~~~~~~~~~~~~~~~~~~~~~~|
    ...     |                        |
    |       |_______________________| 
    bp      |_______________________| < Captured unpack here(If closure).
    bp + 1  |__ CALLWAY & BPOFFSET _|
    bp + 2  |____ RETURN ADDRESS ___|
    bp + 3  |_____ ARGUMENT 0 ______| < Argument count here(If variadic).
    bp + 4  |_____ ARGUMENT 1 ______|
    */

    WOORT_OPCODE_CALLNWO,           // OP6 ADDR26H EX_ADDR32L
    // I hope 6-level page will never appear...
    WOORT_OPCODE_CALLNFP,           // OP6 ADDR26H EX_ADDR32L
    WOORT_OPCODE_CALLNJIT,          // OP6 ADDR26H EX_ADDR32L

    WOORT_OPCODE_CALL,
    // OP6 M2
    //      0(CALLNWO):     _8 S16
    //      1(CALLNFP):     _8 S16
    //      2(CALLNJIT):    _8 S16
    //      3(CALL):        _8 S16

    WOORT_OPCODE_RET,
    // OP6 M2
    //      0(RET):         _8 S16
    //      1(RETN):        S8 COUNT16

    WOORT_OPCODE_JMP,               // OP6 OFFSET26
    WOORT_OPCODE_JMPGC,             // OP6 OFFSET26
    WOORT_OPCODE_JCOND,
    // OP6 M2
    //      0(JCONDNZ): S8 OFFSET16
    //      1(JCONDZ):  S8 OFFSET16
    //      2(JCONDEQ): S8 S8 OFFSET8
    //      3(JCONDNE): S8 S8 OFFSET8
    WOORT_OPCODE_JCONDGC,
    // OP6 M2
    //      0(JCONDNZ): S8 OFFSET16
    //      1(JCONDZ):  S8 OFFSET16
    //      2(JCONDEQ): S8 S8 OFFSET8
    //      3(JCONDNE): S8 S8 OFFSET8

    WOORT_OPCODE_CONS,
    // OP6 M2
    //      0(MKARR):       S8 COUNT16
    //      1(MKMAP):       S8 COUNT16
    //      2(MKSTRUCT):    S8 COUNT16
    //      3(RESERVED)
    WOORT_OPCODE_MKCLOS,
    // OP6 M2 
    //      0(MKCLOS):      S8 S8 COUNT8
    //      1(MKCLOSEXT):   S8 S16 EX_COUNT32
    
    WOORT_OPCODE_DYN,
    // OP6 M2
    //      0(BOXDYN)       T8, S8, S8
    //      1(UNBOXDYN)     T8, S8, S8
    //      2(CHECKDYN)     T8, S8, S8
    //      3(PUSHDYN)      T8, S16

    // Arithmetic:
    // 
    // For integer & real types:
    // 
    // A(Add), S(Sub), M(Mul), D(Div), O(Mod), N(Neg)
    // L(Less), G(Greater) S(Less or Equal), R(Greater or Equal)
    // E(Equal), N(Not Equal)
    // 
    // For logical types:
    // 
    // A(And), O(Or), N(Not)
    WOORT_OPCODE_OPIASMD,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPIONLG,            
    // Non-NEG:     OP6 M2 S8 S8 S8
    // NEG:         OP6 M2 S8 S16
    WOORT_OPCODE_OPISREN,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPRASMD,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPRONLG,            
    // Non-NEG:     OP6 M2 S8 S8 S8
    // NEG:         OP6 M2 S8 S16
    WOORT_OPCODE_OPRSREN,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPSALGS,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPSREN,             // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPLAONI,
    // Non-NOT: OP6 M2 S8 S8 S8
    // NOT1:    OP6 M2 S8 >=> S16
    // I-NOT:   OP6 M2 S8 <=< S16
    WOORT_OPCODE_OPCIASMD,           // OP6 M2 S8 S16
    WOORT_OPCODE_OPCRASMD,           // OP6 M2 S8 S16
    WOORT_OPCODE_OPCSAIOO,           // OP6 M2 S8 S16 
    WOORT_OPCODE_OPCLAON,            // OP6 M2 S8(Exclude NOT) S16

    WOORT_OPCODE_OPCVISDNO,          // OP6 M2 S8 <=< S16 
    WOORT_OPCODE_OPCVRSDNO,          // OP6 M2 S8 <=< S16

} woort_Opcode;
