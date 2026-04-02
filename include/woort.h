#pragma once

#ifdef __cplusplus
extern "C" {
#else
#   include <stdbool.h>
#   include <stddef.h>
#   include <stdint.h>
#endif // __cplusplus

#ifdef _WIN32
#   define WOORT_IMPORT __declspec(dllimport)
#   define WOORT_EXPORT __declspec(dllexport)
#else
#   define WOORT_IMPORT extern
#   define WOORT_EXPORT extern
#endif

#ifdef WOORT_AS_DYLIB
#   ifdef WOORT_IMPL
#       define WOORT_API WOORT_EXPORT
#   else
#       define WOORT_API WOORT_IMPORT
#   endif
#else
#   define WOORT_API
#endif

    WOORT_API void woort_init(void);
    WOORT_API void woort_shutdown(void);

    /*
    调用（Invoke 或 Dispatch）一个函数时，函数可能以下述状态为结果：
    */
    typedef enum woort_VmCallStatus
    {
        WOORT_VM_CALL_STATUS_NORMAL,
        /*
        WOORT_VM_CALL_STATUS_NORMAL
        调用的目标函数以正常预期结果返回，没有特殊情况需要处理。
        */

        WOORT_VM_CALL_STATUS_YIELD,
        /*
        WOORT_VM_CALL_STATUS_YIELD
        虚拟机正在请求以当前状态暂停执行，如果情况允许，可以在稍后继续执
        行。

        * YIELD 仅由解释执行收到中断请求后返回

        * 外部执行的 Dispatch 操作可能以此状态结束
        * 如果一个 Invoke 操作被试图以 YIELD 结束，则 PANIC
        */

        WOORT_VM_CALL_STATUS_ABORTED,
        /*
        WOORT_VM_CALL_STATUS_ABORTED
        程序被终止，虚拟机不能继续执行当前的调用栈，虚拟机同时将被以 Abort
        标记并拒绝执行其他任何操作。

        * ABORTED 仅由解释执行收到中断请求后返回
        */

        WOORT_VM_CALL_STATUS_RESYNC,
        /*
        WOORT_VM_CALL_STATUS_RESYNC
        下一层调用栈涉及到了一些新的虚拟机状态变化，下一层调用栈请求上一层
        调用栈执行一些反向同步或检查点检查以恢复到正常状态

        * 解释执行绝不以 RESYNC 结束

        * JIT 调用 Native 函数发生 RESYNC 时：
            恢复到调用后状态，然后执行一次检查点
        * JIT 调用 JIT 函数发生 RESYNC 时：
            不执行同步，立即以 RESYNC 结束当前 JIT 函数
        * 解释执行调用 Native 函数发生 RESYNC 时：
            恢复到调用后状态，然后执行一次检查点
        * 解释执行调用 JIT 函数发生 RESYNC 时：
            执行一次反同步，然后执行一次检查点

        外部执行的 Invoke 或 Dispatch 操作可能以外部函数或者 JIT 函数为目标，
        如果出现此情况，需要再执行 _woort_VMRuntime_dispatch 以获取准确结果
        */
    } woort_VmCallStatus, woort_api;

    typedef struct woort_VMRuntime* woort_vm;
    typedef struct woort_value { char _[8]; } woort_value;

    typedef woort_api(*woort_NativeFunction)(woort_vm vm, woort_value* args);

    typedef struct woort_CodeEnv woort_CodeEnv;
    typedef struct woort_IRCompiler woort_IRCompiler;
    typedef struct woort_IRFunction woort_IRFunction;
    typedef struct woort_IRValue woort_IRValue;
    typedef struct woort_IRLabel woort_IRLabel;

    typedef uint32_t woort_IRConstantIndex;
    typedef uint32_t woort_IRStaticIndex;

    // VM api

    WOORT_API /* OPTIONAL */ woort_vm woort_vm_create(void);
    WOORT_API void woort_vm_close(woort_vm vm);

    WOORT_API woort_vm woort_vm_swap_running(/* OPTIONAL */ woort_vm vm);

    // IR api
    // IR Compiler
    WOORT_API /* OPTIONAL */ woort_IRCompiler* woort_IRCompiler_create(void);
    WOORT_API void woort_IRCompiler_close(woort_IRCompiler* c);

    WOORT_API bool woort_IRCompiler_add_function(
        woort_IRCompiler* c, uint32_t param_count, woort_IRFunction** out_f);

    WOORT_API woort_IRConstantIndex woort_IRCompiler_add_constant(woort_IRCompiler* c);
    WOORT_API woort_IRStaticIndex woort_IRCompiler_add_static(woort_IRCompiler* c);

    WOORT_API bool woort_IRCompiler_finish(woort_IRCompiler* c, woort_CodeEnv** out_cenv);

    /* 创建新的虚拟寄存器 */
    WOORT_API /* OPTIONAL */ woort_IRValue* woort_IRFunction_new_vreg(woort_IRFunction* f);

    /* 获取函数参数的虚拟寄存器（预分配到 SB+3+idx） */
    WOORT_API /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(woort_IRFunction* f, uint32_t param_idx);

    /* 创建新的 Label */
    WOORT_API /* OPTIONAL */ woort_IRLabel* woort_IRFunction_new_label(woort_IRFunction* f);

    /* 获取一个代表常量 G[idx] 的值。
     * 同一 const_index 多次调用返回相同的 IRValue*（天然去重）。
     * 返回的 IRValue* 的 m_source 为 WOORT_IRVALUE_SOURCE_CONST。
     * 返回 NULL 表示 OOM。 */
    WOORT_API /* OPTIONAL */ woort_IRValue* woort_IRFunction_load_const(
        woort_IRFunction* f, woort_IRConstantIndex idx);

    /*
     * 将一个源码位置推入栈。后续发射的 IR 指令将关联栈顶的源码位置。
     * filepath 必须是通过 woort_IRCompiler_intern_string() 获得的 intern 指针。
     * 返回 false 表示 OOM。
     */
    WOORT_API bool woort_IRFunction_push_srcloc(
        woort_IRFunction* f,
        /* OPTIONAL */ const char* filepath,
        uint32_t begin_line,
        uint32_t begin_column,
        uint32_t end_line,
        uint32_t end_column);

    /*
     * 弹出栈顶的源码位置。
     * 栈必须非空，否则触发 assert。
     */
    WOORT_API void woort_IRFunction_pop_srcloc(woort_IRFunction* f);

    /* ========== 指令发射 API ========== */

/*
 * 所有 woort_IR_* 函数向 f 的线性指令列表追加一条 IROp。
 * 返回 false 表示 OOM。
 */

 /* --- 数据移动 --- */
    WOORT_API bool woort_IR_MOV(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_LOAD(woort_IRFunction* f, woort_IRValue* dst, woort_IRStaticIndex idx);
    WOORT_API bool woort_IR_STORE(woort_IRFunction* f, woort_IRStaticIndex idx, woort_IRValue* src);

    /* --- 栈操作 --- */
    WOORT_API bool woort_IR_PUSHCHK(woort_IRFunction* f, woort_IRValue* src);
    WOORT_API bool woort_IR_POP(woort_IRFunction* f, woort_IRValue* dst);
    WOORT_API bool woort_IR_POPR(woort_IRFunction* f, uint32_t count);
    WOORT_API bool woort_IR_POPRS(woort_IRFunction* f, woort_IRValue* count_src);

    /* --- 类型转换 --- */
    WOORT_API bool woort_IR_ITOR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_ITOS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_RTOI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_RTOS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_STOI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_STOR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

    /* --- 函数调用 --- */
    WOORT_API bool woort_IR_CALLNWO(
        woort_IRFunction* f, woort_IRConstantIndex target,
        uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);
    WOORT_API bool woort_IR_CALLNFP(
        woort_IRFunction* f, woort_IRConstantIndex target,
        uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);
    WOORT_API bool woort_IR_CALLNJIT(
        woort_IRFunction* f, woort_IRConstantIndex target,
        uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);
    WOORT_API bool woort_IR_CALL(
        woort_IRFunction* f, woort_IRValue* func_val,
        uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);

    /* --- 闭包/容器 --- */
    WOORT_API bool woort_IR_MKCLOSURE(
        woort_IRFunction* f, woort_IRValue* dst, uint32_t elem_count, woort_IRConstantIndex func_idx);
    WOORT_API bool woort_IR_MKVEC(woort_IRFunction* f, woort_IRValue* dst, uint32_t elem_count);
    WOORT_API bool woort_IR_MKMAP(woort_IRFunction* f, woort_IRValue* dst, uint32_t kvpair_count);
    WOORT_API bool woort_IR_MKSTRUCT(woort_IRFunction* f, woort_IRValue* dst, uint32_t elem_count);

    /* --- 动态类型 --- */
    WOORT_API bool woort_IR_BOXDYN(woort_IRFunction* f, woort_IRValue* dst, uint8_t typ, woort_IRValue* src);
    WOORT_API bool woort_IR_UNBOXDYN(woort_IRFunction* f, woort_IRValue* dst, uint8_t typ, woort_IRValue* src);
    WOORT_API bool woort_IR_CHECKDYN(woort_IRFunction* f, woort_IRValue* dst, uint8_t typ, woort_IRValue* src);
    WOORT_API bool woort_IR_PUSHBOXDYN(woort_IRFunction* f, uint8_t typ, woort_IRValue* src);

    /* --- 整数算术 (dst = src1 op src2) --- */
    WOORT_API bool woort_IR_ADDI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_SUBI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_MULI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_DIVI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_MODI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_NEGI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

    /* --- 整数比较 --- */
    WOORT_API bool woort_IR_LTI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_GTI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_LEI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_GEI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_EQI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_NEI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);

    /* --- 实数算术 --- */
    WOORT_API bool woort_IR_ADDR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_SUBR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_MULR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_DIVR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_MODR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_NEGR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

    /* --- 实数比较 --- */
    WOORT_API bool woort_IR_LTR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_GTR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_LER(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_GER(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_EQR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_NER(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);

    /* --- 字符串 --- */
    WOORT_API bool woort_IR_ADDS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_LTS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_GTS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_LES(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_GES(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_EQS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_NES(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);

    /* --- 逻辑 --- */
    WOORT_API bool woort_IR_LAND(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_LOR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
    WOORT_API bool woort_IR_LNOT(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

    /* --- 索引加载 --- */
    WOORT_API bool woort_IR_LDIDXVEC(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
    WOORT_API bool woort_IR_LDIDXVECX(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
    WOORT_API bool woort_IR_LDIDXSTRUCT(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, uint32_t idx);
    WOORT_API bool woort_IR_LDIDXSTRING(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);

    WOORT_API bool woort_IR_LDIDXDICTI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
    WOORT_API bool woort_IR_LDIDXDICTR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
    WOORT_API bool woort_IR_LDIDXDICTB(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
    WOORT_API bool woort_IR_LDIDXDICTX(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);

    /* --- 索引存储 (不返回值) --- */
    WOORT_API bool woort_IR_SDIDXVECI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXVECR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXVECB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXVECX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

    WOORT_API bool woort_IR_SDIDXDICTII(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTIR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTIB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTIX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTRI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTRR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTRB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTRX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTBI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTBR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTBB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTBX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTXI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTXR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTXB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXDICTXX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

    WOORT_API bool woort_IR_SDIDXMAPII(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPIR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPIB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPIX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPRI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPRR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPRB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPRX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPBI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPBR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPBB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPBX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPXI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPXR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPXB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
    WOORT_API bool woort_IR_SDIDXMAPXX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

    WOORT_API bool woort_IR_SDIDXSTRUCT(woort_IRFunction* f, woort_IRValue* c, uint32_t idx, woort_IRValue* val);

    /* --- 解包 --- */
    WOORT_API bool woort_IR_UNPACKSTRUCT(woort_IRFunction* f, woort_IRValue* src);
    WOORT_API bool woort_IR_UNPACKVEC(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
    WOORT_API bool woort_IR_UNPACKVECX(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

    /* --- 结构体字段推栈 --- */
    WOORT_API bool woort_IR_PUSHIDXSTRUCT(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
    WOORT_API bool woort_IR_PUSHIDXSTBOXI(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
    WOORT_API bool woort_IR_PUSHIDXSTBOXR(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
    WOORT_API bool woort_IR_PUSHIDXSTBOXB(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
    WOORT_API bool woort_IR_PUSHIDXSTBOXX(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);

    /* ============ 控制流 ============ */

    /* 绑定 Label 到当前位置 */
    WOORT_API bool woort_IR_bind(woort_IRFunction* f, woort_IRLabel* label);

    /* 无条件跳转 */
    WOORT_API bool woort_IR_jmp(woort_IRFunction* f, woort_IRLabel* target);

    /* 条件跳转: if (cond != 0) goto target */
    WOORT_API bool woort_IR_jcc(woort_IRFunction* f, woort_IRValue* cond, woort_IRLabel* target);

    /* 条件跳转: if (cond == 0) goto target */
    WOORT_API bool woort_IR_jccz(woort_IRFunction* f, woort_IRValue* cond, woort_IRLabel* target);

    /* 比较跳转 */
    WOORT_API bool woort_IR_jcc_lt(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
    WOORT_API bool woort_IR_jcc_le(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
    WOORT_API bool woort_IR_jcc_eq(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
    WOORT_API bool woort_IR_jcc_gt(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
    WOORT_API bool woort_IR_jcc_ge(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
    WOORT_API bool woort_IR_jcc_ne(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);

    /* ============ 返回 ============ */

    WOORT_API bool woort_IR_ret(woort_IRFunction* f, woort_IRValue* val);
    WOORT_API bool woort_IR_ret_void(woort_IRFunction* f);


#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
