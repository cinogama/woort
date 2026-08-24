#include "woort.h"

#include "woort_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== 测试基础设施（与 test_dylib.c 相同风格） ========== */

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_BEGIN(name)                                        \
    do {                                                        \
        const char* _test_name = (name);                        \
        g_tests_run++;                                          \
        (void)printf("  [TEST] %-45s ", _test_name);            \
        fflush(stdout);

#define TEST_END()                                              \
    (void)printf("PASS\n");                                     \
    g_tests_passed++;                                           \
    } while(0)

#define TEST_ASSERT(cond)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            (void)printf("FAIL\n");                             \
            (void)printf("    assert failed: %s\n", #cond);     \
            (void)printf("    at %s:%d\n", __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while(0)

/* ========== 有界填充契约（bounded-fill contract） ========== */

#define SENTINEL 0xAA

/* "A漢🙂z" 的 UTF-8 编码：41 E6 BC A2 F0 9F 99 82 7A（9 字节，含 BMP 外字符，
   可覆盖 UTF-16 代理对与多字节 UTF-8 的截断边界情形）。 */
static const char SAMPLE_U8[] =
    "\x41" "\xE6\xBC\xA2" "\xF0\x9F\x99\x82" "\x7A";
#define SAMPLE_U8_LEN (sizeof(SAMPLE_U8) - 1)

static const char16_t SAMPLE_U16[] = {
    0x0041, 0x6F22, 0xD83D, 0xDE42, 0x007A, 0 };
#define SAMPLE_U16_LEN (sizeof(SAMPLE_U16) / sizeof(SAMPLE_U16[0]) - 1)

static const char32_t SAMPLE_U32[] = {
    0x00000041, 0x00006F22, 0x0001F642, 0x0000007A, 0 };
#define SAMPLE_U32_LEN (sizeof(SAMPLE_U32) / sizeof(SAMPLE_U32[0]) - 1)

#if defined(WOORT_PLATFORM_OS_WINDOWS)
static const wchar_t SAMPLE_W[] = {
    0x0041, 0x6F22, 0xD83D, 0xDE42, 0x007A, 0 };
#else
static const wchar_t SAMPLE_W[] = {
    0x00000041, 0x00006F22, 0x0001F642, 0x0000007A, 0 };
#endif
#define SAMPLE_W_LEN (sizeof(SAMPLE_W) / sizeof(SAMPLE_W[0]) - 1)

typedef size_t(*woort_fill_fn)(/* OPTIONAL */ void* outbuf, size_t buflen);

static void fill_sentinel(void* buf, size_t unitsz, size_t count)
{
    const unsigned char pattern = SENTINEL;
    unsigned char* p = (unsigned char*)buf;
    for (size_t i = 0; i < count * unitsz; ++i)
        p[i] = pattern;
}

static int unit_is_sentinel(const void* buf, size_t unitsz, size_t index)
{
    const unsigned char* p = (const unsigned char*)buf + index * unitsz;
    for (size_t i = 0; i < unitsz; ++i)
    {
        if (p[i] != SENTINEL)
            return 0;
    }
    return 1;
}

static int unit_is_nul(const void* buf, size_t unitsz, size_t index)
{
    const unsigned char* p = (const unsigned char*)buf + index * unitsz;
    for (size_t i = 0; i < unitsz; ++i)
    {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

/* 校验统一的缓冲区填充契约：
 *  1. buf=NULL、buflen=0 时仅返回所需长度（不含 0 结束符）；
 *  2. buflen > 所需长度时写入完整内容并补 0 结束符，其后字节不被触碰；
 *  3. buflen == 所需长度时写入全部内容但不写 0 结束符；
 *  4. buflen < 所需长度时最多写入 buflen 个单元（按码点边界截断，可能更少），
 *     不写 0 结束符，返回值仍为完整所需长度。 */
static int check_fill_contract(const char* name, woort_fill_fn fill, size_t unitsz)
{
    void* full = NULL;
    void* fit = NULL;
    void* trunc = NULL;
    int ok = 0;

    const size_t need = fill(NULL, 0);

    /* 参考结果：多分配一个哨兵单元检测越界写入。 */
    full = malloc(unitsz * (need + 2));
    if (full == NULL)
        goto done;
    fill_sentinel(full, unitsz, need + 2);
    if (fill(full, need + 1) != need)
    {
        (void)printf("    %s: roomy call returned wrong length\n", name);
        goto done;
    }
    if (!unit_is_nul(full, unitsz, need))
    {
        (void)printf("    %s: roomy call did not NUL-terminate\n", name);
        goto done;
    }
    if (!unit_is_sentinel(full, unitsz, need + 1))
    {
        (void)printf("    %s: roomy call wrote past the NUL terminator\n", name);
        goto done;
    }

    /* 精确适配：写入全部内容但不写 0 结束符。 */
    fit = malloc(unitsz * (need + 2));
    if (fit == NULL)
        goto done;
    fill_sentinel(fit, unitsz, need + 2);
    if (fill(fit, need) != need)
    {
        (void)printf("    %s: exact-fit call returned wrong length\n", name);
        goto done;
    }
    if (memcmp(fit, full, unitsz * need) != 0)
    {
        (void)printf("    %s: exact-fit content mismatch\n", name);
        goto done;
    }
    for (size_t i = need; i <= need + 1; ++i)
    {
        if (!unit_is_sentinel(fit, unitsz, i))
        {
            (void)printf("    %s: exact-fit call wrote a NUL or out of range\n", name);
            goto done;
        }
    }

    /* 截断：need-1 与 need/2 两个容量（后者用于截断代理对/多字节序列）。
       允许按码点边界少写，但不得写到容量下标处或之后，也不得写 0 结束符。 */
    trunc = malloc(unitsz * (need + 2));
    if (trunc == NULL)
        goto done;
    static const size_t caps[] = { (size_t)-1, 2 };
    for (size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); ++c)
    {
        size_t cap = (caps[c] == (size_t)-1)
            ? ((need > 0) ? need - 1 : 0)
            : (need / 2);
        if (cap == 0)
            continue;

        fill_sentinel(trunc, unitsz, need + 2);
        if (fill(trunc, cap) != need)
        {
            (void)printf("    %s: truncated call returned wrong length\n", name);
            goto done;
        }

        size_t written = cap;
        for (size_t i = 0; i < cap; ++i)
        {
            if (memcmp((const char*)trunc + i * unitsz,
                       (const char*)full + i * unitsz, unitsz) != 0)
            {
                written = i;
                break;
            }
        }
        for (size_t i = written; i <= need + 1; ++i)
        {
            if (!unit_is_sentinel(trunc, unitsz, i))
            {
                (void)printf("    %s: truncated call wrote at/after cap (%zu)\n",
                    name, cap);
                goto done;
            }
        }
    }

    ok = 1;

done:
    free(full);
    free(fit);
    free(trunc);
    return ok;
}

/* ========== 各 API 的填充适配器 ========== */

static size_t fill_exe_path(void* buf, size_t bufsz)
{
    return woort_exe_path((char*)buf, bufsz);
}

static size_t fill_work_path(void* buf, size_t bufsz)
{
    return woort_work_path((char*)buf, bufsz);
}

static size_t fill_get_file_loc(void* buf, size_t bufsz)
{
    return woort_get_file_loc("a/bc/def.txt", (char*)buf, bufsz);
}

static size_t fill_str_to_wstr(void* buf, size_t bufsz)
{
    return woort_str_to_wstr(SAMPLE_U8, (wchar_t*)buf, bufsz);
}

static size_t fill_strn_to_wstr(void* buf, size_t bufsz)
{
    return woort_strn_to_wstr(SAMPLE_U8, SAMPLE_U8_LEN, (wchar_t*)buf, bufsz);
}

static size_t fill_wstr_to_str(void* buf, size_t bufsz)
{
    return woort_wstr_to_str(SAMPLE_W, (char*)buf, bufsz);
}

static size_t fill_wstrn_to_str(void* buf, size_t bufsz)
{
    return woort_wstrn_to_str(SAMPLE_W, SAMPLE_W_LEN, (char*)buf, bufsz);
}

static size_t fill_str_to_u16str(void* buf, size_t bufsz)
{
    return woort_str_to_u16str(SAMPLE_U8, (char16_t*)buf, bufsz);
}

static size_t fill_strn_to_u16str(void* buf, size_t bufsz)
{
    return woort_strn_to_u16str(SAMPLE_U8, SAMPLE_U8_LEN, (char16_t*)buf, bufsz);
}

static size_t fill_u16str_to_str(void* buf, size_t bufsz)
{
    return woort_u16str_to_str(SAMPLE_U16, (char*)buf, bufsz);
}

static size_t fill_u16strn_to_str(void* buf, size_t bufsz)
{
    return woort_u16strn_to_str(SAMPLE_U16, SAMPLE_U16_LEN, (char*)buf, bufsz);
}

static size_t fill_str_to_u32str(void* buf, size_t bufsz)
{
    return woort_str_to_u32str(SAMPLE_U8, (char32_t*)buf, bufsz);
}

static size_t fill_strn_to_u32str(void* buf, size_t bufsz)
{
    return woort_strn_to_u32str(SAMPLE_U8, SAMPLE_U8_LEN, (char32_t*)buf, bufsz);
}

static size_t fill_u32str_to_str(void* buf, size_t bufsz)
{
    return woort_u32str_to_str(SAMPLE_U32, (char*)buf, bufsz);
}

static size_t fill_u32strn_to_str(void* buf, size_t bufsz)
{
    return woort_u32strn_to_str(SAMPLE_U32, SAMPLE_U32_LEN, (char*)buf, bufsz);
}

/* ========== 契约测试 ========== */

static void test_path_fill_contract(void)
{
    TEST_BEGIN("exe_path bounded-fill contract");
    TEST_ASSERT(check_fill_contract("exe_path", fill_exe_path, 1));
    TEST_END();

    TEST_BEGIN("work_path bounded-fill contract");
    TEST_ASSERT(check_fill_contract("work_path", fill_work_path, 1));
    TEST_END();

    TEST_BEGIN("get_file_loc bounded-fill contract");
    TEST_ASSERT(check_fill_contract("get_file_loc", fill_get_file_loc, 1));
    TEST_END();
}

static void test_conversion_fill_contract(void)
{
    TEST_BEGIN("str_to_wstr bounded-fill contract");
    TEST_ASSERT(check_fill_contract("str_to_wstr", fill_str_to_wstr, sizeof(wchar_t)));
    TEST_END();

    TEST_BEGIN("strn_to_wstr bounded-fill contract");
    TEST_ASSERT(check_fill_contract("strn_to_wstr", fill_strn_to_wstr, sizeof(wchar_t)));
    TEST_END();

    TEST_BEGIN("wstr_to_str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("wstr_to_str", fill_wstr_to_str, 1));
    TEST_END();

    TEST_BEGIN("wstrn_to_str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("wstrn_to_str", fill_wstrn_to_str, 1));
    TEST_END();

    TEST_BEGIN("str_to_u16str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("str_to_u16str", fill_str_to_u16str, sizeof(char16_t)));
    TEST_END();

    TEST_BEGIN("strn_to_u16str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("strn_to_u16str", fill_strn_to_u16str, sizeof(char16_t)));
    TEST_END();

    TEST_BEGIN("u16str_to_str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("u16str_to_str", fill_u16str_to_str, 1));
    TEST_END();

    TEST_BEGIN("u16strn_to_str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("u16strn_to_str", fill_u16strn_to_str, 1));
    TEST_END();

    TEST_BEGIN("str_to_u32str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("str_to_u32str", fill_str_to_u32str, sizeof(char32_t)));
    TEST_END();

    TEST_BEGIN("strn_to_u32str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("strn_to_u32str", fill_strn_to_u32str, sizeof(char32_t)));
    TEST_END();

    TEST_BEGIN("u32str_to_str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("u32str_to_str", fill_u32str_to_str, 1));
    TEST_END();

    TEST_BEGIN("u32strn_to_str bounded-fill contract");
    TEST_ASSERT(check_fill_contract("u32strn_to_str", fill_u32strn_to_str, 1));
    TEST_END();
}

static void test_conversion_contents(void)
{
    TEST_BEGIN("conversion contents round-trip");

    char16_t u16buf[SAMPLE_U16_LEN + 2];
    fill_sentinel(u16buf, sizeof(char16_t), SAMPLE_U16_LEN + 2);
    TEST_ASSERT(woort_str_to_u16str(SAMPLE_U8, u16buf, SAMPLE_U16_LEN + 1)
        == SAMPLE_U16_LEN);
    TEST_ASSERT(memcmp(u16buf, SAMPLE_U16,
        SAMPLE_U16_LEN * sizeof(char16_t)) == 0);
    TEST_ASSERT(u16buf[SAMPLE_U16_LEN] == 0);

    char32_t u32buf[SAMPLE_U32_LEN + 2];
    fill_sentinel(u32buf, sizeof(char32_t), SAMPLE_U32_LEN + 2);
    TEST_ASSERT(woort_str_to_u32str(SAMPLE_U8, u32buf, SAMPLE_U32_LEN + 1)
        == SAMPLE_U32_LEN);
    TEST_ASSERT(memcmp(u32buf, SAMPLE_U32,
        SAMPLE_U32_LEN * sizeof(char32_t)) == 0);
    TEST_ASSERT(u32buf[SAMPLE_U32_LEN] == 0);

    char u8buf1[SAMPLE_U8_LEN + 2];
    fill_sentinel(u8buf1, 1, sizeof(u8buf1));
    TEST_ASSERT(woort_u16str_to_str(SAMPLE_U16, u8buf1, SAMPLE_U8_LEN + 1)
        == SAMPLE_U8_LEN);
    TEST_ASSERT(memcmp(u8buf1, SAMPLE_U8, SAMPLE_U8_LEN) == 0);
    TEST_ASSERT(u8buf1[SAMPLE_U8_LEN] == '\0');

    char u8buf2[SAMPLE_U8_LEN + 2];
    fill_sentinel(u8buf2, 1, sizeof(u8buf2));
    TEST_ASSERT(woort_u32str_to_str(SAMPLE_U32, u8buf2, SAMPLE_U8_LEN + 1)
        == SAMPLE_U8_LEN);
    TEST_ASSERT(memcmp(u8buf2, SAMPLE_U8, SAMPLE_U8_LEN) == 0);
    TEST_ASSERT(u8buf2[SAMPLE_U8_LEN] == '\0');

    char u8buf3[SAMPLE_U8_LEN + 2];
    fill_sentinel(u8buf3, 1, sizeof(u8buf3));
    TEST_ASSERT(woort_wstr_to_str(SAMPLE_W, u8buf3, SAMPLE_U8_LEN + 1)
        == SAMPLE_U8_LEN);
    TEST_ASSERT(memcmp(u8buf3, SAMPLE_U8, SAMPLE_U8_LEN) == 0);
    TEST_ASSERT(u8buf3[SAMPLE_U8_LEN] == '\0');

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    TEST_ASSERT(SAMPLE_W_LEN == SAMPLE_U16_LEN);
    wchar_t wbuf[SAMPLE_U16_LEN + 2];
    fill_sentinel(wbuf, sizeof(wchar_t), SAMPLE_U16_LEN + 2);
    TEST_ASSERT(woort_str_to_wstr(SAMPLE_U8, wbuf, SAMPLE_U16_LEN + 1)
        == SAMPLE_U16_LEN);
    TEST_ASSERT(memcmp(wbuf, SAMPLE_U16,
        SAMPLE_U16_LEN * sizeof(wchar_t)) == 0);
    TEST_ASSERT(wbuf[SAMPLE_U16_LEN] == 0);
#else
    TEST_ASSERT(SAMPLE_W_LEN == SAMPLE_U32_LEN);
    wchar_t wbuf[SAMPLE_U32_LEN + 2];
    fill_sentinel(wbuf, sizeof(wchar_t), SAMPLE_U32_LEN + 2);
    TEST_ASSERT(woort_str_to_wstr(SAMPLE_U8, wbuf, SAMPLE_U32_LEN + 1)
        == SAMPLE_U32_LEN);
    TEST_ASSERT(memcmp(wbuf, SAMPLE_U32,
        SAMPLE_U32_LEN * sizeof(wchar_t)) == 0);
    TEST_ASSERT(wbuf[SAMPLE_U32_LEN] == 0);
#endif

    TEST_END();
}

static void test_get_file_loc_edges(void)
{
    TEST_BEGIN("get_file_loc edge cases");

    /* NULL path：返回 0，缓冲区仅在有富余时写入空串。 */
    TEST_ASSERT(woort_get_file_loc(NULL, NULL, 0) == 0);
    char buf[8];
    fill_sentinel(buf, 1, sizeof(buf));
    TEST_ASSERT(woort_get_file_loc(NULL, buf, sizeof(buf)) == 0);
    TEST_ASSERT(buf[0] == '\0');
    TEST_ASSERT(unit_is_sentinel(buf, 1, 1));

    /* 原地剥离：目录部分是路径的真前缀。 */
    char in_place[] = "a/bc/def.txt";
    TEST_ASSERT(woort_get_file_loc(in_place, in_place, sizeof(in_place)) == 4);
    TEST_ASSERT(strcmp(in_place, "a/bc") == 0);

    /* 无分隔符：返回 0。 */
    TEST_ASSERT(woort_get_file_loc("file.txt", buf, sizeof(buf)) == 0);
    TEST_ASSERT(buf[0] == '\0');

#if defined(WOORT_PLATFORM_OS_WINDOWS)
    /* 反斜杠分隔符被规范化为 '/'。 */
    char win_path[] = "a\\bc\\def.txt";
    TEST_ASSERT(woort_get_file_loc(win_path, win_path, sizeof(win_path)) == 4);
    TEST_ASSERT(strcmp(win_path, "a/bc") == 0);
#endif

    TEST_END();
}

/* ========== 主函数 ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== String-Buffer Fill Contract Tests ===\n\n");
    test_path_fill_contract();
    test_conversion_fill_contract();
    test_conversion_contents();
    test_get_file_loc_edges();

    (void)printf("\n=== Results: %d/%d passed ===\n\n",
        g_tests_passed, g_tests_run);

    woort_shutdown(NULL, NULL);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
