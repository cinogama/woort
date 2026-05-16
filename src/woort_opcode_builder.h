#pragma once

#include "woort_opcode_formal.h"

/*
 * 指令帮助宏
 */

/*
 * NOP - 无操作
 */
#define woort_OpCode_NOP() \
    woort_OpCodeFormal_cons(OP6, WOORT_OPCODE_NOP)

/*
 * LOAD - 加载常量到栈
 * LOAD [SB + c8] = G[mab18]
 */
#define woort_OpCode_LOAD(mab18, c8) \
    woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_LOAD, mab18, c8)

/*
 * STORE - 存储栈值到常量
 * STORE G[mab18] = [SB + c8]
 */
#define woort_OpCode_STORE(mab18, c8) \
    woort_OpCodeFormal_cons(OP6_MAB18_C8, WOORT_OPCODE_STORE, mab18, c8)

/*
 * LOADEX - 扩展加载
 * LOADEX [SB + bc16] = G[ex32]
 */
#define woort_OpCode_LOADEX(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDSTEX, 0, bc16)

/*
 * STOREEX - 扩展存储
 * STOREEX G[ex32] = [SB + bc16]
 */
#define woort_OpCode_STOREEX(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDSTEX, 1, bc16)

/*
 * CHKDIVI - 整数除法检查
 * CHKDIVIL  (mode=0): 检查被除数 [SB + bc16] 是否为 INT64_MIN（溢出）
 * CHKDIVIR  (mode=1): 检查被除数 [SB + bc16] 是否为 0 或 -1
 * CHKDIVIRZ (mode=2): 检查被除数 [SB + bc16] 是否为 0
 * CHKDIVILR (mode=3): 检查除数 [SB + a8] 和被除数 [SB + bc16]
 */
#define woort_OpCode_CHKDIVIL(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CHKDIVI, 0, bc16)
#define woort_OpCode_CHKDIVIR(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CHKDIVI, 1, bc16)
#define woort_OpCode_CHKDIVIRZ(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CHKDIVI, 2, bc16)
#define woort_OpCode_CHKDIVILR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CHKDIVI, 3, a8, bc16)

/*
 * MOV - 移动
 * MOVLD (mode=0): [SB + a8] = [SB + bc16]
 * MOVST (mode=1): [SB + bc16] = [SB + a8]
 */
#define woort_OpCode_MOVLD(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_MOV, 0, a8, bc16)
#define woort_OpCode_MOVST(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_MOV, 1, a8, bc16)

/*
 * PUSHCHK - 栈检查/预留
 * PUSHRCHK (mode=0): 预留 n24 个栈槽
 * PUSHSCHK (mode=1): 检查栈 [SB + bc16]
 * PUSHCCHK (mode=2): 检查常量 G[abc24]
 */
#define woort_OpCode_PUSHRCHK(n24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 0, n24)
#define woort_OpCode_PUSHSCHK(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_PUSHCHK, 1, bc16)
#define woort_OpCode_PUSHCCHK(abc24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSHCHK, 2, abc24)

/*
 * PUSH - 栈扩展
 * ASSURESSZ (mode=0): 确保栈大小 n24
 * PUSHS (mode=1): 压入 [SB + bc16]
 * PUSHC (mode=2): 压入常量 G[abc24]
 */
#define woort_OpCode_ASSURESSZ(n24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSH, 0, n24)
#define woort_OpCode_PUSHS(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_PUSH, 1, bc16)
#define woort_OpCode_PUSHC(abc24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_PUSH, 2, abc24)

/*
 * POP - 弹栈
 * POPR (mode=0): 弹出 n24 个栈槽
 * POPS (mode=1): 弹出到 [SB + bc16]
 * POPC (mode=2): 弹出到常量 G[abc24]
 */
#define woort_OpCode_POPR(n24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 0, n24)
#define woort_OpCode_POPS(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_POP, 1, bc16)
#define woort_OpCode_POPC(abc24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_POP, 2, abc24)

/*
 * CASTI - 整数类型转换
 * ITORST (mode=0): [SB + a8] -> [SB + bc16] (real)
 * ITORLD (mode=1): [SB + bc16] -> [SB + a8] (real)
 * ITOSST (mode=2): [SB + a8] -> [SB + bc16] (string)
 * ITOSLD (mode=3): [SB + bc16] -> [SB + a8] (string)
 */
#define woort_OpCode_ITORST(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 0, a8, bc16)
#define woort_OpCode_ITORLD(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 1, a8, bc16)
#define woort_OpCode_ITOSST(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 2, a8, bc16)
#define woort_OpCode_ITOSLD(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTI, 3, a8, bc16)

/*
 * CASTR - 实数类型转换
 * RTOIST (mode=0): [SB + a8] -> [SB + bc16] (int)
 * RTOILD (mode=1): [SB + bc16] -> [SB + a8] (int)
 * RTOSST (mode=2): [SB + a8] -> [SB + bc16] (string)
 * RTOSLD (mode=3): [SB + bc16] -> [SB + a8] (string)
 */
#define woort_OpCode_RTOIST(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 0, a8, bc16)
#define woort_OpCode_RTOILD(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 1, a8, bc16)
#define woort_OpCode_RTOSST(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 2, a8, bc16)
#define woort_OpCode_RTOSLD(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTR, 3, a8, bc16)

/*
 * CASTS - 类型转换（字符串/BOXED）
 * CASTSTO   (mode=0): 将字符串 [SB + b8] 转换为 T8 类型 -> [SB + c8]
 * CASTSFROM (mode=1): 将 T8 类型值 [SB + b8] 转换为字符串 -> [SB + c8]
 * CASTDYN   (mode=2): 将 BOXED 值 [SB + b8] 转换为 T8 类型 -> [SB + c8]
 * ASSERTDYN (mode=3): 诊断 BOXED 值 [SB + bc16] 是否是 T8 类型，否则 PANIC
 */
#define woort_OpCode_CASTSTO(t8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_CASTX, 0, t8, b8, c8)
#define woort_OpCode_CASTSFROM(t8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_CASTX, 1, t8, b8, c8)
#define woort_OpCode_CASTDYN(t8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_CASTX, 2, t8, b8, c8)
#define woort_OpCode_ASSERTDYN(t8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CASTX, 3, t8, bc16)

/*
 * CALLNWO/CALLNFP/CALLNJIT - 函数调用
 * 调用 G[u26] 处的函数
 */
#define woort_OpCode_CALLNWO(u26) \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNWO, u26)
#define woort_OpCode_CALLNFP(u26) \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNFP, u26)
#define woort_OpCode_CALLNJIT(u26) \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_CALLNJIT, u26)

/*
 * CALL - 间接调用
 * CALLS (mode=0): 调用 [SB + bc16]
 * CALLC (mode=1): 调用 G[abc24]
 */
#define woort_OpCode_CALLS(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CALL, 0, bc16)
#define woort_OpCode_CALLC(abc24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_CALL, 1, abc24)

/*
 * RET - 返回
 * RET (mode=0): 无返回值返回
 * RETVS (mode=1): 返回 [SB + bc16]
 * RETVC (mode=2): 返回 G[abc24]
 * POPRS (mode=3): 从 [SB + bc16] 读取整数值作为数量N，弹出N个值
 */
#define woort_OpCode_RET() \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_RET, 0)
#define woort_OpCode_RETVS(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_RET, 1, bc16)
#define woort_OpCode_RETVC(abc24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_RET, 2, abc24)
#define woort_OpCode_POPRS(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_RET, 3, bc16)

/*
 * RESULT - 获取调用结果
 * RESULT [SB + bc16], POP n10
 */
#define woort_OpCode_RESULT(n10, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_RESULT, n10, bc16)

/*
 * JFWD/JBCK - 无条件跳转
 * JFWD: 绝对地址跳转到 u26 位置
 * JBCK: 与 JFWD 相同，但是有额外的GC检查点
 */
#define woort_OpCode_JFWD(u26) \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JFWD, u26)
#define woort_OpCode_JBCK(u26) \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JBCK, u26)

/*
 * JFWDCND - 条件前跳
 * JFWDNZ (mode=0): if [SB + a8] != 0, jump u16
 * JFWDZ  (mode=1): if [SB + a8] == 0, jump u16
 * JFWDEQ (mode=2): if [SB + a8] == [SB + b8], jump c8
 * JFWDNEQ(mode=3): if [SB + a8] != [SB + b8], jump c8
 */
#define woort_OpCode_JFWDNZ(a8, u16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JFWDCND, 0, a8, u16)
#define woort_OpCode_JFWDZ(a8, u16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JFWDCND, 1, a8, u16)
#define woort_OpCode_JFWDEQ(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFWDCND, 2, a8, b8, c8)
#define woort_OpCode_JFWDNEQ(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JFWDCND, 3, a8, b8, c8)

/*
 * JBCKCND - 条件后跳
 * JBCKNZ (mode=0): if [SB + a8] != 0, jump u16
 * JBCKZ  (mode=1): if [SB + a8] == 0, jump u16
 * JBCKEQ (mode=2): if [SB + a8] == [SB + b8], jump c8
 * JBCKNEQ(mode=3): if [SB + a8] != [SB + b8], jump c8
 */
#define woort_OpCode_JBCKNZ(a8, u16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JBCKCND, 0, a8, u16)
#define woort_OpCode_JBCKZ(a8, u16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_JBCKCND, 1, a8, u16)
#define woort_OpCode_JBCKEQ(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_JBCKCND, 2, a8, b8, c8)
#define woort_OpCode_JBCKNEQ(a8, b8, c8) \
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
 * MKUNION  (mode=3): 构造结构体，但是是为 Union 特化的处理，((woort_Int)n8, [SB + b8]) -> [SB + c8]
 */
#define woort_OpCode_MKVEC(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONS, 0, n8, bc16)
#define woort_OpCode_MKMAP(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONS, 1, n8, bc16)
#define woort_OpCode_MKSTRUCT(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONS, 2, n8, bc16)
#define woort_OpCode_MKUNION(n8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_CONS, 3, n8, b8, c8)

/*
 * MKCLOSURE - 闭包创建
 * 创建闭包，捕获 n10 个值，函数在 G[ex32]
 */
#define woort_OpCode_MKCLOSURE(n10, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_MKCLOSURE, n10, bc16)

/*
 * DYN - 动态类型操作
 * BOXDYN    (mode=0): 装箱 [SB + b8] -> [SB + c8]，类型 t8
 * UNBOXDYN  (mode=1): 拆箱 [SB + b8] -> [SB + c8]，类型 t8
 * CHECKDYN  (mode=2): 检查 [SB + b8] 类型为 t8，结果存 [SB + c8]
 * PUSHBOXDYN(mode=3): 装箱 [SB + bc16] 并压栈，类型 t8
 */
#define woort_OpCode_BOXDYN(t8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 0, t8, b8, c8)
#define woort_OpCode_UNBOXDYN(t8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 1, t8, b8, c8)
#define woort_OpCode_CHECKDYN(t8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_DYN, 2, t8, b8, c8)
#define woort_OpCode_PUSHBOXDYN(t8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_DYN, 3, t8, bc16)

/*
 * OPIASMD - 整数算术运算
 * ADDI (mode=0): [SB + a8] + [SB + b8] -> [SB + c8]
 * SUBI (mode=1): [SB + a8] - [SB + b8] -> [SB + c8]
 * MULI (mode=2): [SB + a8] * [SB + b8] -> [SB + c8]
 * DIVI (mode=3): [SB + a8] / [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_ADDI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 0, a8, b8, c8)
#define woort_OpCode_SUBI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 1, a8, b8, c8)
#define woort_OpCode_MULI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 2, a8, b8, c8)
#define woort_OpCode_DIVI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIASMD, 3, a8, b8, c8)

/*
 * OPIONLG - 整数其他运算
 * MODI (mode=0): [SB + a8] % [SB + b8] -> [SB + c8]
 * NEGI (mode=1): -[SB + a8] -> [SB + bc16]
 * LTI  (mode=2): [SB + a8] < [SB + b8] -> [SB + c8]
 * GTI  (mode=3): [SB + a8] > [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_MODI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 0, a8, b8, c8)
#define woort_OpCode_NEGI(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPIONLG, 1, a8, bc16)
#define woort_OpCode_LTI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 2, a8, b8, c8)
#define woort_OpCode_GTI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPIONLG, 3, a8, b8, c8)

/*
 * OPISREN - 整数比较运算
 * LEI (mode=0): [SB + a8] <= [SB + b8] -> [SB + c8]
 * GEI (mode=1): [SB + a8] >= [SB + b8] -> [SB + c8]
 * EQI (mode=2): [SB + a8] == [SB + b8] -> [SB + c8]
 * NEI (mode=3): [SB + a8] != [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_LEI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 0, a8, b8, c8)
#define woort_OpCode_GEI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 1, a8, b8, c8)
#define woort_OpCode_EQI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 2, a8, b8, c8)
#define woort_OpCode_NEI(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPISREN, 3, a8, b8, c8)

/*
 * OPRASMD - 实数算术运算
 * ADDR (mode=0): [SB + a8] + [SB + b8] -> [SB + c8]
 * SUBR (mode=1): [SB + a8] - [SB + b8] -> [SB + c8]
 * MULR (mode=2): [SB + a8] * [SB + b8] -> [SB + c8]
 * DIVR (mode=3): [SB + a8] / [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_ADDR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 0, a8, b8, c8)
#define woort_OpCode_SUBR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 1, a8, b8, c8)
#define woort_OpCode_MULR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 2, a8, b8, c8)
#define woort_OpCode_DIVR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRASMD, 3, a8, b8, c8)

/*
 * OPRONLG - 实数其他运算
 * MODR (mode=0): [SB + a8] % [SB + b8] -> [SB + c8]
 * NEGR (mode=1): -[SB + a8] -> [SB + bc16]
 * LTR  (mode=2): [SB + a8] < [SB + b8] -> [SB + c8]
 * GTR  (mode=3): [SB + a8] > [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_MODR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 0, a8, b8, c8)
#define woort_OpCode_NEGR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPRONLG, 1, a8, bc16)
#define woort_OpCode_LTR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 2, a8, b8, c8)
#define woort_OpCode_GTR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRONLG, 3, a8, b8, c8)

/*
 * OPRSREN - 实数比较运算
 * LER (mode=0): [SB + a8] <= [SB + b8] -> [SB + c8]
 * GER (mode=1): [SB + a8] >= [SB + b8] -> [SB + c8]
 * EQR (mode=2): [SB + a8] == [SB + b8] -> [SB + c8]
 * NER (mode=3): [SB + a8] != [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_LER(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 0, a8, b8, c8)
#define woort_OpCode_GER(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 1, a8, b8, c8)
#define woort_OpCode_EQR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 2, a8, b8, c8)
#define woort_OpCode_NER(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPRSREN, 3, a8, b8, c8)

/*
 * OPSALGS - 字符串算术和比较
 * ADDS (mode=0): [SB + a8] + [SB + b8] -> [SB + c8] (concat)
 * LTS  (mode=1): [SB + a8] < [SB + b8] -> [SB + c8]
 * GTS  (mode=2): [SB + a8] > [SB + b8] -> [SB + c8]
 * LES  (mode=3): [SB + a8] <= [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_ADDS(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 0, a8, b8, c8)
#define woort_OpCode_LTS(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 1, a8, b8, c8)
#define woort_OpCode_GTS(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 2, a8, b8, c8)
#define woort_OpCode_LES(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSALGS, 3, a8, b8, c8)

/*
 * OPSREN - 字符串比较（续）
 * GES (mode=0): [SB + a8] >= [SB + b8] -> [SB + c8]
 * EQS (mode=1): [SB + a8] == [SB + b8] -> [SB + c8]
 * NES (mode=2): [SB + a8] != [SB + b8] -> [SB + c8]
 */
#define woort_OpCode_GES(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 0, a8, b8, c8)
#define woort_OpCode_EQS(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 1, a8, b8, c8)
#define woort_OpCode_NES(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPSREN, 2, a8, b8, c8)

/*
 * OPLAONI - 逻辑运算
 * LAND (mode=0): [SB + a8] && [SB + b8] -> [SB + c8]
 * LOR  (mode=1): [SB + a8] || [SB + b8] -> [SB + c8]
 * LNOT (mode=2): ![SB + a8] -> [SB + bc16]
 */
#define woort_OpCode_LAND(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPLAONI, 0, a8, b8, c8)
#define woort_OpCode_LOR(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_OPLAONI, 1, a8, b8, c8)
#define woort_OpCode_LNOT(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPLAONI, 2, a8, bc16)

/*
 * OPCIASMD - 整数复合算术运算
 * CADDI (mode=0): [SB + bc16] + [SB + a8] -> [SB + bc16]
 * CSUBI (mode=1): [SB + bc16] - [SB + a8] -> [SB + bc16]
 * CMULI (mode=2): [SB + bc16] * [SB + a8] -> [SB + bc16]
 * CDIVI (mode=3): [SB + bc16] / [SB + a8] -> [SB + bc16]
 */
#define woort_OpCode_CADDI(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 0, a8, bc16)
#define woort_OpCode_CSUBI(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 1, a8, bc16)
#define woort_OpCode_CMULI(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 2, a8, bc16)
#define woort_OpCode_CDIVI(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCIASMD, 3, a8, bc16)

/*
 * OPCRASMD - 实数复合算术运算
 * CADDR (mode=0): [SB + bc16] + [SB + a8] -> [SB + bc16]
 * CSUBR (mode=1): [SB + bc16] - [SB + a8] -> [SB + bc16]
 * CMULR (mode=2): [SB + bc16] * [SB + a8] -> [SB + bc16]
 * CDIVR (mode=3): [SB + bc16] / [SB + a8] -> [SB + bc16]
 */
#define woort_OpCode_CADDR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 0, a8, bc16)
#define woort_OpCode_CSUBR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 1, a8, bc16)
#define woort_OpCode_CMULR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 2, a8, bc16)
#define woort_OpCode_CDIVR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCRASMD, 3, a8, bc16)

/*
 * OPCSAIOO - 字符串和模运算复合操作
 * CADDS  (mode=0): [SB + bc16] + [SB + a8] -> [SB + bc16] (concat, bc16 为前缀)
 * CVADDS (mode=1): [SB + a8] + [SB + bc16] -> [SB + bc16] (concat, a8 为前缀)
 * CMODI  (mode=2): [SB + bc16] % [SB + a8] -> [SB + bc16]
 * CMODR  (mode=3): [SB + bc16] % [SB + a8] -> [SB + bc16]
 */
#define woort_OpCode_CADDS(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 0, a8, bc16)
#define woort_OpCode_CVADDS(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 1, a8, bc16)
#define woort_OpCode_CMODI(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 2, a8, bc16)
#define woort_OpCode_CMODR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCSAIOO, 3, a8, bc16)

/*
 * OPCLAON - 复合逻辑运算
 * CLAND (mode=0): [SB + bc16] && [SB + a8] -> [SB + bc16]
 * CLOR  (mode=1): [SB + bc16] || [SB + a8] -> [SB + bc16]
 * CLNOT (mode=2): ![SB + bc16] -> [SB + bc16]
 */
#define woort_OpCode_CLAND(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCLAON, 0, a8, bc16)
#define woort_OpCode_CLOR(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_OPCLAON, 1, a8, bc16)
#define woort_OpCode_CLNOT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_OPCLAON, 2, bc16)

/*
 * LDIDX - 索引加载
 * LDIDXVEC    (mode=0): [SB + a8][[SB + b8]] -> [SB + c8], vec in [SB + a8], idx in [SB + b8]
 * LDIDXVECX   (mode=1): [SB + a8][[SB + b8]] -> [SB + c8] (dynamic), vec in [SB + a8], idx in [SB + b8]
 * LDIDSTRUCT  (mode=2): [SB + b8].field_n8 -> [SB + c8], struct in [SB + b8]
 * LDIDSTRING  (mode=3): [SB + a8][[SB + b8]] -> [SB + c8], str in [SB + a8], idx in [SB + b8]
 */
#define woort_OpCode_LDIDXVEC(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 0, a8, b8, c8)
#define woort_OpCode_LDIDXVECX(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 1, a8, b8, c8)
#define woort_OpCode_LDIDSTRUCT(n8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 2, n8, b8, c8)
#define woort_OpCode_LDIDSTRING(a8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDX, 3, a8, b8, c8)

/*
 * LDIDXDICT - 字典索引加载（按键类型）
 * LDIDXDICTI (mode=0): [SB + a8][[SB + b8]] -> [SB + c8], dict in [SB + a8], int key in [SB + b8]
 * LDIDXDICTR (mode=1): [SB + a8][[SB + b8]] -> [SB + c8], dict in [SB + a8], real key in [SB + b8]
 * LDIDXDICTB (mode=2): [SB + a8][[SB + b8]] -> [SB + c8], dict in [SB + a8], bool key in [SB + b8]
 * LDIDXDICTX (mode=3): [SB + a8][[SB + b8]] -> [SB + c8], dict in [SB + a8], dynamic key in [SB + b8]
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
 * STIDXVECI (mode=0): vec in [SB + a8], idx in [SB + b8], val in [SB + c8]; vec[[SB + b8]] = (int)[SB + c8]
 * STIDXVECR (mode=1): vec in [SB + a8], idx in [SB + b8], val in [SB + c8]; vec[[SB + b8]] = (real)[SB + c8]
 * STIDXVECB (mode=2): vec in [SB + a8], idx in [SB + b8], val in [SB + c8]; vec[[SB + b8]] = (bool)[SB + c8]
 * STIDXVECX (mode=3): vec in [SB + a8], idx in [SB + b8], val in [SB + c8]; vec[[SB + b8]] = (dyn)[SB + c8]
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
 * STIDXDICTII/IR/IB/IX: map in [SB + a8], key in [SB + b8], val in [SB + c8]; map[B8] = C8
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
 * 同 STIDXDICTI，但 key 在 B8 为 real 类型
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
 * 同 STIDXDICTI，但 key 在 B8 为 bool 类型
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
 * 同 STIDXDICTI，但 key 在 B8 为 dynamic 类型
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
 * LDIDXDICTIX (mode=0): [SB + a8][[SB + b8]] -> [SB + c8] (dyn), dict in [SB + a8], int key in [SB + b8]
 * LDIDXDICTRX (mode=1): [SB + a8][[SB + b8]] -> [SB + c8] (dyn), dict in [SB + a8], real key in [SB + b8]
 * LDIDXDICTBX (mode=2): [SB + a8][[SB + b8]] -> [SB + c8] (dyn), dict in [SB + a8], bool key in [SB + b8]
 * LDIDXDICTXX (mode=3): [SB + a8][[SB + b8]] -> [SB + c8] (dyn), dict in [SB + a8], dynamic key in [SB + b8]
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
#define woort_OpCode_LDIDXVECEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXEX, 0, bc16)
#define woort_OpCode_LDIDXVECXEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXEX, 1, bc16)
#define woort_OpCode_LDIDSTRUCTEXT(n24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_LDIDXEX, 2, n24)
#define woort_OpCode_LDIDSTRINGEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXEX, 3, bc16)

/*
 * LDIDXDICTEX - 扩展字典索引加载（按键类型）
 */
#define woort_OpCode_LDIDXDICTIEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 0, bc16)
#define woort_OpCode_LDIDXDICTREXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 1, bc16)
#define woort_OpCode_LDIDXDICTBEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 2, bc16)
#define woort_OpCode_LDIDXDICTXEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEX, 3, bc16)

/*
 * LDIDXDICTEXX - 扩展字典索引加载（动态值类型，按键类型）
 */
#define woort_OpCode_LDIDXDICTIXEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 0, bc16)
#define woort_OpCode_LDIDXDICTRXEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 1, bc16)
#define woort_OpCode_LDIDXDICTBXEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 2, bc16)
#define woort_OpCode_LDIDXDICTXXEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_LDIDXDICTEXX, 3, bc16)

/*
 * STIDXMAPI - Map存储（int键）
 * STIDXMAPII/IR/IB/IX: map in [SB + a8], key in [SB + b8], val in [SB + c8]; map[B8] = C8 (create if missing)
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
 * MKUNIONEXT  (mode=3): 构造结构体，但是是为 Union 特化的处理，((woort_Int)n32, [SB + a8]) -> [SB + bc16]
 */
#define woort_OpCode_MKVECEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CONSEX, 0, bc16)
#define woort_OpCode_MKMAPEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CONSEX, 1, bc16)
#define woort_OpCode_MKSTRUCTEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_CONSEX, 2, bc16)
#define woort_OpCode_MKUNIONEXT(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_CONSEX, 3, a8, bc16)

/*
 * PUSHCEXT/POPCEXT/PUSHCCHKEXT - 扩展常量操作
 * PUSHCEXT    : 压入常量 G[ex32]
 * POPCEXT     : 弹出到常量 G[ex32]
 * PUSHCCHKEXT : 检查常量 G[ex32]
 */
#define woort_OpCode_PUSHCEXT() \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_PUSH, 3)
#define woort_OpCode_POPCEXT() \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_POP, 3)
#define woort_OpCode_PUSHCCHKEXT() \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_PUSHCHK, 3)

/*
 * MOVLDEXT/MOVSTEXT - 扩展移动操作
 * MOVLDEXT (mode=2): [SB + bc16] = [SB + ex32]
 * MOVSTEXT (mode=3): [SB + ex32] = [SB + bc16]
 */
#define woort_OpCode_MOVLDEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_MOV, 2, bc16)
#define woort_OpCode_MOVSTEXT(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_MOV, 3, bc16)

/*
 * UNPACK - 解包操作，将指定的数组中的元素展开到栈上
 * UNPACKVEC        (mode=0): 解包并 unbox 数组 [SB + bc16] 到栈上，展开 N8 个元素，若数组元素数量不满足则 panic
 * UNPACKVECX       (mode=1): 解包数组 [SB + bc16] 到栈上，展开 N8 个元素，若数组元素数量不满足则 panic
 * UNPACKVECALL     (mode=2): 解包数组 [SB + b8] 到栈上，至少展开并 unbox N8 个元素，剩余元素原样展开，若数组元素数量不满足则 panic，将实际展开的参数数量写入到 c8中
 * UNPACKVECXALL    (mode=3)：解包数组 [SB + b8] 到栈上，至少展开 N8 个元素，若数组元素数量不满足则 panic，将实际展开的参数数量写入到 c8中
 */
#define woort_OpCode_UNPACKVEC(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_UNPACK, 0, n8, bc16)
#define woort_OpCode_UNPACKVECX(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_UNPACK, 1, n8, bc16)
#define woort_OpCode_UNPACKVECALL(n8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_UNPACK, 2, n8, b8, c8)
#define woort_OpCode_UNPACKVECXALL(n8, b8, c8) \
    woort_OpCodeFormal_cons(OP6_M2_A8_B8_C8, WOORT_OPCODE_UNPACK, 3, n8, b8, c8)

/*
 * PUSHIDXSTBOX - 压入结构体字段引用
 * PUSHIDXSTBOXI/R/B/X: 压入 struct.n8 的引用到栈，类型 int/real/bool/dynamic
 */
#define woort_OpCode_PUSHIDXSTRUCT(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_PUSHIDXSTBOX, 0, n8, bc16)
#define woort_OpCode_PUSHIDXSTBOXI(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_PUSHIDXSTBOX, 1, n8, bc16)
#define woort_OpCode_PUSHIDXSTBOXR(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_PUSHIDXSTBOX, 2, n8, bc16)
#define woort_OpCode_PUSHIDXSTBOXB(n8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_PUSHIDXSTBOX, 3, n8, bc16)

/*
 * PACKARG - 打包参数
 * 将 n10 个参数打包到 [SB + bc16]
 */
#define woort_OpCode_PACKARG(n10, bc16) \
    woort_OpCodeFormal_cons(OP6_MA10_BC16, WOORT_OPCODE_PACKARG, n10, bc16)

/*
 * JIFINITED - 一次性初始化跳转
 * 如果 env_data[c32] 的原子标志 == 2（已初始化），跳转到地址 u26
 * 否则尝试 CAS 0->1 并继续执行初始化代码
 */
#define woort_OpCode_JIFINITED(u26) \
    woort_OpCodeFormal_cons(OP6_MABC26, WOORT_OPCODE_JIFINITED, u26)

/*
 * ATOMIC - 原子操作
 * ASTORE (mode=0): G[ex32] = [SB + bc16] (release)
 * ALOAD  (mode=1): [SB + bc16] = G[ex32] (acquire)
 * CAS    (mode=2): CAS G[ex32](desired=[SB + a8], expected=[SB + bc16])
 *   a8 = desired (R_ONLY_S8, written back), bc16 = expected (R_W_S16)
 */
#define woort_OpCode_ASTORE(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_ATOMIC, 0, bc16)
#define woort_OpCode_ALOAD(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_ATOMIC, 1, bc16)
#define woort_OpCode_CAS(a8, bc16) \
    woort_OpCodeFormal_cons(OP6_M2_A8_BC16, WOORT_OPCODE_ATOMIC, 2, a8, bc16)

/*
 * DEBUGTRAP - 陷阱/断点
 */
#define woort_OpCode_DEBUGTRAP() \
    woort_OpCodeFormal_cons(OP6_M2, WOORT_OPCODE_TRAP, 0)

/*
 * PANICS - 从栈槽读取字符串并触发 panic
 * PANICS [SB + bc16]
 */
#define woort_OpCode_PANICS(bc16) \
    woort_OpCodeFormal_cons(OP6_M2_BC16, WOORT_OPCODE_TRAP, 1, bc16)

/*
 * PANICC - 从常量区读取字符串并触发 panic
 * PANICC G[abc24]
 */
#define woort_OpCode_PANICC(abc24) \
    woort_OpCodeFormal_cons(OP6_M2_ABC24, WOORT_OPCODE_TRAP, 2, abc24)
