#pragma once

/*
 * woort_ir_block.h
 *
 * WooRT IR 基本块接口
 *
 * 本文件定义了 woort_IRBlock 的接口，用于在基本块中生成各种 IR 指令。
 *
 * 基本块特性：
 * - 包含一系列按顺序执行的指令
 * - 以一个终止指令结束（ret, br, condbr 等）
 * - 一旦添加了终止指令，就不能再添加其他指令
 *
 * 指令分类：
 * 1. 终止指令：ret, br, condbr
 * 2. 整数算术：addi, subi, muli, divi, negi, modi
 * 3. 实数算术：addr, subr, mulr, divr, negr, modr
 * 4. 整数比较：lti, gti, lei, gei, eqi, nei
 * 5. 实数比较：ltr, gtr, ler, ger, eqr, ner
 * 6. 逻辑运算：land, lor, lnot
 * 7. 复合赋值：caddi, csubi, cmuli, cdivi, caddr, csubr, cmulr, cdivr, cland, clor, clnot
 * 8. 函数调用：call (统一的调用接口)
 * 9. 容器操作：mkvec, mkmap, mkstruct, mkclosure
 * 10. 索引访问：ldidx, stidx, ldidxdict, stidxdict, ldidxstruct, stidstruct
 * 11. 类型转换：casti_to_r, casti_to_s, castr_to_i, castr_to_s, casts_to_i, casts_to_r
 * 12. 动态类型：boxdyn, unboxdyn, checkdyn
 * 13. 字符串操作：adds, lts, gts, les, ges, eqs, nes
 * 14. 其他操作：unpack, packarg, mov
 *
 * 指令命名约定：
 * - 后缀 i: 整数操作
 * - 后缀 r: 实数操作
 * - 后缀 s: 字符串操作
 * - 后缀 b: 布尔操作
 * - 前缀 c: 复合赋值（如 caddi = +=
 * - 前缀 ld: 加载（load）
 * - 前缀 st: 存储（store）
 */

#include "woort_ir_types.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * woort_IRBlock_is_terminated
 *
 * 检查基本块是否已有终止指令。
 *
 * 参数：
 *   block - 基本块实例
 *
 * 返回值：
 *   true  - 块已有终止指令
 *   false - 块没有终止指令
 *
 * 说明：
 * - 一旦块被终止，就不能再添加指令
 * - 所有块在编译前必须有终止指令
 */
WOORT_NODISCARD bool woort_IRBlock_is_terminated(
    const woort_IRBlock* block);

/*
 * woort_IRBlock_get_function
 *
 * 获取基本块所属的函数。
 *
 * 参数：
 *   block - 基本块实例
 *
 * 返回值：
 *   所属的 IRFunction 指针
 */
WOORT_NODISCARD woort_IRFunction* woort_IRBlock_get_function(
    const woort_IRBlock* block);

/* ============================================================================
 * 第一部分：终止指令
 *
 * 终止指令是基本块的最后一条指令，决定控制流如何离开当前块。
 * 每个基本块必须恰好有一条终止指令。
 * ============================================================================
 */

/*
 * woort_IRBlock_ret_void
 *
 * 从函数返回，不返回任何值。
 *
 * 参数：
 *   block - 基本块实例
 *
 * 返回值：
 *   true  - 成功添加返回指令
 *   false - 块已被终止
 *
 * 说明：
 * - 生成 RET 指令
 * - 用于 void 返回类型的函数
 * - 此指令终止当前基本块
 *
 * 示例：
 *   woort_IRBlock_ret_void(block);
 */
WOORT_NODISCARD bool woort_IRBlock_ret_void(
    woort_IRBlock* block);

/*
 * woort_IRBlock_ret
 *
 * 从函数返回一个值。
 *
 * 参数：
 *   block - 基本块实例
 *   value - 要返回的值
 *
 * 返回值：
 *   true  - 成功添加返回指令
 *   false - 块已被终止
 *
 * 说明：
 * - 生成 RETVS 或 RETVC 指令（根据值的来源选择）
 * - 用于非 void 返回类型的函数
 * - 此指令终止当前基本块
 *
 * 示例：
 *   const woort_IRValue* result = woort_IRBlock_addi(block, a, b);
 *   woort_IRBlock_ret(block, result);
 */
WOORT_NODISCARD bool woort_IRBlock_ret(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_ret_const
 *
 * 从函数返回一个常量值。
 *
 * 参数：
 *   block        - 基本块实例
 *   global_index - 全局存储索引
 *
 * 返回值：
 *   true  - 成功添加返回指令
 *   false - 块已被终止
 *
 * 说明：
 * - 生成 RETVC 指令
 * - 直接从全局存储区返回常量值
 * - 此指令终止当前基本块
 */
WOORT_NODISCARD bool woort_IRBlock_ret_const(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_index);

/*
 * woort_IRBlock_br
 *
 * 无条件跳转到目标基本块。
 *
 * 参数：
 *   block       - 当前基本块实例
 *   target      - 跳转目标基本块
 *
 * 返回值：
 *   true  - 成功添加跳转指令
 *   false - 块已被终止
 *
 * 说明：
 * - 生成 JFWD 指令
 * - 此指令终止当前基本块
 * - target 必须属于同一函数
 *
 * 示例：
 *   woort_IRBlock_br(entry, loop_header);
 */
WOORT_NODISCARD bool woort_IRBlock_br(
    woort_IRBlock* block,
    woort_IRBlock* target);

/*
 * woort_IRBlock_condbr
 *
 * 条件分支：根据布尔值选择跳转目标。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   condition  - 条件值（应为布尔类型）
 *   then_block - 条件为真时跳转的块
 *   else_block - 条件为假时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 生成 JFWDNZ 或 JFWDZ 指令
 * - 此指令终止当前基本块
 * - condition 应该是布尔类型的值
 * - then_block 和 else_block 必须属于同一函数
 *
 * 示例：
 *   const woort_IRValue* cond = woort_IRBlock_lti(block, x, y);
 *   woort_IRBlock_condbr(block, cond, less_than_block, greater_equal_block);
 */
WOORT_NODISCARD bool woort_IRBlock_condbr(
    woort_IRBlock* block,
    const woort_IRValue* condition,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_less_than
 *
 * 条件分支：如果 a < b，跳转到 then_block，否则跳转到 else_block。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   a          - 第一个操作数（整数或实数）
 *   b          - 第二个操作数（整数或实数）
 *   then_block - a < b 时跳转的块
 *   else_block - a >= b 时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 这是一个组合指令，相当于：
 *   cond = lti(a, b); condbr(cond, then, else);
 * - 但可能生成更优化的 JFWDLT 指令
 * - 根据操作数类型自动选择整数或实数比较
 * - 此指令终止当前基本块
 *
 * 示例：
 *   woort_IRBlock_condbr_less_than(block, i, limit, loop_body, after_loop);
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_less_than(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_greater_than
 *
 * 条件分支：如果 a > b，跳转到 then_block，否则跳转到 else_block。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   a          - 第一个操作数（整数或实数）
 *   b          - 第二个操作数（整数或实数）
 *   then_block - a > b 时跳转的块
 *   else_block - a <= b 时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 可能生成更优化的 JFWDGT 指令
 * - 根据操作数类型自动选择整数或实数比较
 * - 此指令终止当前基本块
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_greater_than(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_less_equal
 *
 * 条件分支：如果 a <= b，跳转到 then_block，否则跳转到 else_block。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   a          - 第一个操作数（整数或实数）
 *   b          - 第二个操作数（整数或实数）
 *   then_block - a <= b 时跳转的块
 *   else_block - a > b 时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 可能生成更优化的 JFWDEL 指令
 * - 根据操作数类型自动选择整数或实数比较
 * - 此指令终止当前基本块
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_less_equal(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_greater_equal
 *
 * 条件分支：如果 a >= b，跳转到 then_block，否则跳转到 else_block。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   a          - 第一个操作数（整数或实数）
 *   b          - 第二个操作数（整数或实数）
 *   then_block - a >= b 时跳转的块
 *   else_block - a < b 时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 可能生成更优化的 JFWDEG 指令
 * - 根据操作数类型自动选择整数或实数比较
 * - 此指令终止当前基本块
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_greater_equal(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_equal
 *
 * 条件分支：如果 a == b，跳转到 then_block，否则跳转到 else_block。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   a          - 第一个操作数
 *   b          - 第二个操作数
 *   then_block - a == b 时跳转的块
 *   else_block - a != b 时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 可能生成更优化的 JFWDEQ 指令
 * - 根据操作数类型自动选择比较方式
 * - 此指令终止当前基本块
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_equal(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_not_equal
 *
 * 条件分支：如果 a != b，跳转到 then_block，否则跳转到 else_block。
 *
 * 参数：
 *   block      - 当前基本块实例
 *   a          - 第一个操作数
 *   b          - 第二个操作数
 *   then_block - a != b 时跳转的块
 *   else_block - a == b 时跳转的块
 *
 * 返回值：
 *   true  - 成功添加条件分支指令
 *   false - 块已被终止
 *
 * 说明：
 * - 可能生成更优化的 JFWDNEQ 指令
 * - 根据操作数类型自动选择比较方式
 * - 此指令终止当前基本块
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_not_equal(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/* ============================================================================
 * 第二部分：整数算术指令
 *
 * 整数算术指令对整数值进行运算，返回整数结果。
 * 所有指令形式：result = a op b 或 result = op a
 * ============================================================================
 */

/*
 * woort_IRBlock_addi
 *
 * 整数加法：result = a + b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表 a + b 的 IRValue 指针
 *
 * 说明：
 * - 生成 ADDI 指令
 * - 如果 a 或 b 来自常量，可能生成优化的 CADDI 指令
 *
 * 示例：
 *   const woort_IRValue* sum = woort_IRBlock_addi(block, x, y);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_addi(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_subi
 *
 * 整数减法：result = a - b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 被减数（整数）
 *   b     - 减数（整数）
 *
 * 返回值：
 *   代表 a - b 的 IRValue 指针
 *
 * 说明：
 * - 生成 SUBI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_subi(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_muli
 *
 * 整数乘法：result = a * b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表 a * b 的 IRValue 指针
 *
 * 说明：
 * - 生成 MULI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_muli(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_divi
 *
 * 整数除法：result = a / b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 被除数（整数）
 *   b     - 除数（整数）
 *
 * 返回值：
 *   代表 a / b 的 IRValue 指针
 *
 * 说明：
 * - 生成 DIVI 指令
 * - 如果 b 为 0，运行时会触发 panic
 * - 整数除法向零截断
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_divi(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_negi
 *
 * 整数取负：result = -a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 操作数（整数）
 *
 * 返回值：
 *   代表 -a 的 IRValue 指针
 *
 * 说明：
 * - 生成 NEGI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_negi(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_modi
 *
 * 整数取模：result = a % b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 被除数（整数）
 *   b     - 除数（整数）
 *
 * 返回值：
 *   代表 a % b 的 IRValue 指针
 *
 * 说明：
 * - 生成 MODI 指令
 * - 如果 b 为 0，运行时会触发 panic
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_modi(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* ============================================================================
 * 第三部分：整数比较指令
 *
 * 整数比较指令比较两个整数值，返回布尔结果。
 * ============================================================================
 */

/*
 * woort_IRBlock_lti
 *
 * 整数小于比较：result = (a < b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 LTI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_lti(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_gti
 *
 * 整数大于比较：result = (a > b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 GTI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_gti(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_lei
 *
 * 整数小于等于比较：result = (a <= b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 LEI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_lei(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_gei
 *
 * 整数大于等于比较：result = (a >= b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 GEI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_gei(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_eqi
 *
 * 整数相等比较：result = (a == b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 EQI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_eqi(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_nei
 *
 * 整数不等比较：result = (a != b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（整数）
 *   b     - 第二个操作数（整数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 NEI 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_nei(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* ============================================================================
 * 第四部分：实数算术指令
 *
 * 实数算术指令对浮点数值进行运算，返回浮点数结果。
 * 所有指令形式：result = a op b 或 result = op a
 * ============================================================================
 */

/*
 * woort_IRBlock_addr
 *
 * 实数加法：result = a + b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表 a + b 的 IRValue 指针
 *
 * 说明：
 * - 生成 ADDR 指令
 * - 如果 a 或 b 来自常量，可能生成优化的 CADDR 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_addr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_subr
 *
 * 实数减法：result = a - b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 被减数（实数）
 *   b     - 减数（实数）
 *
 * 返回值：
 *   代表 a - b 的 IRValue 指针
 *
 * 说明：
 * - 生成 SUBR 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_subr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_mulr
 *
 * 实数乘法：result = a * b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表 a * b 的 IRValue 指针
 *
 * 说明：
 * - 生成 MULR 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_mulr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_divr
 *
 * 实数除法：result = a / b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 被除数（实数）
 *   b     - 除数（实数）
 *
 * 返回值：
 *   代表 a / b 的 IRValue 指针
 *
 * 说明：
 * - 生成 DIVR 指令
 * - 如果 b 为 0，结果为 inf 或 nan
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_divr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_negr
 *
 * 实数取负：result = -a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 操作数（实数）
 *
 * 返回值：
 *   代表 -a 的 IRValue 指针
 *
 * 说明：
 * - 生成 NEGR 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_negr(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_modr
 *
 * 实数取模：result = a % b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 被除数（实数）
 *   b     - 除数（实数）
 *
 * 返回值：
 *   代表 a % b 的 IRValue 指针
 *
 * 说明：
 * - 生成 MODR 指令
 * - 结果的符号与被除数相同
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_modr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* ============================================================================
 * 第五部分：实数比较指令
 *
 * 实数比较指令比较两个浮点数值，返回布尔结果。
 * ============================================================================
 */

/*
 * woort_IRBlock_ltr
 *
 * 实数小于比较：result = (a < b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 LTR 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ltr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_gtr
 *
 * 实数大于比较：result = (a > b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 GTR 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_gtr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_ler
 *
 * 实数小于等于比较：result = (a <= b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 LER 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ler(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_ger
 *
 * 实数大于等于比较：result = (a >= b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 GER 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ger(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_eqr
 *
 * 实数相等比较：result = (a == b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 EQR 指令
 * - 注意：浮点数相等比较应谨慎使用
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_eqr(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_ner
 *
 * 实数不等比较：result = (a != b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（实数）
 *   b     - 第二个操作数（实数）
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 NER 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ner(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* ============================================================================
 * 第六部分：逻辑运算指令
 *
 * 逻辑运算指令对布尔值进行运算，返回布尔结果。
 * ============================================================================
 */

/*
 * woort_IRBlock_land
 *
 * 逻辑与：result = a && b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（布尔）
 *   b     - 第二个操作数（布尔）
 *
 * 返回值：
 *   代表 a && b 的 IRValue 指针（布尔类型）
 *
 * 说明：
 * - 生成 LAND 指令
 * - 短路求值由前端处理，此指令计算两个已知布尔值的与
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_land(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_lor
 *
 * 逻辑或：result = a || b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个操作数（布尔）
 *   b     - 第二个操作数（布尔）
 *
 * 返回值：
 *   代表 a || b 的 IRValue 指针（布尔类型）
 *
 * 说明：
 * - 生成 LOR 指令
 * - 短路求值由前端处理，此指令计算两个已知布尔值的或
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_lor(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_lnot
 *
 * 逻辑非：result = !a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 操作数（布尔）
 *
 * 返回值：
 *   代表 !a 的 IRValue 指针（布尔类型）
 *
 * 说明：
 * - 生成 LNOT 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_lnot(
    woort_IRBlock* block,
    const woort_IRValue* a);

/* ============================================================================
 * 第七部分：复合赋值指令
 *
 * 复合赋值指令直接在目标位置上进行运算，形式：dest = dest op src
 * 这些指令比分离的 load-op-store 更高效，因为它们：
 * 1. 减少指令数量
 * 2. 减少栈访问次数
 * 3. 允许更好的寄存器利用
 *
 * 注意：复合赋值指令直接修改目标位置的值，而不是返回新值。
 * 它们主要用于 IRStorage 的原地更新。
 * ============================================================================
 */

/*
 * woort_IRBlock_caddi
 *
 * 整数复合加法：dest = dest + src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（整数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CADDI 指令
 * - 直接在 dest 位置上加 src
 * - 比 load + addi + store 更高效
 *
 * 示例：
 *   // accumulator += delta
 *   woort_IRBlock_caddi(block, accumulator, delta);
 */
WOORT_NODISCARD bool woort_IRBlock_caddi(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_csubi
 *
 * 整数复合减法：dest = dest - src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（整数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CSUBI 指令
 */
WOORT_NODISCARD bool woort_IRBlock_csubi(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cmuli
 *
 * 整数复合乘法：dest = dest * src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（整数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CMULI 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cmuli(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cdivi
 *
 * 整数复合除法：dest = dest / src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（整数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CDIVI 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cdivi(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cmodi
 *
 * 整数复合取模：dest = dest % src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（整数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CMODI 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cmodi(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_caddr
 *
 * 实数复合加法：dest = dest + src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（实数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CADDR 指令
 */
WOORT_NODISCARD bool woort_IRBlock_caddr(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_csubr
 *
 * 实数复合减法：dest = dest - src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（实数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CSUBR 指令
 */
WOORT_NODISCARD bool woort_IRBlock_csubr(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cmulr
 *
 * 实数复合乘法：dest = dest * src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（实数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CMULR 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cmulr(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cdivr
 *
 * 实数复合除法：dest = dest / src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（实数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CDIVR 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cdivr(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cmodr
 *
 * 实数复合取模：dest = dest % src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（实数）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CMODR 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cmodr(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_cland
 *
 * 逻辑复合与：dest = dest && src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（布尔）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CLAND 指令
 */
WOORT_NODISCARD bool woort_IRBlock_cland(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_clor
 *
 * 逻辑复合或：dest = dest || src
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *   src      - 源操作数（布尔）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CLOR 指令
 */
WOORT_NODISCARD bool woort_IRBlock_clor(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_clnot
 *
 * 逻辑复合非：dest = !dest
 *
 * 参数：
 *   block    - 基本块实例
 *   dest     - 目标存储位置
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 CLNOT 指令
 * - 原地取反目标位置的布尔值
 */
WOORT_NODISCARD bool woort_IRBlock_clnot(
    woort_IRBlock* block,
    woort_IRStorage* dest);

/* ============================================================================
 * 第八部分：函数调用指令
 *
 * 函数调用指令用于调用各种类型的函数：
 * - 脚本函数（字节码函数）
 * - 原生函数（Native Function）
 * - JIT 编译的函数
 *
 * 调用约定：
 * - 参数按逆序压栈：第一个参数在 [SP+1]，第二个在 [SP+2]，以此类推
 * - 调用后，返回值在 [SP]（如果有）
 * - 调用方负责清理参数栈
 * ============================================================================
 */

/*
 * woort_IRBlock_call
 *
 * 统一的函数调用接口。
 *
 * 参数：
 *   block        - 基本块实例
 *   func_idx     - 函数的全局存储索引
 *   call_kind    - 调用类型（脚本/原生/JIT）
 *   args         - 参数数组（可以为 NULL 如果无参数）
 *   arg_count    - 参数数量
 *
 * 返回值：
 *   代表返回值的 IRValue 指针，如果函数返回 void 则返回 NULL
 *
 * 说明：
 * - 根据 call_kind 生成相应的 CALL* 指令
 * - WOORT_IR_CALL_KIND_SCRIPT -> CALLNWO
 * - WOORT_IR_CALL_KIND_NATIVE_WITH_OPAQUE -> CALLNWO
 * - WOORT_IR_CALL_KIND_NATIVE_FP -> CALLNFP
 * - WOORT_IR_CALL_KIND_JIT -> CALLNJIT
 *
 * 示例：
 *   const woort_IRValue* args[] = { arg1, arg2 };
 *   const woort_IRValue* result = woort_IRBlock_call(
 *       block, func_idx, WOORT_IR_CALL_KIND_SCRIPT, args, 2);
 */
WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRBlock_call(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    woort_IRCallKind call_kind,
    /* OPTIONAL */ const woort_IRValue** args,
    size_t arg_count);

/*
 * woort_IRBlock_call_script
 *
 * 调用脚本函数（字节码函数）。
 *
 * 参数：
 *   block     - 基本块实例
 *   func_idx  - 函数的全局存储索引
 *   args      - 参数数组（可以为 NULL 如果无参数）
 *   arg_count - 参数数量
 *
 * 返回值：
 *   代表返回值的 IRValue 指针，如果函数返回 void 则返回 NULL
 *
 * 说明：
 * - 生成 CALLNWO 指令
 * - 用于调用同一 CodeEnv 中的脚本函数
 *
 * 示例：
 *   const woort_IRValue* args[] = { n };
 *   const woort_IRValue* result = woort_IRBlock_call_script(
 *       block, fib_func_idx, args, 1);
 */
WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRBlock_call_script(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    /* OPTIONAL */ const woort_IRValue** args,
    size_t arg_count);

/*
 * woort_IRBlock_call_native_opaque
 *
 * 调用原生函数（使用 Opaque 调用约定）。
 *
 * 参数：
 *   block     - 基本块实例
 *   func_idx  - 函数的全局存储索引
 *   args      - 参数数组（可以为 NULL 如果无参数）
 *   arg_count - 参数数量
 *
 * 返回值：
 *   代表返回值的 IRValue 指针，如果函数返回 void 则返回 NULL
 *
 * 说明：
 * - 生成 CALLNWO 指令
 * - Opaque 调用约定：传递 VM 上下文和参数数组
 * - 适用于需要访问 VM 内部状态的原生函数
 */
WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRBlock_call_native_opaque(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    /* OPTIONAL */ const woort_IRValue** args,
    size_t arg_count);

/*
 * woort_IRBlock_call_native_fp
 *
 * 调用原生函数（使用函数指针调用约定）。
 *
 * 参数：
 *   block     - 基本块实例
 *   func_idx  - 函数的全局存储索引
 *   args      - 参数数组（可以为 NULL 如果无参数）
 *   arg_count - 参数数量
 *
 * 返回值：
 *   代表返回值的 IRValue 指针，如果函数返回 void 则返回 NULL
 *
 * 说明：
 * - 生成 CALLNFP 指令
 * - 直接使用 C 函数指针调用
 * - 适用于简单的原生函数，性能更好
 */
WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRBlock_call_native_fp(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    /* OPTIONAL */ const woort_IRValue** args,
    size_t arg_count);

/*
 * woort_IRBlock_call_jit
 *
 * 调用 JIT 编译的函数。
 *
 * 参数：
 *   block     - 基本块实例
 *   func_idx  - 函数的全局存储索引
 *   args      - 参数数组（可以为 NULL 如果无参数）
 *   arg_count - 参数数量
 *
 * 返回值：
 *   代表返回值的 IRValue 指针，如果函数返回 void 则返回 NULL
 *
 * 说明：
 * - 生成 CALLNJIT 指令
 * - 用于调用 JIT 编译生成的机器码
 */
WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRBlock_call_jit(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    /* OPTIONAL */ const woort_IRValue** args,
    size_t arg_count);

/*
 * woort_IRBlock_call_indirect
 *
 * 间接调用：通过函数值调用。
 *
 * 参数：
 *   block     - 基本块实例
 *   func_val  - 函数值（闭包或函数引用）
 *   args      - 参数数组（可以为 NULL 如果无参数）
 *   arg_count - 参数数量
 *
 * 返回值：
 *   代表返回值的 IRValue 指针，如果函数返回 void 则返回 NULL
 *
 * 说明：
 * - 生成 CALLS 或 CALLC 指令
 * - 用于调用存储在变量中的函数（如高阶函数）
 * - func_val 应该是一个闭包或函数引用
 */
WOORT_NODISCARD /* OPTIONAL */ const woort_IRValue* woort_IRBlock_call_indirect(
    woort_IRBlock* block,
    const woort_IRValue* func_val,
    /* OPTIONAL */ const woort_IRValue** args,
    size_t arg_count);

/*
 * woort_IRBlock_result
 *
 * 获取函数调用的返回值并清理参数栈。
 *
 * 参数：
 *   block       - 基本块实例
 *   pop_count   - 要弹出的参数数量
 *   out_result  - 输出参数，接收返回值的存储位置（可以为 NULL）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 RESULT 指令
 * - 用于获取调用结果并清理参数栈
 * - pop_count 指定要弹出的参数数量
 * - 如果函数返回 void，使用 popr 代替
 *
 * 示例：
 *   const woort_IRValue* result;
 *   woort_IRBlock_result(block, 2, &result);
 */
WOORT_NODISCARD bool woort_IRBlock_result(
    woort_IRBlock* block,
    size_t pop_count,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * woort_IRBlock_popr
 *
 * 弹出指定数量的栈元素（用于清理 void 函数的参数）。
 *
 * 参数：
 *   block      - 基本块实例
 *   pop_count  - 要弹出的元素数量
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 POPR 指令
 * - 用于清理 void 函数调用后的参数栈
 */
WOORT_NODISCARD bool woort_IRBlock_popr(
    woort_IRBlock* block,
    size_t pop_count);

/* ============================================================================
 * 第九部分：容器构造指令
 *
 * 容器构造指令用于创建各种 GC 管理的容器对象：
 * - Vec（向量/数组）
 * - Map（字典/哈希表）
 * - Struct（结构体）
 * - Closure（闭包）
 * ============================================================================
 */

/*
 * woort_IRBlock_mkvec
 *
 * 创建向量（数组）。
 *
 * 参数：
 *   block       - 基本块实例
 *   elements    - 元素值数组
 *   elem_count  - 元素数量
 *
 * 返回值：
 *   代表新创建向量的 IRValue 指针
 *
 * 说明：
 * - 生成 MKVEC 或 MKVECEXT 指令（根据元素数量选择）
 * - 向量元素从栈中弹出，按逆序存储
 * - 返回的向量是 GC 管理的对象
 *
 * 示例：
 *   const woort_IRValue* elems[] = { a, b, c };
 *   const woort_IRValue* vec = woort_IRBlock_mkvec(block, elems, 3);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_mkvec(
    woort_IRBlock* block,
    const woort_IRValue** elements,
    size_t elem_count);

/*
 * woort_IRBlock_mkmap
 *
 * 创建字典（哈希表）。
 *
 * 参数：
 *   block       - 基本块实例
 *   entries     - 键值对数组（键值交替存储）
 *   entry_count - 键值对数量
 *
 * 返回值：
 *   代表新创建字典的 IRValue 指针
 *
 * 说明：
 * - 生成 MKMAP 或 MKMAPEXT 指令
 * - entries 数组格式：{ key0, val0, key1, val1, ... }
 * - 键值对从栈中弹出
 *
 * 示例：
 *   const woort_IRValue* entries[] = { k1, v1, k2, v2 };
 *   const woort_IRValue* map = woort_IRBlock_mkmap(block, entries, 2);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_mkmap(
    woort_IRBlock* block,
    const woort_IRValue** entries,
    size_t entry_count);

/*
 * woort_IRBlock_mkstruct
 *
 * 创建结构体。
 *
 * 参数：
 *   block       - 基本块实例
 *   fields      - 字段值数组
 *   field_count - 字段数量
 *
 * 返回值：
 *   代表新创建结构体的 IRValue 指针
 *
 * 说明：
 * - 生成 MKSTRUCT 或 MKSTRUCTEXT 指令
 * - 字段按声明的顺序存储
 * - 字段值从栈中弹出
 *
 * 示例：
 *   const woort_IRValue* fields[] = { x, y, z };
 *   const woort_IRValue* s = woort_IRBlock_mkstruct(block, fields, 3);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_mkstruct(
    woort_IRBlock* block,
    const woort_IRValue** fields,
    size_t field_count);

/*
 * woort_IRBlock_mkclosure
 *
 * 创建闭包。
 *
 * 参数：
 *   block           - 基本块实例
 *   func_idx        - 函数的全局存储索引
 *   captured_values - 捕获的值数组
 *   captured_count  - 捕获值的数量
 *
 * 返回值：
 *   代表新创建闭包的 IRValue 指针
 *
 * 说明：
 * - 生成 MKCLOSURE 指令
 * - 闭包包含函数引用和捕获的变量
 * - 捕获的值按顺序存储在闭包中
 *
 * 示例：
 *   const woort_IRValue* captured[] = { x, y };
 *   const woort_IRValue* closure = woort_IRBlock_mkclosure(
 *       block, func_idx, captured, 2);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_mkclosure(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_idx,
    const woort_IRValue** captured_values,
    size_t captured_count);

/* ============================================================================
 * 第十部分：索引访问指令
 *
 * 索引访问指令用于读取和写入容器中的元素：
 * - 向量索引：vec[i]
 * - 字典索引：dict[key]
 * - 结构体字段：struct.field
 * - 字符串索引：str[i]
 * ============================================================================
 */

/*
 * woort_IRBlock_ldidx_vec
 *
 * 从向量中加载元素：result = vec[index]
 *
 * 参数：
 *   block  - 基本块实例
 *   vec    - 向量值
 *   index  - 索引值（整数）
 *
 * 返回值：
 *   代表加载元素的 IRValue 指针
 *
 * 说明：
 * - 生成 LDIDXVEC 或 LDIDXVECX 指令
 * - 如果索引越界，运行时触发 panic
 *
 * 示例：
 *   const woort_IRValue* elem = woort_IRBlock_ldidx_vec(block, vec, idx);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ldidx_vec(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index);

/*
 * woort_IRBlock_ldidx_dict
 *
 * 从字典中加载元素：result = dict[key]
 *
 * 参数：
 *   block - 基本块实例
 *   dict  - 字典值
 *   key   - 键值
 *
 * 返回值：
 *   代表加载值的 IRValue 指针
 *
 * 说明：
 * - 生成 LDIDXDICT* 系列指令（根据键类型选择）
 * - 如果键不存在，返回 nil
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ldidx_dict(
    woort_IRBlock* block,
    const woort_IRValue* dict,
    const woort_IRValue* key);

/*
 * woort_IRBlock_ldidx_struct
 *
 * 从结构体中加载字段：result = struct.field
 *
 * 参数：
 *   block      - 基本块实例
 *   struct_val - 结构体值
 *   field_idx  - 字段索引（从 0 开始）
 *
 * 返回值：
 *   代表字段值的 IRValue 指针
 *
 * 说明：
 * - 生成 LDIDSTRUCT 或 LDIDSTRUCTEXT 指令
 * - field_idx 是字段的编译时索引，不是运行时值
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ldidx_struct(
    woort_IRBlock* block,
    const woort_IRValue* struct_val,
    size_t field_idx);

/*
 * woort_IRBlock_ldidx_string
 *
 * 从字符串中加载字符：result = str[index]
 *
 * 参数：
 *   block  - 基本块实例
 *   str    - 字符串值
 *   index  - 索引值（整数）
 *
 * 返回值：
 *   代表字符的 IRValue 指针（整数类型，UTF-8 码点）
 *
 * 说明：
 * - 生成 LDIDSTRING 指令
 * - 返回 UTF-8 码点值
 * - 如果索引越界，运行时触发 panic
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ldidx_string(
    woort_IRBlock* block,
    const woort_IRValue* str,
    const woort_IRValue* index);

/*
 * woort_IRBlock_stidx_vec
 *
 * 存储元素到向量：vec[index] = value
 *
 * 参数：
 *   block  - 基本块实例
 *   vec    - 向量值（会被修改）
 *   index  - 索引值（整数）
 *   value  - 要存储的值
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 STIDXVEC* 系列指令（根据值类型选择）
 * - 如果索引越界，运行时触发 panic
 */
WOORT_NODISCARD bool woort_IRBlock_stidx_vec(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index,
    const woort_IRValue* value);

/*
 * woort_IRBlock_stidx_dict
 *
 * 存储键值对到字典：dict[key] = value
 *
 * 参数：
 *   block  - 基本块实例
 *   dict   - 字典值（会被修改）
 *   key    - 键值
 *   value  - 要存储的值
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 STIDXDICT* 或 STIDXMAP* 系列指令
 * - 如果键已存在，覆盖旧值
 * - 如果键不存在，插入新键值对
 */
WOORT_NODISCARD bool woort_IRBlock_stidx_dict(
    woort_IRBlock* block,
    const woort_IRValue* dict,
    const woort_IRValue* key,
    const woort_IRValue* value);

/*
 * woort_IRBlock_stidx_struct
 *
 * 存储值到结构体字段：struct.field = value
 *
 * 参数：
 *   block      - 基本块实例
 *   struct_val - 结构体值（会被修改）
 *   field_idx  - 字段索引（从 0 开始）
 *   value      - 要存储的值
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 STIDSTRUCT 指令
 * - field_idx 是字段的编译时索引
 */
WOORT_NODISCARD bool woort_IRBlock_stidx_struct(
    woort_IRBlock* block,
    const woort_IRValue* struct_val,
    size_t field_idx,
    const woort_IRValue* value);

/* ============================================================================
 * 第十一部分：类型转换指令
 *
 * 类型转换指令用于在不同类型之间进行转换：
 * - 整数 <-> 实数
 * - 整数 <-> 字符串
 * - 实数 <-> 字符串
 *
 * 注意：IR 不进行类型检查，前端负责确保类型正确。
 * ============================================================================
 */

/*
 * woort_IRBlock_casti_to_r
 *
 * 整数转实数：result = (real)a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 整数值
 *
 * 返回值：
 *   代表实数值的 IRValue 指针
 *
 * 说明：
 * - 生成 ITORST 指令（store 到目标位置）
 * - 将整数精确转换为实数
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_casti_to_r(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_casti_to_s
 *
 * 整数转字符串：result = tostring(a)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 整数值
 *
 * 返回值：
 *   代表字符串值的 IRValue 指针
 *
 * 说明：
 * - 生成 ITOSST 指令
 * - 将整数转换为十进制字符串表示
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_casti_to_s(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_castr_to_i
 *
 * 实数转整数：result = (int)a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 实数值
 *
 * 返回值：
 *   代表整数值的 IRValue 指针
 *
 * 说明：
 * - 生成 RTOIST 指令
 * - 截断小数部分，向零取整
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_castr_to_i(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_castr_to_s
 *
 * 实数转字符串：result = tostring(a)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 实数值
 *
 * 返回值：
 *   代表字符串值的 IRValue 指针
 *
 * 说明：
 * - 生成 RTOSST 指令
 * - 将实数转换为字符串表示
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_castr_to_s(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_casts_to_i
 *
 * 字符串转整数：result = (int)a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 字符串值
 *
 * 返回值：
 *   代表整数值的 IRValue 指针
 *
 * 说明：
 * - 生成 STOIST 指令
 * - 解析字符串为整数
 * - 如果字符串不是有效整数，行为未定义
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_casts_to_i(
    woort_IRBlock* block,
    const woort_IRValue* a);

/*
 * woort_IRBlock_casts_to_r
 *
 * 字符串转实数：result = (real)a
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 字符串值
 *
 * 返回值：
 *   代表实数值的 IRValue 指针
 *
 * 说明：
 * - 生成 STORST 指令
 * - 解析字符串为实数
 * - 如果字符串不是有效实数，行为未定义
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_casts_to_r(
    woort_IRBlock* block,
    const woort_IRValue* a);

/* ============================================================================
 * 第十二部分：动态类型指令
 *
 * 动态类型指令用于处理动态类型的值：
 * - boxdyn: 将静态类型的值装箱为动态类型
 * - unboxdyn: 将动态类型的值拆箱为静态类型
 * - checkdyn: 检查动态类型值的运行时类型
 * ============================================================================
 */

/*
 * woort_IRBlock_boxdyn
 *
 * 将值装箱为动态类型。
 *
 * 参数：
 *   block      - 基本块实例
 *   value      - 要装箱的值
 *   value_type - 值的类型
 *
 * 返回值：
 *   代表动态类型值的 IRValue 指针
 *
 * 说明：
 * - 生成 BOXDYN 指令
 * - 将静态类型的值包装为动态类型
 * - 动态类型值在运行时携带类型信息
 *
 * 示例：
 *   const woort_IRValue* dyn = woort_IRBlock_boxdyn(
 *       block, int_val, WOORT_IR_TYPE_INTEGER);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_boxdyn(
    woort_IRBlock* block,
    const woort_IRValue* value,
    woort_IRType value_type);

/*
 * woort_IRBlock_unboxdyn
 *
 * 将动态类型值拆箱为静态类型。
 *
 * 参数：
 *   block          - 基本块实例
 *   dyn_value      - 动态类型值
 *   expected_type  - 期望的类型
 *
 * 返回值：
 *   代表静态类型值的 IRValue 指针
 *
 * 说明：
 * - 生成 UNBOXDYN 指令
 * - 将动态类型的值拆箱为指定的静态类型
 * - 如果运行时类型不匹配，触发 panic
 *
 * 示例：
 *   const woort_IRValue* int_val = woort_IRBlock_unboxdyn(
 *       block, dyn_val, WOORT_IR_TYPE_INTEGER);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_unboxdyn(
    woort_IRBlock* block,
    const woort_IRValue* dyn_value,
    woort_IRType expected_type);

/*
 * woort_IRBlock_checkdyn
 *
 * 检查动态类型值的运行时类型。
 *
 * 参数：
 *   block          - 基本块实例
 *   dyn_value      - 动态类型值
 *   expected_type  - 期望的类型
 *
 * 返回值：
 *   代表布尔结果的 IRValue 指针
 *
 * 说明：
 * - 生成 CHECKDYN 指令
 * - 检查动态类型值的运行时类型是否匹配
 * - 返回 true 如果类型匹配，否则返回 false
 *
 * 示例：
 *   const woort_IRValue* is_int = woort_IRBlock_checkdyn(
 *       block, dyn_val, WOORT_IR_TYPE_INTEGER);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_checkdyn(
    woort_IRBlock* block,
    const woort_IRValue* dyn_value,
    woort_IRType expected_type);

/*
 * woort_IRBlock_pushboxdyn
 *
 * 将值压栈并装箱为动态类型。
 *
 * 参数：
 *   block      - 基本块实例
 *   value      - 要装箱的值
 *   value_type - 值的类型
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 PUSHBOXDYN 指令
 * - 将值装箱并直接压入栈中
 * - 用于准备可变参数函数的参数
 */
WOORT_NODISCARD bool woort_IRBlock_pushboxdyn(
    woort_IRBlock* block,
    const woort_IRValue* value,
    woort_IRType value_type);

/* ============================================================================
 * 第十三部分：字符串操作指令
 *
 * 字符串操作指令用于对字符串值进行运算和比较。
 * ============================================================================
 */

/*
 * woort_IRBlock_adds
 *
 * 字符串拼接：result = a + b
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表拼接结果的 IRValue 指针（字符串类型）
 *
 * 说明：
 * - 生成 ADDS 指令
 * - 返回 a 和 b 拼接后的新字符串
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_adds(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_lts
 *
 * 字符串小于比较：result = (a < b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 LTS 指令
 * - 按字典序比较字符串
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_lts(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_gts
 *
 * 字符串大于比较：result = (a > b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 GTS 指令
 * - 按字典序比较字符串
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_gts(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_les
 *
 * 字符串小于等于比较：result = (a <= b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 LES 指令
 * - 按字典序比较字符串
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_les(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_ges
 *
 * 字符串大于等于比较：result = (a >= b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 GES 指令
 * - 按字典序比较字符串
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ges(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_eqs
 *
 * 字符串相等比较：result = (a == b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 EQS 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_eqs(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/*
 * woort_IRBlock_nes
 *
 * 字符串不等比较：result = (a != b)
 *
 * 参数：
 *   block - 基本块实例
 *   a     - 第一个字符串
 *   b     - 第二个字符串
 *
 * 返回值：
 *   代表比较结果的布尔 IRValue 指针
 *
 * 说明：
 * - 生成 NES 指令
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_nes(
    woort_IRBlock* block,
    const woort_IRValue* a,
    const woort_IRValue* b);

/* ============================================================================
 * 第十四部分：解包和打包指令
 *
 * 解包和打包指令用于处理可变参数和参数包：
 * - unpack: 将向量或结构体解包为多个值
 * - packarg: 打包参数用于调用
 * ============================================================================
 */

/*
 * woort_IRBlock_unpackvec
 *
 * 解包向量元素到栈上。
 *
 * 参数：
 *   block     - 基本块实例
 *   vec       - 要解包的向量
 *   out_count - 输出参数，接收解包的元素数量
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 UNPACKVEC 或 UNPACKVECX 指令
 * - 将向量的所有元素压入栈
 * - 用于可变参数展开
 */
WOORT_NODISCARD bool woort_IRBlock_unpackvec(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    size_t* out_count);

/*
 * woort_IRBlock_unpackstruct
 *
 * 解包结构体字段到栈上。
 *
 * 参数：
 *   block      - 基本块实例
 *   struct_val - 要解包的结构体
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 UNPACKSTRUCT 指令
 * - 将结构体的所有字段按顺序压入栈
 */
WOORT_NODISCARD bool woort_IRBlock_unpackstruct(
    woort_IRBlock* block,
    const woort_IRValue* struct_val);

/*
 * woort_IRBlock_packarg
 *
 * 打包参数到指定位置。
 *
 * 参数：
 *   block     - 基本块实例
 *   arg_count - 参数数量
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 PACKARG 指令
 * - 将栈顶的 arg_count 个值打包
 * - 用于准备函数调用参数
 */
WOORT_NODISCARD bool woort_IRBlock_packarg(
    woort_IRBlock* block,
    size_t arg_count);

/* ============================================================================
 * 第十五部分：数据移动指令
 *
 * 数据移动指令用于在栈和全局存储之间移动数据。
 * ============================================================================
 */

/*
 * woort_IRBlock_mov
 *
 * 数据移动：dest = src
 *
 * 参数：
 *   block - 基本块实例
 *   dest  - 目标位置（IRStorage）
 *   src   - 源值
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 MOVLD 或 MOVST 指令
 * - 将 src 的值复制到 dest
 * - 等价于 woort_IRBlock_store(block, dest, src)
 */
WOORT_NODISCARD bool woort_IRBlock_mov(
    woort_IRBlock* block,
    woort_IRStorage* dest,
    const woort_IRValue* src);

/*
 * woort_IRBlock_load
 *
 * 从全局存储加载值。
 *
 * 参数：
 *   block        - 基本块实例
 *   global_index - 全局存储索引
 *
 * 返回值：
 *   代表加载值的 IRValue 指针
 *
 * 说明：
 * - 生成 LOAD 或 LOADEX 指令
 * - 从全局存储区加载值
 * - 等价于 woort_IRFunction_load_const
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_load(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_index);

/*
 * woort_IRBlock_store_global
 *
 * 存储值到全局存储。
 *
 * 参数：
 *   block        - 基本块实例
 *   global_index - 全局存储索引
 *   value        - 要存储的值
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 STORE 或 STOREEX 指令
 * - 将值存储到全局存储区
 */
WOORT_NODISCARD bool woort_IRBlock_store_global(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_index,
    const woort_IRValue* value);

/* ============================================================================
 * 第十六部分：栈操作指令
 *
 * 栈操作指令用于直接操作栈，主要用于特殊场景。
 * 通常应优先使用更高层的抽象接口。
 * ============================================================================
 */

/*
 * woort_IRBlock_push
 *
 * 将值压入栈。
 *
 * 参数：
 *   block - 基本块实例
 *   value - 要压栈的值
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 PUSHSCHK 或 PUSHCCHK 指令
 * - 将值压入栈顶
 * - 通常不需要直接使用，函数调用会自动处理
 */
WOORT_NODISCARD bool woort_IRBlock_push(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_push_const
 *
 * 将常量压入栈。
 *
 * 参数：
 *   block        - 基本块实例
 *   global_index - 常量的全局存储索引
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 PUSHCCHK 或 PUSHCCHKEXT 指令
 * - 将常量值压入栈顶
 */
WOORT_NODISCARD bool woort_IRBlock_push_const(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_index);

/*
 * woort_IRBlock_ensure_stack
 *
 * 确保栈有足够空间。
 *
 * 参数：
 *   block  - 基本块实例
 *   size   - 需要的栈空间大小（以 Value 为单位）
 *
 * 返回值：
 *   true  - 成功
 *   false - 失败
 *
 * 说明：
 * - 生成 ASSURESSZ 指令
 * - 确保栈至少有 size 个位置的空间
 * - 通常由编译器自动处理
 */
WOORT_NODISCARD bool woort_IRBlock_ensure_stack(
    woort_IRBlock* block,
    size_t size);
