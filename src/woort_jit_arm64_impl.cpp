#include "woort_jit_arm64_bridge.h"
#include "woort_jit_bridge.h"

#include "asmjit/a64.h"

#include <new>
#include <memory>
#include <cassert>

using namespace asmjit;
using namespace asmjit::a64;

struct woort_JIT_Asmjit_arm64_Emmiter
{
    Compiler* c;
    const woort_CodeEnv* cenv;

    CodeHolder  m_code_holder;
    Error       m_last_error;

    FuncNode*   m_func_node;

    woort_JIT_Asmjit_arm64_Emmiter(const woort_JIT_Asmjit_arm64_Emmiter&) = delete;
    woort_JIT_Asmjit_arm64_Emmiter(woort_JIT_Asmjit_arm64_Emmiter&&) = delete;
    woort_JIT_Asmjit_arm64_Emmiter& operator =(const woort_JIT_Asmjit_arm64_Emmiter&) = delete;
    woort_JIT_Asmjit_arm64_Emmiter& operator =(woort_JIT_Asmjit_arm64_Emmiter&&) = delete;

    woort_JIT_Asmjit_arm64_Emmiter(const woort_CodeEnv* cenv_) noexcept
        : c(nullptr)
        , cenv(cenv_)
        , m_code_holder{}
        , m_last_error(Error::kOk)
        , m_func_node(nullptr)
    {
        JitRuntime* const asmjit_runtime =
            static_cast<JitRuntime*>(woort_JIT_Asmjit_get_runtime());

        m_last_error = m_code_holder.init(asmjit_runtime->environment());
        if (m_last_error != Error::kOk)
            return;

        c = new (std::nothrow) Compiler(&m_code_holder);
        if (c == nullptr)
            m_last_error = Error::kOutOfMemory;

        m_last_error = c->add_func_node(Out(m_func_node),
            FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*, const woort_Value*>());
    }
    ~woort_JIT_Asmjit_arm64_Emmiter() noexcept
    {
        if (c != nullptr)
            delete c;
    }

    bool is_okay() const
    {
        return m_last_error == Error::kOk;
    }
};

bool woort_JIT_Backend_arm64_prologue(
    const woort_CodeEnv* cenv,
    void** out_emmiter)
{
    woort_JIT_Asmjit_arm64_Emmiter* const em =
        new (std::nothrow) woort_JIT_Asmjit_arm64_Emmiter(cenv);

    if (em == nullptr)
        return false;

    if (!em->is_okay())
    {
        delete em;
        return false;
    }

    *out_emmiter = em;
    return true;
}

bool woort_JIT_Backend_arm64_epilogue(
    void* emmiter,
    woort_JitFunction* out_code)
{
    woort_JIT_Asmjit_arm64_Emmiter* const em =
        static_cast<woort_JIT_Asmjit_arm64_Emmiter*>(emmiter);

    assert(em != nullptr);

    Error err = em->c->end_func();
    if (err == Error::kOk)
        err = em->c->finalize();

    if (err == Error::kOk)
    {
        JitRuntime* const asmjit_runtime =
            static_cast<JitRuntime*>(woort_JIT_Asmjit_get_runtime());

        woort_JitFunction fn;
        err = asmjit_runtime->add(&fn, &em->m_code_holder);

        *out_code = fn;
    }

    delete em;
    return err == Error::kOk;
}

bool woort_JIT_Backend_arm64_check_state(
    void* emmiter)
{
    woort_JIT_Asmjit_arm64_Emmiter* const em =
        static_cast<woort_JIT_Asmjit_arm64_Emmiter*>(emmiter);

    if (!em->is_okay())
    {
        delete em;
        return false;
    }

    return true;
}

void woort_JIT_Backend_arm64_dropper(
    woort_JitFunction* code)
{
    if (code != NULL && *code != NULL) {
        JitRuntime* const asmjit_runtime =
            static_cast<JitRuntime*>(woort_JIT_Asmjit_get_runtime());

        asmjit_runtime->release(*code);
        *code = NULL;
    }
}

/* -------------------------------------------------------------------------- */
/* 指令派发接口：以下均为占位实现，暂无实际代码生成。                          */
/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_NOP(void* emmiter)
{
    (void)emmiter;
}

void woort_JIT_Backend_arm64_LOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_STORE(void* emmiter, woort_Opcode_Global dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_LOADPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_STOREPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_MOVLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_MOVST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_PUSHRCHK(void* emmiter, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)n;
}

void woort_JIT_Backend_arm64_PUSHSCHK(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_PUSHCCHK(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_ASSURESSZ(void* emmiter, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)n;
}

void woort_JIT_Backend_arm64_PUSHS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_PUSHC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_POPR(void* emmiter, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)n;
}

void woort_JIT_Backend_arm64_POPS(void* emmiter, woort_Opcode_Stack dst)
{
    (void)emmiter;
    (void)dst;
}

void woort_JIT_Backend_arm64_POPC(void* emmiter, woort_Opcode_Global dst)
{
    (void)emmiter;
    (void)dst;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ITORST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_ITORLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_ITOSST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_ITOSLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_RTOIST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_RTOILD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_RTOSST(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_RTOSLD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_CASTSTO(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)target;
    (void)src;
}

void woort_JIT_Backend_arm64_CASTSFROM(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)srctype;
    (void)src;
}

void woort_JIT_Backend_arm64_CASTDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)target;
    (void)src;
}

void woort_JIT_Backend_arm64_ASSERTDYN(void* emmiter, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)target;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_CALLNWO(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

void woort_JIT_Backend_arm64_CALLNFP(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

void woort_JIT_Backend_arm64_CALLNJIT(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

void woort_JIT_Backend_arm64_CALLS(void* emmiter, woort_Opcode_Stack func)
{
    (void)emmiter;
    (void)func;
}

void woort_JIT_Backend_arm64_CALLC(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
}

void woort_JIT_Backend_arm64_RET(void* emmiter)
{
    (void)emmiter;
}

void woort_JIT_Backend_arm64_RETVS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_RETVC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_POPRS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_RESULT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_JFWD(void* emmiter, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)target;
}

void woort_JIT_Backend_arm64_JBCK(void* emmiter, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)target;
}

void woort_JIT_Backend_arm64_JFWDNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JFWDEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

void woort_JIT_Backend_arm64_JBCKEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_MKVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

void woort_JIT_Backend_arm64_MKMAP(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

void woort_JIT_Backend_arm64_MKSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
}

void woort_JIT_Backend_arm64_MKUNION(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_MKCLOSURE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{
    (void)emmiter;
    (void)dst;
    (void)n;
    (void)tmpl;
}

void woort_JIT_Backend_arm64_BOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
}

void woort_JIT_Backend_arm64_UNBOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
}

void woort_JIT_Backend_arm64_CHECKDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
}

void woort_JIT_Backend_arm64_PUSHBOXDYN(void* emmiter, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)type;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_SUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_MULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_DIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_MODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_NEGI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_LTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_GTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_GEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_EQI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_NEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_SUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_MULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_DIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_MODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_NEGR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_LTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_GTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_GER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_EQR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_NER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_GTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_GES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_EQS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_NES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
}

void woort_JIT_Backend_arm64_LNOT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_CADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CSUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CMULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CDIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CSUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CMULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CDIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CVADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CMODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CMODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CLAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CLOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

void woort_JIT_Backend_arm64_CLNOT(void* emmiter, woort_Opcode_Stack dst)
{
    (void)emmiter;
    (void)dst;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_MKPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_LDIDXVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)vec;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXVECX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)vec;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)dst;
    (void)idx;
    (void)obj;
}

void woort_JIT_Backend_arm64_LDIDSTRING(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)str;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTB(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTIX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTRX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTBX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

void woort_JIT_Backend_arm64_LDIDXDICTXX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_STIDXVECI(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXVECR(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXVECB(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXVECX(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXDICTXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDXMAPXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_STIDSTRUCT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)obj;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_STIDSTRUCTEXT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)obj;
    (void)idx;
    (void)src;
}

void woort_JIT_Backend_arm64_UNPACKVEC(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)n;
    (void)vec;
}

void woort_JIT_Backend_arm64_UNPACKVECX(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)n;
    (void)vec;
}

void woort_JIT_Backend_arm64_UNPACKVECALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)count_dst;
    (void)n;
    (void)vec;
}

void woort_JIT_Backend_arm64_UNPACKVECXALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)count_dst;
    (void)n;
    (void)vec;
}

void woort_JIT_Backend_arm64_PUSHIDXSTRUCT(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

void woort_JIT_Backend_arm64_PUSHIDXSTBOXI(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

void woort_JIT_Backend_arm64_PUSHIDXSTBOXR(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

void woort_JIT_Backend_arm64_PUSHIDXSTBOXB(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
}

void woort_JIT_Backend_arm64_PACKARG(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count skip)
{
    (void)emmiter;
    (void)dst;
    (void)skip;
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ASTORE(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)storage;
    (void)src;
}

void woort_JIT_Backend_arm64_ALOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{
    (void)emmiter;
    (void)dst;
    (void)storage;
}

void woort_JIT_Backend_arm64_CAS(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{
    (void)emmiter;
    (void)storage;
    (void)desired;
    (void)expected;
}

void woort_JIT_Backend_arm64_JIFINITED(void* emmiter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)flag;
    (void)target;
}

void woort_JIT_Backend_arm64_DEBUGTRAP(void* emmiter)
{
    (void)emmiter;
}

void woort_JIT_Backend_arm64_PANICS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_PANICC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_CHKDIVIL(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_CHKDIVIR(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_CHKDIVIRZ(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
}

void woort_JIT_Backend_arm64_CHKDIVILR(void* emmiter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{
    (void)emmiter;
    (void)divisor;
    (void)dividend;
}
