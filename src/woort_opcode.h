#pragma once

/*
woort_opcode.h
*/

typedef enum woort_Opcode
{
    NOP /* EXTDATA */,  // OP6 _26

    LOAD,               // OP6 C18 S8
    STORE,              // OP6 C18 S8
    LOADEX,             // OP6 C18L S8   EX_C26H
    STOREEX,            // OP6 C18L S8   EX_C26H

    PUSH,
    // OP6 M2 
    //     0:   COUNT24
    //     1:   C24
    //     2:   S8 _16
    //     3:   S24
    POP,
    // OP6 M2 
    //      0:  COUNT24
    //      1:  C24
    //      2:  S8 _16
    //      3:  S24

    CAST,              
    // OP6 M2
    //     0(ItoR):   S8, S8, _8
    //     1(ItoS):   S8, S8, _8
    //     2(RtoI):   S8, S8, _8
    //     3(RtoS):   S8, S8, _8

    JMP,                // OP6 OFFSET26
    JCOND,               
    // OP6 M2
    //      0(CONDNZ):  S8 OFFSET16
    //      1(CONDZ):   S8 OFFSET16
    //      2(CONDEQ):  S8 S8 OFFSET8
    //      3(CONDNE):  S8 S8 OFFSET8
    JMPGC,              // OP6 OFFSET26
    JCONDGC,
    // OP6 M2
    //      0(CONDNZ):  S8 OFFSET16
    //      1(CONDZ):   S8 OFFSET16
    //      2(CONDEQ):  S8 S8 OFFSET8
    //      3(CONDNE):  S8 S8 OFFSET8

    CALL,
    // OP6 M2
    //      0(CALLNWO):     S8 _16
    //      1(CALLNFP):     S8 _16
    //      2(CALLNJIT):    S8 _16
    //      3(CALL):        S8 _16

    RET,    
    // OP6 M2
    //      0(RET):         S8 _16
    //      1(RETN):        S8 COUNT16

    RESULT, // OP6 _2 S8 COUNT16
    
    CONS,   
    // OP6 M2
    //      0(MKARR):       S8 COUNT16
    //      1(MKMAP):       S8 COUNT16
    //      2(MKSTRUCT):    S8 COUNT16
    MKCLOS, 
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

    OPIASMD,            // OP6 M2 S8 S8 S8
    OPIONLG,            // OP6 M2 S8 S8 S8(Exclude Neg)
    OPISREN,            // OP6 M2 S8 S8 S8
    OPRASMD,            // OP6 M2 S8 S8 S8
    OPRONLG,            // OP6 M2 S8 S8 S8(Exclude Neg)
    OPRSREN,            // OP6 M2 S8 S8 S8
    OPSALGS,            // OP6 M2 S8 S8 S8
    OPSREN,             // OP6 M2 S8 S8 S8
    OPLAON,             // OP6 M2 S8 S8 S8(Exclude NOT)
    OPCIASMD,           // OP6 M2 S8 S8 _8
    OPCRASMD,           // OP6 M2 S8 S8 _8
    OPCSAI,             // OP6 M2 S8 S8 _8
    OPCLAON,            // OP6 M2 S8 S8(Exclude NOT) _8

} woort_Opcode;
