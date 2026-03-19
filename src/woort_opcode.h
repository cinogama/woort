#pragma once

/*
woort_opcode.h
*/

typedef enum woort_Opcode
{
    /*
    WooRT Stack model:

    sp      |                        | < Next value push storage.
    |       |~~~~~~~~~~~~~~~~~~~~~~~|
    ...     |                        |
    |       |_______________________|
    bp      |_______________________| < Captured unpack here(If closure).
    bp + 1  |__ CALLWAY & BPOFFSET _|
    bp + 2  |____ RETURN ADDRESS ___| < * Return value stores here.
    bp + 3  |_____ ARGUMENT 0 ______| < Argument count here(If variadic).
    bp + 4  |_____ ARGUMENT 1 ______|

    操作数说明:
    - R_ONLY: 只读操作数
    - W_ONLY: 只写操作数
    - R_M_S8: 读操作数（8位偏移），这个操作数对应一个容器，其中的值将被修改
    - I: Integer 整数
    - R: Real 实数
    - B: Boolean 布尔
    - X: Dynamic/GCUnit 动态类型
    - S: String 字符串
    - N: Number 计数(无符号)
    - T: Type 类型标识
    - C: Constant 常量索引
    - S: Stack offset 栈偏移
    - U: Unsigned offset 无符号偏移量（用于条件跳转）
    - BA: Branch address 分支地址
    - BR: Branch offset 分支相对偏移（注意：实际实现中使用无符号偏移量）

    所有指令的写操作总是发生在读操作之前
    */

    // ___________________________________________________________________________________________________________
    // |__Main_Command(6bits)___|__Mode_(2bits)__|__A_(8bits)__ _|__B_(8bits)____|__C_(8bits)____|__EX_(32bits)__|
    /* ExampleA */              /*_______________|_______________|_______________|_______________|_______________|  */
    /*      ExampleB            |________________|_______________|_______________|_______________|_______________|  */
    /**/ WOORT_OPCODE_NOP,      /*_______________________________________________________________|_______X_______|  */
    /**/ WOORT_OPCODE_LOAD,     /*_________________R_ONLY_C18____________________|___W_ONLY_S8___|_______X_______|  */
    /**/ WOORT_OPCODE_STORE,    /*_________________W_ONLY_C18____________________|___R_ONLY_S8___|_______X_______|  */
    /**/ WOORT_OPCODE_LOADEX,   /*_______________________________|__________W_ONLY_S16___________|__R_ONLY_C32___|  */
    /**/ WOORT_OPCODE_STOREEX,  /*_______________________________|__________R_ONLY_S16___________|__W_ONLY_C32___|  */
    WOORT_OPCODE_MOV,           /*_____MODE______________________________________________________|_______X_______|  */
    /*      MOVLD               |_______0________|___W_ONLY_S8___|___________R_ONLY_S16__________|_______X_______|  */
    /*      MOVST               |_______1________|___R_ONLY_S8___|___________W_ONLY_S16__________|_______X_______|  */
    /*      MOVLDEXT            |_______2________|_______________|___________W_ONLY_S16__________|__R_ONLY_S32___|  */
    /*      MOVSTEXT            |_______3________|_______________|___________R_ONLY_S16__________|__W_ONLY_S32___|  */
    WOORT_OPCODE_PUSHCHK,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      PUSHRCHK            |_______0________|_______________________N24_____________________|_______X_______|  */
    /*      PUSHSCHK            |_______1________|_______________|___________R_ONLY_S16__________|_______X_______|  */
    /*      PUSHCCHK            |_______2________|___________________R_ONLY_C24__________________|_______X_______|  */
    /*      PUSHCCHKEXT         |_______3________|_______________________________________________|__R_ONLY_C32___|  */
    WOORT_OPCODE_PUSH,          /*_____MODE______________________________________________________|_______X_______|  */
    /*      ASSURESSZ           |_______0________|_______________________N24_____________________|_______X_______|  */
    /*      PUSHS               |_______1________|_______________|___________R_ONLY_S16__________|_______X_______|  */
    /*      PUSHC               |_______2________|___________________R_ONLY_C24__________________|_______X_______|  */
    /*      PUSHCEXT            |_______3________|_______________________________________________|__R_ONLY_C32___|  */
    WOORT_OPCODE_POP,           /*_____MODE______________________________________________________|_______X_______|  */
    /*      POPR                |_______0________|_______________________N24_____________________|_______X_______|  */
    /*      POPS                |_______1________|_______________|___________R_ONLY_S16__________|_______X_______|  */
    /*      POPC                |_______2________|___________________R_ONLY_C24__________________|_______X_______|  */
    /*      POPCEXT             |_______3________|_______________________________________________|__R_ONLY_C32___|  */
    WOORT_OPCODE_CASTI,         /*_____MODE______________________________________________________|_______X_______|  */
    /*      ITORST              |_______0________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      ITORLD              |_______1________|___W_ONLY_S8___|__________R_ONLY_S16___________|_______X_______|  */
    /*      ITOSST              |_______2________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      ITOSLD              |_______3________|___W_ONLY_S8___|__________R_ONLY_S16___________|_______X_______|  */
    WOORT_OPCODE_CASTR,         /*_____MODE______________________________________________________|_______X_______|  */
    /*      RTOIST              |_______0________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      RTOILD              |_______1________|___W_ONLY_S8___|__________R_ONLY_S16___________|_______X_______|  */
    /*      RTOSST              |_______2________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      RTOSLD              |_______3________|___W_ONLY_S8___|__________R_ONLY_S16___________|_______X_______|  */
    WOORT_OPCODE_CASTS,         /*_____MODE______________________________________________________|_______X_______|  */
    /*      STOIST              |_______0________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      STOILD              |_______1________|___W_ONLY_S8___|__________R_ONLY_S16___________|_______X_______|  */
    /*      STORST              |_______2________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      STORLD              |_______3________|___W_ONLY_S8___|__________R_ONLY_S16___________|_______X_______|  */
    /**/ WOORT_OPCODE_CALLNWO,  /*___________________________R_ONLY_C26__________________________|_______X_______|  */
    /**/ WOORT_OPCODE_CALLNFP,  /*___________________________R_ONLY_C26__________________________|_______X_______|  */
    /**/ WOORT_OPCODE_CALLNJIT, /*___________________________R_ONLY_C26__________________________|_______X_______|  */
    /**/ WOORT_OPCODE_CALL,     /*_____MODE______________________________________________________|_______X_______|  */
    /*      CALLS               |_______0________|_______________|__________R_ONLY_S16___________|_______X_______|  */
    /*      CALLC               |_______1________|___________________R_ONLY_C24__________________|_______X_______|  */
    /*      <RESERVED>          |_______2________|_______________|_______________|_______________|_______________|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    WOORT_OPCODE_RET,           /*_____MODE______________________________________________________|_______X_______|  */
    /*      RET                 |_______0________|_______________________X_______________________|_______X_______|  */
    /*      RETVS               |_______1________|_______________|__________R_ONLY_S16___________|_______X_______|  */
    /*      RETVC               |_______2________|___________________R_ONLY_C24__________________|_______X_______|  */
    /*      POPRS               |_______3________|_______________|__________R_ONLY_S16___________|_______________|  */
    /**/ WOORT_OPCODE_RESULT,   /*______________N10______________|__________W_ONLY_S16___________|_______X_______|  */
    /**/ WOORT_OPCODE_JFWD,     /*_____________________________BA26______________________________|_______X_______|  */
    /**/ WOORT_OPCODE_JBCK,     /*_____________________________BA26______________________________|_______X_______|  */
    WOORT_OPCODE_JFWDCND,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      JFWDNZ              |_______0________|___R_ONLY_S8___|_____________U16______________|_______X_______|  */
    /*      JFWDZ               |_______1________|___R_ONLY_S8___|_____________U16______________|_______X_______|  */
    /*      JFWDEQ              |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JFWDNEQ             |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    WOORT_OPCODE_JBCKCND,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      JBCKNZ              |_______0________|___R_ONLY_S8___|_____________U16______________|_______X_______|  */
    /*      JBCKZ               |_______1________|___R_ONLY_S8___|_____________U16______________|_______X_______|  */
    /*      JBCKEQ              |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JBCKNEQ             |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    WOORT_OPCODE_JFDCMP,        /*_____MODE______________________________________________________|_______X_______|  */
    /*      JFWDLT              |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JFWDGT              |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JFWDEL              |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JFWDEG              |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    WOORT_OPCODE_JBCKCMP,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      JBCKLT              |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JBCKGT              |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JBCKEL              |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    /*      JBCKEG              |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|______U8______|_______X_______|  */
    WOORT_OPCODE_CONS,          /*_____MODE______________________________________________________|_______X_______|  */
    /*      MKVEC               |_______0________|_______N8______|__________W_ONLY_S16___________|_______X_______|  */
    /*      MKMAP               |_______1________|_______N8______|__________W_ONLY_S16___________|_______X_______|  */
    /*      MKSTRUCT            |_______2________|_______N8______|__________W_ONLY_S16___________|_______X_______|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    WOORT_OPCODE_CONSEX,        /*_____MODE______________________________________________________|_______X_______|  */
    /*      MKVECEXT            |_______0________|_______________|__________W_ONLY_S16___________|______N32______|  */
    /*      MKMAPEXT            |_______1________|_______________|__________W_ONLY_S16___________|______N32______|  */
    /*      MKSTRUCTEXT         |_______2________|_______________|__________W_ONLY_S16___________|______N32______|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    /**/ WOORT_OPCODE_MKCLOSURE, /*______________N10______________|__________W_ONLY_S16___________|__R_ONLY_C32___|  */
    WOORT_OPCODE_DYN,           /*_____MODE______________________________________________________|_______X_______|  */
    /*      BOXDYN              |_______0________|______T8_______|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      UNBOXDYN            |_______1________|______T8_______|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      CHECKDYN            |_______2________|______T8_______|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      PUSHBOXDYN          |_______3________|______T8_______|__________R_ONLY_S16___________|_______X_______|  */
    WOORT_OPCODE_OPIASMD,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      ADDI                |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      SUBI                |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      MULI                |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      DIVI                |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPIONLG,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      MODI                |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      NEGI                |_______1________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      LTI                 |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      GTI                 |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPISREN,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      LEI                 |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      GEI                 |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      EQI                 |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      NEI                 |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPRASMD,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      ADDR                |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      SUBR                |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      MULR                |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      DIVR                |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPRONLG,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      MODR                |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      NEGR                |_______1________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      LTR                 |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      GTR                 |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPRSREN,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      LER                 |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      GER                 |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      EQR                 |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      NER                 |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPSALGS,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      ADDS                |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LTS                 |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      GTS                 |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LES                 |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_OPSREN,        /*_____MODE______________________________________________________|_______X_______|  */
    /*      GES                 |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      EQS                 |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      NES                 |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    WOORT_OPCODE_OPLAONI,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      LAND                |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LOR                 |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LNOT                |_______2________|___R_ONLY_S8___|__________W_ONLY_S16___________|_______X_______|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    WOORT_OPCODE_OPCIASMD,      /*_____MODE______________________________________________________|_______X_______|  */
    /*      CADDI               |_______0________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CSUBI               |_______1________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CMULI               |_______2________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CDIVI               |_______3________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    WOORT_OPCODE_OPCRASMD,      /*_____MODE______________________________________________________|_______X_______|  */
    /*      CADDR               |_______0________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CSUBR               |_______1________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CMULR               |_______2________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CDIVR               |_______3________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    WOORT_OPCODE_OPCSAIOO,      /*_____MODE______________________________________________________|_______X_______|  */
    /*      CADDS               |_______0________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CVADDS              |_______1________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CMODI               |_______2________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CMODR               |_______3________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    WOORT_OPCODE_OPCLAON,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      CLAND               |_______0________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CLOR                |_______1________|___R_ONLY_S8___|____________R_W_S16____________|_______X_______|  */
    /*      CLNOT               |_______2________|_______________|____________R_W_S16____________|_______X_______|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    WOORT_OPCODE_LDIDX,         /*_____MODE______________________________________________________|_______X_______|  */
    /*      LDIDXVEC            |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXVECX           |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDSTRUCT          |_______2________|_______N8______|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDSTRING          |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_LDIDXDICT,     /*_____MODE______________________________________________________|_______X_______|  */
    /*      LDIDXDICTI          |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXDICTR          |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXDICTB          |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXDICTX          |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_LDIDXDICTX,    /*_____MODE______________________________________________________|_______X_______|  */
    /*      LDIDXDICTIX         |_______0________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXDICTRX         |_______1________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXDICTBX         |_______2________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    /*      LDIDXDICTXX         |_______3________|___R_ONLY_S8___|___R_ONLY_S8___|___W_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_LDIDXEX,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      LDIDXVECEXT         |_______0________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXVECXEXT        |_______1________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDSTRUCTEXT       |_______2________|_______________________N24_____________________|_R_S16_|_W_S16_|  */
    /*      LDIDSTRINGEXT       |_______3________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    WOORT_OPCODE_LDIDXDICTEX,   /*_____MODE______________________________________________________|_______X_______|  */
    /*      LDIDXDICTIEXT       |_______0________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXDICTREXT       |_______1________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXDICTBEXT       |_______2________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXDICTXEXT       |_______3________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    WOORT_OPCODE_LDIDXDICTEXX,  /*_____MODE______________________________________________________|_______X_______|  */
    /*      LDIDXDICTIXEXT       |_______0________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXDICTRXEXT       |_______1________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXDICTBXEXT       |_______2________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    /*      LDIDXDICTXXEXT       |_______3________|_______________|__________R_ONLY_S16___________|_R_S16_|_W_S16_|  */
    WOORT_OPCODE_STIDXVEC,      /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXVECI           |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXVECR           |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXVECB           |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXVECX           |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXDICTI,    /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXDICTII         |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTIR         |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTIB         |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTIX         |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXDICTR,    /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXDICTRI         |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTRR         |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTRB         |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTRX         |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXDICTB,    /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXDICTBI         |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTBR         |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTBB         |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTBX         |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXDICTX,    /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXDICTXI         |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTXR         |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTXB         |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXDICTXX         |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXMAPI,     /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXMAPII          |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPIR          |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPIB          |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPIX          |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXMAPR,     /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXMAPRI          |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPRR          |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPRB          |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPRX          |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXMAPB,     /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXMAPBI          |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPBR          |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPBB          |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPBX          |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXMAPX,     /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXMAPXI          |_______0________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPXR          |_______1________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPXB          |_______2________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /*      STIDXMAPXX          |_______3________|_____R_M_S8____|___R_ONLY_S8___|___R_ONLY_S8___|_______X_______|  */
    /**/WOORT_OPCODE_STIDSTRUCT,/*______________N10______________|_____R_M_S8____|___R_ONLY_S8___|_______X_______|  */
    WOORT_OPCODE_STIDXEX,       /*_____MODE______________________________________________________|_______X_______|  */
    /*      STIDXVECEXT         |_______0________|______T8_______|__________R_ONLY_S16___________|_RMS16_|_R_S16_|  */
    /*      STIDXDICTEXT        |_______1________|___T4__|__T4___|__________R_ONLY_S16___________|_RMS16_|_R_S16_|  */
    /*      STIDXMAPEXT         |_______2________|___T4__|__T4___|__________R_ONLY_S16___________|_RMS16_|_R_S16_|  */
    /*      STIDSTRUCTEXT       |_______3________|_______________________N24_____________________|_RMS16_|_R_S16_|  */
    WOORT_OPCODE_UNPACK,        /*_____MODE______________________________________________________|_______X_______|  */
    /*      UNPACKSTRUCT        |_______0________|_______________|__________R_ONLY_S16___________|_______X_______|  */
    /*      UNPACKVEC           |_______1________|_______________|__________R_ONLY_S16___________|_______X_______|  */
    /*      UNPACKVECX          |_______2________|_______________|__________R_ONLY_S16___________|_______X_______|  */
    /*      <RESERVED>          |_______3________|_______________|_______________|_______________|_______________|  */
    WOORT_OPCODE_PUSHIDXSTBOX,  /*_____MODE______________________________________________________|_______X_______|  */
    /*      PUSHIDXSTBOXI       |_______0________|_______N8______|__________R_ONLY_S16___________|_______X_______|  */
    /*      PUSHIDXSTBOXR       |_______1________|_______N8______|__________R_ONLY_S16___________|_______X_______|  */
    /*      PUSHIDXSTBOXB       |_______2________|_______N8______|__________R_ONLY_S16___________|_______X_______|  */
    /*      PUSHIDXSTBOXX       |_______3________|_______N8______|__________R_ONLY_S16___________|_______X_______|  */
    /**/WOORT_OPCODE_PACKARG,   /*______________N10______________|__________W_ONLY_S16___________|_______X_______|  */

} woort_Opcode;

/*
 * 指令帮助宏
 */

/*
 * NOP - 无操作
 */
#define woort_OpCode_NOP()          \
    woort_OpCodeFormal_cons(OP6, WOORT_OPCODE_NOP)

/*
 * LOAD - 加载常量到栈
 * LOAD [SB + c8] = G[mab18]
 */
#define woort_OpCode_LOAD(mab18, c8)    \
    woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, mab18, c8)

/*
 * STORE - 存储栈值到常量
 * STORE G[mab18] = [SB + c8]
 */
#define woort_OpCode_STORE(mab18, c8)   \
    woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_STORE, mab18, c8)

/*
 * LOADEX - 扩展加载
 * LOADEX [SB + bc16] = G[c32]
 */
#define woort_OpCode_LOADEX(bc16)       \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LOADEX, 0, bc16)

/*
 * STOREEX - 扩展存储
 * STOREEX G[c32] = [SB + bc16]
 */
#define woort_OpCode_STOREEX(bc16)      \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_STOREEX, 0, bc16)

/*
 * MOV - 移动
 * MOVLD (mode=0): [SB + a8] = [SB + bc16]
 * MOVST (mode=1): [SB + bc16] = [SB + a8]
 */
#define woort_OpCode_MOVLD(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_MOV, 0, a8, bc16)
#define woort_OpCode_MOVST(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_MOV, 1, a8, bc16)

/*
 * PUSHCHK - 栈检查/预留
 * PUSHRCHK (mode=0): 预留 n24 个栈槽
 * PUSHSCHK (mode=1): 检查栈 [SB + bc16]
 * PUSHCCHK (mode=2): 检查常量 G[abc24]
 */
#define woort_OpCode_PUSHRCHK(n24)      \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 0, n24)
#define woort_OpCode_PUSHSCHK(bc16)     \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_PUSHCHK, 1, bc16)
#define woort_OpCode_PUSHCCHK(abc24)    \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 2, abc24)

/*
 * PUSH - 栈扩展
 * ASSURESSZ (mode=0): 确保栈大小 n24
 * PUSHS (mode=1): 压入 [SB + bc16]
 * PUSHC (mode=2): 压入常量 G[abc24]
 */
#define woort_OpCode_ASSURESSZ(n24)     \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSH, 0, n24)
#define woort_OpCode_PUSHS(bc16)        \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_PUSH, 1, bc16)
#define woort_OpCode_PUSHC(abc24)       \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSH, 2, abc24)

/*
 * POP - 弹栈
 * POPR (mode=0): 弹出 n24 个栈槽
 * POPS (mode=1): 弹出到 [SB + bc16]
 * POPC (mode=2): 弹出到常量 G[abc24]
 */
#define woort_OpCode_POPR(n24)          \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, n24)
#define woort_OpCode_POPS(bc16)         \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_POP, 1, bc16)
#define woort_OpCode_POPC(abc24)        \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 2, abc24)

/*
 * CASTI - 整数类型转换
 * ITORST (mode=0): [SB + a8] -> [SB + bc16] (real)
 * ITORLD (mode=1): [SB + bc16] -> [SB + a8] (real)
 * ITOSST (mode=2): [SB + a8] -> [SB + bc16] (string)
 * ITOSLD (mode=3): [SB + bc16] -> [SB + a8] (string)
 */
#define woort_OpCode_ITORST(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 0, a8, bc16)
#define woort_OpCode_ITORLD(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 1, a8, bc16)
#define woort_OpCode_ITOSST(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 2, a8, bc16)
#define woort_OpCode_ITOSLD(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 3, a8, bc16)

/*
 * CASTR - 实数类型转换
 * RTOIST (mode=0): [SB + a8] -> [SB + bc16] (int)
 * RTOILD (mode=1): [SB + bc16] -> [SB + a8] (int)
 * RTOSST (mode=2): [SB + a8] -> [SB + bc16] (string)
 * RTOSLD (mode=3): [SB + bc16] -> [SB + a8] (string)
 */
#define woort_OpCode_RTOIST(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 0, a8, bc16)
#define woort_OpCode_RTOILD(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 1, a8, bc16)
#define woort_OpCode_RTOSST(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 2, a8, bc16)
#define woort_OpCode_RTOSLD(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 3, a8, bc16)

/*
 * CASTS - 字符串类型转换
 * STOIST (mode=0): [SB + a8] -> [SB + bc16] (int)
 * STOILD (mode=1): [SB + bc16] -> [SB + a8] (int)
 * STORST (mode=2): [SB + a8] -> [SB + bc16] (real)
 * STORLD (mode=3): [SB + bc16] -> [SB + a8] (real)
 */
#define woort_OpCode_STOIST(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTS, 0, a8, bc16)
#define woort_OpCode_STOILD(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTS, 1, a8, bc16)
#define woort_OpCode_STORST(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTS, 2, a8, bc16)
#define woort_OpCode_STORLD(a8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTS, 3, a8, bc16)

/*
 * CALLNWO/CALLNFP/CALLNJIT - 函数调用
 * 调用 G[u26] 处的函数
 */
#define woort_OpCode_CALLNWO(u26)       \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNWO, u26)
#define woort_OpCode_CALLNFP(u26)       \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, u26)
#define woort_OpCode_CALLNJIT(u26)      \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNJIT, u26)

/*
 * CALL - 间接调用
 * CALLS (mode=0): 调用 [SB + bc16]
 * CALLC (mode=1): 调用 G[abc24]
 */
#define woort_OpCode_CALLS(bc16)        \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CALL, 0, bc16)
#define woort_OpCode_CALLC(abc24)       \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_CALL, 1, abc24)

/*
 * RET - 返回
 * RET (mode=0): 无返回值返回
 * RETVS (mode=1): 返回 [SB + bc16]
 * RETVC (mode=2): 返回 G[abc24]
 * POPRS (mode=3): 从 [SB + bc16] 读取整数值作为数量N，弹出N个值
 */
#define woort_OpCode_RET()              \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0)
#define woort_OpCode_RETVS(bc16)        \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_RET, 1, bc16)
#define woort_OpCode_RETVC(abc24)       \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_RET, 2, abc24)
#define woort_OpCode_POPRS(bc16)        \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_RET, 3, bc16)

/*
 * RESULT - 获取调用结果
 * RESULT [SB + bc16], POP n10
 */
#define woort_OpCode_RESULT(n10, bc16)  \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_RESULT, n10, bc16)

/*
 * JFWD/JBCK - 无条件跳转
 * JFWD: 向前跳转 u26
 * JBCK: 向后跳转 u26
 */
#define woort_OpCode_JFWD(u26)          \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JFWD, u26)
#define woort_OpCode_JBCK(u26)          \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JBCK, u26)

/*
 * JFWDCND - 条件前跳
 * JFWDNZ (mode=0): if [SB + a8] != 0, jump u16
 * JFWDZ  (mode=1): if [SB + a8] == 0, jump u16
 * JFWDEQ (mode=2): if [SB + a8] == [SB + b8], jump c8
 * JFWDNEQ(mode=3): if [SB + a8] != [SB + b8], jump c8
 */
#define woort_OpCode_JFWDNZ(a8, u16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JFWDCND, 0, a8, u16)
#define woort_OpCode_JFWDZ(a8, u16)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JFWDCND, 1, a8, u16)
#define woort_OpCode_JFWDEQ(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFWDCND, 2, a8, b8, c8)
#define woort_OpCode_JFWDNEQ(a8, b8, c8)\
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFWDCND, 3, a8, b8, c8)

/*
 * JBCKCND - 条件后跳
 * JBCKNZ (mode=0): if [SB + a8] != 0, jump u16
 * JBCKZ  (mode=1): if [SB + a8] == 0, jump u16
 * JBCKEQ (mode=2): if [SB + a8] == [SB + b8], jump c8
 * JBCKNEQ(mode=3): if [SB + a8] != [SB + b8], jump c8
 */
#define woort_OpCode_JBCKNZ(a8, u16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JBCKCND, 0, a8, u16)
#define woort_OpCode_JBCKZ(a8, u16)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JBCKCND, 1, a8, u16)
#define woort_OpCode_JBCKEQ(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCND, 2, a8, b8, c8)
#define woort_OpCode_JBCKNEQ(a8, b8, c8)\
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCND, 3, a8, b8, c8)

/*
 * JFDCMP - 比较前跳
 * JFWDLT (mode=0): if [SB + a8] <  [SB + b8], jump c8
 * JFWDGT (mode=1): if [SB + a8] >  [SB + b8], jump c8
 * JFWDEL (mode=2): if [SB + a8] <= [SB + b8], jump c8
 * JFWDEG (mode=3): if [SB + a8] >= [SB + b8], jump c8
 */
#define woort_OpCode_JFWDLT(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFDCMP, 0, a8, b8, c8)
#define woort_OpCode_JFWDGT(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFDCMP, 1, a8, b8, c8)
#define woort_OpCode_JFWDEL(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFDCMP, 2, a8, b8, c8)
#define woort_OpCode_JFWDEG(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFDCMP, 3, a8, b8, c8)

/*
 * JBCKCMP - 比较后跳
 * JBCKLT (mode=0): if [SB + a8] <  [SB + b8], jump c8
 * JBCKGT (mode=1): if [SB + a8] >  [SB + b8], jump c8
 * JBCKEL (mode=2): if [SB + a8] <= [SB + b8], jump c8
 * JBCKEG (mode=3): if [SB + a8] >= [SB + b8], jump c8
 */
#define woort_OpCode_JBCKLT(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCMP, 0, a8, b8, c8)
#define woort_OpCode_JBCKGT(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCMP, 1, a8, b8, c8)
#define woort_OpCode_JBCKEL(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCMP, 2, a8, b8, c8)
#define woort_OpCode_JBCKEG(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCMP, 3, a8, b8, c8)

/*
 * CONS - 容器构造
 * MKVEC    (mode=0): 构造向量，n8 个元素 -> [SB + bc16]
 * MKMAP    (mode=1): 构造字典，n8 个键值对 -> [SB + bc16]
 * MKSTRUCT (mode=2): 构造结构体，n8 个字段 -> [SB + bc16]
 */
#define woort_OpCode_MKVEC(n8, bc16)    \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_CONS, ((n8) << 8) | 0, bc16)
#define woort_OpCode_MKMAP(n8, bc16)    \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_CONS, ((n8) << 8) | 1, bc16)
#define woort_OpCode_MKSTRUCT(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_CONS, ((n8) << 8) | 2, bc16)

/*
 * MKCLOSURE - 闭包创建
 * 创建闭包，捕获 n10 个值，函数在 G[c32]
 */
#define woort_OpCode_MKCLOSURE(n10, c32) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_MKCLOSURE, n10, c32)

/*
 * DYN - 动态类型操作
 * BOXDYN    (mode=0): 装箱 [SB + b8] -> [SB + c8]，类型 t8
 * UNBOXDYN  (mode=1): 拆箱 [SB + b8] -> [SB + c8]，类型 t8
 * CHECKDYN  (mode=2): 检查 [SB + b8] 类型为 t8，结果存 [SB + c8]
 * PUSHBOXDYN(mode=3): 装箱 [SB + bc16] 并压栈，类型 t8
 */
#define woort_OpCode_BOXDYN(t8, b8, c8)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 0, t8, b8, c8)
#define woort_OpCode_UNBOXDYN(t8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 1, t8, b8, c8)
#define woort_OpCode_CHECKDYN(t8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 2, t8, b8, c8)
#define woort_OpCode_PUSHBOXDYN(t8, bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_DYN, 3, t8, bc16)

/*
 * OPIASMD - 整数算术运算
 * ADDI (mode=0): [SB + a8] + [SB + b8] -> [SB + c8]
 * SUBI (mode=1): [SB + a8] - [SB + b8] -> [SB + c8]
 * MULI (mode=2): [SB + a8] * [SB + b8] -> [SB + c8]
 * DIVI (mode=3): [SB + a8] / [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_ADDI(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 0, a8, b8, c8)
#define woort_OpCode_SUBI(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 1, a8, b8, c8)
#define woort_OpCode_MULI(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 2, a8, b8, c8)
#define woort_OpCode_DIVI(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 3, a8, b8, c8)

/*
 * OPIONLG - 整数其他运算
 * MODI (mode=0): [SB + a8] % [SB + b8] -> [SB + c8]
 * NEGI (mode=1): -[SB + a8] -> [SB + bc16]
 * LTI  (mode=2): [SB + a8] < [SB + b8] -> [SB + c8]
 * GTI  (mode=3): [SB + a8] > [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_MODI(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 0, a8, b8, c8)
#define woort_OpCode_NEGI(a8, bc16)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPIONLG, 1, a8, bc16)
#define woort_OpCode_LTI(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 2, a8, b8, c8)
#define woort_OpCode_GTI(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 3, a8, b8, c8)

/*
 * OPISREN - 整数比较运算
 * LEI (mode=0): [SB + a8] <= [SB + b8] -> [SB + c8]
 * GEI (mode=1): [SB + a8] >= [SB + b8] -> [SB + c8]
 * EQI (mode=2): [SB + a8] == [SB + b8] -> [SB + c8]
 * NEI (mode=3): [SB + a8] != [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_LEI(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 0, a8, b8, c8)
#define woort_OpCode_GEI(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 1, a8, b8, c8)
#define woort_OpCode_EQI(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 2, a8, b8, c8)
#define woort_OpCode_NEI(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 3, a8, b8, c8)

/*
 * OPRASMD - 实数算术运算
 * ADDR (mode=0): [SB + a8] + [SB + b8] -> [SB + c8]
 * SUBR (mode=1): [SB + a8] - [SB + b8] -> [SB + c8]
 * MULR (mode=2): [SB + a8] * [SB + b8] -> [SB + c8]
 * DIVR (mode=3): [SB + a8] / [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_ADDR(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 0, a8, b8, c8)
#define woort_OpCode_SUBR(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 1, a8, b8, c8)
#define woort_OpCode_MULR(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 2, a8, b8, c8)
#define woort_OpCode_DIVR(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 3, a8, b8, c8)

/*
 * OPRONLG - 实数其他运算
 * MODR (mode=0): [SB + a8] % [SB + b8] -> [SB + c8]
 * NEGR (mode=1): -[SB + a8] -> [SB + bc16]
 * LTR  (mode=2): [SB + a8] < [SB + b8] -> [SB + c8]
 * GTR  (mode=3): [SB + a8] > [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_MODR(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 0, a8, b8, c8)
#define woort_OpCode_NEGR(a8, bc16)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPRONLG, 1, a8, bc16)
#define woort_OpCode_LTR(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 2, a8, b8, c8)
#define woort_OpCode_GTR(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 3, a8, b8, c8)

/*
 * OPRSREN - 实数比较运算
 * LER (mode=0): [SB + a8] <= [SB + b8] -> [SB + c8]
 * GER (mode=1): [SB + a8] >= [SB + b8] -> [SB + c8]
 * EQR (mode=2): [SB + a8] == [SB + b8] -> [SB + c8]
 * NER (mode=3): [SB + a8] != [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_LER(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 0, a8, b8, c8)
#define woort_OpCode_GER(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 1, a8, b8, c8)
#define woort_OpCode_EQR(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 2, a8, b8, c8)
#define woort_OpCode_NER(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 3, a8, b8, c8)

/*
 * OPSALGS - 字符串算术和比较
 * ADDS (mode=0): [SB + a8] + [SB + b8] -> [SB + c8] (concat)
 * LTS  (mode=1): [SB + a8] < [SB + b8] -> [SB + c8]
 * GTS  (mode=2): [SB + a8] > [SB + b8] -> [SB + c8]
 * LES  (mode=3): [SB + a8] <= [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_ADDS(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 0, a8, b8, c8)
#define woort_OpCode_LTS(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 1, a8, b8, c8)
#define woort_OpCode_GTS(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 2, a8, b8, c8)
#define woort_OpCode_LES(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 3, a8, b8, c8)

/*
 * OPSREN - 字符串比较（续）
 * GES (mode=0): [SB + a8] >= [SB + b8] -> [SB + c8]
 * EQS (mode=1): [SB + a8] == [SB + b8] -> [SB + c8]
 * NES (mode=2): [SB + a8] != [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_GES(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 0, a8, b8, c8)
#define woort_OpCode_EQS(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 1, a8, b8, c8)
#define woort_OpCode_NES(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 2, a8, b8, c8)

/*
 * OPLAONI - 逻辑运算
 * LAND (mode=0): [SB + a8] && [SB + b8] -> [SB + c8]
 * LOR  (mode=1): [SB + a8] || [SB + b8] -> [SB + c8]
 * LNOT (mode=2): ![SB + a8] -> [SB + bc16]
 */
#define woort_OpCode_LAND(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPLAONI, 0, a8, b8, c8)
#define woort_OpCode_LOR(a8, b8, c8)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPLAONI, 1, a8, b8, c8)
#define woort_OpCode_LNOT(a8, bc16)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPLAONI, 2, a8, bc16)

/*
 * OPCIASMD - 整数复合算术运算
 * CADDI (mode=0): [SB + a8] + [SB + bc16] -> [SB + bc16]
 * CSUBI (mode=1): [SB + a8] - [SB + bc16] -> [SB + bc16]
 * CMULI (mode=2): [SB + a8] * [SB + bc16] -> [SB + bc16]
 * CDIVI (mode=3): [SB + a8] / [SB + bc16] -> [SB + bc16]
 */
#define woort_OpCode_CADDI(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 0, a8, bc16)
#define woort_OpCode_CSUBI(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 1, a8, bc16)
#define woort_OpCode_CMULI(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 2, a8, bc16)
#define woort_OpCode_CDIVI(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 3, a8, bc16)

/*
 * OPCRASMD - 实数复合算术运算
 * CADDR (mode=0): [SB + a8] + [SB + bc16] -> [SB + bc16]
 * CSUBR (mode=1): [SB + a8] - [SB + bc16] -> [SB + bc16]
 * CMULR (mode=2): [SB + a8] * [SB + bc16] -> [SB + bc16]
 * CDIVR (mode=3): [SB + a8] / [SB + bc16] -> [SB + bc16]
 */
#define woort_OpCode_CADDR(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 0, a8, bc16)
#define woort_OpCode_CSUBR(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 1, a8, bc16)
#define woort_OpCode_CMULR(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 2, a8, bc16)
#define woort_OpCode_CDIVR(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 3, a8, bc16)

/*
 * OPCSAIOO - 字符串和模运算复合操作
 * CADDS  (mode=0): [SB + a8] + [SB + bc16] -> [SB + bc16] (concat)
 * CVADDS (mode=1): vec concat (reserved)
 * CMODI  (mode=2): [SB + a8] % [SB + bc16] -> [SB + bc16]
 * CMODR  (mode=3): [SB + a8] % [SB + bc16] -> [SB + bc16]
 */
#define woort_OpCode_CADDS(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 0, a8, bc16)
#define woort_OpCode_CMODI(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 2, a8, bc16)
#define woort_OpCode_CMODR(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 3, a8, bc16)

/*
 * OPCLAON - 复合逻辑运算
 * CLAND (mode=0): [SB + a8] && [SB + bc16] -> [SB + bc16]
 * CLOR  (mode=1): [SB + a8] || [SB + bc16] -> [SB + bc16]
 * CLNOT (mode=2): ![SB + bc16] -> [SB + bc16]
 */
#define woort_OpCode_CLAND(a8, bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCLAON, 0, a8, bc16)
#define woort_OpCode_CLOR(a8, bc16)     \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCLAON, 1, a8, bc16)
#define woort_OpCode_CLNOT(bc16)        \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_OPCLAON, 2, bc16)

/*
 * LDIDX - 索引加载
 * LDIDXVEC    (mode=0): vec[SB + b8] -> [SB + c8], vec in [SB + a8]
 * LDIDXVECX   (mode=1): vec[SB + b8] -> [SB + c8] (dynamic)
 * LDIDSTRUCT  (mode=2): struct.n8 -> [SB + c8], struct in [SB + b8]
 * LDIDSTRING  (mode=3): str[SB + b8] -> [SB + c8], str in [SB + a8]
 */
#define woort_OpCode_LDIDXVEC(a8, b8, c8)   \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 0, a8, b8, c8)
#define woort_OpCode_LDIDXVECX(a8, b8, c8)  \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 1, a8, b8, c8)
#define woort_OpCode_LDIDSTRUCT(n8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 2, n8, b8, c8)
#define woort_OpCode_LDIDSTRING(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 3, a8, b8, c8)

/*
 * LDIDXDICT - 字典索引加载（按键类型）
 * LDIDXDICTI (mode=0): dict[[SB + b8]] -> [SB + c8], int key, dict in [SB + a8]
 * LDIDXDICTR (mode=1): dict[[SB + b8]] -> [SB + c8], real key
 * LDIDXDICTB (mode=2): dict[[SB + b8]] -> [SB + c8], bool key
 * LDIDXDICTX (mode=3): dict[[SB + b8]] -> [SB + c8], dynamic key
 */
#define woort_OpCode_LDIDXDICTI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 0, a8, b8, c8)
#define woort_OpCode_LDIDXDICTR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 1, a8, b8, c8)
#define woort_OpCode_LDIDXDICTB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 2, a8, b8, c8)
#define woort_OpCode_LDIDXDICTX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICT, 3, a8, b8, c8)

/*
 * STIDXVEC - 向量索引存储
 * STIDXVECI (mode=0): vec[[SB + b8]] = [SB + c8], int value
 * STIDXVECR (mode=1): vec[[SB + b8]] = [SB + c8], real value
 * STIDXVECB (mode=2): vec[[SB + b8]] = [SB + c8], bool value
 * STIDXVECX (mode=3): vec[[SB + b8]] = [SB + c8], dynamic value
 */
#define woort_OpCode_STIDXVEC_I(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 0, a8, b8, c8)
#define woort_OpCode_STIDXVEC_R(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 1, a8, b8, c8)
#define woort_OpCode_STIDXVEC_B(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 2, a8, b8, c8)
#define woort_OpCode_STIDXVEC_X(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXVEC, 3, a8, b8, c8)

/*
 * STIDXDICTI - 字典存储（int键）
 * STIDXDICTII/IR/IB/IX: dict[int_key] = int/real/bool/dynamic value
 */
#define woort_OpCode_STIDXDICTII(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 0, a8, b8, c8)
#define woort_OpCode_STIDXDICTIR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 1, a8, b8, c8)
#define woort_OpCode_STIDXDICTIB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 2, a8, b8, c8)
#define woort_OpCode_STIDXDICTIX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTI, 3, a8, b8, c8)

/*
 * STIDXDICTR - 字典存储（real键）
 */
#define woort_OpCode_STIDXDICTRI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 0, a8, b8, c8)
#define woort_OpCode_STIDXDICTRR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 1, a8, b8, c8)
#define woort_OpCode_STIDXDICTRB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 2, a8, b8, c8)
#define woort_OpCode_STIDXDICTRX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTR, 3, a8, b8, c8)

/*
 * STIDXDICTB - 字典存储（bool键）
 */
#define woort_OpCode_STIDXDICTBI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 0, a8, b8, c8)
#define woort_OpCode_STIDXDICTBR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 1, a8, b8, c8)
#define woort_OpCode_STIDXDICTBB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 2, a8, b8, c8)
#define woort_OpCode_STIDXDICTBX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTB, 3, a8, b8, c8)

/*
 * STIDXDICTX - 字典存储（dynamic键）
 */
#define woort_OpCode_STIDXDICTXI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 0, a8, b8, c8)
#define woort_OpCode_STIDXDICTXR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 1, a8, b8, c8)
#define woort_OpCode_STIDXDICTXB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 2, a8, b8, c8)
#define woort_OpCode_STIDXDICTXX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXDICTX, 3, a8, b8, c8)

/*
 * STIDSTRUCT - 结构体字段存储
 * struct.n10 = [SB + b8], struct in [SB + a8]
 */
#define woort_OpCode_STIDSTRUCT(n10, a8, b8) \
    woort_OpCodeFormal_cons(OP6_MA10_B8_C8, WOORT_OPCODE_STIDSTRUCT, n10, a8, b8)

/*
 * LDIDXDICTX - 字典索引加载（带动态值类型，按键类型）
 * LDIDXDICTIX (mode=0): dict[[SB + b8]] -> [SB + c8], int key, dynamic value
 * LDIDXDICTRX (mode=1): dict[[SB + b8]] -> [SB + c8], real key, dynamic value
 * LDIDXDICTBX (mode=2): dict[[SB + b8]] -> [SB + c8], bool key, dynamic value
 * LDIDXDICTXX (mode=3): dict[[SB + b8]] -> [SB + c8], dynamic key, dynamic value
 */
#define woort_OpCode_LDIDXDICTIX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICTX, 0, a8, b8, c8)
#define woort_OpCode_LDIDXDICTRX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICTX, 1, a8, b8, c8)
#define woort_OpCode_LDIDXDICTBX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICTX, 2, a8, b8, c8)
#define woort_OpCode_LDIDXDICTXX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDXDICTX, 3, a8, b8, c8)

/*
 * LDIDXEX - 扩展索引加载
 * LDIDXVECEXT  (mode=0): vec[SB + bc16] -> [SB + c16], vec in [SB + a16]
 * LDIDXVECXEXT (mode=1): vec[SB + bc16] -> [SB + c16] (dynamic)
 * LDIDSTRUCTEXT(mode=2): struct.n24 -> [SB + c16]
 * LDIDSTRINGEXT(mode=3): str[SB + bc16] -> [SB + c16], str in [SB + a16]
 * 注: 这些指令需要扩展格式，使用 32 位扩展字段
 */
#define woort_OpCode_LDIDXVECEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXEX, 0, bc16)
#define woort_OpCode_LDIDXVECXEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXEX, 1, bc16)
#define woort_OpCode_LDIDSTRUCTEXT(n24, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_LDIDXEX, 2, n24)
#define woort_OpCode_LDIDSTRINGEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXEX, 3, bc16)

/*
 * LDIDXDICTEX - 扩展字典索引加载（按键类型）
 */
#define woort_OpCode_LDIDXDICTIEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 0, bc16)
#define woort_OpCode_LDIDXDICTREXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 1, bc16)
#define woort_OpCode_LDIDXDICTBEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 2, bc16)
#define woort_OpCode_LDIDXDICTXEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 3, bc16)

/*
 * LDIDXDICTEXX - 扩展字典索引加载（动态值类型，按键类型）
 */
#define woort_OpCode_LDIDXDICTIXEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 0, bc16)
#define woort_OpCode_LDIDXDICTRXEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 1, bc16)
#define woort_OpCode_LDIDXDICTBXEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 2, bc16)
#define woort_OpCode_LDIDXDICTXXEXT(bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 3, bc16)

/*
 * STIDXMAPI - Map存储（int键）
 * STIDXMAPII/IR/IB/IX: map[int_key] = int/real/bool/dynamic value
 */
#define woort_OpCode_STIDXMAPII(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPI, 0, a8, b8, c8)
#define woort_OpCode_STIDXMAPIR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPI, 1, a8, b8, c8)
#define woort_OpCode_STIDXMAPIB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPI, 2, a8, b8, c8)
#define woort_OpCode_STIDXMAPIX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPI, 3, a8, b8, c8)

/*
 * STIDXMAPR - Map存储（real键）
 */
#define woort_OpCode_STIDXMAPRI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPR, 0, a8, b8, c8)
#define woort_OpCode_STIDXMAPRR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPR, 1, a8, b8, c8)
#define woort_OpCode_STIDXMAPRB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPR, 2, a8, b8, c8)
#define woort_OpCode_STIDXMAPRX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPR, 3, a8, b8, c8)

/*
 * STIDXMAPB - Map存储（bool键）
 */
#define woort_OpCode_STIDXMAPBI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPB, 0, a8, b8, c8)
#define woort_OpCode_STIDXMAPBR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPB, 1, a8, b8, c8)
#define woort_OpCode_STIDXMAPBB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPB, 2, a8, b8, c8)
#define woort_OpCode_STIDXMAPBX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPB, 3, a8, b8, c8)

/*
 * STIDXMAPX - Map存储（dynamic键）
 */
#define woort_OpCode_STIDXMAPXI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPX, 0, a8, b8, c8)
#define woort_OpCode_STIDXMAPXR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPX, 1, a8, b8, c8)
#define woort_OpCode_STIDXMAPXB(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPX, 2, a8, b8, c8)
#define woort_OpCode_STIDXMAPXX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_STIDXMAPX, 3, a8, b8, c8)

/*
 * STIDXEX - 扩展索引存储
 * STIDXVECEXT   (mode=0): vec[t8][SB + bc16] = [SB + c16]
 * STIDXDICTEXT  (mode=1): dict[kt4][vt4][SB + bc16] = [SB + c16]
 * STIDXMAPEXT   (mode=2): map[kt4][vt4][SB + bc16] = [SB + c16]
 * STIDSTRUCTEXT (mode=3): struct.n24 = [SB + c16]
 */
#define woort_OpCode_STIDXVECEXT(t8, bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_STIDXEX, 0, t8, bc16)
#define woort_OpCode_STIDXDICTEXT(kt4, vt4, bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_STIDXEX, 1, ((kt4) << 4) | (vt4), bc16)
#define woort_OpCode_STIDXMAPEXT(kt4, vt4, bc16, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_STIDXEX, 2, ((kt4) << 4) | (vt4), bc16)
#define woort_OpCode_STIDSTRUCTEXT(n24, b16, c16) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_STIDXEX, 3, n24)

/*
 * CONSEX - 扩展容器构造
 * MKVECEXT    (mode=0): 构造向量，n32 个元素 -> [SB + bc16]
 * MKMAPEXT    (mode=1): 构造字典，n32 个键值对 -> [SB + bc16]
 * MKSTRUCTEXT (mode=2): 构造结构体，n32 个字段 -> [SB + bc16]
 */
#define woort_OpCode_MKVECEXT(bc16, n32) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CONSEX, 0, bc16)
#define woort_OpCode_MKMAPEXT(bc16, n32) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CONSEX, 1, bc16)
#define woort_OpCode_MKSTRUCTEXT(bc16, n32) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CONSEX, 2, bc16)

/*
 * PUSHCEXT/POPCEXT/PUSHCCHKEXT - 扩展常量操作
 * PUSHCEXT    : 压入常量 G[c32]
 * POPCEXT     : 弹出到常量 G[c32]
 * PUSHCCHKEXT : 检查常量 G[c32]
 */
#define woort_OpCode_PUSHCEXT(c32) \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_PUSH, 3)
#define woort_OpCode_POPCEXT(c32) \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_POP, 3)
#define woort_OpCode_PUSHCCHKEXT(c32) \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_PUSHCHK, 3)

/*
 * MOVLDEXT/MOVSTEXT - 扩展移动操作
 * MOVLDEXT (mode=2): [SB + bc16] = G[c32]
 * MOVSTEXT (mode=3): G[c32] = [SB + bc16]
 */
#define woort_OpCode_MOVLDEXT(bc16, c32) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_MOV, 2, bc16)
#define woort_OpCode_MOVSTEXT(bc16, c32) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_MOV, 3, bc16)

/*
 * UNPACK - 解包操作
 * UNPACKSTRUCT (mode=0): 解包结构体到 [SB + bc16]
 * UNPACKVEC    (mode=1): 解包向量到 [SB + bc16]
 * UNPACKVECX   (mode=2): 解包向量（动态）到 [SB + bc16]
 */
#define woort_OpCode_UNPACKSTRUCT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_UNPACK, 0, bc16)
#define woort_OpCode_UNPACKVEC(bc16)    \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_UNPACK, 1, bc16)
#define woort_OpCode_UNPACKVECX(bc16)   \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_UNPACK, 2, bc16)

/*
 * PUSHIDXSTBOX - 压入结构体字段引用
 * PUSHIDXSTBOXI/R/B/X: 压入 struct.n8 的引用到栈，类型 int/real/bool/dynamic
 */
#define woort_OpCode_PUSHIDXSTBOXI(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_PUSHIDXSTBOX, ((n8) << 8) | 0, bc16)
#define woort_OpCode_PUSHIDXSTBOXR(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_PUSHIDXSTBOX, ((n8) << 8) | 1, bc16)
#define woort_OpCode_PUSHIDXSTBOXB(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_PUSHIDXSTBOX, ((n8) << 8) | 2, bc16)
#define woort_OpCode_PUSHIDXSTBOXX(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_PUSHIDXSTBOX, ((n8) << 8) | 3, bc16)

/*
 * PACKARG - 打包参数
 * 将 n10 个参数打包到 [SB + bc16]
 */
#define woort_OpCode_PACKARG(n10, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_PACKARG, n10, bc16)
