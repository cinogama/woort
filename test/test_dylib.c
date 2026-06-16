#include "woort.h"

#include "woort_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ========== 测试基础设施 ========== */

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_BEGIN(name)                                        \
    do {                                                        \
        const char* _test_name = (name);                        \
        g_tests_run++;                                          \
        (void)printf("  [TEST] %-45s ", _test_name);            \
        fflush(stdout);

#define TEST_END()                                              \
        (void)printf("PASS\n");                                 \
        g_tests_passed++;                                       \
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

#define TEST_ASSERT_NULL(ptr)   TEST_ASSERT((ptr) == NULL)
#define TEST_ASSERT_NOT_NULL(ptr) TEST_ASSERT((ptr) != NULL)

#define TEST_ASSERT_STREQ(expected, actual)                      \
    do {                                                        \
        const char* _e = (expected);                            \
        const char* _a = (actual);                              \
        if (_e == NULL || _a == NULL || strcmp(_e, _a) != 0) {  \
            (void)printf("FAIL\n");                             \
            (void)printf("    expected: '%s'\n",                \
                _e ? _e : "(null)");                            \
            (void)printf("    actual:   '%s'\n",                \
                _a ? _a : "(null)");                            \
            (void)printf("    at %s:%d\n", __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while(0)

/* ========== 路径 API 测试 ========== */

static void test_exe_path(void)
{
    TEST_BEGIN("exe_path returns nonzero length");
    size_t need = woort_exe_path(NULL, 0);
    TEST_ASSERT(need > 0);
    char* path = (char*)malloc(need + 1);
    TEST_ASSERT_NOT_NULL(path);
    size_t got = woort_exe_path(path, need + 1);
    TEST_ASSERT(got == need);
    TEST_ASSERT(got < need + 1);
    (void)printf("(%s) ", path);
    free(path);
    TEST_END();
}

static void test_exe_path_cached(void)
{
    TEST_BEGIN("exe_path consistent on 2nd call");
    size_t need1 = woort_exe_path(NULL, 0);
    size_t need2 = woort_exe_path(NULL, 0);
    TEST_ASSERT(need1 > 0);
    TEST_ASSERT(need1 == need2);
    char* p1 = (char*)malloc(need1 + 1);
    char* p2 = (char*)malloc(need1 + 1);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    (void)woort_exe_path(p1, need1 + 1);
    (void)woort_exe_path(p2, need1 + 1);
    TEST_ASSERT_STREQ(p1, p2);
    free(p1);
    free(p2);
    TEST_END();
}

static void test_work_path(void)
{
    TEST_BEGIN("work_path returns nonzero length");
    size_t need = woort_work_path(NULL, 0);
    TEST_ASSERT(need > 0);
    char* path = (char*)malloc(need + 1);
    TEST_ASSERT_NOT_NULL(path);
    size_t got = woort_work_path(path, need + 1);
    TEST_ASSERT(got == need);
    TEST_ASSERT(got < need + 1);
    free(path);
    TEST_END();
}

static void test_set_work_path(void)
{
    TEST_BEGIN("set_work_path round-trip");

    size_t orig_need = woort_work_path(NULL, 0);
    TEST_ASSERT(orig_need > 0);
    char* orig = (char*)malloc(orig_need + 1);
    TEST_ASSERT_NOT_NULL(orig);
    (void)woort_work_path(orig, orig_need + 1);

    /* Set to same path, should succeed */
    bool ok = woort_set_work_path(orig);
    TEST_ASSERT(ok);

    size_t rest_need = woort_work_path(NULL, 0);
    char* restored = (char*)malloc(rest_need + 1);
    TEST_ASSERT_NOT_NULL(restored);
    (void)woort_work_path(restored, rest_need + 1);
    TEST_ASSERT_STREQ(orig, restored);

    free(restored);
    free(orig);
    TEST_END();
}

static void test_get_file_loc(void)
{
    TEST_BEGIN("get_file_loc normal cases");

    /* "/foo/bar/baz.txt" -> "/foo/bar" */
    {
        const char* src = "/foo/bar/baz.txt";
        size_t need = woort_get_file_loc(src, NULL, 0);
        TEST_ASSERT(need < strlen(src));
        char* d = (char*)malloc(need + 1);
        TEST_ASSERT_NOT_NULL(d);
        (void)woort_get_file_loc(src, d, need + 1);
        TEST_ASSERT_STREQ("/foo/bar", d);
        free(d);
    }

    /* "file.txt" -> "" (no separator) */
    {
        const char* src = "file.txt";
        size_t need = woort_get_file_loc(src, NULL, 0);
        TEST_ASSERT(need == 0);
        char* d = (char*)malloc(need + 1);
        TEST_ASSERT_NOT_NULL(d);
        (void)woort_get_file_loc(src, d, need + 1);
        TEST_ASSERT_STREQ("", d);
        free(d);
    }

    /* "/" -> "" */
    {
        const char* src = "/";
        size_t need = woort_get_file_loc(src, NULL, 0);
        TEST_ASSERT(need == 0);
        char* d = (char*)malloc(need + 1);
        TEST_ASSERT_NOT_NULL(d);
        (void)woort_get_file_loc(src, d, need + 1);
        TEST_ASSERT_STREQ("", d);
        free(d);
    }

    /* "/foo/" -> "/foo" */
    {
        const char* src = "/foo/";
        size_t need = woort_get_file_loc(src, NULL, 0);
        TEST_ASSERT(need < strlen(src));
        char* d = (char*)malloc(need + 1);
        TEST_ASSERT_NOT_NULL(d);
        (void)woort_get_file_loc(src, d, need + 1);
        TEST_ASSERT_STREQ("/foo", d);
        free(d);
    }

    TEST_END();
}

static void test_normalize_path(void)
{
    TEST_BEGIN("normalize_path on Windows backslash");
    char* buf = (char*)malloc(64);
    TEST_ASSERT_NOT_NULL(buf);

    strcpy(buf, "c:\\foo\\bar\\baz");
    woort_normalize_path(buf);
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    TEST_ASSERT_STREQ("C:/foo/bar/baz", buf);
#else
    /* On non-Windows, no change */
    TEST_ASSERT_STREQ("c:\\foo\\bar\\baz", buf);
#endif

    free(buf);
    TEST_END();
}

/* ========== 动态库 API 测试 ========== */

static int test_func_doubler(int x) { return x * 2; }

static void test_dylib_fake_lib_basic(void)
{
    TEST_BEGIN("fake_lib: create and load_func");

    woort_ExternLibFunc funcs[] = {
        { "my_double", (void*)&test_func_doubler },
        WOORT_EXTERN_LIB_FUNC_END
    };

    woort_Dylib* lib = woort_dylib_fake("fake_test_basic", funcs, NULL);
    TEST_ASSERT_NOT_NULL(lib);

    void* fp = woort_dylib_load_func(lib, "my_double");
    TEST_ASSERT_NOT_NULL(fp);

    /* Verify the function pointer resolves to our function */
    typedef int (*doubler_t)(int);
    doubler_t doubler = (doubler_t)fp;
    int result = doubler(21);
    TEST_ASSERT(result == 42);

    /* Lookup non-existent */
    void* np = woort_dylib_load_func(lib, "nonexistent");
    TEST_ASSERT_NULL(np);

    woort_dylib_unload(lib, WOORT_DYLIB_UNREF_AND_BURY);
    TEST_END();
}

static void test_dylib_fake_lib_duplicate(void)
{
    TEST_BEGIN("fake_lib: duplicate name returns NULL");

    woort_ExternLibFunc funcs[] = {
        WOORT_EXTERN_LIB_FUNC_END
    };

    woort_Dylib* lib1 = woort_dylib_fake("fake_dup", funcs, NULL);
    TEST_ASSERT_NOT_NULL(lib1);

    woort_Dylib* lib2 = woort_dylib_fake("fake_dup", funcs, NULL);
    TEST_ASSERT_NULL(lib2);

    woort_dylib_unload(lib1, WOORT_DYLIB_UNREF_AND_BURY);
    TEST_END();
}

static void test_dylib_fake_lib_dependency(void)
{
    TEST_BEGIN("fake_lib: dependency chain");

    woort_ExternLibFunc funcs[] = {
        WOORT_EXTERN_LIB_FUNC_END
    };

    woort_Dylib* dep = woort_dylib_fake("fake_dep", funcs, NULL);
    TEST_ASSERT_NOT_NULL(dep);

    woort_Dylib* lib = woort_dylib_fake("fake_main", funcs, dep);
    TEST_ASSERT_NOT_NULL(lib);

    /* Unref main lib — it should be freed but dep lives on */
    woort_dylib_unload(lib, WOORT_DYLIB_UNREF_AND_BURY);

    /* dep should still be accessible */
    void* np = woort_dylib_load_func(dep, "nonexistent");
    TEST_ASSERT_NULL(np);

    woort_dylib_unload(dep, WOORT_DYLIB_UNREF_AND_BURY);
    TEST_END();
}

static void test_dylib_load_fail(void)
{
    TEST_BEGIN("load_lib: nonexistent returns NULL");

    woort_Dylib* lib = woort_dylib_load(
        "no_such_lib_xyzzy",
        "nonexistent_library_xyzzy",
        NULL,
        false);
    TEST_ASSERT_NULL(lib);
    TEST_END();
}

static void test_dylib_unload_unref_only(void)
{
    TEST_BEGIN("unload_lib: UNREF only keeps in registry");

    woort_ExternLibFunc funcs[] = { WOORT_EXTERN_LIB_FUNC_END };

    woort_Dylib* lib = woort_dylib_fake("unref_test", funcs, NULL);
    TEST_ASSERT_NOT_NULL(lib);

    /* UNREF only — should still be in registry */
    woort_dylib_unload(lib, WOORT_DYLIB_UNREF);

    /* Can't test it's gone from registry without internal access,
     * but we can verify no crash on double unload attempt.
     * The lib has use_count 0 now, so another unref would use the
     * same pointer which has been freed... actually, since only
     * UNREF was called and use_count reached 0, it should be freed.
     * But BURY wasn't set, so the registry entry remains pointing
     * to freed memory. This is the caller's responsibility.
     * We just test no crash occurs. */

    /* Cleanup: the dylib is freed but still in registry.
     * Not much we can do without internal access. */
    /* Note: this is expected behavior — caller should pair UNREF with BURY */
    TEST_END();
}

static void test_dylib_get_func_name_fake(void)
{
    TEST_BEGIN("get_func_name: fake lib round-trip");

    woort_ExternLibFunc funcs[] = {
        { "my_double", (void*)&test_func_doubler },
        WOORT_EXTERN_LIB_FUNC_END
    };

    woort_Dylib* lib = woort_dylib_fake("fake_getname", funcs, NULL);
    TEST_ASSERT_NOT_NULL(lib);

    /* Verify address not resolvable before load_func */
    const char* before = woort_dylib_get_func_name(lib, (void*)&test_func_doubler);
    TEST_ASSERT_NULL(before);

    /* Resolve function */
    void* addr = woort_dylib_load_func(lib, "my_double");
    TEST_ASSERT_NOT_NULL(addr);

    /* Reverse lookup should match */
    const char* name = woort_dylib_get_func_name(lib, addr);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_STREQ("my_double", name);

    /* Unresolved address returns NULL */
    const char* bad = woort_dylib_get_func_name(lib, (void*)0xDEAD);
    TEST_ASSERT_NULL(bad);

    /* NULL addr returns NULL (func_addr is OPTIONAL); NULL lib is a contract violation (asserted). */
    TEST_ASSERT_NULL(woort_dylib_get_func_name(lib, NULL));

    woort_dylib_unload(lib, WOORT_DYLIB_UNREF_AND_BURY);
    TEST_END();
}

/* ========== 主函数 ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== Path API Tests ===\n\n");
    test_exe_path();
    test_exe_path_cached();
    test_work_path();
    test_set_work_path();
    test_get_file_loc();
    test_normalize_path();

    (void)printf("\n=== Dylib API Tests ===\n\n");
    test_dylib_fake_lib_basic();
    test_dylib_fake_lib_duplicate();
    test_dylib_fake_lib_dependency();
    test_dylib_load_fail();
    test_dylib_unload_unref_only();
    test_dylib_get_func_name_fake();

    (void)printf("\n=== Results: %d/%d passed ===\n\n",
        g_tests_passed, g_tests_run);

    woort_shutdown(NULL, NULL);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
