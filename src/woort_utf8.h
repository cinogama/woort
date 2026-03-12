#pragma once

#include <stddef.h>
#include <wchar.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 确保 char16_t 和 char32_t 在 C 模式下可用 (MSVC) */
#if defined(_MSC_VER) && !defined(__cplusplus)
    #ifndef _CHAR16T
        #define _CHAR16T
        typedef uint16_t char16_t;
    #endif
    #ifndef _CHAR32T
        #define _CHAR32T
        typedef uint32_t char32_t;
    #endif
#endif

#define WOORT_UTF8MAXLEN MB_LEN_MAX
#define WOORT_UTF16MAXLEN 2

typedef const char* woort_string_t;

/* UTF-8 字符长度查询 */
size_t woort_u8charnlen(const char* u8charp, size_t bytelen);
size_t woort_u8strnlen(woort_string_t u8str, size_t bytelen);
bool woort_u8strnchar(woort_string_t u8str, size_t bytelen, size_t* out_charsz);

/* 子串操作 */
woort_string_t woort_u8substr(woort_string_t u8str, size_t bytelen, size_t from, size_t* out_len);
woort_string_t woort_u8substrr(woort_string_t u8str, size_t bytelen, size_t from, size_t tail, size_t* out_len);
woort_string_t woort_u8substrn(woort_string_t u8str, size_t bytelen, size_t from, size_t length, size_t* out_len);

/* UTF-8 <-> UTF-32 转换 */
size_t woort_u8combineu32(const char* u8charp, size_t bytelen, char32_t* out_c32);
void woort_u32exractu8(char32_t ch32, char out_c8[WOORT_UTF8MAXLEN], size_t* out_u8len);

/* UTF-8 <-> UTF-16 转换 */
size_t woort_u8combineu16(const char* u8charp, size_t bytelen, char16_t out_c16[WOORT_UTF16MAXLEN], size_t* out_u16len);
size_t woort_u16exractu8(const char16_t* u16charp, size_t charcount, char out_c8[WOORT_UTF8MAXLEN], size_t* out_u8len);

/* UTF-16 代理对判断 */
bool woort_u16hisurrogate(char16_t ch);
bool woort_u16losurrogate(char16_t ch);

/* 字符串编码/解码（返回动态分配的字符串，需调用者释放） */
char* woort_u8enstring(woort_string_t u8str, size_t bytelen, int force_unicode);
char* woort_u8destring(woort_string_t enu8str_zero_term);

/* UTF-8 <-> UTF-32 字符串转换（返回动态分配的字符串，需调用者释放） */
char32_t* woort_u8strtou32(woort_string_t u8str, size_t bytelen, size_t* out_len);
char* woort_u32strtou8(const char32_t* u32charp, size_t u32len, size_t* out_len);

/* UTF-8 <-> UTF-16 字符串转换（返回动态分配的字符串，需调用者释放） */
char16_t* woort_u8strtou16(woort_string_t u8str, size_t bytelen, size_t* out_len);
char* woort_u16strtou8(const char16_t* u16charp, size_t u16len, size_t* out_len);

/* 字符串长度计算 */
size_t woort_u16strcount(const char16_t* u16str);
size_t woort_u32strcount(const char32_t* u32str);

/* 类型判断 */
bool woort_u32isu16(char32_t ch32);

#ifdef __cplusplus
}
#endif
