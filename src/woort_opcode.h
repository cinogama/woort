#pragma once

/*
woort_opcode.h
*/

typedef enum woort_Opcode
{
    WOORT_OPCODE_NOP /* EXTDATA */,  // OP6 _26

    WOORT_OPCODE_LOAD,               // OP6 C18 S8
    WOORT_OPCODE_STORE,              // OP6 C18 S8
    WOORT_OPCODE_LOADEX,             // OP6 C18L S8   EX_C26H
    WOORT_OPCODE_STOREEX,            // OP6 C18L S8   EX_C26H

    WOORT_OPCODE_PUSH,
    // OP6 M2 
    //     0:   COUNT24
    //     1:   C24
    //     2:   S8 _16
    //     3:   _8 S16
    WOORT_OPCODE_POP,
    // OP6 M2 
    //      0:  COUNT24
    //      1:  C24
    //      2:  S8 _16
    //      3:  _8 S16

    WOORT_OPCODE_CAST,
    // OP6 M2
    //     0(ItoR):   S8, S8, _8
    //     1(ItoS):   S8, S8, _8
    //     2(RtoI):   S8, S8, _8
    //     3(RtoS):   S8, S8, _8

    WOORT_OPCODE_JMP,                // OP6 OFFSET26
    WOORT_OPCODE_JCOND,
    // OP6 M2
    //      0(CONDNZ):  S8 OFFSET16
    //      1(CONDZ):   S8 OFFSET16
    //      2(CONDEQ):  S8 S8 OFFSET8
    //      3(CONDNE):  S8 S8 OFFSET8
    WOORT_OPCODE_JMPGC,              // OP6 OFFSET26
    WOORT_OPCODE_JCONDGC,
    // OP6 M2
    //      0(CONDNZ):  S8 OFFSET16
    //      1(CONDZ):   S8 OFFSET16
    //      2(CONDEQ):  S8 S8 OFFSET8
    //      3(CONDNE):  S8 S8 OFFSET8

    WOORT_OPCODE_CALL,
    // OP6 M2
    //      0(CALLNWO):     S8 _16
    //      1(CALLNFP):     S8 _16
    //      2(CALLNJIT):    S8 _16
    //      3(CALL):        S8 _16

    WOORT_OPCODE_RET,
    // OP6 M2
    //      0(RET):         S8 _16
    //      1(RETN):        S8 COUNT16

    WOORT_OPCODE_RESULT, // OP6 _2 S8 COUNT16
    
    WOORT_OPCODE_CONS,
    // OP6 M2
    //      0(MKARR):       S8 COUNT16
    //      1(MKMAP):       S8 COUNT16
    //      2(MKSTRUCT):    S8 COUNT16
    WOORT_OPCODE_MKCLOS,
    // OP6 M2 
    //      0:              S8 S8 COUNT8
    //      1:              S8 S8 S8
      
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
    WOORT_OPCODE_OPIONLG,            // OP6 M2 S8 S8 S8(Exclude Neg)
    WOORT_OPCODE_OPISREN,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPRASMD,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPRONLG,            // OP6 M2 S8 S8 S8(Exclude Neg)
    WOORT_OPCODE_OPRSREN,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPSALGS,            // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPSREN,             // OP6 M2 S8 S8 S8
    WOORT_OPCODE_OPLAON,             // OP6 M2 S8 S8 S8(Exclude NOT)
    WOORT_OPCODE_OPCIASMD,           // OP6 M2 S8 S8 _8
    WOORT_OPCODE_OPCRASMD,           // OP6 M2 S8 S8 _8
    WOORT_OPCODE_OPCSAI,             // OP6 M2 S8 S8 _8
    WOORT_OPCODE_OPCLAON,            // OP6 M2 S8 S8(Exclude NOT) _8

} woort_Opcode;
