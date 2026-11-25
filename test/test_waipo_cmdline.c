/*
 * test_waipo_cmdline.c
 *
 * 端到端验证命令行调试器（woort_WAIPO_Debugger_process_cmdline）围绕 bt / frame 的
 * 调用栈展示：
 *   1. bt 为每一帧输出层号：自栈顶 #0 起向外递增，与 frame 命令的 frameid 同一套编号；
 *   2. frame 按层号选中一帧后，输出同一格式的帧行并展示该帧源码，bt 以 '>' 标出该帧；
 *   3. frame 不带层号时重新显示当前帧；层号越界 / 非数字时给出提示，选中帧不变；
 *   4. bt <depth> 截断调用栈时说明剩余帧数；选中帧被截掉时另行提示；
 *   5. vm 列表里各 VM 的调用栈同样按层号输出（不标记选中帧）。
 *
 * 在陷阱回调里驱动 REPL：把标准输入接到预先写好的命令脚本、标准输出接到临时文件，
 * 会话结束后读回输出按序断言。源码经 VFS 提供（woovf:// 虚拟文件），使 frame 命令
 * 能展示真实的源码行。
 *
 * 程序（各函数的全部指令共享该函数的 srcloc，覆盖该行起 200 列，source 命令整行高亮）：
 *   f3  （行 14）  v = ADDI(a, b); ret v
 *   f2  （行 9）   r = CALLNWO(f3); v = ADDI(r, a); ret v
 *   main（行 4）   r = CALLNWO(f2); v = ADDI(r, a); ret v
 * 行号为 srcloc 行号（0 起始），source 命令显示时加 1。断点设在 f3 所在行，命中时
 * 调用栈由内向外为 f3、f2、main，共 3 层：第 0 层的位置是当前指令，其余各层的位置
 * 是发起调用的指令，均落在各自函数的 srcloc 上。
 */

#ifdef WOORT_STATIC_LIB

/* 混含内部头（woort_waipo_debugger.h 等）需要完整版 woort_Value，
   与库内编译单元一致地声明 WOORT_IMPL；静态库下 WOORT_API 为空，链接不受影响 */
#define WOORT_IMPL 1

#include "woort.h"
#include "woort_waipo_debugger.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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

#define TEST_FILE "test_waipo_cmdline.wo"
#define TEST_FILE_URI "woovf://" TEST_FILE

#define LINE_MAIN 4u  /* main，source 命令显示为第 5 行 */
#define LINE_F2 9u    /* f2，显示为第 10 行 */
#define LINE_F3 14u   /* f3，显示为第 15 行 */

/*
 * 虚拟源码：每行以自身的行号开头，便于与 source 命令输出的行号列对照。
 * 行 5 为 main，行 10 为 f2，行 15 为 f3。
 */
static const char TEST_SOURCE[] =
    " 1| // fake source, provided through the VFS\n"
    " 2| var a = 30\n"
    " 3| var b = 12\n"
    " 4| \n"
    " 5| fn main() { r = f2(); return r + a }\n"
    " 6| \n"
    " 7| \n"
    " 8| \n"
    " 9| \n"
    "10| fn f2() { r = f3(); return r + a }\n"
    "11| \n"
    "12| \n"
    "13| \n"
    "14| \n"
    "15| fn f3() { return a + b }\n"
    "16| \n"
    "17| // end\n";

/*
 * 喂给 REPL 的命令脚本（空行会重复上一条命令，脚本中不能留空行）：
 *   bt             三层调用栈，选中栈顶帧
 *   f 1            选中 f2，展示其源码
 *   bt             f2 所在帧带 '>'
 *   f              不带层号：重新显示当前帧
 *   f 9            越界
 *   f abc          非数字
 *   bt 2           只留最内两层；前两条失败的 frame 不得改变选中帧
 *   f 2            选中 main，展示其源码
 *   bt 1           选中帧被截掉
 *   vm             列表中的调用栈同样带层号
 *   c              结束会话
 */
static const char TEST_COMMANDS[] =
    "bt\n"
    "f 1\n"
    "bt\n"
    "f\n"
    "f 9\n"
    "f abc\n"
    "bt 2\n"
    "f 2\n"
    "bt 1\n"
    "vm\n"
    "c\n";

static char g_output[65536];

static bool g_session_attempted = false;
static bool g_session_ok = false;
static woort_WAIPO_TrapEndBehavior g_session_behavior = WOORT_WAIPO_TRAP_CONTINUE;

/* ===================================================================== */

/* 断言 expected 自 *cursor 起按序出现，并把 *cursor 推进到其后 */
static bool expect_text(const char** cursor, const char* expected)
{
    const size_t len = strlen(expected);
    if (strncmp(*cursor, expected, len) == 0)
    {
        *cursor += len;
        return true;
    }

    printf("FAIL: expected \"%s\"\n"
           "     got      \"%.200s\"\n", expected, *cursor);
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

    printf("FAIL: expected ...\"%s\"\n"
           "     got      \"%.200s\"\n", marker, *cursor);
    return false;
}

/* 断言一帧行，selected 决定层号前是否带 '>'（各帧缩进相同、层号对齐） */
static bool expect_frame(
    const char** cursor, bool selected, const char* id, const char* func, unsigned line)
{
    char expected[256];
    (void)snprintf(expected, sizeof(expected), "%s#%s  %s (%s:%u:1)\n",
        selected ? "  > " : "    ", id, func, TEST_FILE_URI, line);
    return expect_text(cursor, expected);
}

/* 断言 source 命令高亮显示了第 line 行（内容为 content），其余行不关心 */
static bool expect_source_line(const char** cursor, unsigned line, const char* content)
{
    char expected[512];
    (void)snprintf(expected, sizeof(expected),
        "> %5u | " WOORT_ANSI_INV "%s" WOORT_ANSI_RST " \n", line, content);
    return expect_marker(cursor, expected);
}

/*
 * 把标准输入接到命令脚本、标准输出接到临时文件，驱动一次 REPL 会话。
 * 返回会话的结束行为，会话输出读入 g_output；重定向失败时返回 false。
 */
static bool run_session(
    woort_WAIPO_Debugger* debugger,
    woort_VMRuntime* vm,
    woort_WAIPO_TrapEndBehavior* out_behavior)
{
    FILE* const input = tmpfile();
    FILE* const capture = tmpfile();
    if (input == NULL || capture == NULL)
    {
        if (input != NULL)
            (void)fclose(input);
        if (capture != NULL)
            (void)fclose(capture);
        return false;
    }

    bool ok = fputs(TEST_COMMANDS, input) != EOF
        && fseek(input, 0, SEEK_SET) == 0;

    /* 重定向期间不得向 stdout 打印，否则会混入会话输出 */
    (void)fflush(stdout);
    const int saved_stdin = ok ? TEST_DUP(0) : -1;
    const int saved_stdout = saved_stdin >= 0 ? TEST_DUP(1) : -1;
    ok = saved_stdout >= 0
        && TEST_DUP2(TEST_FILENO(input), 0) == 0
        && TEST_DUP2(TEST_FILENO(capture), 1) == 0;

    *out_behavior = WOORT_WAIPO_TRAP_CONTINUE;
    if (ok)
        *out_behavior = woort_WAIPO_Debugger_process_cmdline(debugger, vm);

    (void)fflush(stdout);
    if (saved_stdout >= 0)
    {
        (void)TEST_DUP2(saved_stdout, 1);
        (void)TEST_CLOSE(saved_stdout);
    }
    if (saved_stdin >= 0)
    {
        (void)TEST_DUP2(saved_stdin, 0);
        (void)TEST_CLOSE(saved_stdin);
    }

    g_output[0] = '\0';
    if (ok && fseek(capture, 0, SEEK_SET) == 0)
    {
        /* 期望输出按 '\n' 比对，去掉可能随文本模式写入的回车 */
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
    {
        ok = false;
    }

    (void)fclose(input);
    (void)fclose(capture);
    return ok;
}

/* ===================================================================== */

/* 只在首次停下（f3 入口）时驱动会话，其后（f3 的 RET 再次命中断点）直接放行 */
static woort_WAIPO_TrapEndBehavior cmdline_cb(
    woort_WAIPO_Debugger* debugger, woort_VMRuntime* vm)
{
    if (g_session_attempted)
        return WOORT_WAIPO_TRAP_CONTINUE;

    g_session_attempted = true;
    g_session_ok = run_session(debugger, vm, &g_session_behavior);
    return g_session_ok ? g_session_behavior : WOORT_WAIPO_TRAP_CONTINUE;
}

/* 运行一次 main 闭包，返回执行结果（调用失败时为 -1） */
static woort_Int run_main_once(
    woort_CodeEnv* cenv, woort_IRConstantIndex c_main, woort_VmCallStatus* out_status)
{
    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    const woort_Int result =
        (st == WOORT_VM_CALL_STATUS_NORMAL) ? woort_int(sv + 1) : (woort_Int)-1;

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    if (out_status != NULL)
        *out_status = st;
    return result;
}

/* ===================================================================== */

static void check_session_output(void)
{
    const char* p = g_output;

    /* 跳过停顿横幅（含 VM 地址与字节码偏移）及其后的源码展示 */
    static const char note[] = "Note: You can input '?' for more informations.\n";
    const char* const found = strstr(p, note);
    CHECK(found != NULL);
    if (found == NULL)
        return;
    p = found + sizeof(note) - 1;

    /* ===== 1. bt：三层，层号自栈顶起；当前选中的栈顶帧带 '>' ===== */
    CHECK(expect_text(&p, "> Backtrace:\n"));
    CHECK(expect_frame(&p, true, "0", "f3", LINE_F3 + 1));
    CHECK(expect_frame(&p, false, "1", "f2", LINE_F2 + 1));
    CHECK(expect_frame(&p, false, "2", "main", LINE_MAIN + 1));

    /* ===== 2. f 1：帧行与 bt 同格式，随后展示 f2 的源码（第 10 行高亮） ===== */
    CHECK(expect_text(&p, "> "));
    CHECK(expect_frame(&p, true, "1", "f2", LINE_F2 + 1));
    CHECK(expect_text(&p, TEST_FILE_URI " from line 8 to line 12:\n"));
    CHECK(expect_source_line(&p, LINE_F2 + 1, "10| fn f2() { r = f3(); return r + a }"));
    CHECK(expect_marker(&p, "\n\n")); /* source 命令以空行收尾 */

    /* bt：f2 所在帧带 '>' */
    CHECK(expect_text(&p, "> Backtrace:\n"));
    CHECK(expect_frame(&p, false, "0", "f3", LINE_F3 + 1));
    CHECK(expect_frame(&p, true, "1", "f2", LINE_F2 + 1));
    CHECK(expect_frame(&p, false, "2", "main", LINE_MAIN + 1));

    /* ===== 3. f：不带层号，重新显示当前帧 ===== */
    CHECK(expect_text(&p, "> "));
    CHECK(expect_frame(&p, true, "1", "f2", LINE_F2 + 1));
    CHECK(expect_text(&p, TEST_FILE_URI " from line 8 to line 12:\n"));
    CHECK(expect_source_line(&p, LINE_F2 + 1, "10| fn f2() { r = f3(); return r + a }"));
    CHECK(expect_marker(&p, "\n\n"));

    /* f 9 / f abc：给出提示 */
    CHECK(expect_text(&p, "> No such frame: 9 (valid frame ids: 0-2)\n"));
    CHECK(expect_text(&p, "> Invalid frame id: abc\n"));

    /* bt 2：只留最内两层；两条失败的 frame 之后 f2 所在帧仍被选中 */
    CHECK(expect_text(&p, "> Backtrace:\n"));
    CHECK(expect_frame(&p, false, "0", "f3", LINE_F3 + 1));
    CHECK(expect_frame(&p, true, "1", "f2", LINE_F2 + 1));
    CHECK(expect_text(&p, "    ... (1 more frame, 'bt 3' shows all)\n"));

    /* f 2：选中 main，展示其源码（第 5 行高亮） */
    CHECK(expect_text(&p, "> "));
    CHECK(expect_frame(&p, true, "2", "main", LINE_MAIN + 1));
    CHECK(expect_text(&p, TEST_FILE_URI " from line 3 to line 7:\n"));
    CHECK(expect_source_line(&p, LINE_MAIN + 1, " 5| fn main() { r = f2(); return r + a }"));
    CHECK(expect_marker(&p, "\n\n"));

    /* ===== 4. bt 1：选中帧被截掉 ===== */
    CHECK(expect_text(&p, "> Backtrace:\n"));
    CHECK(expect_frame(&p, false, "0", "f3", LINE_F3 + 1));
    CHECK(expect_text(&p,
        "    ... (2 more frames, 'bt 3' shows all; current frame #2 is not shown)\n"));

    /* ===== 5. vm：列表里当前 VM（行首 '*'）的调用栈同样按层号输出 ===== */
    CHECK(expect_text(&p, "> *["));
    CHECK(expect_marker(&p, "usage="));
    CHECK(expect_marker(&p, "\nBacktrace:\n"));
    CHECK(expect_frame(&p, false, "0", "f3", LINE_F3 + 1));
    CHECK(expect_frame(&p, false, "1", "f2", LINE_F2 + 1));
    CHECK(expect_frame(&p, false, "2", "main", LINE_MAIN + 1));

    /* c：会话结束 */
    CHECK(expect_text(&p, "> Continue running...\n"));
    CHECK(*p == '\0');
}

/* ===================================================================== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    CHECK(woort_vfs_create(TEST_FILE, TEST_SOURCE, sizeof(TEST_SOURCE) - 1, true));

    woort_IRCompiler* irc = woort_IRCompiler_create();

    /* 数据常量先于闭包常量分配（CodeEnv 约定） */
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f3;
    woort_IRFunction* f2;
    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f3);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f2);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRFunction_set_name(f3, "f3");
        (void)woort_IRFunction_push_srcloc(f3, TEST_FILE_URI, LINE_F3, 0, LINE_F3, 200);
        woort_IRValue* v3 = woort_IRFunction_new_vreg(f3);
        (void)woort_IR_ADDI(f3, v3,
            woort_IRFunction_fetch_const(f3, c_a),
            woort_IRFunction_fetch_const(f3, c_b));
        (void)woort_IR_ret(f3, v3);
        woort_IRFunction_pop_srcloc(f3);

        woort_IRFunction_set_name(f2, "f2");
        (void)woort_IRFunction_push_srcloc(f2, TEST_FILE_URI, LINE_F2, 0, LINE_F2, 200);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f2);
        (void)woort_IR_CALLNWO(f2, c_f3, 0, r2);
        woort_IRValue* v2 = woort_IRFunction_new_vreg(f2);
        (void)woort_IR_ADDI(f2, v2, r2, woort_IRFunction_fetch_const(f2, c_a));
        (void)woort_IR_ret(f2, v2);
        woort_IRFunction_pop_srcloc(f2);

        woort_IRFunction_set_name(f_main, "main");
        (void)woort_IRFunction_push_srcloc(f_main, TEST_FILE_URI, LINE_MAIN, 0, LINE_MAIN, 200);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO(f_main, c_f2, 0, r1);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, v1, r1, woort_IRFunction_fetch_const(f_main, c_a));
        (void)woort_IR_ret(f_main, v1);
        woort_IRFunction_pop_srcloc(f_main);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* f3_addr;
    const woort_Bytecode* f2_addr;
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f3, &f3_addr);
    (void)woort_CodeEnv_query_function(cenv, f2, &f2_addr);
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c_a, 30);
    woort_CodeEnv_set_const_int(cenv, c_b, 12);
    /* CALLNWO 读常量槽的 m_script_function（裸函数地址），须用 script_function 而非闭包 */
    woort_CodeEnv_set_const_script_function(cenv, c_f3, f3_addr);
    woort_CodeEnv_set_const_script_function(cenv, c_f2, f2_addr);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_unlock(cenv);

    woort_WAIPO_Debugger* dbg = NULL;
    CHECK(woort_WAIPO_Debugger_attach(&cmdline_cb, &dbg)
        == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS);

    woort_WAIPO_Debugger_BreakpointId id_f3 = 0;
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(dbg, TEST_FILE_URI, LINE_F3, &id_f3));

    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(cenv, c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == (30 + 12) + 30 + 30); /* f3 → f2 → main */
    }

    CHECK(g_session_ok); /* 在 f3 入口停过一次并驱动了会话 */
    CHECK(g_session_behavior == WOORT_WAIPO_TRAP_CONTINUE); /* 脚本以 c 结束 */
    CHECK(dbg != NULL && dbg->m_current_frame_depth == 2); /* 最后一条成功的 frame */

    check_session_output();

    /* 会话输出原样打印，便于直接运行时查看 REPL 的实际表现 */
    printf("---- session output ----\n%s"
           "---- session output end ----\n", g_output);

    /* ===== 清理 ===== */
    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    woort_shutdown(NULL, NULL);

    if (g_failures == 0)
    {
        printf("test_waipo_cmdline: ALL PASS\n");
        return 0;
    }
    printf("test_waipo_cmdline: %d FAILURES\n", g_failures);
    return 1;
}

#else
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
}
#endif
