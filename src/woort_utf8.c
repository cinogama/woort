#include "woort_utf8.h"
#include "woort_diagnosis.h"

#include <stdlib.h>
#include <stdio.h>
#include <wctype.h>
#include <string.h>
#include <stdbool.h>

size_t woort_u8charnlen(const char* u8charp, size_t bytelen)
{
    if (bytelen != 0)
    {
        const uint8_t u8ch = (uint8_t)(*u8charp);
        int8_t mask = (int8_t)(0b10000000);

        for (size_t i = 1; i < WOORT_UTF8MAXLEN; ++i)
        {
            mask >>= 1;

            if ((uint8_t)(mask) != ((uint8_t)(mask)&u8ch)
                || i >= bytelen)
                return i;
            else
            {
                if ((uint8_t)(0b10000000u)
                    != ((uint8_t)(u8charp[i]) & (uint8_t)(0b11000000u)))
                    return 1;
            }
        }
        return 1;
    }
    return 0;
}

size_t woort_u8strnlen(woort_string_t u8str, size_t bytelen)
{
    size_t result = 0;
    while (bytelen)
    {
        const size_t charlen = woort_u8charnlen(u8str, bytelen);
        bytelen -= charlen;
        u8str += charlen;
        ++result;
    }
    return result;
}

bool woort_u8strnchar(woort_string_t u8str, size_t bytelen, size_t* out_charsz)
{
    if (1 == (*out_charsz = woort_u8charnlen(u8str, bytelen)))
    {
        if ((uint8_t)(*u8str) & (uint8_t)(0b10000000))
            return false;
    }
    return true;
}

woort_string_t woort_u8substr(woort_string_t u8str, size_t bytelen, size_t from, size_t* out_len)
{
    const char* p = u8str;
    for (; from != 0 && bytelen != 0; --from)
    {
        const size_t charlen = woort_u8charnlen(p, bytelen);
        p += charlen;
        bytelen -= charlen;
    }
    *out_len = bytelen;
    return p;
}

woort_string_t woort_u8substrn(woort_string_t u8str, size_t bytelen, size_t from, size_t length, size_t* out_len)
{
    size_t step_a_len;
    const char* p = woort_u8substr(u8str, bytelen, from, &step_a_len);
    const char* p_end = woort_u8substr(p, step_a_len, length, &step_a_len);
    *out_len = (size_t)(p_end - p);
    return p;
}

woort_string_t woort_u8substrr(woort_string_t u8str, size_t bytelen, size_t from, size_t tail, size_t* out_len)
{
    return woort_u8substrn(u8str, bytelen, from, tail >= from ? (tail - from) + 1 : 0, out_len);
}

bool woort_u16hisurrogate(char16_t ch)
{
    return ch >= (char16_t)(0xD800u) && ch <= (char16_t)(0xDBFFu);
}

bool woort_u16losurrogate(char16_t ch)
{
    return ch >= (char16_t)(0xDC00u) && ch <= (char16_t)(0xDFFFu);
}

size_t woort_u8combineu32(const char* u8charp, size_t bytelen, char32_t* out_c32)
{
    const uint8_t* u8ptr = (const uint8_t*)(u8charp);
    const size_t charlen = woort_u8charnlen(u8charp, bytelen);

    switch (charlen)
    {
    case 0:
        *out_c32 = 0;
        return 0;
    case 1:
        *out_c32 = *u8ptr;
        break;
    case 2:
        *out_c32 = ((u8ptr[0] & 0x1F) << 6) | (u8ptr[1] & 0x3F);
        break;
    case 3:
        *out_c32 = ((u8ptr[0] & 0x0F) << 12) | ((u8ptr[1] & 0x3F) << 6) | (u8ptr[2] & 0x3F);
        break;
    case 4:
        *out_c32 = ((u8ptr[0] & 0x07) << 18) | ((u8ptr[1] & 0x3F) << 12) |
            ((u8ptr[2] & 0x3F) << 6) | (u8ptr[3] & 0x3F);
        break;
    default:
        *out_c32 = (char32_t)0xFFFD;
        break;
    }

    return charlen;
}

void woort_u32exractu8(char32_t ch32, char out_c8[WOORT_UTF8MAXLEN], size_t* out_u8len)
{
    if (ch32 <= 0x7F)
    {
        out_c8[0] = (char)(ch32);
        *out_u8len = 1;
    }
    else if (ch32 <= 0x7FF)
    {
        out_c8[0] = (char)(0xC0 | (ch32 >> 6));
        out_c8[1] = (char)(0x80 | (ch32 & 0x3F));
        *out_u8len = 2;
    }
    else if (ch32 <= 0xFFFF)
    {
        out_c8[0] = (char)(0xE0 | (ch32 >> 12));
        out_c8[1] = (char)(0x80 | ((ch32 >> 6) & 0x3F));
        out_c8[2] = (char)(0x80 | (ch32 & 0x3F));
        *out_u8len = 3;
    }
    else if (ch32 <= 0x10FFFF)
    {
        out_c8[0] = (char)(0xF0 | (ch32 >> 18));
        out_c8[1] = (char)(0x80 | ((ch32 >> 12) & 0x3F));
        out_c8[2] = (char)(0x80 | ((ch32 >> 6) & 0x3F));
        out_c8[3] = (char)(0x80 | (ch32 & 0x3F));
        *out_u8len = 4;
    }
    else
    {
        out_c8[0] = (char)(0xEF);
        out_c8[1] = (char)(0xBF);
        out_c8[2] = (char)(0xBD);
        *out_u8len = 3;
    }
}

size_t woort_u8combineu16(const char* u8charp, size_t bytelen, char16_t out_c16[WOORT_UTF16MAXLEN], size_t* out_u16len)
{
    char32_t codepoint;
    const size_t charlen = woort_u8combineu32(u8charp, bytelen, &codepoint);

    if (charlen == 0)
    {
        out_c16[0] = 0;
        *out_u16len = 0;
        return 0;
    }

    if (codepoint <= 0xFFFF)
    {
        out_c16[0] = (char16_t)(codepoint);
        *out_u16len = 1;
    }
    else if (codepoint <= 0x10FFFF)
    {
        codepoint -= 0x10000;
        out_c16[0] = (char16_t)(0xD800 + (codepoint >> 10));
        out_c16[1] = (char16_t)(0xDC00 + (codepoint & 0x3FF));
        *out_u16len = 2;
    }
    else
    {
        out_c16[0] = (char16_t)0xFFFD;
        *out_u16len = 1;
    }
    return charlen;
}

size_t woort_u16exractu8(const char16_t* u16charp, size_t charcount, char out_c8[WOORT_UTF8MAXLEN], size_t* out_u8len)
{
    if (charcount == 0)
    {
        out_c8[0] = '\0';
        *out_u8len = 0;
        return 0;
    }

    char32_t codepoint;
    if (charcount >= 2
        && woort_u16hisurrogate(u16charp[0])
        && woort_u16losurrogate(u16charp[1]))
    {
        codepoint = (char32_t)(
            ((u16charp[0] - (char16_t)(0xD800)) << (char16_t)(10))
            | (u16charp[1] - (char16_t)(0xDC00)))
            + (char32_t)(0x10000);
    }
    else
    {
        codepoint = (char32_t)(u16charp[0]);
    }

    woort_u32exractu8(codepoint, out_c8, out_u8len);
    if (*out_u8len >= 4)
        return 2;

    return 1;
}

char* woort_u8enstring(woort_string_t u8str, size_t bytelen, int force_unicode)
{
    char* result = (char*)malloc(bytelen * 6 + 3);
    if (!result) return NULL;

    result[0] = '\0';
    char escape_serial[13];
    size_t result_len = 0;

    const char* p = u8str;
    const char* p_end = u8str + bytelen;

    result[result_len++] = '"';

    while (p != p_end)
    {
        char16_t u16buf[WOORT_UTF16MAXLEN];
        size_t u16len = 0;

        const size_t this_char_u8_length = woort_u8combineu16(p, bytelen, u16buf, &u16len);

        switch (u16len)
        {
        case 1:
        {
            const wchar_t this_char = (wchar_t)(u16buf[0]);
            if (iswprint(this_char))
            {
                switch (this_char)
                {
                case L'\\':
                case L'"':
                    result[result_len++] = '\\';
                    /* fallthrough */
                default:
                {
                    size_t i;
                    for (i = 0; i < this_char_u8_length && result_len < bytelen * 6 + 2; ++i)
                        result[result_len++] = p[i];
                }
                break;
                }
            }
            else
            {
                int r;
                if (u16buf[0] > (char16_t)(0x00FFu) || force_unicode)
                {
                    r = snprintf(escape_serial, sizeof(escape_serial), "\\u%04X", (uint16_t)(u16buf[0]));
                }
                else
                {
                    r = snprintf(escape_serial, sizeof(escape_serial), "\\x%02X", (uint8_t)(u16buf[0]));
                }

                for (int i = 0; i < r && result_len < bytelen * 6 + 2; ++i)
                    result[result_len++] = escape_serial[i];
            }
            break;
        }
        case 2:
        {
            int r = snprintf(escape_serial, sizeof(escape_serial), "\\u%04X\\u%04X",
                (uint16_t)(u16buf[0]), (uint16_t)(u16buf[1]));

            for (int i = 0; i < r && result_len < bytelen * 6 + 2; ++i)
                result[result_len++] = escape_serial[i];
            break;
        }
        }

        p += this_char_u8_length;
    }

    result[result_len++] = '"';
    result[result_len] = '\0';

    return result;
}

char* woort_u8destring(woort_string_t enu8str_zero_term)
{
    size_t len = strlen(enu8str_zero_term);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    size_t result_len = 0;
    const char* p = enu8str_zero_term;

    if (*p == '"')
        ++p;

    while (*p)
    {
        const char pch = *p;
        if (pch == '\\')
        {
            const char pescch = *(++p);
            switch (pescch)
            {
            case 'a': result[result_len++] = '\a'; break;
            case 'b': result[result_len++] = '\b'; break;
            case 'f': result[result_len++] = '\f'; break;
            case 'n': result[result_len++] = '\n'; break;
            case 'r': result[result_len++] = '\r'; break;
            case 't': result[result_len++] = '\t'; break;
            case 'v': result[result_len++] = '\v'; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
            {
                uint8_t oct_ascii = (uint8_t)(pescch - '0');
                for (int i = 0; i < 2; i++)
                {
                    const char nextch = *++p;
                    if (nextch >= '0' && nextch <= '8')
                    {
                        oct_ascii = (uint8_t)(oct_ascii * 8 + (nextch - '0'));
                    }
                    else
                    {
                        --p;
                        break;
                    }
                }
                result[result_len++] = (char)(oct_ascii);
                break;
            }
            case 'x':
            {
                uint8_t hex_ascii = 0;
                for (int i = 0; i < 2; i++)
                {
                    const char nextch = *++p;
                    if (nextch >= '0' && nextch <= '9')
                    {
                        hex_ascii = (uint8_t)(hex_ascii * 16 + (nextch - '0'));
                    }
                    else if (nextch >= 'A' && nextch <= 'F')
                    {
                        hex_ascii = (uint8_t)(hex_ascii * 16 + (nextch - 'A') + 10);
                    }
                    else if (nextch >= 'a' && nextch <= 'f')
                    {
                        hex_ascii = (uint8_t)(hex_ascii * 16 + (nextch - 'a') + 10);
                    }
                    else
                    {
                        --p;
                        break;
                    }
                }
                result[result_len++] = (char)(hex_ascii);
                break;
            }
            case 'u':
            {
                char16_t hex_u16[WOORT_UTF16MAXLEN] = { 0 };
                size_t hex_u16_count = 1;

                for (int i = 0; i < 4; i++)
                {
                    const char nextch = *++p;
                    if (nextch >= '0' && nextch <= '9')
                    {
                        hex_u16[0] = (char16_t)(hex_u16[0] * 16 + (nextch - '0'));
                    }
                    else if (nextch >= 'A' && nextch <= 'F')
                    {
                        hex_u16[0] = (char16_t)(hex_u16[0] * 16 + (nextch - 'A') + 10);
                    }
                    else if (nextch >= 'a' && nextch <= 'f')
                    {
                        hex_u16[0] = (char16_t)(hex_u16[0] * 16 + (nextch - 'a') + 10);
                    }
                    else
                    {
                        --p;
                        break;
                    }
                }

                const char* second_p = p + 1;

                if (woort_u16hisurrogate(hex_u16[0])
                    && *second_p == '\\'
                    && *(++second_p) == 'u')
                {
                    for (int i = 0; i < 4; i++)
                    {
                        const char nextch = *++second_p;
                        if (nextch >= '0' && nextch <= '9')
                        {
                            hex_u16[1] = (char16_t)(hex_u16[1] * 16 + (nextch - '0'));
                        }
                        else if (nextch >= 'A' && nextch <= 'F')
                        {
                            hex_u16[1] = (char16_t)(hex_u16[1] * 16 + (nextch - 'A') + 10);
                        }
                        else if (nextch >= 'a' && nextch <= 'f')
                        {
                            hex_u16[1] = (char16_t)(hex_u16[1] * 16 + (nextch - 'a') + 10);
                        }
                        else
                        {
                            --second_p;
                            break;
                        }
                    }
                    hex_u16_count = 2;
                }

                char u8buf[WOORT_UTF8MAXLEN] = { 0 };
                size_t u8len = 0;

                if (1 < woort_u16exractu8(hex_u16, hex_u16_count, u8buf, &u8len))
                    p = second_p;

                for (size_t i = 0; i < u8len; ++i)
                    result[result_len++] = u8buf[i];
                break;
            }
            default:
                result[result_len++] = pescch;
            }
        }
        else if (pch == '"')
            break;
        else
            result[result_len++] = pch;

        ++p;
    }

    result[result_len] = '\0';
    return result;
}

char32_t* woort_u8strtou32(woort_string_t u8str, size_t bytelen, size_t* out_len)
{
    char32_t* result = (char32_t*)malloc((bytelen + 1) * sizeof(char32_t));
    if (!result) return NULL;

    size_t count = 0;
    while (bytelen != 0)
    {
        char32_t u32char;
        const size_t u8forward = woort_u8combineu32(u8str, bytelen, &u32char);
        result[count++] = u32char;
        u8str += u8forward;
        bytelen -= u8forward;
    }
    result[count] = 0;
    *out_len = count;
    return result;
}

char* woort_u32strtou8(const char32_t* u32charp, size_t u32len, size_t* out_len)
{
    char* result = (char*)malloc(u32len * 4 + 1);
    if (!result) return NULL;

    size_t result_len = 0;
    while (u32len != 0)
    {
        size_t u8len;
        char u8buf[WOORT_UTF8MAXLEN];
        woort_u32exractu8(*u32charp, u8buf, &u8len);

        for (size_t i = 0; i < u8len; ++i)
            result[result_len++] = u8buf[i];

        ++u32charp;
        --u32len;
    }
    result[result_len] = '\0';
    *out_len = result_len;
    return result;
}

char16_t* woort_u8strtou16(woort_string_t u8str, size_t bytelen, size_t* out_len)
{
    char16_t* result = (char16_t*)malloc((bytelen * 2 + 1) * sizeof(char16_t));
    if (!result) return NULL;

    size_t count = 0;
    while (bytelen != 0)
    {
        char16_t u16buf[WOORT_UTF16MAXLEN];
        size_t u16len = 0;
        const size_t u8forward = woort_u8combineu16(u8str, bytelen, u16buf, &u16len);

        for (size_t i = 0; i < u16len; ++i)
            result[count++] = u16buf[i];

        u8str += u8forward;
        bytelen -= u8forward;
    }
    result[count] = 0;
    *out_len = count;
    return result;
}

char* woort_u16strtou8(const char16_t* u16charp, size_t u16len, size_t* out_len)
{
    char* result = (char*)malloc(u16len * 3 + 1);
    if (!result) return NULL;

    size_t result_len = 0;
    while (u16len != 0)
    {
        char u8buf[WOORT_UTF8MAXLEN];
        size_t u8len;
        const size_t u16forward = woort_u16exractu8(u16charp, u16len, u8buf, &u8len);

        for (size_t i = 0; i < u8len; ++i)
            result[result_len++] = u8buf[i];

        u16charp += u16forward;
        u16len -= u16forward;
    }
    result[result_len] = '\0';
    *out_len = result_len;
    return result;
}

size_t woort_u16strcount(const char16_t* u16str)
{
    size_t count = 0;
    while (*(u16str++))
        ++count;
    return count;
}

size_t woort_u32strcount(const char32_t* u32str)
{
    size_t count = 0;
    while (*(u32str++))
        ++count;
    return count;
}

bool woort_u32isu16(char32_t ch32)
{
    return ch32 <= (char32_t)(0xFFFFu);
}

bool woort_u8stridx(const char* str, size_t size, size_t index, char32_t* out_ch)
{
    size_t result_byte_len;
    const char* u8idx = woort_u8substr(
        str,
        size,
        index,
        &result_byte_len);

    char32_t ch;
    if (result_byte_len == 0
        || 0 == woort_u8combineu32(u8idx, result_byte_len, &ch))
    {
        // Index out of range.
        return false;
    }
    *out_ch = ch;
    return true;
}
