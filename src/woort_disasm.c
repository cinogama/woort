#include "woort_disasm.h"
#include "woort_opcode_formal.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/* 获取操作码的高 8 位（OP6 + M2） */
static inline uint8_t get_opm8(woort_Bytecode code) {
    return (uint8_t)(code >> 24);
}

/* 获取 OP6 */
static inline uint8_t get_op6(woort_Bytecode code) {
    return (uint8_t)((code >> 26) & 0x3F);
}

/* 获取 M2 */
static inline uint8_t get_m2(woort_Bytecode code) {
    return (uint8_t)((code >> 24) & 0x03);
}

/* 获取 A8 */
static inline uint8_t get_a8(woort_Bytecode code) {
    return (uint8_t)((code >> 16) & 0xFF);
}

/* 获取 B8 */
static inline uint8_t get_b8(woort_Bytecode code) {
    return (uint8_t)((code >> 8) & 0xFF);
}

/* 获取 C8 */
static inline uint8_t get_c8(woort_Bytecode code) {
    return (uint8_t)(code & 0xFF);
}

/* 获取有符号 C8 */
static inline int8_t get_s8(woort_Bytecode code) {
    return (int8_t)(code & 0xFF);
}

/* 获取 BC16 */
static inline uint16_t get_bc16(woort_Bytecode code) {
    return (uint16_t)(code & 0xFFFF);
}

/* 获取有符号 BC16 */
static inline int16_t get_s16(woort_Bytecode code) {
    return (int16_t)(code & 0xFFFF);
}

/* 获取 ABC24 */
static inline uint32_t get_abc24(woort_Bytecode code) {
    return code & 0xFFFFFF;
}

/* 获取 MABC26 */
static inline uint32_t get_mabc26(woort_Bytecode code) {
    return code & 0x3FFFFFF;
}

/* 获取 MA10 */
static inline uint16_t get_ma10(woort_Bytecode code) {
    return (uint16_t)((code >> 16) & 0x3FF);
}

/* 操作数格式化辅助函数 */

static int fmt_s8(char* buf, size_t size, int8_t val) {
    return snprintf(buf, size, "r[%d]", (int)val);
}

static int fmt_s16(char* buf, size_t size, int16_t val) {
    return snprintf(buf, size, "r[%d]", (int)val);
}

static int fmt_c8(char* buf, size_t size, uint8_t val) {
    return snprintf(buf, size, "const[%u]", (unsigned)val);
}

static int fmt_c24(char* buf, size_t size, uint32_t val) {
    return snprintf(buf, size, "const[%u]", (unsigned)val);
}

static int fmt_n8(char* buf, size_t size, uint8_t val) {
    return snprintf(buf, size, "%u", (unsigned)val);
}

static int fmt_n24(char* buf, size_t size, uint32_t val) {
    return snprintf(buf, size, "%u", (unsigned)val);
}

static int fmt_u8(char* buf, size_t size, uint8_t val) {
    return snprintf(buf, size, "+%u", (unsigned)val);
}

static int fmt_u16(char* buf, size_t size, uint16_t val) {
    return snprintf(buf, size, "+%u", (unsigned)val);
}

static int fmt_c26(char* buf, size_t size, uint32_t val) {
    return snprintf(buf, size, "func[%u]", (unsigned)val);
}

static int fmt_addr(char* buf, size_t size, uint32_t offset) {
    return snprintf(buf, size, "-> +%" PRIu32, offset);
}

/* 反汇编 NOP */
static void disasm_nop(woort_Bytecode code, woort_DisasmResult* result) {
    (void)code;
    result->m_mnemonic = "NOP";
    result->m_operand_str[0] = '\0';
    result->m_instruction_size = 1;
}

/* 反汇编 LOAD */
static void disasm_load(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "LOAD";
    int8_t dst = get_s8(code);
    uint32_t src = WOORT_BYTECODE(MAB18, code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = const[%u]", (int)dst, src);
    result->m_instruction_size = 1;
}

/* 反汇编 STORE */
static void disasm_store(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "STORE";
    int8_t src = get_s8(code);
    uint32_t dst = WOORT_BYTECODE(MAB18, code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "const[%u] = r[%d]", dst, (int)src);
    result->m_instruction_size = 1;
}

/* 反汇编 LOADEX */
static void disasm_loadex(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    result->m_mnemonic = "LOADEX";
    int16_t dst = get_s16(code);
    uint32_t src = next[0];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = const[%u]", (int)dst, src);
    result->m_instruction_size = 2;
}

/* 反汇编 STOREEX */
static void disasm_storeex(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    result->m_mnemonic = "STOREEX";
    int16_t src = get_s16(code);
    uint32_t dst = next[0];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "const[%u] = r[%d]", dst, (int)src);
    result->m_instruction_size = 2;
}

/* 反汇编 MOV */
static void disasm_mov(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    switch (m2) {
    case 0: /* MOVLD */
        result->m_mnemonic = "MOVLD";
        {
            int8_t dst = get_s8(code);
            int16_t src = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d]", (int)dst, (int)src);
        }
        result->m_instruction_size = 1;
        break;
    case 1: /* MOVST */
        result->m_mnemonic = "MOVST";
        {
            int8_t src = get_s8(code);
            int16_t dst = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d]", (int)dst, (int)src);
        }
        result->m_instruction_size = 1;
        break;
    case 2: /* MOVLDEXT */
        result->m_mnemonic = "MOVLDEXT";
        {
            int16_t dst = get_s16(code);
            int32_t src = (int32_t)next[0];
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d]", (int)dst, (int)src);
        }
        result->m_instruction_size = 2;
        break;
    case 3: /* MOVSTEXT */
        result->m_mnemonic = "MOVSTEXT";
        {
            int16_t src = get_s16(code);
            int32_t dst = (int32_t)next[0];
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d]", (int)dst, (int)src);
        }
        result->m_instruction_size = 2;
        break;
    }
}

/* 反汇编 PUSHCHK */
static void disasm_pushchk(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    switch (m2) {
    case 0: /* PUSHRCHK */
        result->m_mnemonic = "PUSHRCHK";
        {
            uint32_t n = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "check stack reserve %u", n);
        }
        result->m_instruction_size = 1;
        break;
    case 1: /* PUSHSCHK */
        result->m_mnemonic = "PUSHSCHK";
        {
            int16_t src = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "chkpush r[%d]", (int)src);
        }
        result->m_instruction_size = 1;
        break;
    case 2: /* PUSHCCHK */
        result->m_mnemonic = "PUSHCCHK";
        {
            uint32_t src = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "chkpush const[%u]", src);
        }
        result->m_instruction_size = 1;
        break;
    default:
        result->m_mnemonic = "PUSHCCHKEXT";
        result->m_operand_str[0] = '\0';
        result->m_instruction_size = 2;
        break;
    }
}

/* 反汇编 PUSH */
static void disasm_push(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    switch (m2) {
    case 0: /* ASSURESSZ */
        result->m_mnemonic = "ASSURESSZ";
        {
            uint32_t n = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "ensure stack %u", n);
        }
        result->m_instruction_size = 1;
        break;
    case 1: /* PUSHS */
        result->m_mnemonic = "PUSHS";
        {
            int16_t src = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "push r[%d]", (int)src);
        }
        result->m_instruction_size = 1;
        break;
    case 2: /* PUSHC */
        result->m_mnemonic = "PUSHC";
        {
            uint32_t src = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "push const[%u]", src);
        }
        result->m_instruction_size = 1;
        break;
    case 3: /* PUSHCEXT */
        result->m_mnemonic = "PUSHCEXT";
        {
            uint32_t src = next[0];
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "push const[%u]", src);
        }
        result->m_instruction_size = 2;
        break;
    }
}

/* 反汇编 POP */
static void disasm_pop(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    switch (m2) {
    case 0: /* POPR */
        result->m_mnemonic = "POPR";
        {
            uint32_t n = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "pop %u", n);
        }
        result->m_instruction_size = 1;
        break;
    case 1: /* POPS */
        result->m_mnemonic = "POPS";
        {
            int16_t dst = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "pop -> r[%d]", (int)dst);
        }
        result->m_instruction_size = 1;
        break;
    case 2: /* POPC */
        result->m_mnemonic = "POPC";
        {
            uint32_t dst = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "pop -> const[%u]", dst);
        }
        result->m_instruction_size = 1;
        break;
    case 3: /* POPCEXT */
        result->m_mnemonic = "POPCEXT";
        {
            uint32_t dst = next[0];
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "pop -> const[%u]", dst);
        }
        result->m_instruction_size = 2;
        break;
    }
}

/* 反汇编 CALLNWO */
static void disasm_callnwo(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "CALLNWO";
    uint32_t func = get_mabc26(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "call script_func[%u]", func);
    result->m_instruction_size = 1;
}

/* 反汇编 CALLNFP */
static void disasm_callnfp(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "CALLNFP";
    uint32_t func = get_mabc26(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "call native_func[%u]", func);
    result->m_instruction_size = 1;
}

/* 反汇编 CALLNJIT */
static void disasm_callnjit(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "CALLNJIT";
    uint32_t func = get_mabc26(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "call jit_func[%u]", func);
    result->m_instruction_size = 1;
}

/* 反汇编 CALL */
static void disasm_call(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    switch (m2) {
    case 0: /* CALLS */
        result->m_mnemonic = "CALLS";
        {
            int16_t func = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "call r[%d]", (int)func);
        }
        result->m_instruction_size = 1;
        break;
    case 1: /* CALLC */
        result->m_mnemonic = "CALLC";
        {
            uint32_t func = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "call const[%u]", func);
        }
        result->m_instruction_size = 1;
        break;
    default:
        result->m_mnemonic = "CALL";
        result->m_operand_str[0] = '\0';
        result->m_instruction_size = 1;
        break;
    }
}

/* 反汇编 RET */
static void disasm_ret(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    switch (m2) {
    case 0: /* RET */
        result->m_mnemonic = "RET";
        result->m_operand_str[0] = '\0';
        result->m_instruction_size = 1;
        break;
    case 1: /* RETVS */
        result->m_mnemonic = "RETVS";
        {
            int16_t src = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "return r[%d]", (int)src);
        }
        result->m_instruction_size = 1;
        break;
    case 2: /* RETVC */
        result->m_mnemonic = "RETVC";
        {
            uint32_t src = get_abc24(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "return const[%u]", src);
        }
        result->m_instruction_size = 1;
        break;
    case 3: /* POPRS */
        result->m_mnemonic = "POPRS";
        {
            int16_t src = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "pop r[%d]", (int)src);
        }
        result->m_instruction_size = 1;
        break;
    }
}

/* 反汇编 RESULT */
static void disasm_result(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "RESULT";
    uint16_t n = get_ma10(code);
    int16_t dst = get_s16(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = result[%u]", (int)dst, n);
    result->m_instruction_size = 1;
}

/* 反汇编 JFWD */
static void disasm_jfwd(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "JFWD";
    uint32_t addr = get_mabc26(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "jump -> +%" PRIu32, addr);
    result->m_instruction_size = 1;
}

/* 反汇编 JBCK */
static void disasm_jbck(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "JBCK";
    uint32_t addr = get_mabc26(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "jump -> -%" PRIu32, addr);
    result->m_instruction_size = 1;
}

/* 反汇编 JFWDCND */
static void disasm_jfwdcnd(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    uint16_t bc = get_bc16(code);

    switch (m2) {
    case 0: /* JFWDNZ */
        result->m_mnemonic = "JFWDNZ";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "if r[%d] != 0 jump +%u", (int)a, bc);
        break;
    case 1: /* JFWDZ */
        result->m_mnemonic = "JFWDZ";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "if r[%d] == 0 jump +%u", (int)a, bc);
        break;
    case 2: /* JFWDEQ */
        result->m_mnemonic = "JFWDEQ";
        {
            int8_t b = (int8_t)get_b8(code);
            uint8_t c = get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "if r[%d] == r[%d] jump +%u", (int)a, (int)b, c);
        }
        break;
    case 3: /* JFWDNEQ */
        result->m_mnemonic = "JFWDNEQ";
        {
            int8_t b = (int8_t)get_b8(code);
            uint8_t c = get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "if r[%d] != r[%d] jump +%u", (int)a, (int)b, c);
        }
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编 JBCKCND */
static void disasm_jbckcnd(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    uint16_t bc = get_bc16(code);

    switch (m2) {
    case 0: /* JBCKNZ */
        result->m_mnemonic = "JBCKNZ";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "if r[%d] != 0 jump -%u", (int)a, bc);
        break;
    case 1: /* JBCKZ */
        result->m_mnemonic = "JBCKZ";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "if r[%d] == 0 jump -%u", (int)a, bc);
        break;
    case 2: /* JBCKEQ */
        result->m_mnemonic = "JBCKEQ";
        {
            int8_t b = (int8_t)get_b8(code);
            uint8_t c = get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "if r[%d] == r[%d] jump -%u", (int)a, (int)b, c);
        }
        break;
    case 3: /* JBCKNEQ */
        result->m_mnemonic = "JBCKNEQ";
        {
            int8_t b = (int8_t)get_b8(code);
            uint8_t c = get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "if r[%d] != r[%d] jump -%u", (int)a, (int)b, c);
        }
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编 JFDCMP */
static void disasm_jfdcmp(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    uint8_t c = get_c8(code);

    static const char* cmp_names[] = { "JFWDLT", "JFWDGT", "JFWDEL", "JFWDEG" };
    static const char* cmp_ops[] = { "<", ">", "<=", ">=" };

    result->m_mnemonic = cmp_names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "if r[%d] %s r[%d] jump +%u", (int)a, cmp_ops[m2], (int)b, c);
    result->m_instruction_size = 1;
}

/* 反汇编 JBCKCMP */
static void disasm_jbckcmp(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    uint8_t c = get_c8(code);

    static const char* cmp_names[] = { "JBCKLT", "JBCKGT", "JBCKEL", "JBCKEG" };
    static const char* cmp_ops[] = { "<", ">", "<=", ">=" };

    result->m_mnemonic = cmp_names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "if r[%d] %s r[%d] jump -%u", (int)a, cmp_ops[m2], (int)b, c);
    result->m_instruction_size = 1;
}

/* 反汇编整数算术指令（OPIASMD） */
static void disasm_opiasmd(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "ADDI", "SUBI", "MULI", "DIVI" };
    static const char* ops[] = { "+", "-", "*", "/" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = r[%d] %s r[%d]", (int)c, (int)a, ops[m2], (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编整数算术指令（OPIONLG） */
static void disasm_opionlg(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    switch (m2) {
    case 0: /* MODI */
        result->m_mnemonic = "MODI";
        {
            int8_t b = (int8_t)get_b8(code);
            int8_t c = (int8_t)get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d] %% r[%d]", (int)c, (int)a, (int)b);
        }
        break;
    case 1: /* NEGI */
        result->m_mnemonic = "NEGI";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = -r[%d]", (int)bc, (int)a);
        break;
    case 2: /* LTI */
        result->m_mnemonic = "LTI";
        {
            int8_t b = (int8_t)get_b8(code);
            int8_t c = (int8_t)get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d] < r[%d]", (int)c, (int)a, (int)b);
        }
        break;
    case 3: /* GTI */
        result->m_mnemonic = "GTI";
        {
            int8_t b = (int8_t)get_b8(code);
            int8_t c = (int8_t)get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d] > r[%d]", (int)c, (int)a, (int)b);
        }
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编整数比较指令（OPISREN） */
static void disasm_opisren(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "LEI", "GEI", "EQI", "NEI" };
    static const char* ops[] = { "<=", ">=", "==", "!=" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = r[%d] %s r[%d]", (int)c, (int)a, ops[m2], (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编浮点算术指令（OPRASMD） */
static void disasm_oprasmd(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "ADDR", "SUBR", "MULR", "DIVR" };
    static const char* ops[] = { "+", "-", "*", "/" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = r[%d] %s r[%d]", (int)c, (int)a, ops[m2], (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编浮点算术指令（OPRONLG） */
static void disasm_opronlg(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    switch (m2) {
    case 0: /* MODR */
        result->m_mnemonic = "MODR";
        {
            int8_t b = (int8_t)get_b8(code);
            int8_t c = (int8_t)get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d] %% r[%d]", (int)c, (int)a, (int)b);
        }
        break;
    case 1: /* NEGR */
        result->m_mnemonic = "NEGR";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = -r[%d]", (int)bc, (int)a);
        break;
    case 2: /* LTR */
        result->m_mnemonic = "LTR";
        {
            int8_t b = (int8_t)get_b8(code);
            int8_t c = (int8_t)get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d] < r[%d]", (int)c, (int)a, (int)b);
        }
        break;
    case 3: /* GTR */
        result->m_mnemonic = "GTR";
        {
            int8_t b = (int8_t)get_b8(code);
            int8_t c = (int8_t)get_c8(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "r[%d] = r[%d] > r[%d]", (int)c, (int)a, (int)b);
        }
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编浮点比较指令（OPRSREN） */
static void disasm_oprsren(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "LER", "GER", "EQR", "NER" };
    static const char* ops[] = { "<=", ">=", "==", "!=" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = r[%d] %s r[%d]", (int)c, (int)a, ops[m2], (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编 CONS */
static void disasm_cons(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    uint8_t n = get_a8(code);
    int16_t dst = get_s16(code);

    static const char* names[] = { "MKVEC", "MKMAP", "MKSTRUCT", "CONS?" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = %s(%u)", (int)dst, names[m2], n);
    result->m_instruction_size = 1;
}

/* 反汇编 CONSEX */
static void disasm_consex(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int16_t dst = get_s16(code);
    uint32_t n = next[0];

    static const char* names[] = { "MKVECEXT", "MKMAPEXT", "MKSTRUCTEXT", "CONSEX?" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = %s(%u)", (int)dst, names[m2], n);
    result->m_instruction_size = 2;
}

/* 反汇编 MKCLOSURE */
static void disasm_mkclosure(woort_Bytecode code, const woort_Bytecode* next, woort_DisasmResult* result) {
    result->m_mnemonic = "MKCLOSURE";
    uint16_t n = get_ma10(code);
    int16_t dst = get_s16(code);
    uint32_t func = next[0];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = closure(func[%u], %u)", (int)dst, func, n);
    result->m_instruction_size = 2;
}

/* 反汇编 CASTI */
static void disasm_casti(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    static const char* names[] = { "ITORST", "ITORLD", "ITOSST", "ITOSLD" };
    static const char* from[] = { "int", "int", "int", "int" };
    static const char* to[] = { "real", "real", "string", "string" };

    result->m_mnemonic = names[m2];
    if (m2 == 0 || m2 == 2) {
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = (%s)r[%d]", (int)bc, to[m2], (int)a);
    } else {
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = (%s)r[%d]", (int)a, to[m2], (int)bc);
    }
    result->m_instruction_size = 1;
}

/* 反汇编 CASTR */
static void disasm_castr(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    static const char* names[] = { "RTOIST", "RTOILD", "RTOSST", "RTOSLD" };
    static const char* to[] = { "int", "int", "string", "string" };

    result->m_mnemonic = names[m2];
    if (m2 == 0 || m2 == 2) {
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = (%s)r[%d]", (int)bc, to[m2], (int)a);
    } else {
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = (%s)r[%d]", (int)a, to[m2], (int)bc);
    }
    result->m_instruction_size = 1;
}

/* 反汇编 CASTS */
static void disasm_casts(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    static const char* names[] = { "STOIST", "STOILD", "STORST", "STORLD" };
    static const char* to[] = { "int", "int", "real", "real" };

    result->m_mnemonic = names[m2];
    if (m2 == 0 || m2 == 2) {
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = (%s)r[%d]", (int)bc, to[m2], (int)a);
    } else {
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = (%s)r[%d]", (int)a, to[m2], (int)bc);
    }
    result->m_instruction_size = 1;
}

/* 反汇编 DYN */
static void disasm_dyn(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    uint8_t t = get_a8(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "BOXDYN", "UNBOXDYN", "CHECKDYN", "PUSHBOXDYN" };

    result->m_mnemonic = names[m2];
    switch (m2) {
    case 0:
    case 1:
    case 2:
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "type:%u r[%d] -> r[%d]", t, (int)a, (int)c);
        break;
    case 3:
        {
            int16_t bc = get_s16(code);
            snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                     "type:%u r[%d]", t, (int)bc);
        }
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编逻辑运算指令（OPLAONI） */
static void disasm_oplaoni(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);
    int16_t bc = get_s16(code);

    switch (m2) {
    case 0: /* LAND */
        result->m_mnemonic = "LAND";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = r[%d] && r[%d]", (int)c, (int)a, (int)b);
        break;
    case 1: /* LOR */
        result->m_mnemonic = "LOR";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = r[%d] || r[%d]", (int)c, (int)a, (int)b);
        break;
    case 2: /* LNOT */
        result->m_mnemonic = "LNOT";
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = !r[%d]", (int)bc, (int)a);
        break;
    default:
        result->m_mnemonic = "OPLAONI?";
        result->m_operand_str[0] = '\0';
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编字符串运算指令（OPSALGS） */
static void disasm_opsalgs(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "ADDS", "LTS", "GTS", "LES" };
    static const char* ops[] = { "+", "<", ">", "<=" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = r[%d] %s r[%d]", (int)c, (int)a, ops[m2], (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编字符串比较指令（OPSREN） */
static void disasm_opsren(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "GES", "EQS", "NES", "OPSREN?" };
    static const char* ops[] = { ">=", "==", "!=", "?" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = r[%d] %s r[%d]", (int)c, (int)a, ops[m2], (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编复合整数算术指令（OPCIASMD） */
static void disasm_opciasmd(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    static const char* names[] = { "CADDI", "CSUBI", "CMULI", "CDIVI" };
    static const char* ops[] = { "+=", "-=", "*=", "/=" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] %s r[%d]", (int)bc, ops[m2], (int)a);
    result->m_instruction_size = 1;
}

/* 反汇编复合浮点算术指令（OPCRASMD） */
static void disasm_opcrasmd(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int16_t bc = get_s16(code);

    static const char* names[] = { "CADDR", "CSUBR", "CMULR", "CDIVR" };
    static const char* ops[] = { "+=", "-=", "*=", "/=" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] %s r[%d]", (int)bc, ops[m2], (int)a);
    result->m_instruction_size = 1;
}

/* 反汇编 LDIDX */
static void disasm_ldidx(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);
    uint8_t n = get_a8(code);

    static const char* names[] = { "LDIDXVEC", "LDIDXVECX", "LDIDSTRUCT", "LDIDSTRING" };

    result->m_mnemonic = names[m2];
    switch (m2) {
    case 0:
    case 1:
    case 3:
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = r[%d][r[%d]]", (int)c, (int)a, (int)b);
        break;
    case 2:
        snprintf(result->m_operand_str, sizeof(result->m_operand_str),
                 "r[%d] = r[%d].field[%u]", (int)c, (int)b, n);
        break;
    }
    result->m_instruction_size = 1;
}

/* 反汇编 STIDXVEC */
static void disasm_stidxvec(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    int8_t c = (int8_t)get_c8(code);

    static const char* names[] = { "STIDXVECI", "STIDXVECR", "STIDXVECB", "STIDXVECX" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d][r[%d]] = r[%d]", (int)a, (int)b, (int)c);
    result->m_instruction_size = 1;
}

/* 反汇编 UNPACK */
static void disasm_unpack(woort_Bytecode code, woort_DisasmResult* result) {
    uint8_t m2 = get_m2(code);
    int16_t bc = get_s16(code);

    static const char* names[] = { "UNPACKSTRUCT", "UNPACKVEC", "UNPACKVECX", "UNPACK?" };

    result->m_mnemonic = names[m2];
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "unpack r[%d]", (int)bc);
    result->m_instruction_size = 1;
}

/* 反汇编 STIDSTRUCT */
static void disasm_stidstruct(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "STIDSTRUCT";
    uint16_t n = get_ma10(code);
    int8_t a = get_s8(code);
    int8_t b = (int8_t)get_b8(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d].field[%u] = r[%d]", (int)a, n, (int)b);
    result->m_instruction_size = 1;
}

/* 反汇编 PACKARG */
static void disasm_packarg(woort_Bytecode code, woort_DisasmResult* result) {
    result->m_mnemonic = "PACKARG";
    uint16_t n = get_ma10(code);
    int16_t dst = get_s16(code);
    snprintf(result->m_operand_str, sizeof(result->m_operand_str),
             "r[%d] = pack_args(%u)", (int)dst, n);
    result->m_instruction_size = 1;
}

/* 主反汇编函数 */
void woort_disasm_instruction(
    const woort_Bytecode* code,
    woort_DisasmResult* out_result)
{
    const woort_Bytecode c = code[0];
    const uint8_t op6 = get_op6(c);
    const uint8_t m2 = get_m2(c);

    switch (op6) {
    case WOORT_OPCODE_NOP:
        disasm_nop(c, out_result);
        break;
    case WOORT_OPCODE_LOAD:
        disasm_load(c, out_result);
        break;
    case WOORT_OPCODE_STORE:
        disasm_store(c, out_result);
        break;
    case WOORT_OPCODE_LOADEX:
        disasm_loadex(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_STOREEX:
        disasm_storeex(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_MOV:
        disasm_mov(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_PUSHCHK:
        disasm_pushchk(c, out_result);
        break;
    case WOORT_OPCODE_PUSH:
        disasm_push(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_POP:
        disasm_pop(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_CALLNWO:
        disasm_callnwo(c, out_result);
        break;
    case WOORT_OPCODE_CALLNFP:
        disasm_callnfp(c, out_result);
        break;
    case WOORT_OPCODE_CALLNJIT:
        disasm_callnjit(c, out_result);
        break;
    case WOORT_OPCODE_CALL:
        disasm_call(c, out_result);
        break;
    case WOORT_OPCODE_RET:
        disasm_ret(c, out_result);
        break;
    case WOORT_OPCODE_RESULT:
        disasm_result(c, out_result);
        break;
    case WOORT_OPCODE_JFWD:
        disasm_jfwd(c, out_result);
        break;
    case WOORT_OPCODE_JBCK:
        disasm_jbck(c, out_result);
        break;
    case WOORT_OPCODE_JFWDCND:
        disasm_jfwdcnd(c, out_result);
        break;
    case WOORT_OPCODE_JBCKCND:
        disasm_jbckcnd(c, out_result);
        break;
    case WOORT_OPCODE_JFDCMP:
        disasm_jfdcmp(c, out_result);
        break;
    case WOORT_OPCODE_JBCKCMP:
        disasm_jbckcmp(c, out_result);
        break;
    case WOORT_OPCODE_OPIASMD:
        disasm_opiasmd(c, out_result);
        break;
    case WOORT_OPCODE_OPIONLG:
        disasm_opionlg(c, out_result);
        break;
    case WOORT_OPCODE_OPISREN:
        disasm_opisren(c, out_result);
        break;
    case WOORT_OPCODE_OPRASMD:
        disasm_oprasmd(c, out_result);
        break;
    case WOORT_OPCODE_OPRONLG:
        disasm_opronlg(c, out_result);
        break;
    case WOORT_OPCODE_OPRSREN:
        disasm_oprsren(c, out_result);
        break;
    case WOORT_OPCODE_CONS:
        disasm_cons(c, out_result);
        break;
    case WOORT_OPCODE_CONSEX:
        disasm_consex(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_MKCLOSURE:
        disasm_mkclosure(c, code + 1, out_result);
        break;
    case WOORT_OPCODE_CASTI:
        disasm_casti(c, out_result);
        break;
    case WOORT_OPCODE_CASTR:
        disasm_castr(c, out_result);
        break;
    case WOORT_OPCODE_CASTS:
        disasm_casts(c, out_result);
        break;
    case WOORT_OPCODE_DYN:
        disasm_dyn(c, out_result);
        break;
    case WOORT_OPCODE_OPLAONI:
        disasm_oplaoni(c, out_result);
        break;
    case WOORT_OPCODE_OPSALGS:
        disasm_opsalgs(c, out_result);
        break;
    case WOORT_OPCODE_OPSREN:
        disasm_opsren(c, out_result);
        break;
    case WOORT_OPCODE_OPCIASMD:
        disasm_opciasmd(c, out_result);
        break;
    case WOORT_OPCODE_OPCRASMD:
        disasm_opcrasmd(c, out_result);
        break;
    case WOORT_OPCODE_LDIDX:
        disasm_ldidx(c, out_result);
        break;
    case WOORT_OPCODE_STIDXVEC:
        disasm_stidxvec(c, out_result);
        break;
    case WOORT_OPCODE_STIDSTRUCT:
        disasm_stidstruct(c, out_result);
        break;
    case WOORT_OPCODE_UNPACK:
        disasm_unpack(c, out_result);
        break;
    case WOORT_OPCODE_PACKARG:
        disasm_packarg(c, out_result);
        break;
    default:
        /* 未知操作码 */
        out_result->m_mnemonic = "UNKNOWN";
        snprintf(out_result->m_operand_str, sizeof(out_result->m_operand_str),
                 "op=0x%02X m=0x%02X", op6, m2);
        out_result->m_instruction_size = 1;
        break;
    }
}

/* 反汇编代码块并输出 */
void woort_disasm_dump(
    const woort_Bytecode* code_begin,
    const woort_Bytecode* code_end,
    FILE* out)
{
    const woort_Bytecode* ip = code_begin;
    size_t offset = 0;

    while (ip < code_end) {
        woort_DisasmResult result;
        woort_disasm_instruction(ip, &result);

        fprintf(out, "0x%04zX: %-14s %s\n",
                offset,
                result.m_mnemonic,
                result.m_operand_str);

        ip += result.m_instruction_size;
        offset += result.m_instruction_size;
    }
}