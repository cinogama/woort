#pragma once

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const char* wo_string_t;

/*
 * UTF-8 字符串处理工具
 * 
 * 提供基本的 UTF-8 字符串操作功能，包括：
 * - 计算字符数（而非字节数）
 * - 按字符索引访问
 * - 子串提取
 */

/* 表示无效的字符串位置 */
#define WO_U8STR_NPOS ((size_t)(-1))

/*
 * 计算 UTF-8 字符串的字符数
 * 
 * @param u8str  UTF-8 编码的字符串
 * @return       字符串中的字符数量（不是字节数）
 */
size_t wo_u8strlen(wo_string_t u8str);

/*
 * 获取 UTF-8 字符串中指定字符索引位置的指针
 * 
 * @param u8str  UTF-8 编码的字符串
 * @param chidx  字符索引（从 0 开始）
 * @return       指向该字符位置的指针
 */
wo_string_t wo_u8stridxstr(wo_string_t u8str, size_t chidx);

/*
 * 获取 UTF-8 字符串中指定字符索引位置的宽字符
 * 
 * @param u8str  UTF-8 编码的字符串
 * @param chidx  字符索引（从 0 开始）
 * @return       该位置的宽字符（wchar_t）
 */
wchar_t wo_u8stridx(wo_string_t u8str, size_t chidx);

/*
 * 提取 UTF-8 字符串的子串
 * 
 * @param u8str        UTF-8 编码的字符串
 * @param from         起始字符索引
 * @param length       要提取的字符数量
 * @param out_sub_len  输出参数，返回子串的字节长度
 * @return             指向子串起始位置的指针
 */
wo_string_t wo_u8substr(wo_string_t u8str, size_t from, size_t length, size_t* out_sub_len);

#ifdef __cplusplus
}
#endif
