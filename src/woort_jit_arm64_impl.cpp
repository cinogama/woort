#ifdef WOORT_BUILD_WITH_ASMJIT

#include "woort_jit_arm64_bridge.h"
#include "woort_jit_bridge.h"
#include "woort_value_types.h"
#include "woort_gc_vec_types.h"
#include "woort_opcode.h"
#include "woort_opcode_formal.h"

extern "C" woort_GCVec* woort_GCVec_new(void);
extern "C" void _woort_GCVec_extern(woort_GCVec* vec, size_t size);

#include "woomem.h"

#include "asmjit/a64.h"

#include <new>
#include <memory>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <unordered_map>
#include <optional>

using namespace std;
using namespace asmjit;
using namespace asmjit::a64;

#define WOORT_JIT_CODE(CMD) em->update_last_error(em->c->CMD)

/*
 * arm64 的 blr 只接受寄存器操作数；asmjit a64 的 invoke 不会自动将立即数地址
 * 物化到寄存器（invoke_(Imm/uint64_t) 直接生成非法的 `blr #imm`）。因此所有
 * 以函数指针立即数为目标的 invoke 都必须先将地址 mov 进一个 Gp 再调用。
 */
#define WOORT_JIT_INVOKE_ADDR(NODE_OUT, FUNC_PTR, ...) \
    do { \
        const Gp _woort_jit_invoke_tgt_ = em->c->new_gp_ptr(); \
        em->update_last_error(em->c->mov(_woort_jit_invoke_tgt_, reinterpret_cast<uintptr_t>(FUNC_PTR))); \
        em->update_last_error(em->c->invoke(NODE_OUT, _woort_jit_invoke_tgt_, __VA_ARGS__)); \
    } while (0)

/*
 * fmod 在 C++ <cmath> 中是重载族（float/double/long double），取其地址时需要消歧。
 * 此处固定为 double 版本，供 MODR/CMODR 的 invoke 调用使用。
 */
static double (*const WOORT_JIT_FMOD)(double, double) = fmod;

static void* (*const WOORT_JIT_MEMCPY)(void*, const void*, size_t) = memcpy;

struct woort_JIT_Asmjit_LoggingErrorHandler : public ErrorHandler
{
    void handle_error(Error err, const char* message, BaseEmitter* origin) override
    {
        fprintf(stderr,
            "[woort_jit_arm64] asmjit error 0x%08X (%s): %s\n",
            static_cast<unsigned>(err),
            DebugUtils::error_as_string(err),
            message ? message : "(no message)");
    }
};

struct woort_JIT_Asmjit_arm64_emitter
{
    Compiler* c;
    const woort_CodeEnv* cenv;

    const woort_Bytecode* m_cenv_codes;
    const woort_Bytecode* m_cenv_codes_end;
    const size_t m_cenv_constant_count;
    const woort_Value* const m_cenv_static_storage;

    CodeHolder  m_code_holder;
    woort_JIT_Asmjit_LoggingErrorHandler m_error_handler;
    Error       m_last_error;

    FuncNode* m_func_node;
    const woort_Bytecode** m_ip;

    /* runtime states */
    Gp          m_vm;
    Gp          m_sb;

    Gp          m_sp;
    Gp          m_stack;
    Gp          m_stack_end;

    /* A1: single-slot TOS register cache — caches the most recent store_stack
     * destination in a virtual register so a consecutive load_stack_gp of the
     * same slot reuses it instead of reloading from [sb+slot]. Single-use:
     * a cache hit consumes the entry. Memory is always written (store_stack
     * never skips the str), so a missed flush only ever forgoes optimization;
     * the cache MUST be flushed wherever m_sb/m_sp are reloaded or control
     * rejoins (see flush_tos call sites). */
    bool               m_tos_valid;
    woort_Opcode_Stack m_tos_slot;
    Gp                 m_tos_vreg;

    /* A4/A6/A11: lazily-materialized, function-wide constant virtual registers.
     * Created on first use and reused for every subsequent site in the same JIT
     * function, hoisting the (multi-instruction on AArch64) address/constant
     * materialization out of hot loops. */
    optional<Gp> m_gc_flag_ptr;   /* A4: &woomem_gc_marking_state_flag          */
    optional<Gp> m_static_base;   /* A11: m_cenv_static_storage base address    */
    optional<Gp> m_neg_sign_mask; /* A6:  INT64_MIN sign mask for NEGR          */

    /* A8: number of stack slots known to be reserved below m_sp by a preceding
     * PUSHRCHK/ASSURESSZ; subsequent PUSHSCHK/PUSHCCHK within this budget skip
     * their per-push overflow check. Must be flushed wherever m_sp is reloaded
     * or control rejoins. */
    int32_t m_push_budget;

    optional<JumpAnnotation*> m_checkpoint_resume_annotation;
    Label           m_checkpoint_slow;
    Gp              m_checkpoint_resume;
    size_t          m_checkpoint_site_count;

    optional<JumpAnnotation*> m_stack_overflow_resume_annotation;
    Label           m_stack_overflow_slow;
    Gp              m_stack_overflow_resume;
    size_t          m_stack_overflow_site_count;

    optional<JumpAnnotation*> m_jit_call_resync_resume_annotation;
    Label           m_jit_call_resync_slow;
    Gp              m_jit_call_resync_resume;
    size_t          m_jit_call_resync_site_count;

    optional<JumpAnnotation*> m_sync_runtime_status_resume_annotation;
    Label           m_sync_runtime_status_slow;
    Gp              m_sync_runtime_status_resume;
    size_t          m_sync_runtime_status_site_count;

    unordered_map<const woort_Bytecode*, Label> m_opcode_label;

    /* A8: for each run of consecutive checked dynamic pushes (PUSHSCHK/PUSHCCHK)
     * of length >= 2 (not crossing a jump-target label), maps the run's FIRST
     * opcode pointer to the run length k. The run-start emits a single combined
     * overflow check covering k slots and sets m_push_budget = k-1; the
     * following k-1 pushes skip their per-push check. Built once by
     * scan_push_runs(). */
    unordered_map<const woort_Bytecode*, uint32_t> m_push_run_start;

    woort_JIT_Asmjit_arm64_emitter(const woort_JIT_Asmjit_arm64_emitter&) = delete;
    woort_JIT_Asmjit_arm64_emitter(woort_JIT_Asmjit_arm64_emitter&&) = delete;
    woort_JIT_Asmjit_arm64_emitter& operator =(const woort_JIT_Asmjit_arm64_emitter&) = delete;
    woort_JIT_Asmjit_arm64_emitter& operator =(woort_JIT_Asmjit_arm64_emitter&&) = delete;

    woort_JIT_Asmjit_arm64_emitter(const woort_CodeEnv* cenv_, const woort_Bytecode** ip) noexcept
        : c(nullptr)
        , cenv(cenv_)
        , m_cenv_codes(woort_JIT_CodeEnv_codes(cenv_))
        , m_cenv_codes_end(woort_JIT_CodeEnv_code_end(cenv_))
        , m_cenv_constant_count(woort_JIT_CodeEnv_constant_count(cenv_))
        , m_cenv_static_storage(woort_JIT_CodeEnv_static_data(cenv_))
        , m_code_holder{}
        , m_error_handler{}
        , m_last_error(Error::kOk)
        , m_func_node(nullptr)
        , m_ip(ip)
        , m_checkpoint_resume_annotation(nullopt)
        , m_checkpoint_site_count(0)
        , m_stack_overflow_resume_annotation(nullopt)
        , m_stack_overflow_site_count(0)
        , m_jit_call_resync_resume_annotation(nullopt)
        , m_jit_call_resync_site_count(0)
        , m_sync_runtime_status_site_count(0)
    {
        JitRuntime* const asmjit_runtime =
            static_cast<JitRuntime*>(woort_JIT_Asmjit_get_runtime());

        m_last_error = m_code_holder.init(asmjit_runtime->environment());
        if (m_last_error != Error::kOk)
            return;

        m_code_holder.set_error_handler(&m_error_handler);

        c = new (nothrow) Compiler(&m_code_holder);
        if (c == nullptr)
            m_last_error = Error::kOutOfMemory;

        m_vm = c->new_gp_ptr();
        m_sp = c->new_gp_ptr();
        m_sb = c->new_gp_ptr();
        m_stack = c->new_gp_ptr();
        m_stack_end = c->new_gp_ptr();

        m_tos_valid = false;
        m_tos_slot = 0;
        m_push_budget = 0;

        m_last_error = c->add_func_node(Out(m_func_node),
            FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*, const woort_Value*, const woort_Value*>());

        m_func_node->set_arg(0, m_vm);
        m_func_node->set_arg(1, m_sb);
        m_func_node->set_arg(2, m_sp);
    }
    ~woort_JIT_Asmjit_arm64_emitter() noexcept
    {
        if (c != nullptr)
            delete c;
    }

    bool is_okay() const
    {
        return m_last_error == Error::kOk;
    }
    void update_last_error(Error err)
    {
        if (err != Error::kOk)
            m_last_error = err;
    }

    // ===================================================== //
    /* A1: drop the TOS register cache. Call wherever m_sb/m_sp are reloaded
     * or where control flow rejoins (labels, VM calls, deopt, returns). */
    void flush_tos() { m_tos_valid = false; }
    /* A8: drop the push reservation budget. Same flush discipline as TOS. */
    void flush_push_budget() { m_push_budget = 0; }

    /* A4: lazily create a vreg holding &woomem_gc_marking_state_flag. */
    Gp get_gc_flag_ptr()
    {
        auto* const em = this;
        if (!m_gc_flag_ptr.has_value())
        {
            Gp r = c->new_gp_ptr();
            WOORT_JIT_CODE(mov(r, reinterpret_cast<uintptr_t>(&woomem_gc_marking_state_flag)));
            m_gc_flag_ptr = r;
        }
        return *m_gc_flag_ptr;
    }
    /* A11: lazily create a vreg holding the m_cenv_static_storage base. */
    Gp get_static_base()
    {
        auto* const em = this;
        if (!m_static_base.has_value())
        {
            Gp r = c->new_gp_ptr();
            WOORT_JIT_CODE(mov(r, reinterpret_cast<uintptr_t>(m_cenv_static_storage)));
            m_static_base = r;
        }
        return *m_static_base;
    }
    /* A6: lazily create a vreg holding INT64_MIN (sign mask for NEGR). */
    Gp get_neg_sign_mask()
    {
        auto* const em = this;
        if (!m_neg_sign_mask.has_value())
        {
            Gp r = c->new_gp64();
            WOORT_JIT_CODE(mov(r, Imm(static_cast<int64_t>(INT64_MIN))));
            m_neg_sign_mask = r;
        }
        return *m_neg_sign_mask;
    }

    /* A11: address m_cenv_static_storage[idx] as [static_base + idx*8], reusing
     * the lazily-cached base register instead of materializing a fresh address
     * (multi-instruction on AArch64) on every access.
     *
     * Note: like sb_slot, when the scaled displacement falls outside the
     * unsigned-12-bit (ldur/str scaled) range the returned Mem is emitted by
     * asmjit via a temporary; the base register itself stays cached. */
    Mem static_slot(woort_Opcode_Global idx)
    {
        return ptr(get_static_base(),
            idx * static_cast<int32_t>(sizeof(woort_Value)));
    }
    /* A11: materialize a Gp pointing at m_cenv_static_storage[idx] from the
     * cached base. Needed where a base register is mandatory (arm64 exclusive
     * ops stlr/ldar/casal) or where the address is passed to an invoke. */
    Gp static_slot_ptr(woort_Opcode_Global idx)
    {
        auto* const em = this;
        const Gp r = c->new_gp_ptr();
        const Gp off = c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(
            static_cast<int64_t>(idx) * static_cast<int64_t>(sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(r, get_static_base(), off));
        return r;
    }

    /* A3: inline woort_DynBox_unbox_no_check — turn a DynBox (in val_in) into a
     * raw 8-byte woort_Value (in out) via tag decode + bit manipulation, avoiding
     * the per-element function call (caller-save spill/reload) in LDIDVEC /
     * UNPACKVEC. Mirrors the tag-decode of the CASTDYN/CASTR opcodes.
     * val_in is not clobbered (a copy is used internally). */
    void emit_unbox_dyn_no_check(const Gp& out, const Gp& val_in)
    {
        auto* const em = this;

        const Label L_imm   = c->new_label();
        const Label L_plain = c->new_label();
        const Label L_exint = c->new_label();
        const Label L_done  = c->new_label();

        const Gp val = c->new_gp64();
        WOORT_JIT_CODE(mov(val, val_in));

        /* immediate (tagged) if any of the low 3 bits is set */
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_imm));

        /* --- pointer path (low 3 bits == 0) --- */
        WOORT_JIT_CODE(tst(val, val));                   /* nil (== 0)? */
        WOORT_JIT_CODE(b_eq(L_plain));
        {
            const Gp proxy = c->new_gp64();
            WOORT_JIT_CODE(ldr(proxy, ptr(val)));        /* m_proxy at offset 0 */
            const Gp ex_sym = c->new_gp64();
            WOORT_JIT_CODE(mov(ex_sym, reinterpret_cast<uintptr_t>(&WOORT_EX_BOX_PROXY)));
            WOORT_JIT_CODE(cmp(proxy, ex_sym));
            WOORT_JIT_CODE(b_ne(L_plain));

            /* EX_BOX: dispatch on m_is_int */
            const Gp is_int = c->new_gp32();
            WOORT_JIT_CODE(ldrb(is_int.w(), ptr(val,
                static_cast<int32_t>(offsetof(woort_BoxedExValue, m_is_int)))));
            WOORT_JIT_CODE(tst(is_int, is_int));
            WOORT_JIT_CODE(b_ne(L_exint));

            WOORT_JIT_CODE(ldr(out, ptr(val,
                static_cast<int32_t>(offsetof(woort_BoxedExValue, m_real)))));
            WOORT_JIT_CODE(b(L_done));
        }
        WOORT_JIT_CODE(bind(L_exint));
        WOORT_JIT_CODE(ldr(out, ptr(val,
            static_cast<int32_t>(offsetof(woort_BoxedExValue, m_int)))));
        WOORT_JIT_CODE(b(L_done));

        WOORT_JIT_CODE(bind(L_plain));
        WOORT_JIT_CODE(mov(out, val));                   /* gcinstance == val */
        WOORT_JIT_CODE(b(L_done));

        /* --- immediate path --- */
        WOORT_JIT_CODE(bind(L_imm));
        {
            const Label L_real = c->new_label();
            const Label L_int  = c->new_label();

            WOORT_JIT_CODE(tst(val, Imm(0b001)));
            WOORT_JIT_CODE(b_ne(L_real));

            WOORT_JIT_CODE(tst(val, Imm(0b010)));        /* INT tag = 0b010 */
            WOORT_JIT_CODE(b_ne(L_int));

            /* BOOL: out = val >> 3 (yields 0/1) */
            WOORT_JIT_CODE(mov(out, val));
            WOORT_JIT_CODE(lsr(out, out, 3));
            WOORT_JIT_CODE(b(L_done));

            WOORT_JIT_CODE(bind(L_int));
            WOORT_JIT_CODE(mov(out, val));
            WOORT_JIT_CODE(asr(out, out, 2));
            WOORT_JIT_CODE(b(L_done));

            /* REAL (Float63): decompress back to IEEE-754 double bits. */
            WOORT_JIT_CODE(bind(L_real));
            {
                const Gp sign = c->new_gp64();
                const Gp sign_mask = c->new_gp64();
                WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(0x8000000000000000ULL))));
                WOORT_JIT_CODE(mov(sign, val));
                WOORT_JIT_CODE(and_(sign, sign, sign_mask));

                const Gp exp_bit = c->new_gp64();
                WOORT_JIT_CODE(mov(exp_bit, val));
                WOORT_JIT_CODE(lsr(exp_bit, exp_bit, 62));
                WOORT_JIT_CODE(and_(exp_bit, exp_bit, Imm(1)));
                WOORT_JIT_CODE(eor(exp_bit, exp_bit, Imm(1)));
                WOORT_JIT_CODE(lsl(exp_bit, exp_bit, 62));

                const Gp low62_mask = c->new_gp64();
                WOORT_JIT_CODE(mov(low62_mask, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
                WOORT_JIT_CODE(mov(out, val));
                WOORT_JIT_CODE(lsr(out, out, 1));
                WOORT_JIT_CODE(and_(out, out, low62_mask));
                WOORT_JIT_CODE(orr(out, out, exp_bit));
                WOORT_JIT_CODE(orr(out, out, sign));
            }
        }

        WOORT_JIT_CODE(bind(L_done));
    }

    // ===================================================== //
    void resync_vm_stack_state_fully()
    {
        auto* const em = this;

        em->flush_tos();
        em->flush_push_budget();

        WOORT_JIT_CODE(ldr(em->m_sp, ptr(em->m_vm, WOORT_VM_OFFSETOF_SP)));
        WOORT_JIT_CODE(ldr(em->m_sb, ptr(em->m_vm, WOORT_VM_OFFSETOF_SB)));
        WOORT_JIT_CODE(ldr(em->m_stack, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK)));
        WOORT_JIT_CODE(ldr(em->m_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
    }
    void return_with_status(woort_VmCallStatus status)
    {
        auto* const em = this;

        const Gp depth = c->new_gp32();
        WOORT_JIT_CODE(ldr(depth, ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH)));
        WOORT_JIT_CODE(sub(depth, depth, 1));
        WOORT_JIT_CODE(str(depth, ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH)));

        const Gp ret_val = c->new_gp32();
        WOORT_JIT_CODE(mov(ret_val, (int32_t)status));
        WOORT_JIT_CODE(ret(ret_val));
    }
    void return_with_status(const Gp& status)
    {
        auto* const em = this;

        const Gp depth = c->new_gp32();
        WOORT_JIT_CODE(ldr(depth, ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH)));
        WOORT_JIT_CODE(sub(depth, depth, 1));
        WOORT_JIT_CODE(str(depth, ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH)));

        WOORT_JIT_CODE(ret(status));
    }

    void emit_ret()
    {
        auto* const em = this;

        // Get callway.
        static_assert(sizeof(woort_CallWay) == 4, "");
        static_assert(0 == offsetof(woort_Value, m_ret_addr), "");
        static_assert(sizeof(woort_Value) == 8, "");

        // ret_way = sb[1].m_ret_bp.m_way  (offset = 1*8 + 0)
        const int32_t way_off =
            1 * static_cast<int32_t>(sizeof(woort_Value)) +
            static_cast<int32_t>(offsetof(woort_RetBP, m_way));
        // sb[1].m_ret_bp.m_bp_offset (offset = 1*8 + 4)
        const int32_t bp_off =
            1 * static_cast<int32_t>(sizeof(woort_Value)) +
            static_cast<int32_t>(offsetof(woort_RetBP, m_bp_offset));

        const Gp way = c->new_gp32();
        const Label L_normal_ret = c->new_label();

        WOORT_JIT_CODE(ldr(way, ptr(em->m_sb, way_off)));
        WOORT_JIT_CODE(cmp(way, Imm(static_cast<int32_t>(WOORT_CALL_WAY_FROM_NATIVE))));
        WOORT_JIT_CODE(b_ne(L_normal_ret));

        // 此调用发起自 Native，需要正同步以确保状态回退到调用前
        {
            /*
            vm->sp = rt_sb + 2;
            vm->sb = vm->sp + vm->sp[-1].m_ret_bp.m_bp_offset
            vm->ip = vm->sp[0].m_ret_addr;
            */

            // sp = sb + 2*sizeof(woort_Value)
            WOORT_JIT_CODE(add(em->m_sp, em->m_sb,
                static_cast<int32_t>(sizeof(woort_Value)) * 2));

            // bp_offset = sb[1].m_ret_bp.m_bp_offset（从 m_sb 正偏移读取，等价于 sp[-1]）
            const Gp bp_offset = c->new_gp64();
            WOORT_JIT_CODE(ldr(bp_offset.w(), ptr(em->m_sb, bp_off)));
            // sb = sp + bp_offset*8   (shift=3 => scale=8)
            WOORT_JIT_CODE(add(em->m_sb, em->m_sp, bp_offset, lsl(3)));

            const Gp ret_ip = c->new_gp64();
            WOORT_JIT_CODE(ldr(ret_ip, ptr(em->m_sp)));

            em->emit_sync_rt_ip_status(ret_ip);

            em->emit_sync_runtime_status(L_normal_ret);
        }
        WOORT_JIT_CODE(bind(L_normal_ret));
        em->return_with_status(WOORT_VM_CALL_STATUS_NORMAL);
    }
    void emit_checkpoint(const woort_Bytecode* ip)
    {
        auto* const em = this;

        em->flush_tos();
        em->flush_push_budget();

        if (!em->m_checkpoint_resume_annotation.has_value())
        {
            em->m_checkpoint_slow = c->new_label();
            em->m_checkpoint_resume = c->new_gp_ptr();
            em->m_checkpoint_resume_annotation = c->new_jump_annotation();
        }

        const Gp    check_mask = c->new_gp32();
        const Label L_continue = c->new_label();

        WOORT_JIT_CODE(ldr(check_mask, ptr(em->m_vm, WOORT_VM_OFFSETOF_CHECK_REQUEST_MASK)));
        WOORT_JIT_CODE(tst(check_mask, check_mask));
        WOORT_JIT_CODE(b_eq(L_continue));

        {
            em->emit_sync_rt_ip_status(ip);

            WOORT_JIT_CODE(adr(em->m_checkpoint_resume, L_continue));
            em->update_last_error(
                em->m_checkpoint_resume_annotation.value()->add_label(L_continue));

            ++em->m_checkpoint_site_count;

            WOORT_JIT_CODE(b(em->m_checkpoint_slow));
        }

        WOORT_JIT_CODE(bind(L_continue));
    }
    void emit_extern_stack(const woort_Bytecode* ip, Label L_resume)
    {
        auto* const em = this;

        em->flush_tos();
        em->flush_push_budget();

        if (!em->m_stack_overflow_resume_annotation.has_value())
        {
            em->m_stack_overflow_slow = c->new_label();
            em->m_stack_overflow_resume = c->new_gp_ptr();
            em->m_stack_overflow_resume_annotation = c->new_jump_annotation();
        }

        {
            em->emit_sync_rt_ip_status(ip);

            WOORT_JIT_CODE(adr(em->m_stack_overflow_resume, L_resume));
            em->update_last_error(
                em->m_stack_overflow_resume_annotation.value()->add_label(L_resume));

            ++em->m_stack_overflow_site_count;

            WOORT_JIT_CODE(b(em->m_stack_overflow_slow));
        }
    }
    void emit_jit_call_resync(Label L_resume)
    {
        auto* const em = this;

        em->flush_tos();
        em->flush_push_budget();

        if (!em->m_jit_call_resync_resume_annotation.has_value())
        {
            em->m_jit_call_resync_slow = c->new_label();
            em->m_jit_call_resync_resume = c->new_gp_ptr();
            em->m_jit_call_resync_resume_annotation = c->new_jump_annotation();
        }

        {
            WOORT_JIT_CODE(adr(em->m_jit_call_resync_resume, L_resume));
            em->update_last_error(
                em->m_jit_call_resync_resume_annotation.value()->add_label(L_resume));

            ++em->m_jit_call_resync_site_count;

            WOORT_JIT_CODE(b(em->m_jit_call_resync_slow));
        }
    }
    void emit_sync_rt_ip_status(Gp ip)
    {
        auto* const em = this;

        WOORT_JIT_CODE(str(ip, ptr(em->m_vm, WOORT_VM_OFFSETOF_IP)));
    }
    void emit_sync_rt_ip_status(const woort_Bytecode* ip)
    {
        auto* const em = this;

        const Gp ip_reg = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(ip_reg, Imm((intptr_t)ip)));

        emit_sync_rt_ip_status(ip_reg);
    }
    void emit_sync_runtime_status(Label L_resume)
    {
        /*
        将对 SP/SB/ENV 的写入推迟到单一的 slow path（在 epilogue 中发射），
        调用方需在调用前自行写入 vm->ip。风格仿照 emit_extern_stack。
        */
        auto* const em = this;

        em->flush_tos();
        em->flush_push_budget();

        if (!em->m_sync_runtime_status_resume_annotation.has_value())
        {
            em->m_sync_runtime_status_slow = c->new_label();
            em->m_sync_runtime_status_resume = c->new_gp_ptr();
            em->m_sync_runtime_status_resume_annotation = c->new_jump_annotation();
        }

        {
            WOORT_JIT_CODE(adr(em->m_sync_runtime_status_resume, L_resume));
            em->update_last_error(
                em->m_sync_runtime_status_resume_annotation.value()->add_label(L_resume));

            ++em->m_sync_runtime_status_site_count;

            WOORT_JIT_CODE(b(em->m_sync_runtime_status_slow));
        }
    }
    void emit_failed_fallback(const woort_Bytecode* ip)
    {
        auto* const em = this;

        em->flush_tos();
        em->flush_push_budget();

        emit_sync_rt_ip_status(ip);

        const Label L_after_sync_st = em->c->new_label();
        emit_sync_runtime_status(L_after_sync_st);
        WOORT_JIT_CODE(bind(L_after_sync_st));

        return_with_status(WOORT_VM_CALL_STATUS_RESYNC);
    }

    // ===================================================== //
    Mem sb_slot(woort_Opcode_Stack slot)
    {
        auto* const em = this;

        const int32_t offset = slot * static_cast<int32_t>(sizeof(woort_Value));

        /*
         * ARM64 访存立即数偏移的可编码范围：
         *   - str/ldr（scaled，无符号 12 位）：偏移须为元素大小（64 位时为 8）的倍数且 ∈ [0, 32760]。
         *   - stur/ldur（unscaled，有符号 9 位）：偏移 ∈ [-256, 255]。
         * 栈槽偏移恒为 8 的倍数，故两者并集为 [-256, 32760]。当函数栈帧过深
         * （slot < -32 或 slot > 4095）时单条指令无法编码，需先把 [m_sb + offset]
         * 物化到临时寄存器再访存。x86-64 后端无此限制（支持完整 32 位位移）。
         */
        if (offset >= -256 && offset <= 32760)
            return ptr(m_sb, offset);

        const Gp off_reg = c->new_gp64();
        WOORT_JIT_CODE(mov(off_reg, Imm(static_cast<int64_t>(offset))));

        const Gp addr = c->new_gp_ptr();
        WOORT_JIT_CODE(add(addr, m_sb, off_reg));

        return ptr(addr);
    }
    Gp load_stack_gp(woort_Opcode_Stack src)
    {
        auto* const em = this;

        /* A1: if this slot is currently cached in a register (the destination of
         * the immediately preceding register-producing instruction), reuse that
         * register instead of reloading from [sb+src]. Single-use: the hit
         * consumes the entry. Memory is always kept up to date by store_stack,
         * so this is a pure load-elimination; the cache is flushed wherever
         * m_sb/m_sp are reloaded or control rejoins (see flush_tos sites). */
        if (em->m_tos_valid && src == em->m_tos_slot)
        {
            em->m_tos_valid = false;
            return em->m_tos_vreg;
        }

        const Gp reg = c->new_gp64();
        WOORT_JIT_CODE(ldr(reg, sb_slot(src)));
        return reg;
    }
    void store_stack(woort_Opcode_Stack dst, const Gp& v)
    {
        auto* const em = this;

        WOORT_JIT_CODE(str(v, sb_slot(dst)));

        /* A1: record the just-written slot+register so a consecutive
         * load_stack_gp(dst) reuses v, avoiding a redundant reload. */
        em->m_tos_slot = dst;
        em->m_tos_vreg = v;
        em->m_tos_valid = true;
    }
    void store_stack(woort_Opcode_Stack dst, const Imm& v)
    {
        auto* const em = this;

        /* A1: no live Gp result to cache for the imm path. */
        em->m_tos_valid = false;

        const Gp reg = c->new_gp64();
        WOORT_JIT_CODE(mov(reg, v));
        WOORT_JIT_CODE(str(reg, sb_slot(dst)));
    }
    void store_stack(woort_Opcode_Stack dst, const Mem& v)
    {
        auto* const em = this;

        em->m_tos_valid = false;

        const Gp reg = c->new_gp64();
        WOORT_JIT_CODE(ldr(reg, v));
        WOORT_JIT_CODE(str(reg, sb_slot(dst)));
    }
    /* A1: FP (Vec) results bypass the Gp cache; invalidate so a later
     * load_stack_gp(dst) does not reuse a stale Gp holding the old bits. */
    void store_stack(woort_Opcode_Stack dst, const Vec& v)
    {
        auto* const em = this;

        em->m_tos_valid = false;

        WOORT_JIT_CODE(str(v, sb_slot(dst)));
    }

    Label get_label(const woort_Bytecode* c)
    {
        auto* const em = this;

        const auto it = em->m_opcode_label.find(c);
        if (it != em->m_opcode_label.end())
            return it->second;

        const Label lbl = em->c->new_label();
        em->m_opcode_label.emplace(c, lbl);
        return lbl;
    }

    void bind_label(const woort_Bytecode* c)
    {
        auto* const em = this;

        WOORT_JIT_CODE(bind(get_label(c)));
    }

    void scan_jump_targets()
    {
        const woort_Bytecode* cur = m_cenv_codes;
        while (cur < m_cenv_codes_end)
        {
            const uint32_t bc = cur[0];
            const uint8_t  op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
            const uint8_t  m2  = (uint8_t)WOORT_BYTECODE(M2,  bc);

            switch (op6)
            {
            case WOORT_OPCODE_JFWD:
            case WOORT_OPCODE_JBCK:
            case WOORT_OPCODE_JIFINITED:
                get_label(m_cenv_codes + WOORT_BYTECODE(MABC26, bc));
                break;
            case WOORT_OPCODE_JFWDCND:
                if (m2 <= 1u)
                    get_label(cur + (uint16_t)WOORT_BYTECODE(BC16, bc));
                else
                    get_label(cur + (uint8_t)WOORT_BYTECODE(C8, bc));
                break;
            case WOORT_OPCODE_JBCKCND:
                if (m2 <= 1u)
                    get_label(cur - (uint16_t)WOORT_BYTECODE(BC16, bc));
                else
                    get_label(cur - (uint8_t)WOORT_BYTECODE(C8, bc));
                break;
            case WOORT_OPCODE_JFDCMP:
                get_label(cur + (uint8_t)WOORT_BYTECODE(C8, bc));
                break;
            case WOORT_OPCODE_JBCKCMP:
                get_label(cur - (uint8_t)WOORT_BYTECODE(C8, bc));
                break;
            default:
                break;
            }

            cur = woort_JIT_next_bytecode(cur);
        }
    }

    /* A8: detect maximal runs of consecutive checked dynamic pushes
     * (PUSHSCHK/PUSHCCHK) that do not span a jump-target label, recording each
     * run of length >= 2 so its start can emit one combined overflow check. */
    void scan_push_runs()
    {
        const woort_Bytecode* run_start = nullptr;
        uint32_t run_len = 0;

        const woort_Bytecode* cur = m_cenv_codes;
        while (cur < m_cenv_codes_end)
        {
            const uint32_t bc = cur[0];
            const uint8_t  op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
            const uint8_t  m2  = (uint8_t)WOORT_BYTECODE(M2,  bc);

            const bool is_push =
                (op6 == WOORT_OPCODE_PUSHCHK) && (m2 == 1u || m2 == 2u);
            const bool is_target = (m_opcode_label.count(cur) != 0);

            if (is_push && run_len > 0 && !is_target)
            {
                ++run_len;                                 /* extend current run */
            }
            else if (is_push)
            {
                if (run_len >= 2)
                    m_push_run_start[run_start] = run_len; /* close previous run */
                run_start = cur;                           /* start new run here  */
                run_len = 1;
            }
            else
            {
                if (run_len >= 2)
                    m_push_run_start[run_start] = run_len;
                run_len = 0;
                run_start = nullptr;
            }

            cur = woort_JIT_next_bytecode(cur);
        }
        if (run_len >= 2)
            m_push_run_start[run_start] = run_len;
    }

    /* A8: emit the overflow guard for k consecutive single-slot pushes.
     * Equivalent to k individual `cmp m_sp, m_stack; b_hi` checks (the binding
     * one being on the last slot at m_sp-(k-1)*8). On overflow, retry via the
     * shared slow path. */
    void emit_push_overflow_check(uint32_t k)
    {
        auto* const em = this;

        const Label L_retry = c->new_label();
        WOORT_JIT_CODE(bind(L_retry));

        const Label L_ok = c->new_label();
        if (k <= 1)
        {
            WOORT_JIT_CODE(cmp(m_sp, m_stack));
        }
        else
        {
            const Gp t = c->new_gp_ptr();
            WOORT_JIT_CODE(sub(t, m_sp,
                static_cast<int32_t>((k - 1u) * static_cast<uint32_t>(sizeof(woort_Value)))));
            WOORT_JIT_CODE(cmp(t, m_stack));
        }
        WOORT_JIT_CODE(b_hi(L_ok));

        emit_extern_stack(*m_ip, L_retry);

        WOORT_JIT_CODE(bind(L_ok));
    }

    /* A8: classify the current checked push (*m_ip) and emit/omit its overflow
     * guard accordingly. Run-starts emit a combined check for the whole run and
     * seed m_push_budget; continuations consume the budget and skip; isolated
     * pushes emit a single check. */
    void emit_pushchk_guard()
    {
        const auto it = m_push_run_start.find(*m_ip);
        if (it != m_push_run_start.end())
        {
            emit_push_overflow_check(it->second);
            m_push_budget = it->second - 1u;   /* following pushes skip */
        }
        else if (m_push_budget > 0)
        {
            --m_push_budget;                   /* continuation: skip check */
        }
        else
        {
            emit_push_overflow_check(1);       /* isolated push */
        }
    }
};

bool woort_JIT_Backend_arm64_prologue(
    const woort_CodeEnv* cenv,
    const woort_Bytecode** ip,
    void** out_emitter)
{
    woort_JIT_Asmjit_arm64_emitter* const em =
        new (nothrow) woort_JIT_Asmjit_arm64_emitter(cenv, ip);

    if (em == nullptr)
        return false;

    if (em->is_okay())
    {
        // Ok, generate codes for JIT function overload.

        // 0. Apply state.
        {
            WOORT_JIT_CODE(ldr(em->m_stack, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK)));
            WOORT_JIT_CODE(ldr(em->m_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
        }
        // 1. Check JIT function depth.
        {
            const Label L_ok = em->c->new_label();

            const Gp depth = em->c->new_gp32();
            WOORT_JIT_CODE(ldr(depth, ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH)));
            WOORT_JIT_CODE(add(depth, depth, 1));
            WOORT_JIT_CODE(str(depth, ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH)));

            WOORT_JIT_CODE(cmp(depth, Imm(WOORT_VM_MAX_JIT_CALL_DEPTH)));
            WOORT_JIT_CODE(b_ls(L_ok));
            {
                em->emit_sync_rt_ip_status(*ip);

                const Label L_resync_ret = em->c->new_label();
                em->emit_sync_runtime_status(L_resync_ret);
                WOORT_JIT_CODE(bind(L_resync_ret));
            }
            em->return_with_status(WOORT_VM_CALL_STATUS_RESYNC);
            WOORT_JIT_CODE(bind(L_ok));
        }

        em->scan_jump_targets();
        em->scan_push_runs();
    }

    if (!em->is_okay())
    {
        delete em;
        return false;
    }

    *out_emitter = em;
    return true;
}

bool woort_JIT_Backend_arm64_epilogue(
    void* emitter,
    woort_JitFunction* out_code)
{
    woort_JIT_Asmjit_arm64_emitter* const em =
        static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    assert(em != nullptr);

    // Check for checkpoint
    if (em->m_checkpoint_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_checkpoint_slow));

        const Mem checkpoint_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(str(em->m_checkpoint_resume, checkpoint_resume_slot));

        {
            const Label L_after_sync_st = em->c->new_label();
            em->emit_sync_runtime_status(L_after_sync_st);
            WOORT_JIT_CODE(bind(L_after_sync_st));
        }

        static_assert(sizeof(woort_VmCallStatus) == 4, "");

        const Label checkpoint_exit = em->c->new_label();
        const Gp    checkpoint_status = em->c->new_gp32();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_VMRuntime_JIT_request_handler, FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*>());

        invoke_node->set_arg(0, em->m_vm);
        invoke_node->set_ret(0, checkpoint_status);

        WOORT_JIT_CODE(cmp(checkpoint_status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_NORMAL)));
        WOORT_JIT_CODE(b_ne(checkpoint_exit));

        em->resync_vm_stack_state_fully();

        const Gp resume = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(resume, checkpoint_resume_slot));
        WOORT_JIT_CODE(br(resume, em->m_checkpoint_resume_annotation.value()));

        WOORT_JIT_CODE(bind(checkpoint_exit));
        em->return_with_status(checkpoint_status);
    }

    // Check for stack overflow.
    if (em->m_stack_overflow_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_stack_overflow_slow));

        const Mem so_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(str(em->m_stack_overflow_resume, so_resume_slot));

        {
            const Label L_after_sync = em->c->new_label();
            em->emit_sync_runtime_status(L_after_sync);
            WOORT_JIT_CODE(bind(L_after_sync));
        }

        static_assert(sizeof(woort_VmCallStatus) == 4, "");

        const Label so_exit = em->c->new_label();
        const Gp    so_status = em->c->new_gp32();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_VMRuntime_JIT_stack_overflow_handler, FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*>());

        invoke_node->set_arg(0, em->m_vm);
        invoke_node->set_ret(0, so_status);

        WOORT_JIT_CODE(cmp(so_status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_NORMAL)));
        WOORT_JIT_CODE(b_ne(so_exit));

        em->resync_vm_stack_state_fully();

        const Gp resume = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(resume, so_resume_slot));
        WOORT_JIT_CODE(br(resume, em->m_stack_overflow_resume_annotation.value()));

        WOORT_JIT_CODE(bind(so_exit));
        em->return_with_status(so_status);
    }

    // Resync cached stack registers after a JIT-to-JIT call that reallocated the stack.
    if (em->m_jit_call_resync_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_jit_call_resync_slow));

        const Mem jit_call_resync_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(str(em->m_jit_call_resync_resume, jit_call_resync_resume_slot));

        const Gp vm_stack_end = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(vm_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));

        const Gp off = em->c->new_gp64();

        WOORT_JIT_CODE(mov(off, em->m_stack_end));
        WOORT_JIT_CODE(sub(off, off, em->m_sp));
        WOORT_JIT_CODE(mov(em->m_sp, vm_stack_end));
        WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, off));

        WOORT_JIT_CODE(mov(off, em->m_stack_end));
        WOORT_JIT_CODE(sub(off, off, em->m_sb));
        WOORT_JIT_CODE(mov(em->m_sb, vm_stack_end));
        WOORT_JIT_CODE(sub(em->m_sb, em->m_sb, off));

        WOORT_JIT_CODE(ldr(em->m_stack, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK)));
        WOORT_JIT_CODE(mov(em->m_stack_end, vm_stack_end));

        const Gp resume = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(resume, jit_call_resync_resume_slot));
        WOORT_JIT_CODE(br(resume,
            em->m_jit_call_resync_resume_annotation.value()));
    }

    // Shared slow path for SP/SB/ENV sync (caller writes vm->ip inline).
    assert(em->m_sync_runtime_status_site_count > 0);
    {
        WOORT_JIT_CODE(bind(em->m_sync_runtime_status_slow));

        const Mem sync_runtime_status_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(str(em->m_sync_runtime_status_resume, sync_runtime_status_resume_slot));

        WOORT_JIT_CODE(str(em->m_sp, ptr(em->m_vm, WOORT_VM_OFFSETOF_SP)));
        WOORT_JIT_CODE(str(em->m_sb, ptr(em->m_vm, WOORT_VM_OFFSETOF_SB)));
        const Gp env_tmp = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(env_tmp, (uintptr_t)em->cenv));
        WOORT_JIT_CODE(str(env_tmp, ptr(em->m_vm, WOORT_VM_OFFSETOF_ENV)));

        const Gp resume = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(resume, sync_runtime_status_resume_slot));
        WOORT_JIT_CODE(br(resume,
            em->m_sync_runtime_status_resume_annotation.value()));
    }

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

bool woort_JIT_Backend_arm64_pre_dispatch(
    void* emitter)
{
    woort_JIT_Asmjit_arm64_emitter* const em =
        static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const auto it = em->m_opcode_label.find(*em->m_ip);
    if (it != em->m_opcode_label.end())
    {
        em->flush_tos();
        em->flush_push_budget();
        WOORT_JIT_CODE(bind(it->second));
    }

    return true;
}

bool woort_JIT_Backend_arm64_post_dispatch(
    void* emitter)
{
    woort_JIT_Asmjit_arm64_emitter* const em =
        static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

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
/* 指令派发接口                                                               */
/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_NOP(void* emitter)
{
    (void)emitter;
}

void woort_JIT_Backend_arm64_LOAD(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];

    if (src < em->m_cenv_constant_count)
        em->store_stack(dst, Imm(src_addr->m_integer));
    else
    {
        em->store_stack(dst, em->static_slot(src));
    }
}

void woort_JIT_Backend_arm64_STORE(void* emitter, woort_Opcode_Global dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Gp dst_ptr = em->static_slot_ptr(dst);

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->get_gc_flag_ptr();
    const Gp flag_val = em->c->new_gp32();
    WOORT_JIT_CODE(ldrb(flag_val, ptr(flag_ptr)));
    WOORT_JIT_CODE(cbz(flag_val, L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GC_mixed_write_barrier_value, FuncSignature::build<void, woort_Value*, uint64_t>());

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, val);
    }
    WOORT_JIT_CODE(b(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(str(val, ptr(dst_ptr)));

    WOORT_JIT_CODE(bind(L_end));
}

void woort_JIT_Backend_arm64_LOADPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp ptr_reg = em->load_stack_gp(src);
    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(val, ptr(ptr_reg)));
    em->store_stack(dst, val);
}

void woort_JIT_Backend_arm64_STOREPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp dst_ptr = em->load_stack_gp(dst);
    const Gp val = em->load_stack_gp(src);

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->get_gc_flag_ptr();
    const Gp flag_val = em->c->new_gp32();
    WOORT_JIT_CODE(ldrb(flag_val, ptr(flag_ptr)));
    WOORT_JIT_CODE(cbz(flag_val, L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GC_mixed_write_barrier_value, FuncSignature::build<void, woort_Value*, uint64_t>());

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, val);
    }
    WOORT_JIT_CODE(b(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(str(val, ptr(dst_ptr)));

    WOORT_JIT_CODE(bind(L_end));
}

void woort_JIT_Backend_arm64_MOV(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_src = em->load_stack_gp(src);
    em->store_stack(dst, reg_src);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_PUSHRCHK(void* emitter, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp size_bytes = em->c->new_gp64();
    WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));
    WOORT_JIT_CODE(mov(em->m_sp, new_sp));
}

void woort_JIT_Backend_arm64_PUSHSCHK(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    /* A8: coalesce overflow checks across consecutive checked pushes. */
    em->emit_pushchk_guard();

    WOORT_JIT_CODE(str(val, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_PUSHCCHK(void* emitter, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];
    const bool is_constant = (src < em->m_cenv_constant_count);

    /* A8: coalesce overflow checks across consecutive checked pushes. */
    em->emit_pushchk_guard();

    {
        const Gp val = em->c->new_gp64();
        if (is_constant)
            WOORT_JIT_CODE(mov(val, Imm(src_addr->m_integer)));
        else
        {
            WOORT_JIT_CODE(ldr(val, em->static_slot(src)));
        }
        WOORT_JIT_CODE(str(val, ptr(em->m_sp)));
    }

    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_ASSURESSZ(void* emitter, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp size_bytes = em->c->new_gp64();
    WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_arm64_PUSHS(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    WOORT_JIT_CODE(str(val, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_PUSHC(void* emitter, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];
    const bool is_constant = (src < em->m_cenv_constant_count);

    {
        const Gp val = em->c->new_gp64();
        if (is_constant)
            WOORT_JIT_CODE(mov(val, Imm(src_addr->m_integer)));
        else
        {
            WOORT_JIT_CODE(ldr(val, em->static_slot(src)));
        }
        WOORT_JIT_CODE(str(val, ptr(em->m_sp)));
    }

    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_POPR(void* emitter, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    if (n != 0)
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(em->m_sp, em->m_sp, size_bytes));
    }
}

void woort_JIT_Backend_arm64_POPS(void* emitter, woort_Opcode_Stack dst)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    WOORT_JIT_CODE(add(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));

    em->store_stack(dst, ptr(em->m_sp));
}

void woort_JIT_Backend_arm64_POPC(void* emitter, woort_Opcode_Global dst)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    WOORT_JIT_CODE(add(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(val, ptr(em->m_sp)));

    const Gp dst_ptr = em->static_slot_ptr(dst);

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->get_gc_flag_ptr();
    const Gp flag_val = em->c->new_gp32();
    WOORT_JIT_CODE(ldrb(flag_val, ptr(flag_ptr)));
    WOORT_JIT_CODE(cbz(flag_val, L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GC_mixed_write_barrier_value, FuncSignature::build<void, woort_Value*, uint64_t>());

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, val);
    }
    WOORT_JIT_CODE(b(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(str(val, ptr(dst_ptr)));

    WOORT_JIT_CODE(bind(L_end));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ITOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_src = em->load_stack_gp(src);
    const Vec xmm = em->c->new_vec_d();
    WOORT_JIT_CODE(scvtf(xmm, reg_src));

    const Gp result = em->c->new_gp64();
    WOORT_JIT_CODE(fmov(result, xmm));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_ITOS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp int_val = em->load_stack_gp(src);

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_from_integer, FuncSignature::build<const woort_GCString*, woort_Int>());

    invoke_node->set_arg(0, int_val);
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_RTOI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_src = em->load_stack_gp(src);
    const Vec xmm = em->c->new_vec_d();
    WOORT_JIT_CODE(fmov(xmm, reg_src));

    const Gp result = em->c->new_gp64();
    WOORT_JIT_CODE(fcvtzs(result, xmm));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_RTOS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_src = em->load_stack_gp(src);
    const Vec xmm = em->c->new_vec_d();
    WOORT_JIT_CODE(fmov(xmm, reg_src));

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_from_real, FuncSignature::build<const woort_GCString*, woort_Real>());

    invoke_node->set_arg(0, xmm);
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_CASTSTO(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    switch (target)
    {
    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const Gp reg_src = em->load_stack_gp(src);
        em->store_stack(dst, reg_src);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp str_ptr = em->load_stack_gp(src);

        const Gp result = em->c->new_gp64();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_to_integer,
            FuncSignature::build<woort_Int, const woort_GCString*>());

        invoke_node->set_arg(0, str_ptr);
        invoke_node->set_ret(0, result);

        em->store_stack(dst, result);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Gp str_ptr = em->load_stack_gp(src);

        const Vec xmm = em->c->new_vec_d();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_to_real,
            FuncSignature::build<woort_Real, const woort_GCString*>());

        invoke_node->set_arg(0, str_ptr);
        invoke_node->set_ret(0, xmm);

        const Gp result = em->c->new_gp64();
        WOORT_JIT_CODE(fmov(result, xmm));

        em->store_stack(dst, result);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp str_ptr = em->load_stack_gp(src);
        const Gp result = em->c->new_gp64();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GCString_to_bool,
            FuncSignature::build<woort_Int, const woort_GCString*>());

        invoke_node->set_arg(0, str_ptr);
        invoke_node->set_ret(0, result);

        em->store_stack(dst, result);
    }
    break;

    default:
        abort();
        break;
    }
}

void woort_JIT_Backend_arm64_CASTSFROM(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    switch (srctype)
    {
    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const Gp reg_src = em->load_stack_gp(src);
        em->store_stack(dst, reg_src);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp int_val = em->load_stack_gp(src);
        const Gp dst_val = em->c->new_gp64();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_from_integer,
            FuncSignature::build<const woort_GCString*, woort_Int>());

        invoke_node->set_arg(0, int_val);
        invoke_node->set_ret(0, dst_val);

        em->store_stack(dst, dst_val);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Gp reg_src = em->load_stack_gp(src);
        const Gp dst_val = em->c->new_gp64();

        const Vec xmm = em->c->new_vec_d();
        WOORT_JIT_CODE(fmov(xmm, reg_src));

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_from_real,
            FuncSignature::build<const woort_GCString*, woort_Real>());

        invoke_node->set_arg(0, xmm);
        invoke_node->set_ret(0, dst_val);

        em->store_stack(dst, dst_val);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp bool_val = em->load_stack_gp(src);
        const Gp dst_val = em->c->new_gp64();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GCString_from_bool,
            FuncSignature::build<const woort_GCString*, woort_Int>());

        invoke_node->set_arg(0, bool_val);
        invoke_node->set_ret(0, dst_val);

        em->store_stack(dst, dst_val);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_NIL:
    case WOORT_BOX_VALUE_TYPE_STRUCT:
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
    {
        const char* lit;
        size_t len;
        switch (srctype)
        {
        case WOORT_BOX_VALUE_TYPE_NIL:       lit = "nil";        len = 3;  break;
        case WOORT_BOX_VALUE_TYPE_STRUCT:    lit = "<struct>";   len = 8;  break;
        case WOORT_BOX_VALUE_TYPE_GCHANDLE:  lit = "<gchandle>"; len = 10; break;
        default:                             lit = "<function>"; len = 10; break;
        }

        const Gp dst_val = em->c->new_gp64();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_make_string,
            FuncSignature::build<const woort_GCString*, const char*, size_t>());

        invoke_node->set_arg(0, Imm(reinterpret_cast<intptr_t>(lit)));
        invoke_node->set_arg(1, Imm(static_cast<intptr_t>(len)));
        invoke_node->set_ret(0, dst_val);

        em->store_stack(dst, dst_val);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        const Gp obj_ptr = em->load_stack_gp(src);
        const Gp dst_val = em->c->new_gp64();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_serialize_vec,
            FuncSignature::build<const woort_GCString*, woort_GCVec*>());

        invoke_node->set_arg(0, obj_ptr);
        invoke_node->set_ret(0, dst_val);

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(tst(dst_val, dst_val));
        WOORT_JIT_CODE(b_ne(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));

        em->store_stack(dst, dst_val);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        const Gp obj_ptr = em->load_stack_gp(src);
        const Gp dst_val = em->c->new_gp64();

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_serialize_map,
            FuncSignature::build<const woort_GCString*, woort_GCMap*>());

        invoke_node->set_arg(0, obj_ptr);
        invoke_node->set_ret(0, dst_val);

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(tst(dst_val, dst_val));
        WOORT_JIT_CODE(b_ne(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));

        em->store_stack(dst, dst_val);
    }
    break;

    default:
        abort();
        break;
    }
}

void woort_JIT_Backend_arm64_CASTDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Label L_done = em->c->new_label();

    auto unbox_real = [&](const Gp& v) -> Vec {
        const Gp sign = em->c->new_gp64();
        const Gp sign_mask = em->c->new_gp64();
        WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(0x8000000000000000ULL))));
        WOORT_JIT_CODE(mov(sign, v));
        WOORT_JIT_CODE(and_(sign, sign, sign_mask));

        const Gp exp_bit = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_bit, v));
        WOORT_JIT_CODE(lsr(exp_bit, exp_bit, 62));
        WOORT_JIT_CODE(and_(exp_bit, exp_bit, Imm(1)));
        WOORT_JIT_CODE(eor(exp_bit, exp_bit, Imm(1)));
        WOORT_JIT_CODE(lsl(exp_bit, exp_bit, 62));

        const Gp bits = em->c->new_gp64();
        const Gp low62_mask = em->c->new_gp64();
        WOORT_JIT_CODE(mov(low62_mask, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
        WOORT_JIT_CODE(mov(bits, v));
        WOORT_JIT_CODE(lsr(bits, bits, 1));
        WOORT_JIT_CODE(and_(bits, bits, low62_mask));
        WOORT_JIT_CODE(orr(bits, bits, exp_bit));
        WOORT_JIT_CODE(orr(bits, bits, sign));

        const Vec xmm = em->c->new_vec_d();
        WOORT_JIT_CODE(fmov(xmm, bits));
        return xmm;
    };

    auto finish_real = [&](const Vec& xmm) {
        const Gp r = em->c->new_gp64();
        WOORT_JIT_CODE(fmov(r, xmm));
        em->store_stack(dst, r);
    };

    auto unbox_int = [&](const Gp& v) -> Gp {
        const Gp r = em->c->new_gp64();
        WOORT_JIT_CODE(mov(r, v));
        WOORT_JIT_CODE(asr(r, r, 2));
        return r;
    };

    auto unbox_bool = [&](const Gp& v) -> Gp {
        const Gp r = em->c->new_gp64();
        WOORT_JIT_CODE(mov(r, v));
        WOORT_JIT_CODE(lsr(r, r, 3));
        return r;
    };

    auto load_proxy = [&]() -> Gp {
        const Gp proxy = em->c->new_gp64();
        WOORT_JIT_CODE(ldr(proxy, ptr(val)));
        return proxy;
    };

    auto proxy_is = [&](const Gp& proxy, const woort_GCUnitProxy& sym, const Label& L) {
        const Gp tmp = em->c->new_gp64();
        WOORT_JIT_CODE(mov(tmp, reinterpret_cast<uintptr_t>(&sym)));
        WOORT_JIT_CODE(cmp(proxy, tmp));
        WOORT_JIT_CODE(b_eq(L));
    };

    auto read_ex_int = [&]() -> Gp {
        const Gp iv = em->c->new_gp64();
        WOORT_JIT_CODE(ldr(iv, ptr(val, static_cast<int32_t>(offsetof(woort_BoxedExValue, m_int)))));
        return iv;
    };
    auto read_ex_real = [&]() -> Vec {
        const Vec rv = em->c->new_vec_d();
        WOORT_JIT_CODE(ldr(rv, ptr(val, static_cast<int32_t>(offsetof(woort_BoxedExValue, m_real)))));
        return rv;
    };
    auto ex_is_int = [&]() -> Gp {
        const Gp r = em->c->new_gp32();
        WOORT_JIT_CODE(ldrb(r, ptr(val, static_cast<int32_t>(offsetof(woort_BoxedExValue, m_is_int)))));
        return r;
    };

    switch (target)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        auto from_int = [&](const Gp& iv) { em->store_stack(dst, iv); };
        auto from_real = [&](const Vec& rv) {
            const Gp r = em->c->new_gp64();
            WOORT_JIT_CODE(fcvtzs(r, rv));
            em->store_stack(dst, r);
        };

        const Label L_scalar = em->c->new_label();
        const Label L_heap = em->c->new_label();
        const Label L_real_i = em->c->new_label();
        const Label L_int_i = em->c->new_label();
        const Label L_nil = em->c->new_label();
        const Label L_ex = em->c->new_label();
        const Label L_ex_real = em->c->new_label();
        const Label L_str = em->c->new_label();
        const Label L_bad = em->c->new_label();

        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_scalar));
        WOORT_JIT_CODE(b(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(b_ne(L_real_i));
        WOORT_JIT_CODE(tst(val, Imm(0b010)));
        WOORT_JIT_CODE(b_ne(L_int_i));
        { from_int(unbox_bool(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(tst(val, val));
            WOORT_JIT_CODE(b_eq(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            WOORT_JIT_CODE(b(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        { em->store_stack(dst, Imm(0)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(tst(is_int, is_int));
            WOORT_JIT_CODE(b_eq(L_ex_real));
            { from_int(read_ex_int()); WOORT_JIT_CODE(b(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_real));
            { from_real(read_ex_real()); WOORT_JIT_CODE(b(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_to_integer,
                FuncSignature::build<woort_Int, const woort_GCString*>());
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, r);
            em->store_stack(dst, r);
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        auto from_int = [&](const Gp& iv) {
            const Vec xmm = em->c->new_vec_d();
            WOORT_JIT_CODE(scvtf(xmm, iv));
            finish_real(xmm);
        };
        auto from_real = [&](const Vec& rv) { finish_real(rv); };

        const Label L_scalar = em->c->new_label();
        const Label L_heap = em->c->new_label();
        const Label L_real_i = em->c->new_label();
        const Label L_int_i = em->c->new_label();
        const Label L_nil = em->c->new_label();
        const Label L_ex = em->c->new_label();
        const Label L_ex_int = em->c->new_label();
        const Label L_str = em->c->new_label();
        const Label L_bad = em->c->new_label();

        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_scalar));
        WOORT_JIT_CODE(b(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(b_ne(L_real_i));
        WOORT_JIT_CODE(tst(val, Imm(0b010)));
        WOORT_JIT_CODE(b_ne(L_int_i));
        { from_int(unbox_bool(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(tst(val, val));
            WOORT_JIT_CODE(b_eq(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            WOORT_JIT_CODE(b(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        {
            const Gp zero = em->c->new_gp64();
            const Vec xmm = em->c->new_vec_d();
            WOORT_JIT_CODE(mov(zero, Imm(0)));
            WOORT_JIT_CODE(fmov(xmm, zero));
            finish_real(xmm);
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(tst(is_int, is_int));
            WOORT_JIT_CODE(b_ne(L_ex_int));
            { from_real(read_ex_real()); WOORT_JIT_CODE(b(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_int));
            { from_int(read_ex_int()); WOORT_JIT_CODE(b(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        {
            const Vec xmm = em->c->new_vec_d();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_to_real,
                FuncSignature::build<woort_Real, const woort_GCString*>());
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, xmm);
            finish_real(xmm);
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        auto from_int = [&](const Gp& iv) {
            const Gp r = em->c->new_gp64();
            WOORT_JIT_CODE(tst(iv, iv));
            WOORT_JIT_CODE(cset(r, CondCode::kNE));
            em->store_stack(dst, r);
        };
        auto from_real = [&](const Vec& rv) {
            const Vec zero = em->c->new_vec_d();
            {
                const Gp zg = em->c->new_gp64();
                WOORT_JIT_CODE(mov(zg, Imm(0)));
                WOORT_JIT_CODE(fmov(zero, zg));
            }
            const Gp r = em->c->new_gp64();
            WOORT_JIT_CODE(fcmp(rv, zero));
            WOORT_JIT_CODE(cset(r, CondCode::kNE));
            em->store_stack(dst, r);
        };

        const Label L_scalar = em->c->new_label();
        const Label L_heap = em->c->new_label();
        const Label L_real_i = em->c->new_label();
        const Label L_int_i = em->c->new_label();
        const Label L_nil = em->c->new_label();
        const Label L_ex = em->c->new_label();
        const Label L_ex_real = em->c->new_label();
        const Label L_str = em->c->new_label();
        const Label L_bad = em->c->new_label();

        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_scalar));
        WOORT_JIT_CODE(b(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(b_ne(L_real_i));
        WOORT_JIT_CODE(tst(val, Imm(0b010)));
        WOORT_JIT_CODE(b_ne(L_int_i));
        { from_int(unbox_bool(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(tst(val, val));
            WOORT_JIT_CODE(b_eq(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            WOORT_JIT_CODE(b(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        { em->store_stack(dst, Imm(0)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(tst(is_int, is_int));
            WOORT_JIT_CODE(b_eq(L_ex_real));
            { from_int(read_ex_int()); WOORT_JIT_CODE(b(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_real));
            { from_real(read_ex_real()); WOORT_JIT_CODE(b(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GCString_to_bool,
                FuncSignature::build<woort_Int, const woort_GCString*>());
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, r);
            em->store_stack(dst, r);
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        auto from_int = [&](const Gp& iv) {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_from_integer,
                FuncSignature::build<const woort_GCString*, woort_Int>());
            invoke_node->set_arg(0, iv);
            invoke_node->set_ret(0, r);
            em->store_stack(dst, r);
        };
        auto from_real = [&](const Vec& rv) {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_from_real,
                FuncSignature::build<const woort_GCString*, woort_Real>());
            invoke_node->set_arg(0, rv);
            invoke_node->set_ret(0, r);
            em->store_stack(dst, r);
        };
        auto from_bool = [&](const Gp& bv) {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GCString_from_bool,
                FuncSignature::build<const woort_GCString*, woort_Int>());
            invoke_node->set_arg(0, bv);
            invoke_node->set_ret(0, r);
            em->store_stack(dst, r);
        };
        auto make_literal = [&](const char* lit, size_t len) {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_make_string,
                FuncSignature::build<const woort_GCString*, const char*, size_t>());
            invoke_node->set_arg(0, Imm(reinterpret_cast<intptr_t>(lit)));
            invoke_node->set_arg(1, Imm(static_cast<intptr_t>(len)));
            invoke_node->set_ret(0, r);
            em->store_stack(dst, r);
        };

        const Label L_scalar = em->c->new_label();
        const Label L_heap = em->c->new_label();
        const Label L_real_i = em->c->new_label();
        const Label L_int_i = em->c->new_label();
        const Label L_nil = em->c->new_label();
        const Label L_ex = em->c->new_label();
        const Label L_ex_int = em->c->new_label();
        const Label L_str = em->c->new_label();
        const Label L_vec = em->c->new_label();
        const Label L_map = em->c->new_label();
        const Label L_struct = em->c->new_label();
        const Label L_handle = em->c->new_label();
        const Label L_closure = em->c->new_label();
        const Label L_bad = em->c->new_label();

        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_scalar));
        WOORT_JIT_CODE(b(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(b_ne(L_real_i));
        WOORT_JIT_CODE(tst(val, Imm(0b010)));
        WOORT_JIT_CODE(b_ne(L_int_i));
        { from_bool(unbox_bool(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(tst(val, val));
            WOORT_JIT_CODE(b_eq(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            proxy_is(proxy, WOORT_GCVEC_UNIT_PROXY, L_vec);
            proxy_is(proxy, WOORT_GCMAP_UNIT_PROXY, L_map);
            proxy_is(proxy, WOORT_GCSTRUCT_UNIT_PROXY, L_struct);
            proxy_is(proxy, WOORT_GCHANDLE_UNIT_PROXY, L_handle);
            proxy_is(proxy, WOORT_GCCLOSURE_UNIT_PROXY, L_closure);
            WOORT_JIT_CODE(b(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        { make_literal("nil", 3); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(tst(is_int, is_int));
            WOORT_JIT_CODE(b_ne(L_ex_int));
            { from_real(read_ex_real()); WOORT_JIT_CODE(b(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_int));
            { from_int(read_ex_int()); WOORT_JIT_CODE(b(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        { em->store_stack(dst, val); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_vec));
        {
            const Gp result = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_serialize_vec,
                FuncSignature::build<const woort_GCString*, woort_GCVec*>());
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, result);
            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(tst(result, result));
            WOORT_JIT_CODE(b_ne(L_ok));
            em->emit_failed_fallback(*em->m_ip);
            WOORT_JIT_CODE(bind(L_ok));
            em->store_stack(dst, result);
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_map));
        {
            const Gp result = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_serialize_map,
                FuncSignature::build<const woort_GCString*, woort_GCMap*>());
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, result);
            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(tst(result, result));
            WOORT_JIT_CODE(b_ne(L_ok));
            em->emit_failed_fallback(*em->m_ip);
            WOORT_JIT_CODE(bind(L_ok));
            em->store_stack(dst, result);
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_struct));
        { make_literal("<struct>", 8); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_handle));
        { make_literal("<gchandle>", 10); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_closure));
        { make_literal("<function>", 10); WOORT_JIT_CODE(b(L_done)); }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    default:
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    WOORT_JIT_CODE(bind(L_done));
}

void woort_JIT_Backend_arm64_ASSERTDYN(void* emitter, woort_BoxValueType target, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Gp result = em->c->new_gp64();

    switch (target)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Label L_inline = em->c->new_label();
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_inline));

        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_check_int_ex,
                FuncSignature::build<bool, woort_BoxedValue>());

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, ok);

            WOORT_JIT_CODE(cmp(ok, 0));
            WOORT_JIT_CODE(cset(result, CondCode::kNE));
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_inline));
        WOORT_JIT_CODE(eor(result, val, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));
        WOORT_JIT_CODE(tst(result, Imm(0b011)));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Label L_inline = em->c->new_label();
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_inline));

        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_check_real_ex,
                FuncSignature::build<bool, woort_BoxedValue>());

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, ok);

            WOORT_JIT_CODE(cmp(ok, 0));
            WOORT_JIT_CODE(cset(result, CondCode::kNE));
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_inline));
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(cset(result, CondCode::kNE));

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        WOORT_JIT_CODE(eor(result, val, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        WOORT_JIT_CODE(tst(result, Imm(0b111)));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_NIL:
    {
        WOORT_JIT_CODE(tst(val, val));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));
        break;
    }
    default:
    {
        const Gp ok = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_check_gc,
            FuncSignature::build<bool, woort_BoxedValue, woort_BoxValueType>());

        invoke_node->set_arg(0, val);
        invoke_node->set_arg(1, Imm(static_cast<int32_t>(target)));
        invoke_node->set_ret(0, ok);

        WOORT_JIT_CODE(cmp(ok, 0));
        WOORT_JIT_CODE(cset(result, CondCode::kNE));
        break;
    }
    }

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(result, result));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_CALLNWO(void* emitter, woort_Opcode_Global func)
{
    (void)func;

    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_arm64_CALLNFP(void* emitter, woort_Opcode_Global func)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const woort_Value* const func_addr = &em->m_cenv_static_storage[func];

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(2 * sizeof(woort_Value)))));
        WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));
    }

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    {
        const int32_t way_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_way);
        const int32_t bp_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_bp_offset);
        const int32_t addr_off = 2 * (int32_t)sizeof(woort_Value);

        const Gp way = em->c->new_gp32();
        WOORT_JIT_CODE(mov(way, Imm(static_cast<int32_t>(WOORT_CALL_WAY_NEAR))));
        WOORT_JIT_CODE(str(way, ptr(new_sp, way_off)));

        const Gp bp_offset = em->c->new_gp64();
        WOORT_JIT_CODE(sub(bp_offset, em->m_sb, em->m_sp));
        WOORT_JIT_CODE(lsr(bp_offset, bp_offset, 3));
        WOORT_JIT_CODE(str(bp_offset.w(), ptr(new_sp, bp_off)));

        const Gp ret_addr = em->c->new_gp64();
        WOORT_JIT_CODE(mov(ret_addr, Imm(reinterpret_cast<intptr_t>(*em->m_ip + 1))));
        WOORT_JIT_CODE(str(ret_addr, ptr(new_sp, addr_off)));
    }

    /* native_fn = *(func_addr)：物化绝对地址后 ldr */
    const Gp addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(addr, reinterpret_cast<uintptr_t>(func_addr)));
    const Gp native_fn = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(native_fn, ptr(addr)));
    em->emit_sync_rt_ip_status(native_fn);
    WOORT_JIT_CODE(str(new_sp, ptr(em->m_vm, WOORT_VM_OFFSETOF_SP)));
    WOORT_JIT_CODE(str(new_sp, ptr(em->m_vm, WOORT_VM_OFFSETOF_SB)));

    const Gp status = em->c->new_gp32();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        native_fn,
        FuncSignature::build<woort_VmCallStatus>()));

    invoke_node->set_ret(0, status);

    {
        const Label L_after_realloc = em->c->new_label();
        const Gp vm_stack_end = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(vm_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
        WOORT_JIT_CODE(cmp(vm_stack_end, em->m_stack_end));
        WOORT_JIT_CODE(b_eq(L_after_realloc));
        em->emit_jit_call_resync(L_after_realloc);
        WOORT_JIT_CODE(bind(L_after_realloc));
    }

    {
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(cmp(status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_RESYNC)));
        WOORT_JIT_CODE(b_ne(L_done));
        em->emit_checkpoint(woort_JIT_next_bytecode(*em->m_ip));
        WOORT_JIT_CODE(bind(L_done));
    }
}

void woort_JIT_Backend_arm64_CALLNJIT(void* emitter, woort_Opcode_Global func)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    /* Operand `func` is now an index into m_jit_functions (not a cidx).
       func_addr is the absolute address of the slot holding the woort_JitFunction. */
    const void* const func_addr =
        woort_JIT_CodeEnv_jit_function_slot(em->cenv, func);

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(2 * sizeof(woort_Value)))));
        WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));
    }

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    // Apply callstack.
    {
        const int32_t way_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_way);
        const int32_t bp_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_bp_offset);
        const int32_t addr_off = 2 * (int32_t)sizeof(woort_Value);

        const Gp way = em->c->new_gp32();
        WOORT_JIT_CODE(mov(way, Imm(static_cast<int32_t>(WOORT_CALL_WAY_FAR))));
        WOORT_JIT_CODE(str(way, ptr(new_sp, way_off)));

        const Gp bp_offset = em->c->new_gp64();
        WOORT_JIT_CODE(sub(bp_offset, em->m_sb, em->m_sp));
        WOORT_JIT_CODE(lsr(bp_offset, bp_offset, 3));
        WOORT_JIT_CODE(str(bp_offset.w(), ptr(new_sp, bp_off)));

        const Gp ret_addr = em->c->new_gp64();
        WOORT_JIT_CODE(mov(ret_addr, Imm(reinterpret_cast<intptr_t>(*em->m_ip + 1))));
        WOORT_JIT_CODE(str(ret_addr, ptr(new_sp, addr_off)));
    }

    /* jit_fn = *(func_addr) */
    const Gp addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(addr, reinterpret_cast<uintptr_t>(func_addr)));
    const Gp jit_fn = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(jit_fn, ptr(addr)));

    const Gp status = em->c->new_gp32();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        jit_fn,
        FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*, const woort_Value*, const woort_Value*>()));

    invoke_node->set_arg(0, em->m_vm);
    invoke_node->set_arg(1, new_sp);
    invoke_node->set_arg(2, new_sp);
    invoke_node->set_ret(0, status);

    const Label L_normal = em->c->new_label();
    WOORT_JIT_CODE(cmp(status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_NORMAL)));
    WOORT_JIT_CODE(b_eq(L_normal));

    em->return_with_status(status);

    WOORT_JIT_CODE(bind(L_normal));

    const Label L_continue = em->c->new_label();
    {
        const Gp vm_stack_end = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(vm_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
        WOORT_JIT_CODE(cmp(vm_stack_end, em->m_stack_end));
        WOORT_JIT_CODE(b_eq(L_continue));
        em->emit_jit_call_resync(L_continue);
    }
    WOORT_JIT_CODE(bind(L_continue));
}

static void woort_JIT_arm64_emit_closure_call(
    woort_JIT_Asmjit_arm64_emitter* em, const Gp& target_closure)
{
    static_assert(sizeof(woort_Value) == 8, "");

    const int32_t off_script_fn =
        WOORT_GCCLOSURE_OFFSETOF_SCRIPT_FUNCTION;
    const int32_t off_fn =
        WOORT_GCCLOSURE_OFFSETOF_JIT_FUNCTION;
    const int32_t off_size =
        WOORT_GCCLOSURE_OFFSETOF_SIZE;
    const int32_t off_datas =
        WOORT_GCCLOSURE_OFFSETOF_DATAS;

    const int32_t way_off =
        1 * static_cast<int32_t>(sizeof(woort_Value)) +
        static_cast<int32_t>(offsetof(woort_RetBP, m_way));
    const int32_t bp_off =
        1 * static_cast<int32_t>(sizeof(woort_Value)) +
        static_cast<int32_t>(offsetof(woort_RetBP, m_bp_offset));
    const int32_t addr_off =
        2 * static_cast<int32_t>(sizeof(woort_Value));

    const Gp fn_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(fn_ptr, ptr(target_closure, off_fn)));

    const Gp script_fn = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(script_fn, ptr(target_closure, off_script_fn)));

    {
        const Label L_inline = em->c->new_label();

        WOORT_JIT_CODE(tst(script_fn, script_fn));
        WOORT_JIT_CODE(b_eq(L_inline));

        WOORT_JIT_CODE(tst(fn_ptr, fn_ptr));
        WOORT_JIT_CODE(b_ne(L_inline));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_inline));
    }

    const Gp size_bytes = em->c->new_gp64();
    {
        const Gp size_count = em->c->new_gp64();
        WOORT_JIT_CODE(ldr(size_count, ptr(target_closure, off_size)));
        WOORT_JIT_CODE(lsl(size_bytes, size_count, 3));
    }

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sb = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sb, em->m_sp));
    {
        const Gp two = em->c->new_gp64();
        WOORT_JIT_CODE(mov(two, Imm(static_cast<int64_t>(2 * sizeof(woort_Value)))));
        WOORT_JIT_CODE(sub(new_sb, new_sb, two));
    }

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, new_sb));
    WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));

    {
        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
        WOORT_JIT_CODE(b_hs(L_ok));

        em->emit_extern_stack(*em->m_ip, L_retry);

        WOORT_JIT_CODE(bind(L_ok));
    }

    {
        const Label L_no_datas = em->c->new_label();
        WOORT_JIT_CODE(tst(size_bytes, size_bytes));
        WOORT_JIT_CODE(b_eq(L_no_datas));

        const Gp dst = em->c->new_gp_ptr();
        WOORT_JIT_CODE(add(dst, new_sp, static_cast<int32_t>(sizeof(woort_Value))));

        const Gp src = em->c->new_gp_ptr();
        WOORT_JIT_CODE(add(src, target_closure, off_datas));

        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), WOORT_JIT_MEMCPY,
            FuncSignature::build<void, void*, const void*, size_t>());

        invoke_node->set_arg(0, dst);
        invoke_node->set_arg(1, src);
        invoke_node->set_arg(2, size_bytes);

        WOORT_JIT_CODE(bind(L_no_datas));
    }

    {
        const Gp bp_offset = em->c->new_gp64();
        WOORT_JIT_CODE(sub(bp_offset, em->m_sb, em->m_sp));
        WOORT_JIT_CODE(lsr(bp_offset, bp_offset, 3));
        WOORT_JIT_CODE(str(bp_offset.w(), ptr(new_sb, bp_off)));

        const Gp ret_addr = em->c->new_gp64();
        WOORT_JIT_CODE(mov(ret_addr, Imm(reinterpret_cast<intptr_t>(*em->m_ip + 1))));
        WOORT_JIT_CODE(str(ret_addr, ptr(new_sb, addr_off)));
    }

    {
        const Label L_native = em->c->new_label();
        const Label L_call_done = em->c->new_label();

        WOORT_JIT_CODE(tst(script_fn, script_fn));
        WOORT_JIT_CODE(b_eq(L_native));

        {
            const Gp way = em->c->new_gp32();
            WOORT_JIT_CODE(mov(way, Imm(static_cast<int32_t>(WOORT_CALL_WAY_FAR))));
            WOORT_JIT_CODE(str(way, ptr(new_sb, way_off)));
        }

        {
            const Gp status = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                fn_ptr,
                FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*, const woort_Value*, const woort_Value*>()));

            invoke_node->set_arg(0, em->m_vm);
            invoke_node->set_arg(1, new_sb);
            invoke_node->set_arg(2, new_sp);
            invoke_node->set_ret(0, status);

            const Label L_normal = em->c->new_label();
            WOORT_JIT_CODE(cmp(status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_RESYNC)));
            WOORT_JIT_CODE(b_ne(L_normal));
            em->return_with_status(status);
            WOORT_JIT_CODE(bind(L_normal));
        }

        {
            const Label L_continue = em->c->new_label();
            const Gp vm_stack_end = em->c->new_gp_ptr();
            WOORT_JIT_CODE(ldr(vm_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
            WOORT_JIT_CODE(cmp(vm_stack_end, em->m_stack_end));
            WOORT_JIT_CODE(b_eq(L_continue));
            em->emit_jit_call_resync(L_continue);
            WOORT_JIT_CODE(bind(L_continue));
        }
        WOORT_JIT_CODE(b(L_call_done));

        WOORT_JIT_CODE(bind(L_native));

        {
            const Gp way = em->c->new_gp32();
            WOORT_JIT_CODE(mov(way, Imm(static_cast<int32_t>(WOORT_CALL_WAY_NEAR))));
            WOORT_JIT_CODE(str(way, ptr(new_sb, way_off)));
        }

        em->emit_sync_rt_ip_status(fn_ptr);

        WOORT_JIT_CODE(str(new_sp, ptr(em->m_vm, WOORT_VM_OFFSETOF_SP)));
        WOORT_JIT_CODE(str(new_sp, ptr(em->m_vm, WOORT_VM_OFFSETOF_SB)));

        {
            const Gp status = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                fn_ptr,
                FuncSignature::build<woort_VmCallStatus>()));

            invoke_node->set_ret(0, status);

            const Label L_after_realloc = em->c->new_label();
            const Gp vm_stack_end = em->c->new_gp_ptr();
            WOORT_JIT_CODE(ldr(vm_stack_end, ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
            WOORT_JIT_CODE(cmp(vm_stack_end, em->m_stack_end));
            WOORT_JIT_CODE(b_eq(L_after_realloc));
            em->emit_jit_call_resync(L_after_realloc);
            WOORT_JIT_CODE(bind(L_after_realloc));

            const Label L_done = em->c->new_label();
            WOORT_JIT_CODE(cmp(status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_RESYNC)));
            WOORT_JIT_CODE(b_ne(L_done));
            em->emit_checkpoint(woort_JIT_next_bytecode(*em->m_ip));
            WOORT_JIT_CODE(bind(L_done));
        }

        WOORT_JIT_CODE(bind(L_call_done));
    }
}

void woort_JIT_Backend_arm64_CALLS(void* emitter, woort_Opcode_Stack func)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp target_closure = em->load_stack_gp(func);

    woort_JIT_arm64_emit_closure_call(em, target_closure);
}

void woort_JIT_Backend_arm64_CALLC(void* emitter, woort_Opcode_Global func)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const woort_Value* const func_addr = &em->m_cenv_static_storage[func];

    const Gp addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(addr, reinterpret_cast<uintptr_t>(func_addr)));
    const Gp target_closure = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(target_closure, ptr(addr)));

    woort_JIT_arm64_emit_closure_call(em, target_closure);
}

void woort_JIT_Backend_arm64_RET(void* emitter)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    em->emit_ret();
}

void woort_JIT_Backend_arm64_RETVS(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp ret_val = em->load_stack_gp(src);

    WOORT_JIT_CODE(str(ret_val, ptr(em->m_sb, 2 * static_cast<int32_t>(sizeof(woort_Value)))));

    em->emit_ret();
}

void woort_JIT_Backend_arm64_RETVC(void* emitter, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];
    const bool is_constant = (src < em->m_cenv_constant_count);

    const int32_t slot_off = 2 * static_cast<int32_t>(sizeof(woort_Value));

    if (is_constant &&
        src_addr->m_integer >= INT32_MIN &&
        src_addr->m_integer <= INT32_MAX)
    {
        const Gp ret_val = em->c->new_gp64();
        WOORT_JIT_CODE(mov(ret_val, Imm(static_cast<int32_t>(src_addr->m_integer))));
        WOORT_JIT_CODE(str(ret_val, ptr(em->m_sb, slot_off)));
    }
    else
    {
        const Gp ret_val = em->c->new_gp64();
        if (is_constant)
            WOORT_JIT_CODE(mov(ret_val, Imm(src_addr->m_integer)));
        else
        {
            WOORT_JIT_CODE(ldr(ret_val, em->static_slot(src)));
        }
        WOORT_JIT_CODE(str(ret_val, ptr(em->m_sb, slot_off)));
    }

    em->emit_ret();
}

void woort_JIT_Backend_arm64_POPRS(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp count = em->load_stack_gp(src);

    /* lea(m_sp, ptr(m_sp, count, 3)) -> m_sp += count * 8 */
    WOORT_JIT_CODE(add(em->m_sp, em->m_sp, count, lsl(3)));
}

void woort_JIT_Backend_arm64_RESULT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    em->store_stack(dst, ptr(em->m_sp));
    if (n != 0)
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(em->m_sp, em->m_sp, size_bytes));
    }
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_JFWD(void* emitter, woort_Opcode_CodeAbs target)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Label lbl = em->get_label(em->m_cenv_codes + target);

    WOORT_JIT_CODE(b(lbl));
}

void woort_JIT_Backend_arm64_JBCK(void* emitter, woort_Opcode_CodeAbs target)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Label lbl = em->get_label(em->m_cenv_codes + target);

    em->emit_checkpoint(em->m_cenv_codes + target);

    WOORT_JIT_CODE(b(lbl));
}

void woort_JIT_Backend_arm64_JFWDNZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_cond = em->load_stack_gp(cond);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cbnz(reg_cond, lbl));
}

void woort_JIT_Backend_arm64_JFWDZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_cond = em->load_stack_gp(cond);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cbz(reg_cond, lbl));
}

void woort_JIT_Backend_arm64_JFWDEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_eq(lbl));
}

void woort_JIT_Backend_arm64_JFWDNEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_ne(lbl));
}

void woort_JIT_Backend_arm64_JBCKNZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_cond = em->load_stack_gp(cond);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cbz(reg_cond, L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JBCKZ(void* emitter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_cond = em->load_stack_gp(cond);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cbnz(reg_cond, L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JBCKEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_ne(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JBCKNEQ(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_eq(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JFWDLT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_lt(lbl));
}

void woort_JIT_Backend_arm64_JFWDGT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_gt(lbl));
}

void woort_JIT_Backend_arm64_JFWDEL(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label target = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_le(target));
}

void woort_JIT_Backend_arm64_JFWDEG(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_ge(lbl));
}

void woort_JIT_Backend_arm64_JBCKLT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_ge(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JBCKGT(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_le(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JBCKEL(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_gt(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_arm64_JBCKEG(void* emitter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_lt(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(b(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_MKVEC(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_make_vec,
        FuncSignature::build<woort_GCVec*, woort_Value*, size_t>());

    invoke_node->set_arg(0, em->m_sp);
    invoke_node->set_arg(1, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);

    if (n != 0)
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(em->m_sp, em->m_sp, size_bytes));
    }
}

void woort_JIT_Backend_arm64_MKMAP(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_make_map,
        FuncSignature::build<woort_GCMap*, woort_Value*, size_t>());

    invoke_node->set_arg(0, em->m_sp);
    invoke_node->set_arg(1, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);

    if (n != 0)
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * 2 * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(em->m_sp, em->m_sp, size_bytes));
    }
}

void woort_JIT_Backend_arm64_MKSTRUCT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_make_struct,
        FuncSignature::build<void*, woort_Value*, size_t>());

    invoke_node->set_arg(0, em->m_sp);
    invoke_node->set_arg(1, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);

    if (n != 0)
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(em->m_sp, em->m_sp, size_bytes));
    }
}

void woort_JIT_Backend_arm64_MKUNION(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp src_val = em->load_stack_gp(src);

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_make_union,
        FuncSignature::build<void*, woort_Int, uint64_t>());

    invoke_node->set_arg(0, Imm(static_cast<woort_Int>(idx)));
    invoke_node->set_arg(1, src_val);
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_MKCLOSURE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const woort_GCClosure* const tmpl_closure =
        em->m_cenv_static_storage[tmpl].m_closure;

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_make_closure,
        FuncSignature::build<void*, const void*, woort_Value*, size_t>());

    invoke_node->set_arg(0, Imm(reinterpret_cast<intptr_t>(tmpl_closure)));
    invoke_node->set_arg(1, em->m_sp);
    invoke_node->set_arg(2, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);

    if (n != 0)
    {
        const Gp size_bytes = em->c->new_gp64();
        WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(em->m_sp, em->m_sp, size_bytes));
    }
}

void woort_JIT_Backend_arm64_BOXDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_GCUNIT:
    {
        em->store_stack(dst, val);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        WOORT_JIT_CODE(lsl(val, val, 3));
        WOORT_JIT_CODE(orr(val, val, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        em->store_stack(dst, val);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp boxed = em->c->new_gp64();
        WOORT_JIT_CODE(lsl(boxed, val, 2));
        WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));

        const Gp check = em->c->new_gp64();
        WOORT_JIT_CODE(asr(check, boxed, 2));

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(cmp(check, val));
        WOORT_JIT_CODE(b_eq(L_ok));

        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_box_int_ex,
                FuncSignature::build<woort_BoxedValue, woort_Int>());

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_ok));
        em->store_stack(dst, boxed);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Vec xmm_val = em->c->new_vec_d();
        WOORT_JIT_CODE(fmov(xmm_val, val));

        const Gp bits = em->c->new_gp64();
        WOORT_JIT_CODE(fmov(bits, xmm_val));

        const Gp exp_b = em->c->new_gp64();
        WOORT_JIT_CODE(lsr(exp_b, bits, 61));

        const Gp exp_t = em->c->new_gp64();
        WOORT_JIT_CODE(lsr(exp_t, exp_b, 1));
        WOORT_JIT_CODE(eor(exp_b, exp_b, exp_t));

        const Label L_ex = em->c->new_label();
        WOORT_JIT_CODE(tst(exp_b, Imm(1)));
        WOORT_JIT_CODE(b_eq(L_ex));

        const Gp boxed = em->c->new_gp64();
        {
            const Gp sign = em->c->new_gp64();
            const Gp sign_mask = em->c->new_gp64();
            WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(0x8000000000000000ULL))));
            WOORT_JIT_CODE(mov(sign, bits));
            WOORT_JIT_CODE(and_(sign, sign, sign_mask));

            const Gp low62 = em->c->new_gp64();
            const Gp low62_mask = em->c->new_gp64();
            WOORT_JIT_CODE(mov(low62_mask, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
            WOORT_JIT_CODE(mov(low62, bits));
            WOORT_JIT_CODE(and_(low62, low62, low62_mask));
            WOORT_JIT_CODE(lsl(low62, low62, 1));

            WOORT_JIT_CODE(mov(boxed, sign));
            WOORT_JIT_CODE(orr(boxed, boxed, low62));
            WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_REAL))));
        }

        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(b(L_done));

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_box_real_ex,
                FuncSignature::build<woort_BoxedValue, woort_Real>());

            invoke_node->set_arg(0, xmm_val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_done));
        em->store_stack(dst, boxed);
        break;
    }
    default:
    {
        assert(false);
        break;
    }
    }
}

void woort_JIT_Backend_arm64_UNBOXDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    /* 取 dst 槽地址的公共代码：sb + dst*sizeof(woort_Value)，偏移物化以避免立即数范围问题 */
    auto dst_addr = [&]() -> Gp {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(static_cast<int64_t>(dst) * static_cast<int64_t>(sizeof(woort_Value))))));
        const Gp a = em->c->new_gp_ptr();
        WOORT_JIT_CODE(add(a, em->m_sb, off));
        return a;
    };

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Label L_ex = em->c->new_label();
        const Label L_done = em->c->new_label();
        const Label L_bad = em->c->new_label();
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_eq(L_ex));

        /* 内联标量必须是 INT（bit0=0, bit1=1），否则按 bad_type 回退 */
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(b_ne(L_bad));
        WOORT_JIT_CODE(tst(val, Imm(0b010)));
        WOORT_JIT_CODE(b_eq(L_bad));

        WOORT_JIT_CODE(asr(val, val, 2));
        em->store_stack(dst, val);
        WOORT_JIT_CODE(b(L_done));

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp out_addr = dst_addr();

            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_unbox_int_ex,
                FuncSignature::build<bool, woort_BoxedValue, woort_Int*>());

            invoke_node->set_arg(0, val);
            invoke_node->set_arg(1, out_addr);
            invoke_node->set_ret(0, ok);

            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(tst(ok, ok));
            WOORT_JIT_CODE(b_ne(L_ok));

            em->emit_failed_fallback(*em->m_ip);

            WOORT_JIT_CODE(bind(L_ok));
        }

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Gp unboxed = em->c->new_gp64();
        const Label L_ex = em->c->new_label();
        const Label L_done = em->c->new_label();
        const Label L_bad = em->c->new_label();
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_eq(L_ex));

        /* 内联标量必须是 REAL（bit0=1），否则按 bad_type 回退 */
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(b_eq(L_bad));

        const Gp sign = em->c->new_gp64();
        const Gp sign_mask = em->c->new_gp64();
        WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(0x8000000000000000ULL))));
        WOORT_JIT_CODE(mov(sign, val));
        WOORT_JIT_CODE(and_(sign, sign, sign_mask));

        const Gp exp_bit = em->c->new_gp64();
        WOORT_JIT_CODE(lsr(exp_bit, val, 62));
        WOORT_JIT_CODE(eor(exp_bit, exp_bit, Imm(1)));
        WOORT_JIT_CODE(lsl(exp_bit, exp_bit, 62));

        const Gp low62_mask = em->c->new_gp64();
        WOORT_JIT_CODE(mov(low62_mask, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
        WOORT_JIT_CODE(mov(unboxed, val));
        WOORT_JIT_CODE(lsr(unboxed, unboxed, 1));
        WOORT_JIT_CODE(and_(unboxed, unboxed, low62_mask));
        WOORT_JIT_CODE(orr(unboxed, unboxed, exp_bit));
        WOORT_JIT_CODE(orr(unboxed, unboxed, sign));

        em->store_stack(dst, unboxed);
        WOORT_JIT_CODE(b(L_done));

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp out_addr = dst_addr();

            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_unbox_real_ex,
                FuncSignature::build<bool, woort_BoxedValue, woort_Real*>());

            invoke_node->set_arg(0, val);
            invoke_node->set_arg(1, out_addr);
            invoke_node->set_ret(0, ok);

            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(tst(ok, ok));
            WOORT_JIT_CODE(b_ne(L_ok));

            em->emit_failed_fallback(*em->m_ip);

            WOORT_JIT_CODE(bind(L_ok));
        }

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp tag = em->c->new_gp64();
        WOORT_JIT_CODE(eor(tag, val, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        WOORT_JIT_CODE(tst(tag, Imm(0b111)));

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(b_eq(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));
        {
            const Gp unboxed = em->c->new_gp64();
            WOORT_JIT_CODE(lsr(unboxed, val, 3));
            em->store_stack(dst, unboxed);
        }
        break;
    }
    case WOORT_BOX_VALUE_TYPE_NIL:
    {
        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(tst(val, val));
        WOORT_JIT_CODE(b_eq(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));
        em->store_stack(dst, Imm(0));
        break;
    }
    default:
    {
        const Gp out_addr = dst_addr();

        const Gp ok = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_unbox_gc,
            FuncSignature::build<bool, woort_BoxedValue, woort_BoxValueType, woort_Value*>());

        invoke_node->set_arg(0, val);
        invoke_node->set_arg(1, Imm(static_cast<int32_t>(type)));
        invoke_node->set_arg(2, out_addr);
        invoke_node->set_ret(0, ok);

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(tst(ok, ok));
        WOORT_JIT_CODE(b_ne(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));
        break;
    }
    }
}

void woort_JIT_Backend_arm64_CHECKDYN(void* emitter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Gp result = em->c->new_gp64();

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Label L_inline = em->c->new_label();
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_inline));

        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_check_int_ex,
                FuncSignature::build<bool, woort_BoxedValue>());

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, ok);

            WOORT_JIT_CODE(cmp(ok, 0));
            WOORT_JIT_CODE(cset(result, CondCode::kNE));
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_inline));
        WOORT_JIT_CODE(eor(result, val, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));
        WOORT_JIT_CODE(tst(result, Imm(0b011)));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Label L_inline = em->c->new_label();
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(tst(val, Imm(0b111)));
        WOORT_JIT_CODE(b_ne(L_inline));

        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_check_real_ex,
                FuncSignature::build<bool, woort_BoxedValue>());

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, ok);

            WOORT_JIT_CODE(cmp(ok, 0));
            WOORT_JIT_CODE(cset(result, CondCode::kNE));
            WOORT_JIT_CODE(b(L_done));
        }

        WOORT_JIT_CODE(bind(L_inline));
        WOORT_JIT_CODE(tst(val, Imm(0b001)));
        WOORT_JIT_CODE(cset(result, CondCode::kNE));

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        WOORT_JIT_CODE(eor(result, val, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        WOORT_JIT_CODE(tst(result, Imm(0b111)));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_NIL:
    {
        WOORT_JIT_CODE(tst(val, val));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));
        break;
    }
    default:
    {
        const Gp ok = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_check_gc,
            FuncSignature::build<bool, woort_BoxedValue, woort_BoxValueType>());

        invoke_node->set_arg(0, val);
        invoke_node->set_arg(1, Imm(static_cast<int32_t>(type)));
        invoke_node->set_ret(0, ok);

        WOORT_JIT_CODE(cmp(ok, 0));
        WOORT_JIT_CODE(cset(result, CondCode::kNE));
        break;
    }
    }

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_PUSHBOXDYN(void* emitter, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(b_hi(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp val = em->load_stack_gp(src);

    const Gp boxed = em->c->new_gp64();

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_GCUNIT:
    {
        WOORT_JIT_CODE(mov(boxed, val));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        WOORT_JIT_CODE(lsl(boxed, val, 3));
        WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        WOORT_JIT_CODE(lsl(boxed, val, 2));
        WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));

        const Gp check = em->c->new_gp64();
        WOORT_JIT_CODE(asr(check, boxed, 2));

        const Label L_ok2 = em->c->new_label();
        WOORT_JIT_CODE(cmp(check, val));
        WOORT_JIT_CODE(b_eq(L_ok2));

        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_box_int_ex,
                FuncSignature::build<woort_BoxedValue, woort_Int>());

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_ok2));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Vec xmm_val = em->c->new_vec_d();
        WOORT_JIT_CODE(fmov(xmm_val, val));

        const Gp bits = em->c->new_gp64();
        WOORT_JIT_CODE(fmov(bits, xmm_val));

        const Gp exp_b = em->c->new_gp64();
        WOORT_JIT_CODE(lsr(exp_b, bits, 61));

        const Gp exp_t = em->c->new_gp64();
        WOORT_JIT_CODE(lsr(exp_t, exp_b, 1));
        WOORT_JIT_CODE(eor(exp_b, exp_b, exp_t));

        const Label L_ex = em->c->new_label();
        WOORT_JIT_CODE(tst(exp_b, Imm(1)));
        WOORT_JIT_CODE(b_eq(L_ex));

        {
            const Gp sign = em->c->new_gp64();
            const Gp sign_mask = em->c->new_gp64();
            WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(0x8000000000000000ULL))));
            WOORT_JIT_CODE(mov(sign, bits));
            WOORT_JIT_CODE(and_(sign, sign, sign_mask));

            const Gp low62 = em->c->new_gp64();
            const Gp low62_mask = em->c->new_gp64();
            WOORT_JIT_CODE(mov(low62_mask, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
            WOORT_JIT_CODE(mov(low62, bits));
            WOORT_JIT_CODE(and_(low62, low62, low62_mask));
            WOORT_JIT_CODE(lsl(low62, low62, 1));

            WOORT_JIT_CODE(mov(boxed, sign));
            WOORT_JIT_CODE(orr(boxed, boxed, low62));
            WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_REAL))));
        }

        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(b(L_done));

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_box_real_ex,
                FuncSignature::build<woort_BoxedValue, woort_Real>());

            invoke_node->set_arg(0, xmm_val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    default:
    {
        assert(false);
        break;
    }
    }

    WOORT_JIT_CODE(str(boxed, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ADDI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp result = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);

    WOORT_JIT_CODE(add(result, result, reg_b));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_SUBI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp result = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);

    WOORT_JIT_CODE(sub(result, result, reg_b));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_MULI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp result = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);

    WOORT_JIT_CODE(mul(result, result, reg_b));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_DIVI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);

    const Gp result = em->c->new_gp64();
    WOORT_JIT_CODE(sdiv(result, reg_a, reg_b));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_MODI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);

    /* arm64 无 idiv 余数形式；result = reg_a - (reg_a / reg_b) * reg_b */
    const Gp quotient = em->c->new_gp64();
    WOORT_JIT_CODE(sdiv(quotient, reg_a, reg_b));

    const Gp result = em->c->new_gp64();
    WOORT_JIT_CODE(msub(result, quotient, reg_b, reg_a));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_NEGI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_src = em->load_stack_gp(src);

    const Gp result = em->c->new_gp64();
    WOORT_JIT_CODE(neg(result, reg_src));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(cset(result, CondCode::kLT));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_GTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(cset(result, CondCode::kGT));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(cset(result, CondCode::kLE));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_GEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(cset(result, CondCode::kGE));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_EQI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(cset(result, CondCode::kEQ));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_NEI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(cset(result, CondCode::kNE));

    em->store_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ADDR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fadd(vec_a, vec_a, vec_b));
    em->store_stack(dst, vec_a);
}

void woort_JIT_Backend_arm64_SUBR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fsub(vec_a, vec_a, vec_b));
    em->store_stack(dst, vec_a);
}

void woort_JIT_Backend_arm64_MULR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fmul(vec_a, vec_a, vec_b));
    em->store_stack(dst, vec_a);
}

void woort_JIT_Backend_arm64_DIVR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fdiv(vec_a, vec_a, vec_b));
    em->store_stack(dst, vec_a);
}

void woort_JIT_Backend_arm64_MODR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));

    const Vec vec_ret = em->c->new_vec_d();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), WOORT_JIT_FMOD, FuncSignature::build<double, double, double>());

    invoke_node->set_arg(0, vec_a);
    invoke_node->set_arg(1, vec_b);
    invoke_node->set_ret(0, vec_ret);

    em->store_stack(dst, vec_ret);
}

void woort_JIT_Backend_arm64_NEGR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* NEGR: dst.m_real = -src.m_real。栈槽以 64 位原始位模式存放，
     * 取负等价于翻转最高符号位（与 0x8000000000000000 异或），无需经 FP 寄存器。
     * 这也正确处理 -0.0（翻转后得 +0.0，与 C 的 - 运算一致）。 */
    const Gp result = em->c->new_gp64();
    const Gp sign_mask = em->get_neg_sign_mask();

    WOORT_JIT_CODE(ldr(result, em->sb_slot(src)));
    WOORT_JIT_CODE(eor(result, result, sign_mask));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LTR: dst.m_integer = (a.m_real < b.m_real)。fcmp 设置 NZCV；有序时 kLT 即 a<b */
    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fcmp(vec_a, vec_b));
    WOORT_JIT_CODE(cset(result, CondCode::kMI));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_GTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* GTR: dst.m_integer = (a.m_real > b.m_real) */
    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fcmp(vec_a, vec_b));
    WOORT_JIT_CODE(cset(result, CondCode::kGT));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LER: dst.m_integer = (a.m_real <= b.m_real) */
    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fcmp(vec_a, vec_b));
    WOORT_JIT_CODE(cset(result, CondCode::kLS));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_GER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* GER: dst.m_integer = (a.m_real >= b.m_real)。fcmp 后 kGE 即 a>=b（有序） */
    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fcmp(vec_a, vec_b));
    WOORT_JIT_CODE(cset(result, CondCode::kGE));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_EQR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* EQR: dst.m_integer = (a.m_real == b.m_real) */
    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fcmp(vec_a, vec_b));
    WOORT_JIT_CODE(cset(result, CondCode::kEQ));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_NER(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* NER: dst.m_integer = (a.m_real != b.m_real)。Woolang 静态类型保证不出现 NaN，
     * 故 kNE 与 C 的 != 语义一致。 */
    const Vec vec_a = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(ldr(vec_a, em->sb_slot(a)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(b)));
    WOORT_JIT_CODE(fcmp(vec_a, vec_b));
    WOORT_JIT_CODE(cset(result, CondCode::kNE));

    em->store_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* ADDS: dst.m_string = woort_GCString_add_string(a.m_string, b.m_string) */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_add_string, FuncSignature::build<const woort_GCString*, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LTS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LTS: dst.m_integer = woort_GCString_compare(a, b) < 0 */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->c->new_gp64();

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_compare, FuncSignature::build<int, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, 0));
    WOORT_JIT_CODE(cset(result, CondCode::kLT));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_GTS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* GTS: dst.m_integer = woort_GCString_compare(a, b) > 0 */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->c->new_gp64();

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_compare, FuncSignature::build<int, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, 0));
    WOORT_JIT_CODE(cset(result, CondCode::kGT));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LES: dst.m_integer = woort_GCString_compare(a, b) <= 0 */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->c->new_gp64();

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_compare, FuncSignature::build<int, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, 0));
    WOORT_JIT_CODE(cset(result, CondCode::kLE));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_GES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* GES: dst.m_integer = woort_GCString_compare(a, b) >= 0 */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->c->new_gp64();

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_compare, FuncSignature::build<int, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, 0));
    WOORT_JIT_CODE(cset(result, CondCode::kGE));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_EQS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* EQS: dst.m_integer = (a == b) || woort_GCString_compare(a, b) == 0.
     * Pointer-equal strings are certainly equal — short-circuit before the
     * (expensive, full-content) compare call. Common for interned strings. */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    const Label L_ptr_eq = em->c->new_label();
    const Label L_done = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_eq(L_ptr_eq));
    {
        const Gp cmp_result = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_compare, FuncSignature::build<int, const woort_GCString*, const woort_GCString*>());
        invoke_node->set_arg(0, reg_a);
        invoke_node->set_arg(1, reg_b);
        invoke_node->set_ret(0, cmp_result);

        WOORT_JIT_CODE(cmp(cmp_result, 0));
        WOORT_JIT_CODE(cset(result, CondCode::kEQ));
    }
    WOORT_JIT_CODE(b(L_done));

    WOORT_JIT_CODE(bind(L_ptr_eq));
    WOORT_JIT_CODE(mov(result, 1));

    WOORT_JIT_CODE(bind(L_done));
    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_NES(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* NES: dst.m_integer = (a != b) && woort_GCString_compare(a, b) != 0.
     * Pointer-equal strings are certainly NOT unequal — short-circuit to 0
     * before the compare call. */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    const Label L_done = em->c->new_label();

    WOORT_JIT_CODE(mov(result, 0));
    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(b_eq(L_done));
    {
        const Gp cmp_result = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_compare, FuncSignature::build<int, const woort_GCString*, const woort_GCString*>());
        invoke_node->set_arg(0, reg_a);
        invoke_node->set_arg(1, reg_b);
        invoke_node->set_ret(0, cmp_result);

        WOORT_JIT_CODE(cmp(cmp_result, 0));
        WOORT_JIT_CODE(cset(result, CondCode::kNE));
    }
    WOORT_JIT_CODE(bind(L_done));
    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LAND(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LAND: dst.m_integer = (a.m_integer != 0) && (b.m_integer != 0)
     * 线性计算两个条件的非零布尔值后按位与，避免短路跳转。 */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    /* result = (reg_a != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_a, reg_a));
    WOORT_JIT_CODE(cset(result, CondCode::kNE));

    /* result &= (reg_b != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_b, reg_b));
    const Gp b_nz = em->c->new_gp64();
    WOORT_JIT_CODE(cset(b_nz, CondCode::kNE));
    WOORT_JIT_CODE(and_(result, result, b_nz));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LOR: dst.m_integer = (a.m_integer != 0) || (b.m_integer != 0)
     * 线性计算两个条件的非零布尔值后按位或，避免短路跳转。 */
    const Gp reg_a = em->load_stack_gp(a);
    const Gp reg_b = em->load_stack_gp(b);
    const Gp result = em->c->new_gp64();

    /* result = (reg_a != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_a, reg_a));
    WOORT_JIT_CODE(cset(result, CondCode::kNE));

    /* result |= (reg_b != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_b, reg_b));
    const Gp b_nz = em->c->new_gp64();
    WOORT_JIT_CODE(cset(b_nz, CondCode::kNE));
    WOORT_JIT_CODE(orr(result, result, b_nz));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LNOT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* LNOT: dst.m_integer = (src.m_integer == 0) ? 1 : 0
     * dst 为只写槽，tst + cset 取逻辑非。 */
    const Gp reg_src = em->load_stack_gp(src);
    const Gp result = em->c->new_gp64();

    WOORT_JIT_CODE(tst(reg_src, reg_src));
    WOORT_JIT_CODE(cset(result, CondCode::kEQ));

    em->store_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_CADDI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    WOORT_JIT_CODE(add(reg_dst, reg_dst, reg_src));
    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CSUBI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    WOORT_JIT_CODE(sub(reg_dst, reg_dst, reg_src));
    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CMULI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    WOORT_JIT_CODE(mul(reg_dst, reg_dst, reg_src));
    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CDIVI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp dividend = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    WOORT_JIT_CODE(sdiv(dividend, dividend, reg_src));

    em->store_stack(dst, dividend);
}

void woort_JIT_Backend_arm64_CADDR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CADDR: [SB + dst] += [SB + src]，dst 为读写槽 */
    const Vec vec = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec, em->sb_slot(dst)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(src)));
    WOORT_JIT_CODE(fadd(vec, vec, vec_b));
    em->store_stack(dst, vec);
}

void woort_JIT_Backend_arm64_CSUBR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CSUBR: [SB + dst] -= [SB + src]，dst 为读写槽 */
    const Vec vec = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec, em->sb_slot(dst)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(src)));
    WOORT_JIT_CODE(fsub(vec, vec, vec_b));
    em->store_stack(dst, vec);
}

void woort_JIT_Backend_arm64_CMULR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CMULR: [SB + dst] *= [SB + src]，dst 为读写槽 */
    const Vec vec = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec, em->sb_slot(dst)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(src)));
    WOORT_JIT_CODE(fmul(vec, vec, vec_b));
    em->store_stack(dst, vec);
}

void woort_JIT_Backend_arm64_CDIVR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CDIVR: [SB + dst] /= [SB + src]，dst 为读写槽 */
    const Vec vec = em->c->new_vec_d();
    const Vec vec_b = em->c->new_vec_d();

    WOORT_JIT_CODE(ldr(vec, em->sb_slot(dst)));
    WOORT_JIT_CODE(ldr(vec_b, em->sb_slot(src)));
    WOORT_JIT_CODE(fdiv(vec, vec, vec_b));
    em->store_stack(dst, vec);
}

void woort_JIT_Backend_arm64_CADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CADDS: [dst].m_string = woort_GCString_add_string([dst].m_string, [src].m_string) */
    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_add_string, FuncSignature::build<const woort_GCString*, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_dst);
    invoke_node->set_arg(1, reg_src);
    invoke_node->set_ret(0, reg_dst);

    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CVADDS(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CVADDS: [dst].m_string = woort_GCString_add_string([src].m_string, [dst].m_string)
     * 注意与 CADDS 的操作数顺序相反（src 在前） */
    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCString_add_string, FuncSignature::build<const woort_GCString*, const woort_GCString*, const woort_GCString*>());

    invoke_node->set_arg(0, reg_src);
    invoke_node->set_arg(1, reg_dst);
    invoke_node->set_ret(0, reg_dst);

    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CMODI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp dividend = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    /* result = dividend - (dividend / reg_src) * reg_src */
    const Gp quotient = em->c->new_gp64();
    WOORT_JIT_CODE(sdiv(quotient, dividend, reg_src));

    const Gp result = em->c->new_gp64();
    WOORT_JIT_CODE(msub(result, quotient, reg_src, dividend));

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_CMODR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CMODR: [SB + dst] = fmod([SB + dst], [SB + src])，dst 为读写槽 */
    const Vec vec_dst = em->c->new_vec_d();
    const Vec vec_src = em->c->new_vec_d();
    WOORT_JIT_CODE(ldr(vec_dst, em->sb_slot(dst)));
    WOORT_JIT_CODE(ldr(vec_src, em->sb_slot(src)));

    const Vec vec_ret = em->c->new_vec_d();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), WOORT_JIT_FMOD, FuncSignature::build<double, double, double>());

    invoke_node->set_arg(0, vec_dst);
    invoke_node->set_arg(1, vec_src);
    invoke_node->set_ret(0, vec_ret);

    em->store_stack(dst, vec_ret);
}

void woort_JIT_Backend_arm64_CLAND(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CLAND: dst.m_integer = (dst.m_integer != 0) && (src.m_integer != 0)
     * dst 为读写槽，线性计算两个条件的非零布尔值后按位与，避免短路跳转。 */
    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    /* reg_dst = (reg_dst != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_dst, reg_dst));
    WOORT_JIT_CODE(cset(reg_dst, CondCode::kNE));

    /* reg_dst &= (reg_src != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_src, reg_src));
    const Gp src_nz = em->c->new_gp64();
    WOORT_JIT_CODE(cset(src_nz, CondCode::kNE));
    WOORT_JIT_CODE(and_(reg_dst, reg_dst, src_nz));

    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CLOR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CLOR: dst.m_integer = (dst.m_integer != 0) || (src.m_integer != 0)
     * dst 为读写槽，线性计算两个条件的非零布尔值后按位或，避免短路跳转。 */
    const Gp reg_dst = em->load_stack_gp(dst);
    const Gp reg_src = em->load_stack_gp(src);

    /* reg_dst = (reg_dst != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_dst, reg_dst));
    WOORT_JIT_CODE(cset(reg_dst, CondCode::kNE));

    /* reg_dst |= (reg_src != 0) ? 1 : 0 */
    WOORT_JIT_CODE(tst(reg_src, reg_src));
    const Gp src_nz = em->c->new_gp64();
    WOORT_JIT_CODE(cset(src_nz, CondCode::kNE));
    WOORT_JIT_CODE(orr(reg_dst, reg_dst, src_nz));

    em->store_stack(dst, reg_dst);
}

void woort_JIT_Backend_arm64_CLNOT(void* emitter, woort_Opcode_Stack dst)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    /* CLNOT: dst.m_integer = (dst.m_integer == 0) ? 1 : 0
     * dst 为读写槽，tst + cset 取逻辑非。 */
    const Gp reg_dst = em->load_stack_gp(dst);

    WOORT_JIT_CODE(tst(reg_dst, reg_dst));
    WOORT_JIT_CODE(cset(reg_dst, CondCode::kEQ));

    em->store_stack(dst, reg_dst);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_MKPVALUE(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp src_val = em->load_stack_gp(src);

    const Gp result = em->c->new_gp64();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_make_pvalue,
        FuncSignature::build<woort_Value*, uint64_t>());

    invoke_node->set_arg(0, src_val);
    invoke_node->set_ret(0, result);

    em->store_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_LDIDVEC(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->load_stack_gp(vec);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, vec_len));
    WOORT_JIT_CODE(b_lo(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(datas, idx_val, lsl(3))));

    /* A3: inline the DynBox tag-decode instead of invoking
     * woort_JIT_unbox_dyn_no_check (avoids caller-save spill/reload). */
    const Gp result = em->c->new_gp64();
    em->emit_unbox_dyn_no_check(result, elem);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LDIDVECX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->load_stack_gp(vec);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, vec_len));
    WOORT_JIT_CODE(b_lo(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(datas, idx_val, lsl(3))));

    em->store_stack(dst, elem);
}

void woort_JIT_Backend_arm64_LDIDSTRUCT(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp obj_ptr = em->load_stack_gp(obj);

    const int32_t disp =
        WOORT_GCSTRUCT_OFFSETOF_DATAS +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(disp))));
        WOORT_JIT_CODE(add(addr, obj_ptr, off));
    }

    const Gp field = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(field, ptr(addr)));

    em->store_stack(dst, field);
}

void woort_JIT_Backend_arm64_LDIDSTRING(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp str_ptr = em->load_stack_gp(str);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp out_addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(static_cast<int64_t>(dst) * static_cast<int64_t>(sizeof(woort_Value))))));
        WOORT_JIT_CODE(add(out_addr, em->m_sb, off));
    }

    const Gp ok = em->c->new_gp32();
    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_ldidstring,
        FuncSignature::build<bool, const woort_GCString*, woort_Int, woort_Value*>());

    invoke_node->set_arg(0, str_ptr);
    invoke_node->set_arg(1, idx_val);
    invoke_node->set_arg(2, out_addr);
    invoke_node->set_ret(0, ok);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(ok, ok));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_arm64_LDIDDICTI(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_int,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    const Gp result = em->c->new_gp64();
    InvokeNode* unbox_node;
    WOORT_JIT_INVOKE_ADDR(Out(unbox_node), woort_JIT_unbox_dyn_no_check,
        FuncSignature::build<uint64_t, woort_BoxedValue>());

    unbox_node->set_arg(0, elem);
    unbox_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LDIDDICTR(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_real,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    const Gp result = em->c->new_gp64();
    InvokeNode* unbox_node;
    WOORT_JIT_INVOKE_ADDR(Out(unbox_node), woort_JIT_unbox_dyn_no_check,
        FuncSignature::build<uint64_t, woort_BoxedValue>());

    unbox_node->set_arg(0, elem);
    unbox_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LDIDDICTB(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_bool,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    const Gp result = em->c->new_gp64();
    InvokeNode* unbox_node;
    WOORT_JIT_INVOKE_ADDR(Out(unbox_node), woort_JIT_unbox_dyn_no_check,
        FuncSignature::build<uint64_t, woort_BoxedValue>());

    unbox_node->set_arg(0, elem);
    unbox_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LDIDDICTX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_dyn,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    const Gp result = em->c->new_gp64();
    InvokeNode* unbox_node;
    WOORT_JIT_INVOKE_ADDR(Out(unbox_node), woort_JIT_unbox_dyn_no_check,
        FuncSignature::build<uint64_t, woort_BoxedValue>());

    unbox_node->set_arg(0, elem);
    unbox_node->set_ret(0, result);

    em->store_stack(dst, result);
}

void woort_JIT_Backend_arm64_LDIDDICTIX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_int,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    em->store_stack(dst, elem);
}

void woort_JIT_Backend_arm64_LDIDDICTRX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_real,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    em->store_stack(dst, elem);
}

void woort_JIT_Backend_arm64_LDIDDICTBX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_bool,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    em->store_stack(dst, elem);
}

void woort_JIT_Backend_arm64_LDIDDICTXX(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), woort_JIT_map_get_dyn,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(elem, ptr(val_ptr)));

    em->store_stack(dst, elem);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_STIDVECI(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->load_stack_gp(vec);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, vec_len));
    WOORT_JIT_CODE(b_lo(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(add(dst_addr, datas, idx_val, lsl(3)));

    const Gp src_val = em->load_stack_gp(src);

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_store_dynbox_int,
        FuncSignature::build<void, woort_DynBox*, woort_Int>());

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_arm64_STIDVECR(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->load_stack_gp(vec);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, vec_len));
    WOORT_JIT_CODE(b_lo(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(add(dst_addr, datas, idx_val, lsl(3)));

    const Gp src_val = em->load_stack_gp(src);

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_store_dynbox_real,
        FuncSignature::build<void, woort_DynBox*, woort_BoxedValue>());

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_arm64_STIDVECB(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->load_stack_gp(vec);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, vec_len));
    WOORT_JIT_CODE(b_lo(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(add(dst_addr, datas, idx_val, lsl(3)));

    const Gp src_val = em->load_stack_gp(src);

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_store_dynbox_bool,
        FuncSignature::build<void, woort_DynBox*, woort_Int>());

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_arm64_STIDVECX(void* emitter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->load_stack_gp(vec);
    const Gp idx_val = em->load_stack_gp(idx);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, vec_len));
    WOORT_JIT_CODE(b_lo(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas, ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(add(dst_addr, datas, idx_val, lsl(3)));

    const Gp src_val = em->load_stack_gp(src);

    InvokeNode* invoke_node;
    WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_store_dynbox_dyn,
        FuncSignature::build<void, woort_DynBox*, woort_BoxedValue>());

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

template <auto LookupFn, typename KeyT, auto StoreFn, typename ValT>
static void woort_JIT_stid_dict_impl(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(key);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), LookupFn,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, KeyT>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val_ptr, val_ptr));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp src_val = em->load_stack_gp(src);

    InvokeNode* store_node;
    WOORT_JIT_INVOKE_ADDR(Out(store_node), StoreFn,
        FuncSignature::build<void, woort_DynBox*, ValT>());

    store_node->set_arg(0, val_ptr);
    store_node->set_arg(1, src_val);
}

template <auto LookupFn, typename KeyT, auto StoreFn, typename ValT>
static void woort_JIT_stid_map_impl(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->load_stack_gp(map);
    const Gp key_val = em->load_stack_gp(key);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_INVOKE_ADDR(Out(lookup_node), LookupFn,
        FuncSignature::build<woort_DynBox*, woort_GCMap*, KeyT>());

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Gp src_val = em->load_stack_gp(src);

    InvokeNode* store_node;
    WOORT_JIT_INVOKE_ADDR(Out(store_node), StoreFn,
        FuncSignature::build<void, woort_DynBox*, ValT>());

    store_node->set_arg(0, val_ptr);
    store_node->set_arg(1, src_val);
}

void woort_JIT_Backend_arm64_STIDDICTII(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTIR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTIB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTIX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTRI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTRR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTRB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTRX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTBI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTBR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTBB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTBX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTXI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTXR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTXB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDDICTXX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPII(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPIR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPIB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPIX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPRI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPRR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPRB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPRX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPBI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPBR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPBB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPBX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPXI(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPXR(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPXB(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emitter, map, key, src);
}

void woort_JIT_Backend_arm64_STIDMAPXX(void* emitter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emitter, map, key, src);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_STIDSTRUCT(void* emitter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp obj_ptr = em->load_stack_gp(obj);

    const int32_t disp =
        WOORT_GCSTRUCT_OFFSETOF_DATAS +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp dst_addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(disp))));
        WOORT_JIT_CODE(add(dst_addr, obj_ptr, off));
    }

    const Gp src_val = em->load_stack_gp(src);

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->get_gc_flag_ptr();
    const Gp flag_val = em->c->new_gp32();
    WOORT_JIT_CODE(ldrb(flag_val, ptr(flag_ptr)));
    WOORT_JIT_CODE(cbz(flag_val, L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_GC_mixed_write_barrier_value,
            FuncSignature::build<void, woort_Value*, uint64_t>());

        invoke_node->set_arg(0, dst_addr);
        invoke_node->set_arg(1, src_val);
    }
    WOORT_JIT_CODE(b(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(str(src_val, ptr(dst_addr)));

    WOORT_JIT_CODE(bind(L_end));
}

void woort_JIT_Backend_arm64_UNPACKVEC(void* emitter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");
    static_assert(sizeof(woort_DynBox) == 8, "");

    const Gp gcvec_ptr = em->load_stack_gp(vec);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(vec_len, Imm(static_cast<int32_t>(n))));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    if (n != 0)
    {
        const Label L_retry = em->c->new_label();
        WOORT_JIT_CODE(bind(L_retry));

        const Gp new_sp = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(new_sp, em->m_sp));
        {
            const Gp size_bytes = em->c->new_gp64();
            WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
            WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));
        }

        const Label L_sok = em->c->new_label();
        WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
        WOORT_JIT_CODE(b_hs(L_sok));

        em->emit_extern_stack(*em->m_ip, L_retry);

        WOORT_JIT_CODE(bind(L_sok));
        WOORT_JIT_CODE(mov(em->m_sp, new_sp));

        const Gp datas_ptr = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(datas_ptr, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

        for (woort_Opcode_Count i = 0; i < n; ++i)
        {
            const Gp dynbox_val = em->c->new_gp64();
            WOORT_JIT_CODE(ldr(dynbox_val, ptr(datas_ptr, static_cast<int32_t>(i * static_cast<int32_t>(sizeof(woort_DynBox))))));

            /* A3: inline the DynBox tag-decode instead of invoking
             * woort_JIT_unbox_dyn_no_check per element. */
            const Gp result = em->c->new_gp64();
            em->emit_unbox_dyn_no_check(result, dynbox_val);

            WOORT_JIT_CODE(str(result, ptr(em->m_sp, static_cast<int32_t>((i + 1) * static_cast<int32_t>(sizeof(woort_Value))))));
        }
    }
}

void woort_JIT_Backend_arm64_UNPACKVECX(void* emitter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");
    static_assert(sizeof(woort_DynBox) == 8, "");

    const Gp gcvec_ptr = em->load_stack_gp(vec);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(vec_len, Imm(static_cast<int32_t>(n))));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    if (n != 0)
    {
        const Label L_retry = em->c->new_label();
        WOORT_JIT_CODE(bind(L_retry));

        const Gp new_sp = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(new_sp, em->m_sp));
        {
            const Gp size_bytes = em->c->new_gp64();
            WOORT_JIT_CODE(mov(size_bytes, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
            WOORT_JIT_CODE(sub(new_sp, new_sp, size_bytes));
        }

        const Label L_sok = em->c->new_label();
        WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
        WOORT_JIT_CODE(b_hs(L_sok));

        em->emit_extern_stack(*em->m_ip, L_retry);

        WOORT_JIT_CODE(bind(L_sok));
        WOORT_JIT_CODE(mov(em->m_sp, new_sp));

        const Gp datas_ptr = em->c->new_gp_ptr();
        WOORT_JIT_CODE(ldr(datas_ptr, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

        for (woort_Opcode_Count i = 0; i < n; ++i)
        {
            const Gp val = em->c->new_gp64();
            WOORT_JIT_CODE(ldr(val, ptr(datas_ptr, static_cast<int32_t>(i * static_cast<int32_t>(sizeof(woort_DynBox))))));
            WOORT_JIT_CODE(str(val, ptr(em->m_sp, static_cast<int32_t>((i + 1) * static_cast<int32_t>(sizeof(woort_Value))))));
        }
    }
}

void woort_JIT_Backend_arm64_UNPACKVECALL(void* emitter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");
    static_assert(sizeof(woort_DynBox) == 8, "");

    const Gp gcvec_ptr = em->load_stack_gp(vec);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(vec_len, Imm(static_cast<int32_t>(n))));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp byte_count = em->c->new_gp64();
    WOORT_JIT_CODE(mov(byte_count, vec_len));
    WOORT_JIT_CODE(lsl(byte_count, byte_count, 3));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, new_sp, byte_count));

    const Label L_sok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(b_hs(L_sok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_sok));
    WOORT_JIT_CODE(mov(em->m_sp, new_sp));

    const Gp datas_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas_ptr, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    for (woort_Opcode_Count i = 0; i < n; ++i)
    {
        const Gp dynbox_val = em->c->new_gp64();
        WOORT_JIT_CODE(ldr(dynbox_val, ptr(datas_ptr, static_cast<int32_t>(i * static_cast<int32_t>(sizeof(woort_DynBox))))));

        const Gp result = em->c->new_gp64();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_unbox_dyn_no_check,
            FuncSignature::build<uint64_t, woort_BoxedValue>());

        invoke_node->set_arg(0, dynbox_val);
        invoke_node->set_ret(0, result);

        WOORT_JIT_CODE(str(result, ptr(em->m_sp, static_cast<int32_t>((i + 1) * static_cast<int32_t>(sizeof(woort_Value))))));
    }

    const Gp remaining = em->c->new_gp64();
    WOORT_JIT_CODE(mov(remaining, vec_len));
    {
        const Gp n_reg = em->c->new_gp64();
        WOORT_JIT_CODE(mov(n_reg, Imm(static_cast<int64_t>(n))));
        WOORT_JIT_CODE(sub(remaining, remaining, n_reg));
    }

    const Gp rem_bytes = em->c->new_gp64();
    WOORT_JIT_CODE(mov(rem_bytes, remaining));
    WOORT_JIT_CODE(lsl(rem_bytes, rem_bytes, 3));

    const Gp rem_src = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(static_cast<size_t>(n) * sizeof(woort_DynBox)))));
        WOORT_JIT_CODE(add(rem_src, datas_ptr, off));
    }

    const Gp rem_dst = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(static_cast<size_t>(n + 1) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(rem_dst, em->m_sp, off));
    }

    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), WOORT_JIT_MEMCPY,
            FuncSignature::build<void, void*, const void*, size_t>());

        invoke_node->set_arg(0, rem_dst);
        invoke_node->set_arg(1, rem_src);
        invoke_node->set_arg(2, rem_bytes);
    }

    em->store_stack(count_dst, vec_len);
}

void woort_JIT_Backend_arm64_UNPACKVECXALL(void* emitter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");
    static_assert(sizeof(woort_DynBox) == 8, "");

    const Gp gcvec_ptr = em->load_stack_gp(vec);

    const Gp vec_len = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(vec_len, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(vec_len, Imm(static_cast<int32_t>(n))));
    WOORT_JIT_CODE(b_hs(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp byte_count = em->c->new_gp64();
    WOORT_JIT_CODE(mov(byte_count, vec_len));
    WOORT_JIT_CODE(lsl(byte_count, byte_count, 3));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, new_sp, byte_count));

    const Label L_sok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(b_hs(L_sok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_sok));
    WOORT_JIT_CODE(mov(em->m_sp, new_sp));

    const Gp datas_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas_ptr, ptr(gcvec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_ptr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(dst_ptr, em->m_sp, off));
    }

    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), WOORT_JIT_MEMCPY,
            FuncSignature::build<void, void*, const void*, size_t>());

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, datas_ptr);
        invoke_node->set_arg(2, byte_count);
    }

    em->store_stack(count_dst, vec_len);
}

void woort_JIT_Backend_arm64_PUSHIDSTRUCT(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(b_hi(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->load_stack_gp(obj);

    const int32_t disp =
        WOORT_GCSTRUCT_OFFSETOF_DATAS +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(disp))));
        WOORT_JIT_CODE(add(addr, obj_ptr, off));
    }

    const Gp field = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(field, ptr(addr)));

    WOORT_JIT_CODE(str(field, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_PUSHIDSTBOXI(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(b_hi(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->load_stack_gp(obj);

    const int32_t disp =
        WOORT_GCSTRUCT_OFFSETOF_DATAS +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(disp))));
        WOORT_JIT_CODE(add(addr, obj_ptr, off));
    }

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(val, ptr(addr)));

    const Gp boxed = em->c->new_gp64();
    WOORT_JIT_CODE(lsl(boxed, val, 2));
    WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));

    const Gp check = em->c->new_gp64();
    WOORT_JIT_CODE(asr(check, boxed, 2));

    const Label L_fit = em->c->new_label();
    WOORT_JIT_CODE(cmp(check, val));
    WOORT_JIT_CODE(b_eq(L_fit));

    {
        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_box_int_ex,
            FuncSignature::build<woort_BoxedValue, woort_Int>());

        invoke_node->set_arg(0, val);
        invoke_node->set_ret(0, result);

        WOORT_JIT_CODE(mov(boxed, result));
    }

    WOORT_JIT_CODE(bind(L_fit));

    WOORT_JIT_CODE(str(boxed, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_PUSHIDSTBOXR(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(b_hi(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->load_stack_gp(obj);

    const int32_t disp =
        WOORT_GCSTRUCT_OFFSETOF_DATAS +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(disp))));
        WOORT_JIT_CODE(add(addr, obj_ptr, off));
    }

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(val, ptr(addr)));

    const Vec xmm_val = em->c->new_vec_d();
    WOORT_JIT_CODE(fmov(xmm_val, val));

    const Gp bits = em->c->new_gp64();
    WOORT_JIT_CODE(fmov(bits, xmm_val));

    const Gp exp_b = em->c->new_gp64();
    WOORT_JIT_CODE(lsr(exp_b, bits, 61));

    const Gp exp_t = em->c->new_gp64();
    WOORT_JIT_CODE(lsr(exp_t, exp_b, 1));
    WOORT_JIT_CODE(eor(exp_b, exp_b, exp_t));

    const Gp boxed = em->c->new_gp64();

    const Label L_ex = em->c->new_label();
    WOORT_JIT_CODE(tst(exp_b, Imm(1)));
    WOORT_JIT_CODE(b_eq(L_ex));

    {
        const Gp sign = em->c->new_gp64();
        const Gp sign_mask = em->c->new_gp64();
        WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(0x8000000000000000ULL))));
        WOORT_JIT_CODE(mov(sign, bits));
        WOORT_JIT_CODE(and_(sign, sign, sign_mask));

        const Gp low62 = em->c->new_gp64();
        const Gp low62_mask = em->c->new_gp64();
        WOORT_JIT_CODE(mov(low62_mask, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
        WOORT_JIT_CODE(mov(low62, bits));
        WOORT_JIT_CODE(and_(low62, low62, low62_mask));
        WOORT_JIT_CODE(lsl(low62, low62, 1));

        WOORT_JIT_CODE(mov(boxed, sign));
        WOORT_JIT_CODE(orr(boxed, boxed, low62));
        WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_REAL))));
    }

    const Label L_done = em->c->new_label();
    WOORT_JIT_CODE(b(L_done));

    WOORT_JIT_CODE(bind(L_ex));
    {
        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_JIT_box_real_ex,
            FuncSignature::build<woort_BoxedValue, woort_Real>());

        invoke_node->set_arg(0, xmm_val);
        invoke_node->set_ret(0, result);

        WOORT_JIT_CODE(mov(boxed, result));
    }

    WOORT_JIT_CODE(bind(L_done));

    WOORT_JIT_CODE(str(boxed, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_PUSHIDSTBOXB(void* emitter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(b_hi(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->load_stack_gp(obj);

    const int32_t disp =
        WOORT_GCSTRUCT_OFFSETOF_DATAS +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp addr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(disp))));
        WOORT_JIT_CODE(add(addr, obj_ptr, off));
    }

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(ldr(val, ptr(addr)));

    const Gp boxed = em->c->new_gp64();
    WOORT_JIT_CODE(lsl(boxed, val, 3));
    WOORT_JIT_CODE(orr(boxed, boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));

    WOORT_JIT_CODE(str(boxed, ptr(em->m_sp)));
    WOORT_JIT_CODE(sub(em->m_sp, em->m_sp, static_cast<int32_t>(sizeof(woort_Value))));
}

void woort_JIT_Backend_arm64_PACKARG(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Count skip)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    static_assert(sizeof(woort_Value) == 8, "");
    static_assert(sizeof(woort_DynBox) == 8, "");

    const Gp sb_3_val = em->load_stack_gp(3);
    const Gp pack_argc = em->c->new_gp64();
    WOORT_JIT_CODE(mov(pack_argc, sb_3_val));
    WOORT_JIT_CODE(sub(pack_argc, pack_argc, Imm(static_cast<int32_t>(skip))));

    const Gp gcvec = em->c->new_gp_ptr();
    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), woort_GCVec_new,
            FuncSignature::build<woort_GCVec*>());
        invoke_node->set_ret(0, gcvec);
    }

    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), _woort_GCVec_extern,
            FuncSignature::build<void, woort_GCVec*, size_t>());
        invoke_node->set_arg(0, gcvec);
        invoke_node->set_arg(1, pack_argc);
    }

    const Gp datas_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(ldr(datas_ptr, ptr(gcvec, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp src_ptr = em->c->new_gp_ptr();
    {
        const Gp off = em->c->new_gp64();
        WOORT_JIT_CODE(mov(off, Imm(static_cast<int64_t>(static_cast<size_t>(4 + skip) * sizeof(woort_Value)))));
        WOORT_JIT_CODE(add(src_ptr, em->m_sb, off));
    }

    const Gp byte_count = em->c->new_gp64();
    WOORT_JIT_CODE(mov(byte_count, pack_argc));
    WOORT_JIT_CODE(lsl(byte_count, byte_count, 3));

    {
        InvokeNode* invoke_node;
        WOORT_JIT_INVOKE_ADDR(Out(invoke_node), WOORT_JIT_MEMCPY,
            FuncSignature::build<void, void*, const void*, size_t>());
        invoke_node->set_arg(0, datas_ptr);
        invoke_node->set_arg(1, src_ptr);
        invoke_node->set_arg(2, byte_count);
    }

    em->store_stack(dst, gcvec);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_arm64_ASTORE(void* emitter, woort_Opcode_Global storage, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Gp storage_ptr = em->static_slot_ptr(storage);
    WOORT_JIT_CODE(stlr(val, ptr(storage_ptr)));
}

void woort_JIT_Backend_arm64_ALOAD(void* emitter, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp storage_ptr = em->static_slot_ptr(storage);

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(ldar(val, ptr(storage_ptr)));
    em->store_stack(dst, val);
}

void woort_JIT_Backend_arm64_CAS(void* emitter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp expected_val = em->load_stack_gp(expected);
    const Gp desired_val = em->load_stack_gp(desired);

    const Gp storage_ptr = em->static_slot_ptr(storage);

    const Gp acc = em->c->new_gp64();
    WOORT_JIT_CODE(mov(acc, expected_val));
    WOORT_JIT_CODE(casal(acc, desired_val, ptr(storage_ptr)));
    em->store_stack(expected, acc);
}

void woort_JIT_Backend_arm64_JIFINITED(void* emitter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Label target_lbl = em->get_label(em->m_cenv_codes + target);

    const Gp storage_ptr = em->static_slot_ptr(flag);

    const Gp flag_stat = em->c->new_gp64();
    WOORT_JIT_CODE(ldar(flag_stat, ptr(storage_ptr)));

    WOORT_JIT_CODE(cmp(flag_stat, Imm(2)));
    WOORT_JIT_CODE(b_eq(target_lbl));

    WOORT_JIT_CODE(tst(flag_stat, flag_stat));
    const Label L_spin = em->c->new_label();
    WOORT_JIT_CODE(b_ne(L_spin));

    {
        const Gp expected = em->c->new_gp64();
        WOORT_JIT_CODE(mov(expected, Imm(0)));
        const Gp desired = em->c->new_gp64();
        WOORT_JIT_CODE(mov(desired, Imm(1)));
        WOORT_JIT_CODE(casal(expected, desired, ptr(storage_ptr)));

        const Label L_init = em->c->new_label();
        WOORT_JIT_CODE(cmp(expected, Imm(0)));
        WOORT_JIT_CODE(b_eq(L_init));

        WOORT_JIT_CODE(bind(L_spin));
        em->emit_checkpoint(*em->m_ip);
        WOORT_JIT_CODE(ldar(flag_stat, ptr(storage_ptr)));
        WOORT_JIT_CODE(cmp(flag_stat, Imm(2)));
        WOORT_JIT_CODE(b_ne(L_spin));

        WOORT_JIT_CODE(b(target_lbl));

        WOORT_JIT_CODE(bind(L_init));
    }
}

void woort_JIT_Backend_arm64_DEBUGTRAP(void* emitter)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_arm64_PANICS(void* emitter, woort_Opcode_Stack src)
{
    (void)src;

    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_arm64_PANICC(void* emitter, woort_Opcode_Global src)
{
    (void)src;

    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_arm64_CHKDIVIL(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Gp min_val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(min_val, Imm(static_cast<int64_t>(INT64_MIN))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(val, min_val));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_arm64_CHKDIVIR(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Label L_fail = em->c->new_label();
    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val, val));
    WOORT_JIT_CODE(b_eq(L_fail));

    {
        const Gp m1 = em->c->new_gp64();
        WOORT_JIT_CODE(mov(m1, Imm(static_cast<int64_t>(-1))));
        WOORT_JIT_CODE(cmp(val, m1));
    }
    WOORT_JIT_CODE(b_ne(L_ok));

    WOORT_JIT_CODE(bind(L_fail));
    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_arm64_CHKDIVIRZ(void* emitter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp val = em->load_stack_gp(src);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(val, val));
    WOORT_JIT_CODE(b_ne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_arm64_CHKDIVILR(void* emitter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{
    woort_JIT_Asmjit_arm64_emitter* const em = static_cast<woort_JIT_Asmjit_arm64_emitter*>(emitter);

    const Gp divisor_val = em->load_stack_gp(divisor);
    const Gp dividend_val = em->load_stack_gp(dividend);

    const Label L_fail = em->c->new_label();
    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(tst(dividend_val, dividend_val));
    WOORT_JIT_CODE(b_eq(L_fail));

    {
        const Gp m1 = em->c->new_gp64();
        WOORT_JIT_CODE(mov(m1, Imm(static_cast<int64_t>(-1))));
        WOORT_JIT_CODE(cmp(dividend_val, m1));
    }
    WOORT_JIT_CODE(b_ne(L_ok));

    {
        const Gp min_val = em->c->new_gp64();
        WOORT_JIT_CODE(mov(min_val, Imm(static_cast<int64_t>(INT64_MIN))));
        WOORT_JIT_CODE(cmp(divisor_val, min_val));
    }
    WOORT_JIT_CODE(b_ne(L_ok));

    WOORT_JIT_CODE(bind(L_fail));
    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

#endif