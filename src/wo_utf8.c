#include "wo_utf8.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*
 * 获取当前 UTF-8 字符的字节数
 * 
 * @param str  指向 UTF-8 字符的指针
 * @return     该字符占用的字节数（1-4），如果无法解码则返回 1
 */
static uint8_t wo_u8chsize(const char* str)
{
    mbstate_t mb = {0};
    size_t strlength = strlen(str);

    if (mbrlen(str, 1, &mb) == (size_t)-2)
    {
        if (strlength > 0)
        {
            uint8_t strsz = strlength > 255 ? 255 : (uint8_t)strlength;
            size_t chsize = mbrlen(str + 1, strsz - 1, &mb) + 1;

            /* 解码失败：0 表示编码错误，(size_t)-1 表示字符串被截断 */
            if (chsize == 0 || chsize == (size_t)-1)
                return 1;

            return (uint8_t)chsize;
        }
    }
    return 1;
}

size_t wo_u8strlen(wo_string_t u8str)
{
    size_t strlength = 0;
    while (*u8str)
    {
        strlength++;
        u8str += wo_u8chsize(u8str);
    }
    return strlength;
}

wo_string_t wo_u8stridxstr(wo_string_t u8str, size_t chidx)
{
    while (chidx && *u8str)
    {
        --chidx;
        u8str += wo_u8chsize(u8str);
    }
    return u8str;
}

wchar_t wo_u8stridx(wo_string_t u8str, size_t chidx)
{
    mbstate_t mb = {0};
    wchar_t wc;
    wo_string_t target_place = wo_u8stridxstr(u8str, chidx);
    size_t parse_result = mbrtowc(&wc, target_place, strlen(target_place), &mb);
    
    if (parse_result > 0 && parse_result != (size_t)-1 && parse_result != (size_t)-2)
        return wc;
    
    return (wchar_t)(unsigned char)*target_place;
}

wo_string_t wo_u8substr(wo_string_t u8str, size_t from, size_t length, size_t* out_sub_len)
{
    wo_string_t substr = wo_u8stridxstr(u8str, from);
    wo_string_t end_place = wo_u8stridxstr(u8str, from + length);
    *out_sub_len = (size_t)(end_place - substr);
    return substr;
}
