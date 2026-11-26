/*
 * test_gc_shutdown_trace.c
 *
 * 端到端验证关机清单的版式。woort_GC_shutdown 发现仍有 VM 未关闭时，输出警告、各 VM 的
 * 调用栈与未显示的数量。每个 VM 以标题行起头，它的调用栈缩进到标题之下一级，多个 VM
 * 连排时才能一眼看出哪些帧属于哪个 VM：
 *
 *   WOORT: 4 VM(s) have not been closed during shutdown.     总览，顶格
 *       VM <地址> trace:                                     VM 标题，缩进 4 格
 *           at ...                                          帧行，缩进 8 格
 *           ... (1 more frames not shown)                    与帧行对齐
 *       ...
 *       ... 1 more VM(s) not shown                           与标题对齐
 *
 * 关机时「仍在运行」的 VM 这样得到：4 个线程各建一个 VM 运行 main → f1 → f2 → f3 →
 * （CALLNFP）native 函数 park_until_dumped（下文简称 park），park 停住不返回，VM 便停在
 * f3 的这次调用上。主线程等 4 个 VM 全部停住后把标准错误接到临时文件，调用
 * woort_shutdown，再读回输出逐项断言。
 * 只断言第一份清单：关机循环每秒至多重发一次警告，极端调度下（某个线程恰跨秒界才销毁
 * 它的 VM）后面可能再跟一份，与这里要验证的版式无关。
 *
 * park 何时返回，全程靠握手，不靠 sleep 时长：
 *   * 关机循环对每个 VM 置 TERMINATE → 输出清单 → 触发一轮 GC；GC 开始时给所有 VM 置
 *     GC_CHECK，再等它们各自到检查点。停在 native 函数里的 VM 要等 park 返回才前进，
 *     而 park 等 GC_CHECK——所以 park 一返回，清单必然已经输出完毕。
 *   * 放行只认「TERMINATE 之后置位的 GC_CHECK」，即上面那一轮置位的。若在此之前恰有别的
 *     GC 轮次在途，它同样会给本 VM 置 GC_CHECK 并等它到检查点：park 代为应答（与检查点
 *     相同的自标记手续），否则两边互相等待；GC_MARK_FINISHED 不参与放行，一律应答。
 *   * 各处等待都带超时：异常时测试以断言失败收场，而不是挂起。
 *
 * 程序（各函数的全部指令共享该函数的 srcloc；行号为 srcloc 行号，显示时加 1）：
 *   f3  （行 16）  r = CALLNFP(park); ret r
 *   f2  （行 12）  r = CALLNWO(f3);  ret r
 *   f1  （行 8）   r = CALLNWO(f2);  ret r
 *   main（行 4）   r = CALLNWO(f1);  ret r
 * 停住时调用栈由内向外为 park、f3、f2、f1、main。第 0 层的位置是 VM 的当前地址——park 是
 * 没有注册到任何库的 native 函数，显示为 <unknown>；其余各层的位置是发起调用的指令。
 * 宿主调起的顶层帧之外没有返回地址，trace 到 main 为止。清单每个 VM 至多输出 4 帧，
 * main 落在「还有 1 帧未显示」里。
 */

#ifdef WOORT_STATIC_LIB

/* 混含内部头（woort_vm.h）需要完整版 woort_Value，
   与库内编译单元一致地声明 WOORT_IMPL；静态库下 WOORT_API 为空，链接不受影响 */
#define WOORT_IMPL 1

#include "woort.h"

#include "woort_threads.h"
#include "woort_vm.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define TEST_DUP    _dup
#define TEST_DUP2   _dup2
#define TEST_CLOSE  _close
#define TEST_FILENO _fileno
#else
#include <unistd.h>
#define TEST_DUP    dup
#define TEST_DUP2   dup2
#define TEST_CLOSE  close
#define TEST_FILENO fileno
#endif

/* ===================================================================== */

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

enum
{
    VM_COUNT = 4,          /* 多于清单配额（3 个），第 4 个只计入「未显示」 */
    WAIT_TIMEOUT_MS = 30000,
};

#define TEST_FILE "test_gc_shutdown_trace.wo"

#define LINE_MAIN 4u  /* main，显示为第 5 行 */
#define LINE_F1   8u  /* f1，显示为第 9 行 */
#define LINE_F2   12u /* f2，显示为第 13 行 */
#define LINE_F3   16u /* f3，显示为第 17 行 */

/* 清单的缩进：标题一级，帧行在标题之下再一级 */
#define INDENT_LEVEL "    "
#define HEADER_INDENT INDENT_LEVEL
#define FRAME_INDENT  HEADER_INDENT INDENT_LEVEL

static const char SUMMARY_LINE[] =
    "WOORT: 4 VM(s) have not been closed during shutdown.\n";

static char g_output[16384];

static woort_CodeEnv*        g_cenv = NULL;
static woort_IRConstantIndex g_c_main;

static woort_AtomicUInt32 g_parked_count;      /* 已停进 park 的 VM 数 */
static woort_AtomicInt32  g_give_up;           /* 置位后 park 不再等放行 */
static woort_AtomicInt32  g_park_unreleased;   /* park 等到超时仍未被放行 */

static woort_VmCallStatus g_status[VM_COUNT];

/* ===================================================================== */

/* 把 s 的前 n 个字符转义后存入 out（换行、回车显示为 \n、\r），用于打印比对失败的现场 */
static void escape_into(char* out, size_t cap, const char* s, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; ++i)
    {
        if (s[i] == '\n')
        {
            out[o++] = '\\';
            out[o++] = 'n';
        }
        else if (s[i] == '\r')
        {
            out[o++] = '\\';
            out[o++] = 'r';
        }
        else
            out[o++] = s[i];
    }
    out[o] = '\0';
}

/* 断言 expected 自 *cursor 起按序出现，并把 *cursor 推进到其后 */
static bool expect_text(const char** cursor, const char* expected)
{
    const size_t len = strlen(expected);
    if (strncmp(*cursor, expected, len) == 0)
    {
        *cursor += len;
        return true;
    }

    char want[512];
    char got[512];
    escape_into(want, sizeof(want), expected, len);
    escape_into(got, sizeof(got), *cursor, 100);
    printf("FAIL: expected \"%s\"\n"
           "     got      \"%s\"\n", want, got);
    return false;
}

/* 断言 marker 出现（忽略 *cursor 与它之间的输出），并把 *cursor 推进到其后 */
static bool expect_marker(const char** cursor, const char* marker)
{
    const char* const found = strstr(*cursor, marker);
    if (found != NULL)
    {
        *cursor = found + strlen(marker);
        return true;
    }

    char got[512];
    escape_into(got, sizeof(got), *cursor, 100);
    printf("FAIL: expected ...\"%s\"\n"
           "     got      \"%s\"\n", marker, got);
    return false;
}

/* 断言一帧行：位置落在 func 的 srcloc 上 */
static bool expect_frame(const char** cursor, const char* func, unsigned line)
{
    char expected[256];
    (void)snprintf(expected, sizeof(expected),
        FRAME_INDENT "at %s (%s:%u:1)\n", func, TEST_FILE, line + 1);
    return expect_text(cursor, expected);
}

/*
 * 断言一个 VM 的清单段：缩进一级的标题行（地址不关心），后随缩进两级的帧行。
 * 每一项都比对，不因前面某项不符就跳过后面的。
 */
static bool expect_vm_block(const char** cursor)
{
    bool ok = expect_text(cursor, HEADER_INDENT "VM ")
        && expect_marker(cursor, " trace:\n");

    /* 栈顶是 native 函数 park：没有注册到任何库，解析不出名字 */
    ok = expect_text(cursor, FRAME_INDENT "at <unknown>\n") && ok;
    ok = expect_frame(cursor, "f3", LINE_F3) && ok;
    ok = expect_frame(cursor, "f2", LINE_F2) && ok;
    ok = expect_frame(cursor, "f1", LINE_F1) && ok;
    ok = expect_text(cursor, FRAME_INDENT "... (1 more frames not shown)\n") && ok;
    return ok;
}

/* ===================================================================== */

/*
 * 停在这里不返回，直到关机清单输出完毕（见文件头）。
 */
woort_api park_until_dumped(void)
{
    woort_VMRuntime* const vm = woort_VMRuntime_current();

    (void)woort_atomic_fetch_add_explicit(
        &g_parked_count, 1, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);

    bool released = false;
    uint32_t waited_ms = 0;
    while (!released)
    {
        const uint32_t mask = woort_atomic_load_explicit(
            &vm->m_check_request_mask, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        if ((mask & WOORT_VMRUNTIME_CHECK_REQUEST_GC_MARK_FINISHED) != 0
            || ((mask & WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE) == 0
                && (mask & (WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK
                    | WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING)) != 0))
        {
            /* 在途 GC 轮次在等本 VM 到检查点：代为应答 */
            woort_VMRuntime_gc_checkpoint(vm);
        }
        else if ((mask & WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE) != 0
            && (mask & WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK) != 0)
        {
            /* 关机流程置 TERMINATE 在前、输出清单居中、GC 置 GC_CHECK 在后：清单已输出完毕 */
            released = true;
        }

        if (released
            || woort_atomic_load_explicit(
                &g_give_up, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE) != 0
            || waited_ms >= WAIT_TIMEOUT_MS)
            break;

        woort_thread_sleep_ms(1);
        ++waited_ms;
    }

    if (!released)
        woort_atomic_store_explicit(
            &g_park_unreleased, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    return woort_ret_int(0);
}

/* 建一个 VM 运行 main：它会一路调到 f3，停进 park；关机清单输出完毕后 park 返回，运行结束 */
static void vm_thread_entry(void* user_data)
{
    const uintptr_t index = (uintptr_t)user_data;
    woort_VmCallStatus status = WOORT_VM_CALL_STATUS_ABORTED;

    woort_VMRuntime* vm;
    if (woort_VMRuntime_create(&vm))
    {
        (void)woort_VMRuntime_swap(vm);

        woort_StackValue sv;
        if (woort_push_reserve(2, &sv))
        {
            woort_load_const(sv, g_cenv, g_c_main);
            status = woort_invoke(sv + 1, sv);
            woort_pop(2);
        }

        (void)woort_VMRuntime_swap(NULL);
        woort_VMRuntime_destroy(vm);
    }

    g_status[index] = status;
}

/* 等所有 VM 停进 park，返回停住的个数（超时则可能不足） */
static uint32_t wait_all_parked(void)
{
    uint32_t waited_ms = 0;
    while (woort_atomic_load_explicit(
               &g_parked_count, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE) < VM_COUNT
        && waited_ms < WAIT_TIMEOUT_MS)
    {
        woort_thread_sleep_ms(1);
        ++waited_ms;
    }

    return (uint32_t)woort_atomic_load_explicit(
        &g_parked_count, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

/*
 * 关机。woort_log 写标准错误，把它接到临时文件，读回输出（去掉可能随文本模式写入的
 * 回车）存入 g_output。返回是否捕获到了输出；无论成败都会执行关机。
 */
static bool shutdown_and_capture(void)
{
    FILE* const capture = tmpfile();

    (void)fflush(stderr);
    const int saved_stderr = capture != NULL ? TEST_DUP(2) : -1;
    /* dup2 成功时返回新描述符（这里是 2），只有 Windows 的 _dup2 才返回 0 */
    bool ok = saved_stderr >= 0
        && TEST_DUP2(TEST_FILENO(capture), 2) >= 0;

    woort_shutdown(NULL, NULL);

    (void)fflush(stderr);
    if (saved_stderr >= 0)
    {
        (void)TEST_DUP2(saved_stderr, 2);
        (void)TEST_CLOSE(saved_stderr);
    }

    g_output[0] = '\0';
    if (ok && fseek(capture, 0, SEEK_SET) == 0)
    {
        const size_t len = fread(g_output, 1, sizeof(g_output) - 1, capture);
        size_t kept = 0;
        for (size_t i = 0; i < len; ++i)
        {
            if (g_output[i] != '\r')
                g_output[kept++] = g_output[i];
        }
        g_output[kept] = '\0';
    }
    else
        ok = false;

    if (capture != NULL)
        (void)fclose(capture);
    return ok;
}

/* ===================================================================== */

static void check_shutdown_output(void)
{
    /* 清单之前可能还有别的输出，先定位到总览行 */
    const char* const found = strstr(g_output, SUMMARY_LINE);
    CHECK(found != NULL);
    if (found == NULL)
        return;

    const char* p = found + sizeof(SUMMARY_LINE) - 1;

    /* 清单配额 3 个 VM：各以标题起头，后随它自己的调用栈 */
    CHECK(expect_vm_block(&p));
    CHECK(expect_vm_block(&p));
    CHECK(expect_vm_block(&p));

    /* 第 4 个 VM 只给数量，与标题对齐 */
    CHECK(expect_text(&p, HEADER_INDENT "... 1 more VM(s) not shown\n"));
}

/* ===================================================================== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    /* ===== 程序：main → f1 → f2 → f3 → park ===== */
    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_f1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_park = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f1;
    woort_IRFunction* f2;
    woort_IRFunction* f3;
    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f1);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f2);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f3);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        /* f3：r = CALLNFP(park); ret r */
        woort_IRFunction_set_name(f3, "f3");
        (void)woort_IRFunction_push_srcloc(f3, TEST_FILE, LINE_F3, 0, LINE_F3, 200);
        woort_IRValue* r3 = woort_IRFunction_new_vreg(f3);
        (void)woort_IR_CALLNFP(f3, c_park, 0, r3);
        (void)woort_IR_ret(f3, r3);
        woort_IRFunction_pop_srcloc(f3);

        /* f2 / f1 / main：r = CALLNWO(<下一个>); ret r */
        woort_IRFunction_set_name(f2, "f2");
        (void)woort_IRFunction_push_srcloc(f2, TEST_FILE, LINE_F2, 0, LINE_F2, 200);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f2);
        (void)woort_IR_CALLNWO(f2, c_f3, 0, r2);
        (void)woort_IR_ret(f2, r2);
        woort_IRFunction_pop_srcloc(f2);

        woort_IRFunction_set_name(f1, "f1");
        (void)woort_IRFunction_push_srcloc(f1, TEST_FILE, LINE_F1, 0, LINE_F1, 200);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f1);
        (void)woort_IR_CALLNWO(f1, c_f2, 0, r1);
        (void)woort_IR_ret(f1, r1);
        woort_IRFunction_pop_srcloc(f1);

        woort_IRFunction_set_name(f_main, "main");
        (void)woort_IRFunction_push_srcloc(f_main, TEST_FILE, LINE_MAIN, 0, LINE_MAIN, 200);
        woort_IRValue* r0 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO(f_main, c_f1, 0, r0);
        (void)woort_IR_ret(f_main, r0);
        woort_IRFunction_pop_srcloc(f_main);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* f1_addr;
    const woort_Bytecode* f2_addr;
    const woort_Bytecode* f3_addr;
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f1, &f1_addr);
    (void)woort_CodeEnv_query_function(cenv, f2, &f2_addr);
    (void)woort_CodeEnv_query_function(cenv, f3, &f3_addr);
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    /* CALLNWO 读常量槽的 m_script_function（裸函数地址），须用 script_function 而非闭包 */
    woort_CodeEnv_set_const_script_function(cenv, c_f1, f1_addr);
    woort_CodeEnv_set_const_script_function(cenv, c_f2, f2_addr);
    woort_CodeEnv_set_const_script_function(cenv, c_f3, f3_addr);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    (void)woort_CodeEnv_set_const_extern_function(
        cenv, c_park, park_until_dumped, NULL, NULL);
    woort_CodeEnv_unlock(cenv);

    /* CodeEnv 在 finish 时已把函数名与源码路径收进自己的字符串池，不再依赖编译器 */
    (void)woort_IRCompiler_close(irc);

    g_cenv = cenv;
    g_c_main = c_main;

    /* ===== 4 个 VM 各自停进 park ===== */
    woort_Thread* threads[VM_COUNT] = { NULL };
    for (uintptr_t i = 0; i < VM_COUNT; ++i)
        CHECK(woort_thread_start(&vm_thread_entry, (void*)i, &threads[i]));

    const uint32_t parked = wait_all_parked();
    CHECK(parked == VM_COUNT); /* 清单里的 VM 数、各 VM 的调用栈都以全部停住为准 */
    if (parked != VM_COUNT)
        woort_atomic_store_explicit(
            &g_give_up, 1, WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    /* ===== 关机并捕获标准错误 ===== */
    const bool captured = shutdown_and_capture();
    CHECK(captured);

    for (uintptr_t i = 0; i < VM_COUNT; ++i)
    {
        if (threads[i] != NULL)
            woort_thread_join(threads[i]);
        printf("vm %" PRIuPTR ": status=%d\n", i, (int)g_status[i]);
    }

    CHECK(woort_atomic_load_explicit(
        &g_park_unreleased, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE) == 0);

    /* 清单原样打印，便于直接运行时查看实际版式 */
    printf("---- shutdown output ----\n%s"
           "---- shutdown output end ----\n", g_output);

    check_shutdown_output();

    if (g_failures == 0)
    {
        printf("test_gc_shutdown_trace: ALL PASS\n");
        return 0;
    }
    printf("test_gc_shutdown_trace: %d FAILURES\n", g_failures);
    return 1;
}

#else

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
}

#endif
