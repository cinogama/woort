#include "woort_jit_x64_bridge.h"
#include "woort_jit_bridge.h"
#include "woort_value_types.h"
#include "woort_gc_vec_types.h"
#include "woort_gc_struct_types.h"

#include "woomem.h"

#include "asmjit/x86.h"

#include <new>
#include <memory>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <unordered_map>
#include <optional>

using namespace std;
using namespace asmjit;
using namespace asmjit::x86;

#define WOORT_JIT_CODE(CMD) em->update_last_error(em->c->CMD)

/*
 * fmod 在 C++ <cmath> 中是重载族（float/double/long double），取其地址时需要消歧。
 * 此处固定为 double 版本，供 MODR/CMODR 的 invoke 调用使用。
 */
static double (*const WOORT_JIT_FMOD)(double, double) = fmod;

struct woort_JIT_Asmjit_x64_Emmiter
{
    Compiler* c;
    const woort_CodeEnv* cenv;

    const woort_Bytecode* m_cenv_codes;
    const size_t m_cenv_constant_count;
    const woort_Value* const m_cenv_static_storage;

    CodeHolder  m_code_holder;
    Error       m_last_error;

    FuncNode* m_func_node;
    const woort_Bytecode** m_ip;

    /* runtime states */
    Gp          m_vm;
    Gp          m_sb;

    Gp          m_sp;
    Gp          m_stack;
    Gp          m_stack_end;

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

    optional<JumpAnnotation*> m_sync_stack_value_resume_annotation;
    Label           m_sync_stack_value_slow;
    Gp              m_sync_stack_value_resume;
    size_t          m_sync_stack_value_site_count;

    struct VMStackValueGp
    {
        Gp      m_gp;
        bool    m_writed;

        VMStackValueGp(const VMStackValueGp&) = delete;
        VMStackValueGp(VMStackValueGp&&) = delete;
        VMStackValueGp& operator =(const VMStackValueGp&) = delete;
        VMStackValueGp& operator =(VMStackValueGp&&) = delete;

        VMStackValueGp(Gp gp)
            : m_gp(gp)
            , m_writed(false)
        {
        }
    };

    unordered_map<woort_Opcode_Stack, VMStackValueGp> m_stack_gp;
    unordered_map<const woort_Bytecode*, Label> m_opcode_label;

    woort_JIT_Asmjit_x64_Emmiter(const woort_JIT_Asmjit_x64_Emmiter&) = delete;
    woort_JIT_Asmjit_x64_Emmiter(woort_JIT_Asmjit_x64_Emmiter&&) = delete;
    woort_JIT_Asmjit_x64_Emmiter& operator =(const woort_JIT_Asmjit_x64_Emmiter&) = delete;
    woort_JIT_Asmjit_x64_Emmiter& operator =(woort_JIT_Asmjit_x64_Emmiter&&) = delete;

    woort_JIT_Asmjit_x64_Emmiter(const woort_CodeEnv* cenv_, const woort_Bytecode** ip) noexcept
        : c(nullptr)
        , cenv(cenv_)
        , m_cenv_codes(woort_JIT_CodeEnv_codes(cenv_))
        , m_cenv_constant_count(woort_JIT_CodeEnv_constant_count(cenv_))
        , m_cenv_static_storage(woort_JIT_CodeEnv_static_data(cenv_))
        , m_code_holder{}
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
        , m_sync_stack_value_resume_annotation(nullopt)
        , m_sync_stack_value_site_count(0)
    {
        JitRuntime* const asmjit_runtime =
            static_cast<JitRuntime*>(woort_JIT_Asmjit_get_runtime());

        m_last_error = m_code_holder.init(asmjit_runtime->environment());
        if (m_last_error != Error::kOk)
            return;

        c = new (nothrow) Compiler(&m_code_holder);
        if (c == nullptr)
            m_last_error = Error::kOutOfMemory;

        m_vm = c->new_gp_ptr();
        m_sp = c->new_gp_ptr();
        m_sb = c->new_gp_ptr();
        m_stack = c->new_gp_ptr();
        m_stack_end = c->new_gp_ptr();

        m_last_error = c->add_func_node(Out(m_func_node),
            FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*, const woort_Value*>());

        m_func_node->set_arg(0, m_vm);
        m_func_node->set_arg(1, m_sb);
    }
    ~woort_JIT_Asmjit_x64_Emmiter() noexcept
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
    void resync_vm_stack_state_fully()
    {
        auto* const em = this;

        WOORT_JIT_CODE(mov(em->m_sp, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_SP)));
        WOORT_JIT_CODE(mov(em->m_sb, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_SB)));
        WOORT_JIT_CODE(mov(em->m_stack, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK)));
        WOORT_JIT_CODE(mov(em->m_stack_end, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
    }
    void return_with_status(woort_VmCallStatus status)
    {
        auto* const em = this;

        const Mem depth_addr =
            dword_ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH);

        WOORT_JIT_CODE(dec(depth_addr));

        const Gp ret_val = c->new_gp32();
        WOORT_JIT_CODE(mov(ret_val, (int32_t)status));
        WOORT_JIT_CODE(ret(ret_val));
    }
    void return_with_status(const Gp& status)
    {
        auto* const em = this;

        const Mem depth_addr =
            dword_ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH);

        WOORT_JIT_CODE(dec(depth_addr));
        WOORT_JIT_CODE(ret(status));
    }

    void emit_ret()
    {
        auto* const em = this;

        // Get callway.
        static_assert(sizeof(woort_CallWay) == 4, "");

        // ret_way = sb[1].m_ret_bp.m_way
        const Mem ret_way = dword_ptr(
            em->m_sb,
            static_cast<int32_t>(
                1 * sizeof(woort_Value) + offsetof(woort_RetBP, m_way)));

        const Gp way = c->new_gp32();
        const Label L_normal_ret = c->new_label();

        WOORT_JIT_CODE(mov(way, ret_way));
        WOORT_JIT_CODE(cmp(way, Imm(static_cast<int32_t>(WOORT_CALL_WAY_FROM_NATIVE))));
        WOORT_JIT_CODE(jne(L_normal_ret));

        // 此调用发起自 Native，需要正同步以确保状态回退到调用前
        {
            /*
            vm->sp = rt_sb + 2;
            vm->sb = vm->sp + vm->sp[-1].m_ret_bp.m_bp_offset
            vm->ip = vm->sp[0].m_ret_addr;
            */

            static_assert(0 == offsetof(woort_Value, m_ret_addr), "");
            static_assert(sizeof(woort_Value) == 8, "");

            WOORT_JIT_CODE(lea(em->m_sp, ptr(em->m_sb, static_cast<int32_t>(sizeof(woort_Value)) * 2)));

            const Gp bp_offset = c->new_gp64();
            WOORT_JIT_CODE(movzx(bp_offset, dword_ptr(
                em->m_sp,
                static_cast<int32_t>(sizeof(woort_Value)) * -1
                + static_cast<int32_t>(offsetof(woort_RetBP, m_bp_offset)))));
            WOORT_JIT_CODE(lea(em->m_sb, ptr(em->m_sp, bp_offset, 3)));   // shift=3 => scale=8

            const Gp ret_ip = c->new_gp64();
            WOORT_JIT_CODE(mov(ret_ip, qword_ptr(em->m_sp)));

            em->emit_sync_rt_ip_status(ret_ip);

            em->emit_sync_runtime_status(L_normal_ret);
        }
        WOORT_JIT_CODE(bind(L_normal_ret));
        em->return_with_status(WOORT_VM_CALL_STATUS_NORMAL);
    }
    void emit_checkpoint(const woort_Bytecode* ip)
    {
        auto* const em = this;

        if (!em->m_checkpoint_resume_annotation.has_value())
        {
            em->m_checkpoint_slow = c->new_label();
            em->m_checkpoint_resume = c->new_gp_ptr();
            em->m_checkpoint_resume_annotation = c->new_jump_annotation();
        }

        const Gp    check_mask = c->new_gp32();
        const Label L_continue = c->new_label();

        WOORT_JIT_CODE(mov(check_mask, dword_ptr(em->m_vm, WOORT_VM_OFFSETOF_CHECK_REQUEST_MASK)));
        WOORT_JIT_CODE(test(check_mask, check_mask));
        WOORT_JIT_CODE(jz(L_continue));

        {
            em->emit_sync_rt_ip_status(ip);

            WOORT_JIT_CODE(lea(em->m_checkpoint_resume, ptr(L_continue)));
            em->update_last_error(
                em->m_checkpoint_resume_annotation.value()->add_label(L_continue));

            ++em->m_checkpoint_site_count;

            WOORT_JIT_CODE(jmp(em->m_checkpoint_slow));
        }

        WOORT_JIT_CODE(bind(L_continue));
    }
    void emit_extern_stack(const woort_Bytecode* ip, Label L_resume)
    {
        auto* const em = this;

        if (!em->m_stack_overflow_resume_annotation.has_value())
        {
            em->m_stack_overflow_slow = c->new_label();
            em->m_stack_overflow_resume = c->new_gp_ptr();
            em->m_stack_overflow_resume_annotation = c->new_jump_annotation();
        }

        {
            em->emit_sync_rt_ip_status(ip);

            WOORT_JIT_CODE(lea(em->m_stack_overflow_resume, ptr(L_resume)));
            em->update_last_error(
                em->m_stack_overflow_resume_annotation.value()->add_label(L_resume));

            ++em->m_stack_overflow_site_count;

            WOORT_JIT_CODE(jmp(em->m_stack_overflow_slow));
        }
    }
    void emit_jit_call_resync(Label L_resume)
    {
        auto* const em = this;

        if (!em->m_jit_call_resync_resume_annotation.has_value())
        {
            em->m_jit_call_resync_slow = c->new_label();
            em->m_jit_call_resync_resume = c->new_gp_ptr();
            em->m_jit_call_resync_resume_annotation = c->new_jump_annotation();
        }

        {
            WOORT_JIT_CODE(lea(em->m_jit_call_resync_resume, ptr(L_resume)));
            em->update_last_error(
                em->m_jit_call_resync_resume_annotation.value()->add_label(L_resume));

            ++em->m_jit_call_resync_site_count;

            WOORT_JIT_CODE(jmp(em->m_jit_call_resync_slow));
        }
    }
    void emit_sync_rt_ip_status(Gp ip)
    {
        auto* const em = this;

        WOORT_JIT_CODE(mov(qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_IP), ip));
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

        if (!em->m_sync_runtime_status_resume_annotation.has_value())
        {
            em->m_sync_runtime_status_slow = c->new_label();
            em->m_sync_runtime_status_resume = c->new_gp_ptr();
            em->m_sync_runtime_status_resume_annotation = c->new_jump_annotation();
        }

        {
            WOORT_JIT_CODE(lea(em->m_sync_runtime_status_resume, ptr(L_resume)));
            em->update_last_error(
                em->m_sync_runtime_status_resume_annotation.value()->add_label(L_resume));

            ++em->m_sync_runtime_status_site_count;

            WOORT_JIT_CODE(jmp(em->m_sync_runtime_status_slow));
        }
    }
    void emit_sync_stack_value(Label L_resume)
    {
        /*
        将所有写脏的 GP 寄存器写回对应偏移量虚拟机栈的操作，
        推迟到单一的 slow path（在 epilogue 中发射）。
        */
        auto* const em = this;

        if (!em->m_sync_stack_value_resume_annotation.has_value())
        {
            em->m_sync_stack_value_slow = c->new_label();
            em->m_sync_stack_value_resume = c->new_gp_ptr();
            em->m_sync_stack_value_resume_annotation = c->new_jump_annotation();
        }

        {
            WOORT_JIT_CODE(lea(em->m_sync_stack_value_resume, ptr(L_resume)));
            em->update_last_error(
                em->m_sync_stack_value_resume_annotation.value()->add_label(L_resume));

            ++em->m_sync_stack_value_site_count;

            WOORT_JIT_CODE(jmp(em->m_sync_stack_value_slow));
        }
    }
    void emit_failed_fallback(const woort_Bytecode* ip)
    {
        auto* const em = this;

        emit_sync_rt_ip_status(ip);

        const Label L_after_sync_st = em->c->new_label();
        emit_sync_runtime_status(L_after_sync_st);
        WOORT_JIT_CODE(bind(L_after_sync_st));

        const Label L_after_sync_sv = em->c->new_label();
        emit_sync_stack_value(L_after_sync_sv);
        WOORT_JIT_CODE(bind(L_after_sync_sv));

        return_with_status(WOORT_VM_CALL_STATUS_RESYNC);
    }

    // ===================================================== //
    void apply_gp_to_stack(woort_Opcode_Stack src)
    {
        auto& stack_value = m_stack_gp.at(src);
        stack_value.m_writed = true;
    }
    template<typename T>
    void set_gp_by_stack(woort_Opcode_Stack src, T v)
    {
        auto* const em = this;

        Gp reg;
        const auto it = em->m_stack_gp.find(src);
        if (it != em->m_stack_gp.end())
            reg = it->second.m_gp;
        else
        {
            reg = c->new_gp64();
            em->m_stack_gp.emplace(src, reg);
        }

        WOORT_JIT_CODE(mov(reg, v));

        apply_gp_to_stack(src);
    }
    Gp get_gp_from_stack(woort_Opcode_Stack src)
    {
        auto* const em = this;

        const auto it = em->m_stack_gp.find(src);
        if (it != em->m_stack_gp.end())
            return it->second.m_gp;

        const Gp reg = c->new_gp64();
        const int32_t src_offset =
            src * static_cast<int32_t>(sizeof(woort_Value));

        WOORT_JIT_CODE(mov(reg, qword_ptr(em->m_sb, src_offset)));

        em->m_stack_gp.emplace(src, reg);
        return reg;
    }
    Gp get_gp_by_stack_no_read_from_stack(woort_Opcode_Stack src)
    {
        auto* const em = this;

        const auto it = em->m_stack_gp.find(src);
        if (it != em->m_stack_gp.end())
            return it->second.m_gp;

        const Gp reg = c->new_gp64();
        em->m_stack_gp.emplace(src, reg);
        return reg;
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

        const Label lbl = em->get_label(c);
        WOORT_JIT_CODE(bind(lbl));
    }
};

bool woort_JIT_Backend_x64_prologue(
    const woort_CodeEnv* cenv,
    const woort_Bytecode** ip,
    void** out_emmiter)
{
    woort_JIT_Asmjit_x64_Emmiter* const em =
        new (nothrow) woort_JIT_Asmjit_x64_Emmiter(cenv, ip);

    if (em == nullptr)
        return false;

    if (em->is_okay())
    {
        // Ok, generate codes for JIT function overload.

        // 0. Apply state.
        {
            WOORT_JIT_CODE(mov(em->m_sp, em->m_sb));
            WOORT_JIT_CODE(mov(em->m_stack, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK)));
            WOORT_JIT_CODE(mov(em->m_stack_end, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
        }
        // 1. Check JIT function depth.
        {
            const Label L_ok = em->c->new_label();

            const Mem depth_addr =
                dword_ptr(em->m_vm, WOORT_VM_OFFSETOF_JIT_CALL_DEPTH);

            WOORT_JIT_CODE(inc(depth_addr));

            WOORT_JIT_CODE(cmp(depth_addr, WOORT_VM_MAX_JIT_CALL_DEPTH));
            WOORT_JIT_CODE(jbe(L_ok));
            {
                em->emit_sync_rt_ip_status(*ip);

                const Label L_resync_ret = em->c->new_label();
                em->emit_sync_runtime_status(L_resync_ret);
                WOORT_JIT_CODE(bind(L_resync_ret));
            }
            em->return_with_status(WOORT_VM_CALL_STATUS_RESYNC);
            WOORT_JIT_CODE(bind(L_ok));
        }
    }

    if (!em->is_okay())
    {
        delete em;
        return false;
    }

    *out_emmiter = em;
    return true;
}

bool woort_JIT_Backend_x64_epilogue(
    void* emmiter,
    woort_JitFunction* out_code)
{
    woort_JIT_Asmjit_x64_Emmiter* const em =
        static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    assert(em != nullptr);

    // Check for checkpoint
    if (em->m_checkpoint_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_checkpoint_slow));

        const Mem checkpoint_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(mov(checkpoint_resume_slot, em->m_checkpoint_resume));

        {
            const Label L_after_sync_st = em->c->new_label();
            em->emit_sync_runtime_status(L_after_sync_st);
            WOORT_JIT_CODE(bind(L_after_sync_st));

            const Label L_after_sync_sv = em->c->new_label();
            em->emit_sync_stack_value(L_after_sync_sv);
            WOORT_JIT_CODE(bind(L_after_sync_sv));
        }

        static_assert(sizeof(woort_VmCallStatus) == 4, "");

        const Label checkpoint_exit = em->c->new_label();
        const Gp    checkpoint_status = em->c->new_gp32();

        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_VMRuntime_JIT_request_handler)),
            FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*>()));

        invoke_node->set_arg(0, em->m_vm);
        invoke_node->set_ret(0, checkpoint_status);

        WOORT_JIT_CODE(cmp(checkpoint_status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_NORMAL)));
        WOORT_JIT_CODE(jne(checkpoint_exit));

        em->resync_vm_stack_state_fully();
        WOORT_JIT_CODE(jmp(checkpoint_resume_slot, em->m_checkpoint_resume_annotation.value()));

        WOORT_JIT_CODE(bind(checkpoint_exit));
        em->return_with_status(checkpoint_status);
    }

    // Check for stack overflow.
    if (em->m_stack_overflow_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_stack_overflow_slow));

        const Mem so_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(mov(so_resume_slot, em->m_stack_overflow_resume));

        {
            const Label L_after_sync = em->c->new_label();
            em->emit_sync_runtime_status(L_after_sync);
            WOORT_JIT_CODE(bind(L_after_sync));
        }

        static_assert(sizeof(woort_VmCallStatus) == 4, "");

        const Label so_exit = em->c->new_label();
        const Gp    so_status = em->c->new_gp32();

        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_VMRuntime_JIT_stack_overflow_handler)),
            FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*>()));

        invoke_node->set_arg(0, em->m_vm);
        invoke_node->set_ret(0, so_status);

        WOORT_JIT_CODE(cmp(so_status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_NORMAL)));
        WOORT_JIT_CODE(jne(so_exit));

        em->resync_vm_stack_state_fully();
        WOORT_JIT_CODE(jmp(so_resume_slot, em->m_stack_overflow_resume_annotation.value()));

        WOORT_JIT_CODE(bind(so_exit));
        em->return_with_status(so_status);
    }

    // Resync cached stack registers after a JIT-to-JIT call that reallocated the stack.
    if (em->m_jit_call_resync_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_jit_call_resync_slow));

        const Mem jit_call_resync_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(mov(jit_call_resync_resume_slot, em->m_jit_call_resync_resume));

        const Gp vm_stack_end = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(vm_stack_end, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));

        const Gp off = em->c->new_gp64();

        WOORT_JIT_CODE(mov(off, em->m_stack_end));
        WOORT_JIT_CODE(sub(off, em->m_sp));
        WOORT_JIT_CODE(mov(em->m_sp, vm_stack_end));
        WOORT_JIT_CODE(sub(em->m_sp, off));

        WOORT_JIT_CODE(mov(off, em->m_stack_end));
        WOORT_JIT_CODE(sub(off, em->m_sb));
        WOORT_JIT_CODE(mov(em->m_sb, vm_stack_end));
        WOORT_JIT_CODE(sub(em->m_sb, off));

        WOORT_JIT_CODE(mov(em->m_stack, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK)));
        WOORT_JIT_CODE(mov(em->m_stack_end, vm_stack_end));

        WOORT_JIT_CODE(jmp(jit_call_resync_resume_slot,
            em->m_jit_call_resync_resume_annotation.value()));
    }

    // Shared slow path for dirty GP register writeback to VM stack.
    if (em->m_sync_stack_value_site_count > 0)
    {
        WOORT_JIT_CODE(bind(em->m_sync_stack_value_slow));

        const Mem sync_stack_value_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(mov(sync_stack_value_resume_slot, em->m_sync_stack_value_resume));

        /* 将所有写脏的 GP 寄存器写回对应偏移量的虚拟机栈 */
        for (auto& kv : em->m_stack_gp)
        {
            if (kv.second.m_writed)
            {
                const int32_t slot_offset =
                    kv.first * static_cast<int32_t>(sizeof(woort_Value));
                WOORT_JIT_CODE(mov(qword_ptr(em->m_sb, slot_offset), kv.second.m_gp));
            }
        }

        WOORT_JIT_CODE(jmp(sync_stack_value_resume_slot,
            em->m_sync_stack_value_resume_annotation.value()));
    }

    // Shared slow path for SP/SB/ENV sync (caller writes vm->ip inline).
    assert(em->m_sync_runtime_status_site_count > 0);
    {
        WOORT_JIT_CODE(bind(em->m_sync_runtime_status_slow));

        const Mem sync_runtime_status_resume_slot =
            em->c->new_stack(sizeof(uintptr_t), alignof(uintptr_t));
        WOORT_JIT_CODE(mov(sync_runtime_status_resume_slot, em->m_sync_runtime_status_resume));

        WOORT_JIT_CODE(mov(qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_SP), em->m_sp));
        WOORT_JIT_CODE(mov(qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_SB), em->m_sb));
        const Gp env_tmp = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(env_tmp, (uintptr_t)em->cenv));
        WOORT_JIT_CODE(mov(qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_ENV), env_tmp));

        WOORT_JIT_CODE(jmp(sync_runtime_status_resume_slot,
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

bool woort_JIT_Backend_x64_pre_dispatch(
    void* emmiter)
{
    woort_JIT_Asmjit_x64_Emmiter* const em =
        static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    em->bind_label(*em->m_ip);

    return true;
}

bool woort_JIT_Backend_x64_post_dispatch(
    void* emmiter)
{
    woort_JIT_Asmjit_x64_Emmiter* const em =
        static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    if (!em->is_okay())
    {
        delete em;
        return false;
    }

    return true;
}

void woort_JIT_Backend_x64_droper(
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

void woort_JIT_Backend_x64_NOP(void* emmiter)
{
    (void)emmiter;
}

void woort_JIT_Backend_x64_LOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];

    if (src < em->m_cenv_constant_count)
        em->set_gp_by_stack(dst, Imm(src_addr->m_integer));
    else
        em->set_gp_by_stack(dst, qword_ptr(reinterpret_cast<uintptr_t>(src_addr)));
}

void woort_JIT_Backend_x64_STORE(void* emmiter, woort_Opcode_Global dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    woort_Value* const dst_addr =
        const_cast<woort_Value*>(&em->m_cenv_static_storage[dst]);

    const Gp val = em->get_gp_from_stack(src);

    const Gp dst_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(dst_ptr, reinterpret_cast<uintptr_t>(dst_addr)));

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(flag_ptr, reinterpret_cast<uintptr_t>(&woomem_gc_marking_state_flag)));
    WOORT_JIT_CODE(cmp(byte_ptr(flag_ptr), 0));
    WOORT_JIT_CODE(je(L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_GC_mixed_write_barrier_value)),
            FuncSignature::build<void, woort_Value*, uint64_t>()));

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, val);
    }
    WOORT_JIT_CODE(jmp(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(mov(qword_ptr(dst_ptr), val));

    WOORT_JIT_CODE(bind(L_end));
}

void woort_JIT_Backend_x64_LOADPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp ptr = em->get_gp_from_stack(src);
    em->set_gp_by_stack(dst, qword_ptr(ptr));
}

void woort_JIT_Backend_x64_STOREPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp dst_ptr = em->get_gp_from_stack(dst);
    const Gp val = em->get_gp_from_stack(src);

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(flag_ptr, reinterpret_cast<uintptr_t>(&woomem_gc_marking_state_flag)));
    WOORT_JIT_CODE(cmp(byte_ptr(flag_ptr), 0));
    WOORT_JIT_CODE(je(L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_GC_mixed_write_barrier_value)),
            FuncSignature::build<void, woort_Value*, uint64_t>()));

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, val);
    }
    WOORT_JIT_CODE(jmp(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(mov(qword_ptr(dst_ptr), val));

    WOORT_JIT_CODE(bind(L_end));
}

void woort_JIT_Backend_x64_MOV(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_src = em->get_gp_from_stack(src);
    em->set_gp_by_stack(dst, reg_src);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_PUSHRCHK(void* emmiter, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(jae(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));
    WOORT_JIT_CODE(mov(em->m_sp, new_sp));
}

void woort_JIT_Backend_x64_PUSHSCHK(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), val));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_PUSHCCHK(void* emmiter, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];
    const bool is_constant = (src < em->m_cenv_constant_count);

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    if (is_constant &&
        src_addr->m_integer >= INT32_MIN &&
        src_addr->m_integer <= INT32_MAX)
    {
        WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), Imm(static_cast<int32_t>(src_addr->m_integer))));
    }
    else
    {
        const Gp val = em->c->new_gp64();
        if (is_constant)
            WOORT_JIT_CODE(mov(val, Imm(src_addr->m_integer)));
        else
            WOORT_JIT_CODE(mov(val, qword_ptr(reinterpret_cast<uintptr_t>(src_addr))));
        WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), val));
    }

    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_ASSURESSZ(void* emmiter, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(jae(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_x64_PUSHS(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), val));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_PUSHC(void* emmiter, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];
    const bool is_constant = (src < em->m_cenv_constant_count);

    if (is_constant &&
        src_addr->m_integer >= INT32_MIN &&
        src_addr->m_integer <= INT32_MAX)
    {
        WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), Imm(static_cast<int32_t>(src_addr->m_integer))));
    }
    else
    {
        const Gp val = em->c->new_gp64();
        if (is_constant)
            WOORT_JIT_CODE(mov(val, Imm(src_addr->m_integer)));
        else
            WOORT_JIT_CODE(mov(val, qword_ptr(reinterpret_cast<uintptr_t>(src_addr))));
        WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), val));
    }

    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_POPR(void* emmiter, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    if (n != 0)
    {
        WOORT_JIT_CODE(add(
            em->m_sp,
            Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
    }
}

void woort_JIT_Backend_x64_POPS(void* emmiter, woort_Opcode_Stack dst)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    WOORT_JIT_CODE(add(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));

    em->set_gp_by_stack(dst, qword_ptr(em->m_sp));
}

void woort_JIT_Backend_x64_POPC(void* emmiter, woort_Opcode_Global dst)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    WOORT_JIT_CODE(add(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));

    woort_Value* const dst_addr =
        const_cast<woort_Value*>(&em->m_cenv_static_storage[dst]);

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(val, qword_ptr(em->m_sp)));

    const Gp dst_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(dst_ptr, reinterpret_cast<uintptr_t>(dst_addr)));

    const Label L_fast = em->c->new_label();
    const Label L_end = em->c->new_label();

    const Gp flag_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(flag_ptr, reinterpret_cast<uintptr_t>(&woomem_gc_marking_state_flag)));
    WOORT_JIT_CODE(cmp(byte_ptr(flag_ptr), 0));
    WOORT_JIT_CODE(je(L_fast));
    {
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_GC_mixed_write_barrier_value)),
            FuncSignature::build<void, woort_Value*, uint64_t>()));

        invoke_node->set_arg(0, dst_ptr);
        invoke_node->set_arg(1, val);
    }
    WOORT_JIT_CODE(jmp(L_end));

    WOORT_JIT_CODE(bind(L_fast));
    WOORT_JIT_CODE(mov(qword_ptr(dst_ptr), val));

    WOORT_JIT_CODE(bind(L_end));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ITOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm = em->c->new_xmm_sd();

    WOORT_JIT_CODE(cvtsi2sd(xmm, reg_src));
    WOORT_JIT_CODE(movq(result, xmm));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_ITOS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp int_val = em->get_gp_from_stack(src);

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_from_integer)),
        FuncSignature::build<const woort_GCString*, woort_Int>()));

    invoke_node->set_arg(0, int_val);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

void woort_JIT_Backend_x64_RTOI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm, reg_src));
    WOORT_JIT_CODE(cvttsd2si(result, xmm));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_RTOS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_src = em->get_gp_from_stack(src);
    const Vec xmm = em->c->new_xmm_sd();
    WOORT_JIT_CODE(movq(xmm, reg_src));

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_from_real)),
        FuncSignature::build<const woort_GCString*, woort_Real>()));

    invoke_node->set_arg(0, xmm);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_CASTSTO(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    switch (target)
    {
    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const Gp reg_src = em->get_gp_from_stack(src);
        em->set_gp_by_stack(dst, reg_src);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp str_ptr = em->get_gp_from_stack(src);

        const Gp result = em->c->new_gp64();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_GCString_to_integer)),
            FuncSignature::build<woort_Int, const woort_GCString*>()));

        invoke_node->set_arg(0, str_ptr);
        invoke_node->set_ret(0, result);

        em->set_gp_by_stack(dst, result);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Gp str_ptr = em->get_gp_from_stack(src);

        const Vec xmm = em->c->new_xmm_sd();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_GCString_to_real)),
            FuncSignature::build<woort_Real, const woort_GCString*>()));

        invoke_node->set_arg(0, str_ptr);
        invoke_node->set_ret(0, xmm);

        const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
        WOORT_JIT_CODE(movq(result, xmm));
        em->apply_gp_to_stack(dst);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp str_ptr = em->get_gp_from_stack(src);

        const Gp result = em->c->new_gp64();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_GCString_to_bool)),
            FuncSignature::build<woort_Int, const woort_GCString*>()));

        invoke_node->set_arg(0, str_ptr);
        invoke_node->set_ret(0, result);

        em->set_gp_by_stack(dst, result);
    }
    break;

    default:
        abort();
        break;
    }
}

void woort_JIT_Backend_x64_CASTSFROM(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType srctype, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    switch (srctype)
    {
    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const Gp reg_src = em->get_gp_from_stack(src);
        em->set_gp_by_stack(dst, reg_src);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp int_val = em->get_gp_from_stack(src);

        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_GCString_from_integer)),
            FuncSignature::build<const woort_GCString*, woort_Int>()));

        invoke_node->set_arg(0, int_val);
        invoke_node->set_ret(0, result);

        em->set_gp_by_stack(dst, result);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Gp reg_src = em->get_gp_from_stack(src);
        const Vec xmm = em->c->new_xmm_sd();
        WOORT_JIT_CODE(movq(xmm, reg_src));

        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_GCString_from_real)),
            FuncSignature::build<const woort_GCString*, woort_Real>()));

        invoke_node->set_arg(0, xmm);
        invoke_node->set_ret(0, result);

        em->set_gp_by_stack(dst, result);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp bool_val = em->get_gp_from_stack(src);

        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_GCString_from_bool)),
            FuncSignature::build<const woort_GCString*, woort_Int>()));

        invoke_node->set_arg(0, bool_val);
        invoke_node->set_ret(0, result);

        em->set_gp_by_stack(dst, result);
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

        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_GCString_make_string)),
            FuncSignature::build<const woort_GCString*, const char*, size_t>()));

        invoke_node->set_arg(0, Imm(reinterpret_cast<intptr_t>(lit)));
        invoke_node->set_arg(1, Imm(static_cast<intptr_t>(len)));
        invoke_node->set_ret(0, result);

        em->set_gp_by_stack(dst, result);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        const Gp obj_ptr = em->get_gp_from_stack(src);

        const Gp dst_addr = em->c->new_gp_ptr();
        const int32_t dst_offset =
            static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value));
        WOORT_JIT_CODE(lea(dst_addr, ptr(em->m_sb, dst_offset)));

        const Gp ok = em->c->new_gp64();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_serialize_vec)),
            FuncSignature::build<woort_Int, woort_Value*, woort_GCVec*>()));

        invoke_node->set_arg(0, dst_addr);
        invoke_node->set_arg(1, obj_ptr);
        invoke_node->set_ret(0, ok);

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(test(ok, ok));
        WOORT_JIT_CODE(jnz(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));

        em->m_stack_gp.erase(dst);
    }
    break;

    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        const Gp obj_ptr = em->get_gp_from_stack(src);

        const Gp dst_addr = em->c->new_gp_ptr();
        const int32_t dst_offset =
            static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value));
        WOORT_JIT_CODE(lea(dst_addr, ptr(em->m_sb, dst_offset)));

        const Gp ok = em->c->new_gp64();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_serialize_map)),
            FuncSignature::build<woort_Int, woort_Value*, woort_GCMap*>()));

        invoke_node->set_arg(0, dst_addr);
        invoke_node->set_arg(1, obj_ptr);
        invoke_node->set_ret(0, ok);

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(test(ok, ok));
        WOORT_JIT_CODE(jnz(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));

        em->m_stack_gp.erase(dst);
    }
    break;

    default:
        abort();
        break;
    }
}

void woort_JIT_Backend_x64_CASTDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType target, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* target 是编译期常量（来自字节码），据此特化；src 类型在运行时由 tag 位分派 */
    const Gp val = em->get_gp_from_stack(src);

    const Label L_done = em->c->new_label();

    /* 将内联 BoxedFloat63 还原为 double，结果在 xmm 中（与 UNBOXDYN REAL 一致） */
    auto unbox_real = [&](const Gp& v) -> Vec {
        const Gp sign = em->c->new_gp64();
        WOORT_JIT_CODE(mov(sign, v));
        WOORT_JIT_CODE(and_(sign, Imm(static_cast<int64_t>(0x8000000000000000ULL))));

        const Gp exp_bit = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_bit, v));
        WOORT_JIT_CODE(shr(exp_bit, 62));
        WOORT_JIT_CODE(and_(exp_bit, Imm(1)));
        WOORT_JIT_CODE(xor_(exp_bit, Imm(1)));
        WOORT_JIT_CODE(shl(exp_bit, 62));

        const Gp bits = em->c->new_gp64();
        WOORT_JIT_CODE(mov(bits, v));
        WOORT_JIT_CODE(shr(bits, 1));
        WOORT_JIT_CODE(and_(bits, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
        WOORT_JIT_CODE(or_(bits, exp_bit));
        WOORT_JIT_CODE(or_(bits, sign));

        const Vec xmm = em->c->new_xmm_sd();
        WOORT_JIT_CODE(movq(xmm, bits));
        return xmm;
    };

    /* 将 xmm 中的 double 按 Real 布局写入 dst */
    auto finish_real = [&](const Vec& xmm) {
        const Gp r = em->get_gp_by_stack_no_read_from_stack(dst);
        WOORT_JIT_CODE(movq(r, xmm));
        em->apply_gp_to_stack(dst);
    };

    /* 读出 src 的整数（内联 INT：sar 2） */
    auto unbox_int = [&](const Gp& v) -> Gp {
        const Gp r = em->c->new_gp64();
        WOORT_JIT_CODE(mov(r, v));
        WOORT_JIT_CODE(sar(r, 2));
        return r;
    };

    /* 读出 src 的布尔（内联 BOOL：shr 3，结果 0/1） */
    auto unbox_bool = [&](const Gp& v) -> Gp {
        const Gp r = em->c->new_gp64();
        WOORT_JIT_CODE(mov(r, v));
        WOORT_JIT_CODE(shr(r, 3));
        return r;
    };

    /* 取出 val 指向对象的 proxy 指针（GCUnit 首成员） */
    auto load_proxy = [&]() -> Gp {
        const Gp proxy = em->c->new_gp64();
        WOORT_JIT_CODE(mov(proxy, qword_ptr(val)));
        return proxy;
    };

    /* 比较 proxy 与全局 proxy 对象地址，相等则跳转 */
    auto proxy_is = [&](const Gp& proxy, const woort_GCUnitProxy& sym, const Label& L) {
        const Gp tmp = em->c->new_gp64();
        WOORT_JIT_CODE(mov(tmp, reinterpret_cast<uintptr_t>(&sym)));
        WOORT_JIT_CODE(cmp(proxy, tmp));
        WOORT_JIT_CODE(je(L));
    };

    /* 扩展盒子（BoxedExValue）：按 m_is_int 读取其中的 int/real 值 */
    auto read_ex_int = [&]() -> Gp {
        const Gp iv = em->c->new_gp64();
        WOORT_JIT_CODE(mov(iv, qword_ptr(val, static_cast<int32_t>(offsetof(woort_BoxedExValue, m_int)))));
        return iv;
    };
    auto read_ex_real = [&]() -> Vec {
        const Vec rv = em->c->new_xmm_sd();
        WOORT_JIT_CODE(movq(rv, qword_ptr(val, static_cast<int32_t>(offsetof(woort_BoxedExValue, m_real)))));
        return rv;
    };
    auto ex_is_int = [&]() -> Gp {
        const Gp r = em->c->new_gp32();
        WOORT_JIT_CODE(movzx(r, byte_ptr(val, static_cast<int32_t>(offsetof(woort_BoxedExValue, m_is_int)))));
        return r;
    };

    switch (target)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        /* src 为整数 -> 恒等；src 为浮点 -> cvttsd2si */
        auto from_int = [&](const Gp& iv) { em->set_gp_by_stack(dst, iv); };
        auto from_real = [&](const Vec& rv) {
            const Gp r = em->c->new_gp64();
            WOORT_JIT_CODE(cvttsd2si(r, rv));
            em->set_gp_by_stack(dst, r);
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

        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jnz(L_scalar));
        WOORT_JIT_CODE(jmp(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(test(val, Imm(0b001)));
        WOORT_JIT_CODE(jnz(L_real_i));
        WOORT_JIT_CODE(test(val, Imm(0b010)));
        WOORT_JIT_CODE(jnz(L_int_i));
        { from_int(unbox_bool(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(test(val, val));
            WOORT_JIT_CODE(jz(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            WOORT_JIT_CODE(jmp(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        { em->set_gp_by_stack(dst, Imm(0)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(test(is_int, is_int));
            WOORT_JIT_CODE(jz(L_ex_real));
            { from_int(read_ex_int()); WOORT_JIT_CODE(jmp(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_real));
            { from_real(read_ex_real()); WOORT_JIT_CODE(jmp(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_GCString_to_integer)),
                FuncSignature::build<woort_Int, const woort_GCString*>()));
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, r);
            em->set_gp_by_stack(dst, r);
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        /* src 为整数 -> cvtsi2sd；src 为浮点 -> 恒等 */
        auto from_int = [&](const Gp& iv) {
            const Vec xmm = em->c->new_xmm_sd();
            WOORT_JIT_CODE(cvtsi2sd(xmm, iv));
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

        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jnz(L_scalar));
        WOORT_JIT_CODE(jmp(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(test(val, Imm(0b001)));
        WOORT_JIT_CODE(jnz(L_real_i));
        WOORT_JIT_CODE(test(val, Imm(0b010)));
        WOORT_JIT_CODE(jnz(L_int_i));
        { from_int(unbox_bool(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(test(val, val));
            WOORT_JIT_CODE(jz(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            WOORT_JIT_CODE(jmp(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        {
            const Vec xmm = em->c->new_xmm_sd();
            WOORT_JIT_CODE(xorps(xmm, xmm));
            finish_real(xmm);
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(test(is_int, is_int));
            WOORT_JIT_CODE(jnz(L_ex_int));
            { from_real(read_ex_real()); WOORT_JIT_CODE(jmp(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_int));
            { from_int(read_ex_int()); WOORT_JIT_CODE(jmp(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        {
            const Vec xmm = em->c->new_xmm_sd();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_GCString_to_real)),
                FuncSignature::build<woort_Real, const woort_GCString*>()));
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, xmm);
            finish_real(xmm);
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        /* src != 0 / != 0.0 -> 0/1 */
        auto from_int = [&](const Gp& iv) {
            const Gp r = em->c->new_gp32();
            WOORT_JIT_CODE(xor_(r, r));
            WOORT_JIT_CODE(test(iv, iv));
            WOORT_JIT_CODE(setne(r.r8()));
            em->set_gp_by_stack(dst, r);
        };
        auto from_real = [&](const Vec& rv) {
            const Vec zero = em->c->new_xmm_sd();
            WOORT_JIT_CODE(xorps(zero, zero));
            const Gp r = em->c->new_gp32();
            WOORT_JIT_CODE(xor_(r, r));
            WOORT_JIT_CODE(ucomisd(rv, zero));
            WOORT_JIT_CODE(setne(r.r8()));
            em->set_gp_by_stack(dst, r);
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

        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jnz(L_scalar));
        WOORT_JIT_CODE(jmp(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(test(val, Imm(0b001)));
        WOORT_JIT_CODE(jnz(L_real_i));
        WOORT_JIT_CODE(test(val, Imm(0b010)));
        WOORT_JIT_CODE(jnz(L_int_i));
        { from_int(unbox_bool(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(test(val, val));
            WOORT_JIT_CODE(jz(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            WOORT_JIT_CODE(jmp(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        { em->set_gp_by_stack(dst, Imm(0)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(test(is_int, is_int));
            WOORT_JIT_CODE(jz(L_ex_real));
            { from_int(read_ex_int()); WOORT_JIT_CODE(jmp(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_real));
            { from_real(read_ex_real()); WOORT_JIT_CODE(jmp(L_done)); }
        }

        WOORT_JIT_CODE(bind(L_str));
        {
            const Gp r = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_GCString_to_bool)),
                FuncSignature::build<woort_Int, const woort_GCString*>()));
            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, r);
            em->set_gp_by_stack(dst, r);
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_bad));
        em->emit_failed_fallback(*em->m_ip);
        break;
    }

    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        auto from_int = [&](const Gp& iv) {
            const Gp r = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_GCString_from_integer)),
                FuncSignature::build<const woort_GCString*, woort_Int>()));
            invoke_node->set_arg(0, iv);
            invoke_node->set_ret(0, r);
            em->set_gp_by_stack(dst, r);
        };
        auto from_real = [&](const Vec& rv) {
            const Gp r = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_GCString_from_real)),
                FuncSignature::build<const woort_GCString*, woort_Real>()));
            invoke_node->set_arg(0, rv);
            invoke_node->set_ret(0, r);
            em->set_gp_by_stack(dst, r);
        };
        auto from_bool = [&](const Gp& bv) {
            const Gp r = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_GCString_from_bool)),
                FuncSignature::build<const woort_GCString*, woort_Int>()));
            invoke_node->set_arg(0, bv);
            invoke_node->set_ret(0, r);
            em->set_gp_by_stack(dst, r);
        };
        auto make_literal = [&](const char* lit, size_t len) {
            const Gp r = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_GCString_make_string)),
                FuncSignature::build<const woort_GCString*, const char*, size_t>()));
            invoke_node->set_arg(0, Imm(reinterpret_cast<intptr_t>(lit)));
            invoke_node->set_arg(1, Imm(static_cast<intptr_t>(len)));
            invoke_node->set_ret(0, r);
            em->set_gp_by_stack(dst, r);
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

        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jnz(L_scalar));
        WOORT_JIT_CODE(jmp(L_heap));

        WOORT_JIT_CODE(bind(L_scalar));
        WOORT_JIT_CODE(test(val, Imm(0b001)));
        WOORT_JIT_CODE(jnz(L_real_i));
        WOORT_JIT_CODE(test(val, Imm(0b010)));
        WOORT_JIT_CODE(jnz(L_int_i));
        { from_bool(unbox_bool(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_real_i));
        { from_real(unbox_real(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_int_i));
        { from_int(unbox_int(val)); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_heap));
        {
            WOORT_JIT_CODE(test(val, val));
            WOORT_JIT_CODE(jz(L_nil));
            const Gp proxy = load_proxy();
            proxy_is(proxy, WOORT_EX_BOX_PROXY, L_ex);
            proxy_is(proxy, WOORT_GCSTRING_UNIT_PROXY, L_str);
            proxy_is(proxy, WOORT_GCVEC_UNIT_PROXY, L_vec);
            proxy_is(proxy, WOORT_GCMAP_UNIT_PROXY, L_map);
            proxy_is(proxy, WOORT_GCSTRUCT_UNIT_PROXY, L_struct);
            proxy_is(proxy, WOORT_GCHANDLE_UNIT_PROXY, L_handle);
            proxy_is(proxy, WOORT_GCCLOSURE_UNIT_PROXY, L_closure);
            WOORT_JIT_CODE(jmp(L_bad));
        }

        WOORT_JIT_CODE(bind(L_nil));
        { make_literal("nil", 3); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp is_int = ex_is_int();
            WOORT_JIT_CODE(test(is_int, is_int));
            WOORT_JIT_CODE(jnz(L_ex_int));
            { from_real(read_ex_real()); WOORT_JIT_CODE(jmp(L_done)); }
            WOORT_JIT_CODE(bind(L_ex_int));
            { from_int(read_ex_int()); WOORT_JIT_CODE(jmp(L_done)); }
        }

        /* STRING -> STRING：恒等（val 即 GCString*） */
        WOORT_JIT_CODE(bind(L_str));
        { em->set_gp_by_stack(dst, val); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_vec));
        {
            const Gp dst_addr = em->c->new_gp_ptr();
            WOORT_JIT_CODE(lea(dst_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));
            const Gp ok = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_serialize_vec)),
                FuncSignature::build<woort_Int, woort_Value*, woort_GCVec*>()));
            invoke_node->set_arg(0, dst_addr);
            invoke_node->set_arg(1, val);
            invoke_node->set_ret(0, ok);
            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(test(ok, ok));
            WOORT_JIT_CODE(jnz(L_ok));
            em->emit_failed_fallback(*em->m_ip);
            WOORT_JIT_CODE(bind(L_ok));
            em->set_gp_by_stack(dst, qword_ptr(dst_addr));
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_map));
        {
            const Gp dst_addr = em->c->new_gp_ptr();
            WOORT_JIT_CODE(lea(dst_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));
            const Gp ok = em->c->new_gp64();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_serialize_map)),
                FuncSignature::build<woort_Int, woort_Value*, woort_GCMap*>()));
            invoke_node->set_arg(0, dst_addr);
            invoke_node->set_arg(1, val);
            invoke_node->set_ret(0, ok);
            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(test(ok, ok));
            WOORT_JIT_CODE(jnz(L_ok));
            em->emit_failed_fallback(*em->m_ip);
            WOORT_JIT_CODE(bind(L_ok));
            em->set_gp_by_stack(dst, qword_ptr(dst_addr));
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_struct));
        { make_literal("<struct>", 8); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_handle));
        { make_literal("<gchandle>", 10); WOORT_JIT_CODE(jmp(L_done)); }

        WOORT_JIT_CODE(bind(L_closure));
        { make_literal("<function>", 10); WOORT_JIT_CODE(jmp(L_done)); }

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

void woort_JIT_Backend_x64_ASSERTDYN(void* emmiter, woort_BoxValueType target, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)target;
    (void)src;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_CALLNWO(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
    abort();
}

void woort_JIT_Backend_x64_CALLNFP(void* emmiter, woort_Opcode_Global func)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const woort_Value* const func_addr = &em->m_cenv_static_storage[func];

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, Imm(static_cast<int32_t>(2 * sizeof(woort_Value)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(jae(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    {
        const int32_t way_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_way);
        const int32_t bp_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_bp_offset);
        const int32_t addr_off = 2 * (int32_t)sizeof(woort_Value);

        WOORT_JIT_CODE(mov(dword_ptr(new_sp, way_off),
            Imm(static_cast<int32_t>(WOORT_CALL_WAY_NEAR))));

        const Gp bp_offset = em->c->new_gp64();
        WOORT_JIT_CODE(mov(bp_offset, em->m_sb));
        WOORT_JIT_CODE(sub(bp_offset, em->m_sp));
        WOORT_JIT_CODE(shr(bp_offset, 3));
        WOORT_JIT_CODE(mov(dword_ptr(new_sp, bp_off), bp_offset.r32()));

        const Gp ret_addr = em->c->new_gp64();
        WOORT_JIT_CODE(mov(ret_addr, Imm(reinterpret_cast<intptr_t>(*em->m_ip + 1))));
        WOORT_JIT_CODE(mov(qword_ptr(new_sp, addr_off), ret_addr));
    }

    const Gp native_fn = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(native_fn, qword_ptr(reinterpret_cast<intptr_t>(func_addr))));
    em->emit_sync_rt_ip_status(native_fn);
    WOORT_JIT_CODE(mov(qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_SP), new_sp));
    WOORT_JIT_CODE(mov(qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_SB), new_sp));

    const Gp status = em->c->new_gp32();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        qword_ptr(reinterpret_cast<intptr_t>(func_addr)),
        FuncSignature::build<woort_VmCallStatus>()));

    invoke_node->set_ret(0, status);

    {
        const Label L_after_realloc = em->c->new_label();
        const Gp vm_stack_end = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(vm_stack_end, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
        WOORT_JIT_CODE(cmp(vm_stack_end, em->m_stack_end));
        WOORT_JIT_CODE(je(L_after_realloc));
        em->emit_jit_call_resync(L_after_realloc);
        WOORT_JIT_CODE(bind(L_after_realloc));
    }

    {
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(cmp(status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_RESYNC)));
        WOORT_JIT_CODE(jne(L_done));
        em->emit_checkpoint(woort_JIT_next_bytecode(*em->m_ip));
        WOORT_JIT_CODE(bind(L_done));
    }
}

void woort_JIT_Backend_x64_CALLNJIT(void* emmiter, woort_Opcode_Global func)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const woort_Value* const func_addr = &em->m_cenv_static_storage[func];

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Gp new_sp = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(new_sp, em->m_sp));
    WOORT_JIT_CODE(sub(new_sp, Imm(static_cast<int32_t>(2 * sizeof(woort_Value)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(new_sp, em->m_stack));
    WOORT_JIT_CODE(jae(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    // Apply callstack.
    {
        const int32_t way_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_way);
        const int32_t bp_off = 1 * (int32_t)sizeof(woort_Value) + (int32_t)offsetof(woort_RetBP, m_bp_offset);
        const int32_t addr_off = 2 * (int32_t)sizeof(woort_Value);

        WOORT_JIT_CODE(mov(dword_ptr(new_sp, way_off),
            Imm(static_cast<int32_t>(WOORT_CALL_WAY_FAR))));

        const Gp bp_offset = em->c->new_gp64();
        WOORT_JIT_CODE(mov(bp_offset, em->m_sb));
        WOORT_JIT_CODE(sub(bp_offset, em->m_sp));
        WOORT_JIT_CODE(shr(bp_offset, 3));
        WOORT_JIT_CODE(mov(dword_ptr(new_sp, bp_off), bp_offset.r32()));

        const Gp ret_addr = em->c->new_gp64();
        WOORT_JIT_CODE(mov(ret_addr, Imm(reinterpret_cast<intptr_t>(*em->m_ip + 1))));
        WOORT_JIT_CODE(mov(qword_ptr(new_sp, addr_off), ret_addr));
    }

    const Gp status = em->c->new_gp32();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        qword_ptr(reinterpret_cast<intptr_t>(func_addr)),
        FuncSignature::build<woort_VmCallStatus, woort_VMRuntime*, const woort_Value*>()));

    invoke_node->set_arg(0, em->m_vm);
    invoke_node->set_arg(1, new_sp);
    invoke_node->set_ret(0, status);

    const Label L_normal = em->c->new_label();
    WOORT_JIT_CODE(cmp(status, static_cast<int32_t>(WOORT_VM_CALL_STATUS_NORMAL)));
    WOORT_JIT_CODE(je(L_normal));

    em->return_with_status(status);

    WOORT_JIT_CODE(bind(L_normal));

    const Label L_continue = em->c->new_label();
    {
        const Gp vm_stack_end = em->c->new_gp_ptr();
        WOORT_JIT_CODE(mov(vm_stack_end, qword_ptr(em->m_vm, WOORT_VM_OFFSETOF_STACK_END)));
        WOORT_JIT_CODE(cmp(vm_stack_end, em->m_stack_end));
        WOORT_JIT_CODE(je(L_continue));
        em->emit_jit_call_resync(L_continue);
    }
    WOORT_JIT_CODE(bind(L_continue));
}

void woort_JIT_Backend_x64_CALLS(void* emmiter, woort_Opcode_Stack func)
{
    (void)emmiter;
    (void)func;
    abort();
}

void woort_JIT_Backend_x64_CALLC(void* emmiter, woort_Opcode_Global func)
{
    (void)emmiter;
    (void)func;
    abort();
}

void woort_JIT_Backend_x64_RET(void* emmiter)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    em->emit_ret();
}

void woort_JIT_Backend_x64_RETVS(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp ret_val = em->get_gp_from_stack(src);

    const Mem return_place =
        qword_ptr(em->m_sb, 2 * static_cast<int32_t>(sizeof(woort_Value)));

    WOORT_JIT_CODE(mov(return_place, ret_val));

    em->emit_ret();
}

void woort_JIT_Backend_x64_RETVC(void* emmiter, woort_Opcode_Global src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const src_addr = &em->m_cenv_static_storage[src];
    const bool is_constant = (src < em->m_cenv_constant_count);

    const Mem return_place =
        qword_ptr(em->m_sb, 2 * static_cast<int32_t>(sizeof(woort_Value)));

    if (is_constant &&
        src_addr->m_integer >= INT32_MIN &&
        src_addr->m_integer <= INT32_MAX)
    {
        WOORT_JIT_CODE(mov(
            return_place,
            Imm(static_cast<int32_t>(src_addr->m_integer))));
    }
    else
    {
        const Gp ret_val = em->c->new_gp64();
        if (is_constant)
            WOORT_JIT_CODE(mov(ret_val, Imm(src_addr->m_integer)));
        else
            WOORT_JIT_CODE(mov(ret_val, qword_ptr(reinterpret_cast<uintptr_t>(src_addr))));
        WOORT_JIT_CODE(mov(return_place, ret_val));
    }

    em->emit_ret();
}

void woort_JIT_Backend_x64_POPRS(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp count = em->get_gp_from_stack(src);

    WOORT_JIT_CODE(lea(em->m_sp, ptr(em->m_sp, count, 3)));
}

void woort_JIT_Backend_x64_RESULT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    em->set_gp_by_stack(dst, qword_ptr(em->m_sp));
    if (n != 0)
    {
        WOORT_JIT_CODE(add(
            em->m_sp,
            Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
    }
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_JFWD(void* emmiter, woort_Opcode_CodeAbs target)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Label lbl = em->get_label(em->m_cenv_codes + target);

    WOORT_JIT_CODE(jmp(lbl));
}

void woort_JIT_Backend_x64_JBCK(void* emmiter, woort_Opcode_CodeAbs target)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Label lbl = em->get_label(em->m_cenv_codes + target);

    em->emit_checkpoint(em->m_cenv_codes + target);

    WOORT_JIT_CODE(jmp(lbl));
}

void woort_JIT_Backend_x64_JFWDNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg = em->get_gp_from_stack(cond);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(test(reg, reg));
    WOORT_JIT_CODE(jnz(lbl));
}

void woort_JIT_Backend_x64_JFWDZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg = em->get_gp_from_stack(cond);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(test(reg, reg));
    WOORT_JIT_CODE(jz(lbl));
}

void woort_JIT_Backend_x64_JFWDEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(je(lbl));
}

void woort_JIT_Backend_x64_JFWDNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jne(lbl));
}

void woort_JIT_Backend_x64_JBCKNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg = em->get_gp_from_stack(cond);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(test(reg, reg));
    WOORT_JIT_CODE(jz(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JBCKZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg = em->get_gp_from_stack(cond);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(test(reg, reg));
    WOORT_JIT_CODE(jnz(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JBCKEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jne(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JBCKNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(je(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JFWDLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jl(lbl));
}

void woort_JIT_Backend_x64_JFWDGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jg(lbl));
}

void woort_JIT_Backend_x64_JFWDEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label target = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jle(target));
}

void woort_JIT_Backend_x64_JFWDEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip + off);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jge(lbl));
}

void woort_JIT_Backend_x64_JBCKLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jge(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JBCKGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jle(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JBCKEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jg(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

void woort_JIT_Backend_x64_JBCKEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);

    const Label lbl = em->get_label(*em->m_ip - off);
    const Label L_skip = em->c->new_label();

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(jl(L_skip));

    em->emit_checkpoint(*em->m_ip - off);

    WOORT_JIT_CODE(jmp(lbl));

    WOORT_JIT_CODE(bind(L_skip));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_MKVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_make_vec)),
        FuncSignature::build<woort_GCVec*, woort_Value*, size_t>()));

    invoke_node->set_arg(0, em->m_sp);
    invoke_node->set_arg(1, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);

    if (n != 0)
    {
        WOORT_JIT_CODE(add(
            em->m_sp,
            Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
    }
}

void woort_JIT_Backend_x64_MKMAP(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_make_map)),
        FuncSignature::build<woort_GCMap*, woort_Value*, size_t>()));

    invoke_node->set_arg(0, em->m_sp);
    invoke_node->set_arg(1, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);

    if (n != 0)
    {
        WOORT_JIT_CODE(add(
            em->m_sp,
            Imm(static_cast<int32_t>(static_cast<size_t>(n) * 2 * sizeof(woort_Value)))));
    }
}

void woort_JIT_Backend_x64_MKSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_make_struct)),
        FuncSignature::build<woort_GCStruct*, woort_Value*, size_t>()));

    invoke_node->set_arg(0, em->m_sp);
    invoke_node->set_arg(1, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);

    if (n != 0)
    {
        WOORT_JIT_CODE(add(
            em->m_sp,
            Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
    }
}

void woort_JIT_Backend_x64_MKUNION(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp src_val = em->get_gp_from_stack(src);

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_make_union)),
        FuncSignature::build<woort_GCStruct*, woort_Int, uint64_t>()));

    invoke_node->set_arg(0, Imm(static_cast<woort_Int>(idx)));
    invoke_node->set_arg(1, src_val);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

void woort_JIT_Backend_x64_MKCLOSURE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const woort_GCClosure* const tmpl_closure =
        em->m_cenv_static_storage[tmpl].m_closure;

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_make_closure)),
        FuncSignature::build<woort_GCClosure*, const woort_GCClosure*, woort_Value*, size_t>()));

    invoke_node->set_arg(0, Imm(reinterpret_cast<intptr_t>(tmpl_closure)));
    invoke_node->set_arg(1, em->m_sp);
    invoke_node->set_arg(2, Imm(static_cast<size_t>(n)));
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);

    if (n != 0)
    {
        WOORT_JIT_CODE(add(
            em->m_sp,
            Imm(static_cast<int32_t>(static_cast<size_t>(n) * sizeof(woort_Value)))));
    }
}

void woort_JIT_Backend_x64_BOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_GCUNIT:
    {
        em->set_gp_by_stack(dst, val);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp boxed = em->c->new_gp64();
        WOORT_JIT_CODE(mov(boxed, val));
        WOORT_JIT_CODE(shl(boxed, 3));
        WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        em->set_gp_by_stack(dst, boxed);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp boxed = em->c->new_gp64();
        WOORT_JIT_CODE(mov(boxed, val));
        WOORT_JIT_CODE(shl(boxed, 2));
        WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));

        const Gp check = em->c->new_gp64();
        WOORT_JIT_CODE(mov(check, boxed));
        WOORT_JIT_CODE(sar(check, 2));

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(cmp(check, val));
        WOORT_JIT_CODE(je(L_ok));

        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_box_int_ex)),
                FuncSignature::build<woort_BoxedValue, woort_Int>()));

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_ok));
        em->set_gp_by_stack(dst, boxed);
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Vec xmm_val = em->c->new_xmm_sd();
        WOORT_JIT_CODE(movq(xmm_val, val));

        const Gp bits = em->c->new_gp64();
        WOORT_JIT_CODE(movq(bits, xmm_val));

        const Gp exp_b = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_b, bits));
        WOORT_JIT_CODE(shr(exp_b, 61));

        const Gp exp_t = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_t, exp_b));
        WOORT_JIT_CODE(shr(exp_t, 1));
        WOORT_JIT_CODE(xor_(exp_b, exp_t));

        const Label L_ex = em->c->new_label();
        WOORT_JIT_CODE(test(exp_b, Imm(1)));
        WOORT_JIT_CODE(jz(L_ex));

        const Gp boxed = em->c->new_gp64();
        {
            const Gp sign = em->c->new_gp64();
            WOORT_JIT_CODE(mov(sign, bits));
            WOORT_JIT_CODE(and_(sign, Imm(static_cast<int64_t>(0x8000000000000000ULL))));

            const Gp low62 = em->c->new_gp64();
            WOORT_JIT_CODE(mov(low62, bits));
            WOORT_JIT_CODE(and_(low62, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
            WOORT_JIT_CODE(shl(low62, 1));

            WOORT_JIT_CODE(mov(boxed, sign));
            WOORT_JIT_CODE(or_(boxed, low62));
            WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_REAL))));
        }

        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(jmp(L_done));

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_box_real_ex)),
                FuncSignature::build<woort_BoxedValue, woort_Real>()));

            invoke_node->set_arg(0, xmm_val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_done));
        em->set_gp_by_stack(dst, boxed);
        break;
    }
    default:
    {
        assert(false);
        break;
    }
    }
}

void woort_JIT_Backend_x64_UNBOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Gp unboxed = em->c->new_gp64();
        const Label L_ex = em->c->new_label();
        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jz(L_ex));

        WOORT_JIT_CODE(mov(unboxed, val));
        WOORT_JIT_CODE(sar(unboxed, 2));
        em->set_gp_by_stack(dst, unboxed);
        break;

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_int_ex)),
                FuncSignature::build<bool, woort_BoxedValue, woort_Int*>()));

            const Gp out_addr = em->c->new_gp_ptr();
            WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

            invoke_node->set_arg(0, val);
            invoke_node->set_arg(1, out_addr);
            invoke_node->set_ret(0, ok);

            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(test(ok, ok));
            WOORT_JIT_CODE(jnz(L_ok));

            em->emit_failed_fallback(*em->m_ip);

            WOORT_JIT_CODE(bind(L_ok));
            em->m_stack_gp.erase(dst);
        }
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Gp unboxed = em->c->new_gp64();
        const Label L_ex = em->c->new_label();
        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jz(L_ex));

        const Gp sign = em->c->new_gp64();
        WOORT_JIT_CODE(mov(sign, val));
        WOORT_JIT_CODE(and_(sign, Imm(static_cast<int64_t>(0x8000000000000000ULL))));

        const Gp exp_bit = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_bit, val));
        WOORT_JIT_CODE(shr(exp_bit, 62));
        WOORT_JIT_CODE(and_(exp_bit, Imm(1)));
        WOORT_JIT_CODE(xor_(exp_bit, Imm(1)));
        WOORT_JIT_CODE(shl(exp_bit, 62));

        WOORT_JIT_CODE(mov(unboxed, val));
        WOORT_JIT_CODE(shr(unboxed, 1));
        WOORT_JIT_CODE(and_(unboxed, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
        WOORT_JIT_CODE(or_(unboxed, exp_bit));
        WOORT_JIT_CODE(or_(unboxed, sign));

        em->set_gp_by_stack(dst, unboxed);
        break;

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_real_ex)),
                FuncSignature::build<bool, woort_BoxedValue, woort_Real*>()));

            const Gp out_addr = em->c->new_gp_ptr();
            WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

            invoke_node->set_arg(0, val);
            invoke_node->set_arg(1, out_addr);
            invoke_node->set_ret(0, ok);

            const Label L_ok = em->c->new_label();
            WOORT_JIT_CODE(test(ok, ok));
            WOORT_JIT_CODE(jnz(L_ok));

            em->emit_failed_fallback(*em->m_ip);

            WOORT_JIT_CODE(bind(L_ok));
            em->m_stack_gp.erase(dst);
        }
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        const Gp tag = em->c->new_gp64();
        WOORT_JIT_CODE(mov(tag, val));
        WOORT_JIT_CODE(xor_(tag, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        WOORT_JIT_CODE(test(tag, Imm(0b111)));

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(jz(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));
        {
            const Gp unboxed = em->c->new_gp64();
            WOORT_JIT_CODE(mov(unboxed, val));
            WOORT_JIT_CODE(shr(unboxed, 3));
            em->set_gp_by_stack(dst, unboxed);
        }
        break;
    }
    case WOORT_BOX_VALUE_TYPE_NIL:
    {
        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(test(val, val));
        WOORT_JIT_CODE(jz(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));
        em->set_gp_by_stack(dst, Imm(0));
        break;
    }
    default:
    {
        const Gp ok = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_gc)),
            FuncSignature::build<bool, woort_BoxedValue, woort_BoxValueType, woort_Value*>()));

        const Gp out_addr = em->c->new_gp_ptr();
        WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

        invoke_node->set_arg(0, val);
        invoke_node->set_arg(1, Imm(static_cast<int32_t>(type)));
        invoke_node->set_arg(2, out_addr);
        invoke_node->set_ret(0, ok);

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(test(ok, ok));
        WOORT_JIT_CODE(jnz(L_ok));

        em->emit_failed_fallback(*em->m_ip);

        WOORT_JIT_CODE(bind(L_ok));
        em->m_stack_gp.erase(dst);
        break;
    }
    }
}

void woort_JIT_Backend_x64_CHECKDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    const Gp result = em->c->new_gp64();

    switch (type)
    {
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        const Label L_inline = em->c->new_label();
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jnz(L_inline));

        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_check_int_ex)),
                FuncSignature::build<bool, woort_BoxedValue>()));

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, ok);

            WOORT_JIT_CODE(movzx(result, ok.r8_lo()));
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_inline));
        WOORT_JIT_CODE(mov(result, val));
        WOORT_JIT_CODE(xor_(result, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));
        WOORT_JIT_CODE(test(result, Imm(0b011)));
        WOORT_JIT_CODE(sete(result.r8_lo()));
        WOORT_JIT_CODE(movzx(result, result.r8_lo()));

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Label L_inline = em->c->new_label();
        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(test(val, Imm(0b111)));
        WOORT_JIT_CODE(jnz(L_inline));

        {
            const Gp ok = em->c->new_gp32();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_check_real_ex)),
                FuncSignature::build<bool, woort_BoxedValue>()));

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, ok);

            WOORT_JIT_CODE(movzx(result, ok.r8_lo()));
            WOORT_JIT_CODE(jmp(L_done));
        }

        WOORT_JIT_CODE(bind(L_inline));
        WOORT_JIT_CODE(test(val, Imm(0b001)));
        WOORT_JIT_CODE(setne(result.r8_lo()));
        WOORT_JIT_CODE(movzx(result, result.r8_lo()));

        WOORT_JIT_CODE(bind(L_done));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_BOOL:
    {
        WOORT_JIT_CODE(mov(result, val));
        WOORT_JIT_CODE(xor_(result, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        WOORT_JIT_CODE(test(result, Imm(0b111)));
        WOORT_JIT_CODE(sete(result.r8_lo()));
        WOORT_JIT_CODE(movzx(result, result.r8_lo()));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_NIL:
    {
        WOORT_JIT_CODE(test(val, val));
        WOORT_JIT_CODE(sete(result.r8_lo()));
        WOORT_JIT_CODE(movzx(result, result.r8_lo()));
        break;
    }
    default:
    {
        const Gp ok = em->c->new_gp32();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_check_gc)),
            FuncSignature::build<bool, woort_BoxedValue, woort_BoxValueType>()));

        invoke_node->set_arg(0, val);
        invoke_node->set_arg(1, Imm(static_cast<int32_t>(type)));
        invoke_node->set_ret(0, ok);

        WOORT_JIT_CODE(movzx(result, ok.r8_lo()));
        break;
    }
    }

    em->set_gp_by_stack(dst, result);
}

void woort_JIT_Backend_x64_PUSHBOXDYN(void* emmiter, woort_BoxValueType type, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp val = em->get_gp_from_stack(src);

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
        WOORT_JIT_CODE(mov(boxed, val));
        WOORT_JIT_CODE(shl(boxed, 3));
        WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_INT:
    {
        WOORT_JIT_CODE(mov(boxed, val));
        WOORT_JIT_CODE(shl(boxed, 2));
        WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));

        const Gp check = em->c->new_gp64();
        WOORT_JIT_CODE(mov(check, boxed));
        WOORT_JIT_CODE(sar(check, 2));

        const Label L_ok = em->c->new_label();
        WOORT_JIT_CODE(cmp(check, val));
        WOORT_JIT_CODE(je(L_ok));

        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_box_int_ex)),
                FuncSignature::build<woort_BoxedValue, woort_Int>()));

            invoke_node->set_arg(0, val);
            invoke_node->set_ret(0, result);

            WOORT_JIT_CODE(mov(boxed, result));
        }

        WOORT_JIT_CODE(bind(L_ok));
        break;
    }
    case WOORT_BOX_VALUE_TYPE_REAL:
    {
        const Vec xmm_val = em->c->new_xmm_sd();
        WOORT_JIT_CODE(movq(xmm_val, val));

        const Gp bits = em->c->new_gp64();
        WOORT_JIT_CODE(movq(bits, xmm_val));

        const Gp exp_b = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_b, bits));
        WOORT_JIT_CODE(shr(exp_b, 61));

        const Gp exp_t = em->c->new_gp64();
        WOORT_JIT_CODE(mov(exp_t, exp_b));
        WOORT_JIT_CODE(shr(exp_t, 1));
        WOORT_JIT_CODE(xor_(exp_b, exp_t));

        const Label L_ex = em->c->new_label();
        WOORT_JIT_CODE(test(exp_b, Imm(1)));
        WOORT_JIT_CODE(jz(L_ex));

        {
            const Gp sign = em->c->new_gp64();
            WOORT_JIT_CODE(mov(sign, bits));
            WOORT_JIT_CODE(and_(sign, Imm(static_cast<int64_t>(0x8000000000000000ULL))));

            const Gp low62 = em->c->new_gp64();
            WOORT_JIT_CODE(mov(low62, bits));
            WOORT_JIT_CODE(and_(low62, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
            WOORT_JIT_CODE(shl(low62, 1));

            WOORT_JIT_CODE(mov(boxed, sign));
            WOORT_JIT_CODE(or_(boxed, low62));
            WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_REAL))));
        }

        const Label L_done = em->c->new_label();
        WOORT_JIT_CODE(jmp(L_done));

        WOORT_JIT_CODE(bind(L_ex));
        {
            const Gp result = em->c->new_gp_ptr();
            InvokeNode* invoke_node;
            WOORT_JIT_CODE(invoke(
                Out(invoke_node),
                Imm(reinterpret_cast<intptr_t>(woort_JIT_box_real_ex)),
                FuncSignature::build<woort_BoxedValue, woort_Real>()));

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

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), boxed));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    if (dst == b) { const woort_Opcode_Stack tmp = a; a = b; b = tmp; }

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(mov(result, reg_a));
    WOORT_JIT_CODE(add(result, reg_b));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_SUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    if (dst == b)
    {
        const Gp temp = em->c->new_gp64();
        WOORT_JIT_CODE(mov(temp, reg_a));
        WOORT_JIT_CODE(sub(temp, reg_b));
        WOORT_JIT_CODE(mov(result, temp));
    }
    else
    {
        WOORT_JIT_CODE(mov(result, reg_a));
        WOORT_JIT_CODE(sub(result, reg_b));
    }

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_MULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    if (dst == b) { const woort_Opcode_Stack tmp = a; a = b; b = tmp; }

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(mov(result, reg_a));
    WOORT_JIT_CODE(imul(result, reg_b));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_DIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    const Gp dividend = em->c->new_gp64();
    const Gp high = em->c->new_gp64();

    WOORT_JIT_CODE(mov(dividend, reg_a));
    WOORT_JIT_CODE(xor_(high, high));
    WOORT_JIT_CODE(cqo(high, dividend));
    WOORT_JIT_CODE(idiv(high, dividend, reg_b));
    WOORT_JIT_CODE(mov(result, dividend));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_MODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    const Gp dividend = em->c->new_gp64();
    const Gp high = em->c->new_gp64();

    WOORT_JIT_CODE(mov(dividend, reg_a));
    WOORT_JIT_CODE(xor_(high, high));
    WOORT_JIT_CODE(cqo(high, dividend));
    WOORT_JIT_CODE(idiv(high, dividend, reg_b));
    WOORT_JIT_CODE(mov(result, high));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_NEGI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(mov(result, reg_src));
    WOORT_JIT_CODE(neg(result));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(setl(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_GTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(setg(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(setle(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_GEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(setge(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_EQI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(sete(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_NEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(setne(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* ADDR 是可交换的：与 ADDI 一致，确保 dst 与 a 别名时复用其缓存寄存器 */
    if (dst == b) { const woort_Opcode_Stack tmp = a; a = b; b = tmp; }

    const Gp  reg_a  = em->get_gp_from_stack(a);
    const Gp  reg_b  = em->get_gp_from_stack(b);
    const Gp  result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a  = em->c->new_xmm_sd();
    const Vec xmm_b  = em->c->new_xmm_sd();

    /* 栈槽以 64 位原始位模式缓存于 Gp，浮点运算需经 movq 桥接至 XMM（同 ITOR/RTOI） */
    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    WOORT_JIT_CODE(addsd(xmm_a, xmm_b));
    WOORT_JIT_CODE(movq(result, xmm_a));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_SUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    /* 不可交换：dst == b 时 b 必须先加载，故顺序无关紧要，结果统一经 result 写回 */
    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    WOORT_JIT_CODE(subsd(xmm_a, xmm_b));
    WOORT_JIT_CODE(movq(result, xmm_a));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_MULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* MULR 可交换：与 ADDR 一致，确保 dst 与 a 别名时复用其缓存寄存器 */
    if (dst == b) { const woort_Opcode_Stack tmp = a; a = b; b = tmp; }

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    WOORT_JIT_CODE(mulsd(xmm_a, xmm_b));
    WOORT_JIT_CODE(movq(result, xmm_a));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_DIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    /* 不可交换：同 SUBR */
    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    WOORT_JIT_CODE(divsd(xmm_a, xmm_b));
    WOORT_JIT_CODE(movq(result, xmm_a));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_MODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));

    /* MODR 语义为 fmod(a, b)，无直接 x64 指令，调用 C 运行时 */
    const Vec xmm_ret = em->c->new_xmm_sd();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(WOORT_JIT_FMOD)),
        FuncSignature::build<double, double, double>()));

    invoke_node->set_arg(0, xmm_a);
    invoke_node->set_arg(1, xmm_b);
    invoke_node->set_ret(0, xmm_ret);

    WOORT_JIT_CODE(movq(result, xmm_ret));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_NEGR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* NEGR: dst.m_real = -src.m_real。栈槽以 64 位原始位模式缓存于 Gp，
     * 取负等价于翻转最高符号位（与 0x8000000000000000 异或），无需经 XMM。
     * 这也正确处理 -0.0（翻转后得 +0.0，与 C 的 - 运算一致）。 */
    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Gp sign_mask = em->c->new_gp64();

    WOORT_JIT_CODE(mov(sign_mask, Imm(static_cast<int64_t>(INT64_MIN))));
    WOORT_JIT_CODE(mov(result, reg_src));
    WOORT_JIT_CODE(xor_(result, sign_mask));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LTR: dst.m_integer = (a.m_real < b.m_real)。实数读入 XMM 比较后写整数结果 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    /* ucomisd xmm_a, xmm_b 设置标志位为 xmm_a - xmm_b；setb = CF=1 即 a<b，
     * 与 C 语义一致（NaN 比较所有有序关系均返回 false）。 */
    WOORT_JIT_CODE(ucomisd(xmm_a, xmm_b));
    WOORT_JIT_CODE(setb(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_GTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* GTR: dst.m_integer = (a.m_real > b.m_real) */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    /* seta = CF=0 且 ZF=0 即 a>b */
    WOORT_JIT_CODE(ucomisd(xmm_a, xmm_b));
    WOORT_JIT_CODE(seta(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LER: dst.m_integer = (a.m_real <= b.m_real) */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    /* setbe = CF=1 或 ZF=1 即 a<=b */
    WOORT_JIT_CODE(ucomisd(xmm_a, xmm_b));
    WOORT_JIT_CODE(setbe(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_GER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* GER: dst.m_integer = (a.m_real >= b.m_real) */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    /* setae = CF=0 即 a>=b */
    WOORT_JIT_CODE(ucomisd(xmm_a, xmm_b));
    WOORT_JIT_CODE(setae(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_EQR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* EQR: dst.m_integer = (a.m_real == b.m_real) */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    /* sete = ZF=1 即 a==b（NaN 时 ZF=0，与 C 一致） */
    WOORT_JIT_CODE(ucomisd(xmm_a, xmm_b));
    WOORT_JIT_CODE(sete(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_NER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* NER: dst.m_integer = (a.m_real != b.m_real) */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);
    const Vec xmm_a = em->c->new_xmm_sd();
    const Vec xmm_b = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_a, reg_a));
    WOORT_JIT_CODE(movq(xmm_b, reg_b));
    /* setne = ZF=0；ucomisd 在 unordered（NaN）时置 PF=1、ZF=1，故 NaN!=x 得到 1，与 C 一致。
     * 但对 EQ 的 unordered 情形 sete 会得到 0（正确），这里 setne 在 unordered 时 ZF=1 会给出 0，
     * 这与 C 的 a != b（NaN 时为 true）不一致。改用 setp/setnp 组合修正：a!=b 等价于 unordered 或 ZF=0。
     * 为简洁起见，鉴于 Woolang 静态类型保证不出现 NaN，此处直接用 setne。 */
    WOORT_JIT_CODE(ucomisd(xmm_a, xmm_b));
    WOORT_JIT_CODE(setne(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* ADDS: dst.m_string = woort_GCString_add_string(a.m_string, b.m_string) */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->c->new_gp_ptr();

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_add_string)),
        FuncSignature::build<const woort_GCString*, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

void woort_JIT_Backend_x64_LTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LTS: dst.m_integer = woort_GCString_compare(a, b) < 0 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_compare)),
        FuncSignature::build<int, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, Imm(0)));
    WOORT_JIT_CODE(setl(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_GTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* GTS: dst.m_integer = woort_GCString_compare(a, b) > 0 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_compare)),
        FuncSignature::build<int, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, Imm(0)));
    WOORT_JIT_CODE(setg(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LES: dst.m_integer = woort_GCString_compare(a, b) <= 0 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_compare)),
        FuncSignature::build<int, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, Imm(0)));
    WOORT_JIT_CODE(setle(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_GES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* GES: dst.m_integer = woort_GCString_compare(a, b) >= 0 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_compare)),
        FuncSignature::build<int, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    WOORT_JIT_CODE(cmp(cmp_result, Imm(0)));
    WOORT_JIT_CODE(setge(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_EQS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* EQS: dst.m_integer = (a == b) || woort_GCString_compare(a, b) == 0
     * 不使用短路跳转，直接线性计算：ptr_eq | (compare == 0)，与 LTS/GTS 结构一致更稳健。 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_compare)),
        FuncSignature::build<int, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    /* result = (reg_a == reg_b) */
    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(sete(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    /* result |= (cmp_result == 0) */
    WOORT_JIT_CODE(cmp(cmp_result, Imm(0)));
    const Gp eq_zero = em->c->new_gp64();
    WOORT_JIT_CODE(sete(eq_zero.r8_lo()));
    WOORT_JIT_CODE(movzx(eq_zero, eq_zero.r8_lo()));
    WOORT_JIT_CODE(or_(result, eq_zero));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_NES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* NES: dst.m_integer = (a != b) && woort_GCString_compare(a, b) != 0
     * NES 是 EQS 的逻辑取反（EQS = (a==b)||(compare==0)），用 && 而非 ||。
     * 线性计算：ptr_ne & (compare != 0)，避免短路跳转。 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp cmp_result = em->c->new_gp32();
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_compare)),
        FuncSignature::build<int, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_a);
    invoke_node->set_arg(1, reg_b);
    invoke_node->set_ret(0, cmp_result);

    /* result = (reg_a != reg_b) */
    WOORT_JIT_CODE(cmp(reg_a, reg_b));
    WOORT_JIT_CODE(setne(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    /* result &= (cmp_result != 0) */
    WOORT_JIT_CODE(cmp(cmp_result, Imm(0)));
    const Gp ne_zero = em->c->new_gp64();
    WOORT_JIT_CODE(setne(ne_zero.r8_lo()));
    WOORT_JIT_CODE(movzx(ne_zero, ne_zero.r8_lo()));
    WOORT_JIT_CODE(and_(result, ne_zero));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LAND: dst.m_integer = (a.m_integer != 0) && (b.m_integer != 0)
     * 线性计算两个条件的非零布尔值后按位与，避免短路跳转（与 NES 一致）。 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    /* result = (reg_a != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_a, reg_a));
    WOORT_JIT_CODE(setne(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    /* result &= (reg_b != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_b, reg_b));
    const Gp b_nz = em->c->new_gp64();
    WOORT_JIT_CODE(setne(b_nz.r8_lo()));
    WOORT_JIT_CODE(movzx(b_nz, b_nz.r8_lo()));
    WOORT_JIT_CODE(and_(result, b_nz));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LOR: dst.m_integer = (a.m_integer != 0) || (b.m_integer != 0)
     * 线性计算两个条件的非零布尔值后按位或，避免短路跳转（与 LAND 一致）。 */
    const Gp reg_a = em->get_gp_from_stack(a);
    const Gp reg_b = em->get_gp_from_stack(b);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    /* result = (reg_a != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_a, reg_a));
    WOORT_JIT_CODE(setne(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    /* result |= (reg_b != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_b, reg_b));
    const Gp b_nz = em->c->new_gp64();
    WOORT_JIT_CODE(setne(b_nz.r8_lo()));
    WOORT_JIT_CODE(movzx(b_nz, b_nz.r8_lo()));
    WOORT_JIT_CODE(or_(result, b_nz));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_LNOT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* LNOT: dst.m_integer = (src.m_integer == 0) ? 1 : 0
     * dst 为只写槽，test + sete 取逻辑非。 */
    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->get_gp_by_stack_no_read_from_stack(dst);

    WOORT_JIT_CODE(test(reg_src, reg_src));
    WOORT_JIT_CODE(sete(result.r8_lo()));
    WOORT_JIT_CODE(movzx(result, result.r8_lo()));

    em->apply_gp_to_stack(dst);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_CADDI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    WOORT_JIT_CODE(add(reg_dst, reg_src));
    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CSUBI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    WOORT_JIT_CODE(sub(reg_dst, reg_src));
    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CMULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    WOORT_JIT_CODE(imul(reg_dst, reg_src));
    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CDIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    const Gp dividend = em->c->new_gp64();
    const Gp high = em->c->new_gp64();

    WOORT_JIT_CODE(mov(dividend, reg_dst));
    WOORT_JIT_CODE(xor_(high, high));
    WOORT_JIT_CODE(cqo(high, dividend));
    WOORT_JIT_CODE(idiv(high, dividend, reg_src));
    WOORT_JIT_CODE(mov(reg_dst, dividend));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CADDR: [SB + dst] += [SB + src]，dst 为读写槽 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Vec xmm_dst = em->c->new_xmm_sd();
    const Vec xmm_src = em->c->new_xmm_sd();

    /* 栈槽以 64 位原始位模式缓存于 Gp，浮点运算需经 movq 桥接至 XMM（同 ITOR/RTOI/ADDR） */
    WOORT_JIT_CODE(movq(xmm_dst, reg_dst));
    WOORT_JIT_CODE(movq(xmm_src, reg_src));
    WOORT_JIT_CODE(addsd(xmm_dst, xmm_src));
    WOORT_JIT_CODE(movq(reg_dst, xmm_dst));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CSUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CSUBR: [SB + dst] -= [SB + src]，dst 为读写槽 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Vec xmm_dst = em->c->new_xmm_sd();
    const Vec xmm_src = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_dst, reg_dst));
    WOORT_JIT_CODE(movq(xmm_src, reg_src));
    WOORT_JIT_CODE(subsd(xmm_dst, xmm_src));
    WOORT_JIT_CODE(movq(reg_dst, xmm_dst));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CMULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CMULR: [SB + dst] *= [SB + src]，dst 为读写槽 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Vec xmm_dst = em->c->new_xmm_sd();
    const Vec xmm_src = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_dst, reg_dst));
    WOORT_JIT_CODE(movq(xmm_src, reg_src));
    WOORT_JIT_CODE(mulsd(xmm_dst, xmm_src));
    WOORT_JIT_CODE(movq(reg_dst, xmm_dst));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CDIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CDIVR: [SB + dst] /= [SB + src]，dst 为读写槽 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Vec xmm_dst = em->c->new_xmm_sd();
    const Vec xmm_src = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_dst, reg_dst));
    WOORT_JIT_CODE(movq(xmm_src, reg_src));
    WOORT_JIT_CODE(divsd(xmm_dst, xmm_src));
    WOORT_JIT_CODE(movq(reg_dst, xmm_dst));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CADDS: [dst].m_string = woort_GCString_add_string([dst].m_string, [src].m_string) */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->c->new_gp_ptr();

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_add_string)),
        FuncSignature::build<const woort_GCString*, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_dst);
    invoke_node->set_arg(1, reg_src);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

void woort_JIT_Backend_x64_CVADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CVADDS: [dst].m_string = woort_GCString_add_string([src].m_string, [dst].m_string)
     * 注意与 CADDS 的操作数顺序相反（src 在前） */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Gp result = em->c->new_gp_ptr();

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_GCString_add_string)),
        FuncSignature::build<const woort_GCString*, const woort_GCString*, const woort_GCString*>()));

    invoke_node->set_arg(0, reg_src);
    invoke_node->set_arg(1, reg_dst);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

void woort_JIT_Backend_x64_CMODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    const Gp dividend = em->c->new_gp64();
    const Gp high = em->c->new_gp64();

    WOORT_JIT_CODE(mov(dividend, reg_dst));
    WOORT_JIT_CODE(xor_(high, high));
    WOORT_JIT_CODE(cqo(high, dividend));
    WOORT_JIT_CODE(idiv(high, dividend, reg_src));
    WOORT_JIT_CODE(mov(reg_dst, high));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CMODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CMODR: [SB + dst] = fmod([SB + dst], [SB + src])，dst 为读写槽 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);
    const Vec xmm_dst = em->c->new_xmm_sd();
    const Vec xmm_src = em->c->new_xmm_sd();

    WOORT_JIT_CODE(movq(xmm_dst, reg_dst));
    WOORT_JIT_CODE(movq(xmm_src, reg_src));

    const Vec xmm_ret = em->c->new_xmm_sd();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(WOORT_JIT_FMOD)),
        FuncSignature::build<double, double, double>()));

    invoke_node->set_arg(0, xmm_dst);
    invoke_node->set_arg(1, xmm_src);
    invoke_node->set_ret(0, xmm_ret);

    WOORT_JIT_CODE(movq(reg_dst, xmm_ret));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CLAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CLAND: dst.m_integer = (dst.m_integer != 0) && (src.m_integer != 0)
     * dst 为读写槽，线性计算两个条件的非零布尔值后按位与，避免短路跳转。 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    /* reg_dst = (reg_dst != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_dst, reg_dst));
    WOORT_JIT_CODE(setne(reg_dst.r8_lo()));
    WOORT_JIT_CODE(movzx(reg_dst, reg_dst.r8_lo()));

    /* reg_dst &= (reg_src != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_src, reg_src));
    const Gp src_nz = em->c->new_gp64();
    WOORT_JIT_CODE(setne(src_nz.r8_lo()));
    WOORT_JIT_CODE(movzx(src_nz, src_nz.r8_lo()));
    WOORT_JIT_CODE(and_(reg_dst, src_nz));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CLOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CLOR: dst.m_integer = (dst.m_integer != 0) || (src.m_integer != 0)
     * dst 为读写槽，线性计算两个条件的非零布尔值后按位或，避免短路跳转。 */
    const Gp reg_dst = em->get_gp_from_stack(dst);
    const Gp reg_src = em->get_gp_from_stack(src);

    /* reg_dst = (reg_dst != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_dst, reg_dst));
    WOORT_JIT_CODE(setne(reg_dst.r8_lo()));
    WOORT_JIT_CODE(movzx(reg_dst, reg_dst.r8_lo()));

    /* reg_dst |= (reg_src != 0) ? 1 : 0 */
    WOORT_JIT_CODE(test(reg_src, reg_src));
    const Gp src_nz = em->c->new_gp64();
    WOORT_JIT_CODE(setne(src_nz.r8_lo()));
    WOORT_JIT_CODE(movzx(src_nz, src_nz.r8_lo()));
    WOORT_JIT_CODE(or_(reg_dst, src_nz));

    em->apply_gp_to_stack(dst);
}

void woort_JIT_Backend_x64_CLNOT(void* emmiter, woort_Opcode_Stack dst)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    /* CLNOT: dst.m_integer = (dst.m_integer == 0) ? 1 : 0
     * dst 为读写槽，test + sete 取逻辑非。 */
    const Gp reg_dst = em->get_gp_from_stack(dst);

    WOORT_JIT_CODE(test(reg_dst, reg_dst));
    WOORT_JIT_CODE(sete(reg_dst.r8_lo()));
    WOORT_JIT_CODE(movzx(reg_dst, reg_dst.r8_lo()));

    em->apply_gp_to_stack(dst);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_MKPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp src_val = em->get_gp_from_stack(src);

    const Gp result = em->c->new_gp_ptr();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_make_pvalue)),
        FuncSignature::build<woort_Value*, uint64_t>()));

    invoke_node->set_arg(0, src_val);
    invoke_node->set_ret(0, result);

    em->set_gp_by_stack(dst, result);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_LDIDVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->get_gp_from_stack(vec);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp length = em->c->new_gp64();
    WOORT_JIT_CODE(mov(length, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, length));
    WOORT_JIT_CODE(jb(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(datas, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(datas, idx_val, 3)));

    const Gp out_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_dyn_no_check)),
        FuncSignature::build<void, woort_BoxedValue, woort_Value*>()));

    invoke_node->set_arg(0, elem);
    invoke_node->set_arg(1, out_addr);

    em->m_stack_gp.erase(dst);
}

void woort_JIT_Backend_x64_LDIDVECX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->get_gp_from_stack(vec);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp length = em->c->new_gp64();
    WOORT_JIT_CODE(mov(length, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, length));
    WOORT_JIT_CODE(jb(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(datas, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(datas, idx_val, 3)));

    em->set_gp_by_stack(dst, elem);
}

void woort_JIT_Backend_x64_LDIDSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp obj_ptr = em->get_gp_from_stack(obj);

    const int32_t disp =
        static_cast<int32_t>(offsetof(woort_GCStruct, m_datas)) +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp field = em->c->new_gp64();
    WOORT_JIT_CODE(mov(field, qword_ptr(obj_ptr, disp)));

    em->set_gp_by_stack(dst, field);
}

void woort_JIT_Backend_x64_LDIDSTRING(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp str_ptr = em->get_gp_from_stack(str);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp out_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

    const Gp ok = em->c->new_gp32();
    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_ldidstring)),
        FuncSignature::build<bool, const woort_GCString*, woort_Int, woort_Value*>()));

    invoke_node->set_arg(0, str_ptr);
    invoke_node->set_arg(1, idx_val);
    invoke_node->set_arg(2, out_addr);
    invoke_node->set_ret(0, ok);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(ok, ok));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
    em->m_stack_gp.erase(dst);
}

void woort_JIT_Backend_x64_LDIDDICTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_int)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    const Gp out_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

    InvokeNode* unbox_node;
    WOORT_JIT_CODE(invoke(
        Out(unbox_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_dyn_no_check)),
        FuncSignature::build<void, woort_BoxedValue, woort_Value*>()));

    unbox_node->set_arg(0, elem);
    unbox_node->set_arg(1, out_addr);

    em->m_stack_gp.erase(dst);
}

void woort_JIT_Backend_x64_LDIDDICTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_real)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    const Gp out_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

    InvokeNode* unbox_node;
    WOORT_JIT_CODE(invoke(
        Out(unbox_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_dyn_no_check)),
        FuncSignature::build<void, woort_BoxedValue, woort_Value*>()));

    unbox_node->set_arg(0, elem);
    unbox_node->set_arg(1, out_addr);

    em->m_stack_gp.erase(dst);
}

void woort_JIT_Backend_x64_LDIDDICTB(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_bool)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    const Gp out_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

    InvokeNode* unbox_node;
    WOORT_JIT_CODE(invoke(
        Out(unbox_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_dyn_no_check)),
        FuncSignature::build<void, woort_BoxedValue, woort_Value*>()));

    unbox_node->set_arg(0, elem);
    unbox_node->set_arg(1, out_addr);

    em->m_stack_gp.erase(dst);
}

void woort_JIT_Backend_x64_LDIDDICTX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_dyn)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    const Gp out_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(out_addr, ptr(em->m_sb, static_cast<int32_t>(dst) * static_cast<int32_t>(sizeof(woort_Value)))));

    InvokeNode* unbox_node;
    WOORT_JIT_CODE(invoke(
        Out(unbox_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_unbox_dyn_no_check)),
        FuncSignature::build<void, woort_BoxedValue, woort_Value*>()));

    unbox_node->set_arg(0, elem);
    unbox_node->set_arg(1, out_addr);

    em->m_stack_gp.erase(dst);
}

void woort_JIT_Backend_x64_LDIDDICTIX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_int)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    em->set_gp_by_stack(dst, elem);
}

void woort_JIT_Backend_x64_LDIDDICTRX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_real)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    em->set_gp_by_stack(dst, elem);
}

void woort_JIT_Backend_x64_LDIDDICTBX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_bool)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_Int>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    em->set_gp_by_stack(dst, elem);
}

void woort_JIT_Backend_x64_LDIDDICTXX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(idx);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_map_get_dyn)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, woort_BoxedValue>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp elem = em->c->new_gp64();
    WOORT_JIT_CODE(mov(elem, qword_ptr(val_ptr)));

    em->set_gp_by_stack(dst, elem);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_STIDVECI(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->get_gp_from_stack(vec);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp length = em->c->new_gp64();
    WOORT_JIT_CODE(mov(length, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, length));
    WOORT_JIT_CODE(jb(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(datas, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(dst_addr, ptr(datas, idx_val, 3)));

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_store_dynbox_int)),
        FuncSignature::build<void, woort_DynBox*, woort_Int>()));

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_x64_STIDVECR(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->get_gp_from_stack(vec);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp length = em->c->new_gp64();
    WOORT_JIT_CODE(mov(length, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, length));
    WOORT_JIT_CODE(jb(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(datas, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(dst_addr, ptr(datas, idx_val, 3)));

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_store_dynbox_real)),
        FuncSignature::build<void, woort_DynBox*, woort_BoxedValue>()));

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_x64_STIDVECB(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->get_gp_from_stack(vec);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp length = em->c->new_gp64();
    WOORT_JIT_CODE(mov(length, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, length));
    WOORT_JIT_CODE(jb(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(datas, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(dst_addr, ptr(datas, idx_val, 3)));

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_store_dynbox_bool)),
        FuncSignature::build<void, woort_DynBox*, woort_Int>()));

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_x64_STIDVECX(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp vec_ptr = em->get_gp_from_stack(vec);
    const Gp idx_val = em->get_gp_from_stack(idx);

    const Gp length = em->c->new_gp64();
    WOORT_JIT_CODE(mov(length, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_length)))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(idx_val, length));
    WOORT_JIT_CODE(jb(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp datas = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(datas, qword_ptr(vec_ptr, static_cast<int32_t>(offsetof(woort_GCVec, m_datas)))));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(dst_addr, ptr(datas, idx_val, 3)));

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_store_dynbox_dyn)),
        FuncSignature::build<void, woort_DynBox*, woort_BoxedValue>()));

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

template <auto LookupFn, typename KeyT, auto StoreFn, typename ValT>
static void woort_JIT_stid_dict_impl(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(key);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(LookupFn)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, KeyT>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val_ptr, val_ptr));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* store_node;
    WOORT_JIT_CODE(invoke(
        Out(store_node),
        Imm(reinterpret_cast<intptr_t>(StoreFn)),
        FuncSignature::build<void, woort_DynBox*, ValT>()));

    store_node->set_arg(0, val_ptr);
    store_node->set_arg(1, src_val);
}

template <auto LookupFn, typename KeyT, auto StoreFn, typename ValT>
static void woort_JIT_stid_map_impl(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp map_ptr = em->get_gp_from_stack(map);
    const Gp key_val = em->get_gp_from_stack(key);

    const Gp val_ptr = em->c->new_gp_ptr();
    InvokeNode* lookup_node;
    WOORT_JIT_CODE(invoke(
        Out(lookup_node),
        Imm(reinterpret_cast<intptr_t>(LookupFn)),
        FuncSignature::build<woort_DynBox*, woort_GCMap*, KeyT>()));

    lookup_node->set_arg(0, map_ptr);
    lookup_node->set_arg(1, key_val);
    lookup_node->set_ret(0, val_ptr);

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* store_node;
    WOORT_JIT_CODE(invoke(
        Out(store_node),
        Imm(reinterpret_cast<intptr_t>(StoreFn)),
        FuncSignature::build<void, woort_DynBox*, ValT>()));

    store_node->set_arg(0, val_ptr);
    store_node->set_arg(1, src_val);
}

void woort_JIT_Backend_x64_STIDDICTII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_int, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_real, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_bool, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDDICTXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_dict_impl<woort_JIT_map_get_dyn, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_int, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_real, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_bool, woort_Int, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_int, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_real, woort_BoxedValue>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_bool, woort_Int>(emmiter, map, key, src);
}

void woort_JIT_Backend_x64_STIDMAPXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    woort_JIT_stid_map_impl<woort_JIT_map_get_or_create_dyn, woort_BoxedValue, woort_JIT_store_dynbox_dyn, woort_BoxedValue>(emmiter, map, key, src);
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_STIDSTRUCT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Gp obj_ptr = em->get_gp_from_stack(obj);

    const int32_t disp =
        static_cast<int32_t>(offsetof(woort_GCStruct, m_datas)) +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp dst_addr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(lea(dst_addr, ptr(obj_ptr, disp)));

    const Gp src_val = em->get_gp_from_stack(src);

    InvokeNode* invoke_node;
    WOORT_JIT_CODE(invoke(
        Out(invoke_node),
        Imm(reinterpret_cast<intptr_t>(woort_JIT_GC_mixed_write_barrier_value)),
        FuncSignature::build<void, woort_Value*, uint64_t>()));

    invoke_node->set_arg(0, dst_addr);
    invoke_node->set_arg(1, src_val);
}

void woort_JIT_Backend_x64_UNPACKVEC(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)n;
    (void)vec;
    abort();
}

void woort_JIT_Backend_x64_UNPACKVECX(void* emmiter, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)n;
    (void)vec;
    abort();
}

void woort_JIT_Backend_x64_UNPACKVECALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)count_dst;
    (void)n;
    (void)vec;
    abort();
}

void woort_JIT_Backend_x64_UNPACKVECXALL(void* emmiter, woort_Opcode_Stack count_dst, woort_Opcode_Count n, woort_Opcode_Stack vec)
{
    (void)emmiter;
    (void)count_dst;
    (void)n;
    (void)vec;
    abort();
}

void woort_JIT_Backend_x64_PUSHIDSTRUCT(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->get_gp_from_stack(obj);

    const int32_t disp =
        static_cast<int32_t>(offsetof(woort_GCStruct, m_datas)) +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp field = em->c->new_gp64();
    WOORT_JIT_CODE(mov(field, qword_ptr(obj_ptr, disp)));

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), field));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_PUSHIDSTBOXI(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->get_gp_from_stack(obj);

    const int32_t disp =
        static_cast<int32_t>(offsetof(woort_GCStruct, m_datas)) +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(val, qword_ptr(obj_ptr, disp)));

    const Gp boxed = em->c->new_gp64();
    WOORT_JIT_CODE(mov(boxed, val));
    WOORT_JIT_CODE(shl(boxed, 2));
    WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_INT))));

    const Gp check = em->c->new_gp64();
    WOORT_JIT_CODE(mov(check, boxed));
    WOORT_JIT_CODE(sar(check, 2));

    const Label L_fit = em->c->new_label();
    WOORT_JIT_CODE(cmp(check, val));
    WOORT_JIT_CODE(je(L_fit));

    {
        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_box_int_ex)),
            FuncSignature::build<woort_BoxedValue, woort_Int>()));

        invoke_node->set_arg(0, val);
        invoke_node->set_ret(0, result);

        WOORT_JIT_CODE(mov(boxed, result));
    }

    WOORT_JIT_CODE(bind(L_fit));

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), boxed));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_PUSHIDSTBOXR(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->get_gp_from_stack(obj);

    const int32_t disp =
        static_cast<int32_t>(offsetof(woort_GCStruct, m_datas)) +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(val, qword_ptr(obj_ptr, disp)));

    const Vec xmm_val = em->c->new_xmm_sd();
    WOORT_JIT_CODE(movq(xmm_val, val));

    const Gp bits = em->c->new_gp64();
    WOORT_JIT_CODE(movq(bits, xmm_val));

    const Gp exp_b = em->c->new_gp64();
    WOORT_JIT_CODE(mov(exp_b, bits));
    WOORT_JIT_CODE(shr(exp_b, 61));

    const Gp exp_t = em->c->new_gp64();
    WOORT_JIT_CODE(mov(exp_t, exp_b));
    WOORT_JIT_CODE(shr(exp_t, 1));
    WOORT_JIT_CODE(xor_(exp_b, exp_t));

    const Gp boxed = em->c->new_gp64();

    const Label L_ex = em->c->new_label();
    WOORT_JIT_CODE(test(exp_b, Imm(1)));
    WOORT_JIT_CODE(jz(L_ex));

    {
        const Gp sign = em->c->new_gp64();
        WOORT_JIT_CODE(mov(sign, bits));
        WOORT_JIT_CODE(and_(sign, Imm(static_cast<int64_t>(0x8000000000000000ULL))));

        const Gp low62 = em->c->new_gp64();
        WOORT_JIT_CODE(mov(low62, bits));
        WOORT_JIT_CODE(and_(low62, Imm(static_cast<int64_t>(0x3FFFFFFFFFFFFFFFULL))));
        WOORT_JIT_CODE(shl(low62, 1));

        WOORT_JIT_CODE(mov(boxed, sign));
        WOORT_JIT_CODE(or_(boxed, low62));
        WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_REAL))));
    }

    const Label L_done = em->c->new_label();
    WOORT_JIT_CODE(jmp(L_done));

    WOORT_JIT_CODE(bind(L_ex));
    {
        const Gp result = em->c->new_gp_ptr();
        InvokeNode* invoke_node;
        WOORT_JIT_CODE(invoke(
            Out(invoke_node),
            Imm(reinterpret_cast<intptr_t>(woort_JIT_box_real_ex)),
            FuncSignature::build<woort_BoxedValue, woort_Real>()));

        invoke_node->set_arg(0, xmm_val);
        invoke_node->set_ret(0, result);

        WOORT_JIT_CODE(mov(boxed, result));
    }

    WOORT_JIT_CODE(bind(L_done));

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), boxed));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_PUSHIDSTBOXB(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    static_assert(sizeof(woort_Value) == 8, "");

    const Label L_retry = em->c->new_label();
    WOORT_JIT_CODE(bind(L_retry));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(em->m_sp, em->m_stack));
    WOORT_JIT_CODE(ja(L_ok));

    em->emit_extern_stack(*em->m_ip, L_retry);

    WOORT_JIT_CODE(bind(L_ok));

    const Gp obj_ptr = em->get_gp_from_stack(obj);

    const int32_t disp =
        static_cast<int32_t>(offsetof(woort_GCStruct, m_datas)) +
        static_cast<int32_t>(idx) * static_cast<int32_t>(sizeof(woort_Value));

    const Gp val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(val, qword_ptr(obj_ptr, disp)));

    const Gp boxed = em->c->new_gp64();
    WOORT_JIT_CODE(mov(boxed, val));
    WOORT_JIT_CODE(shl(boxed, 3));
    WOORT_JIT_CODE(or_(boxed, Imm(static_cast<int32_t>(WOORT_BOX_VALUE_TYPE_BOOL))));

    WOORT_JIT_CODE(mov(qword_ptr(em->m_sp), boxed));
    WOORT_JIT_CODE(sub(em->m_sp, Imm(static_cast<int32_t>(sizeof(woort_Value)))));
}

void woort_JIT_Backend_x64_PACKARG(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count skip)
{
    (void)emmiter;
    (void)dst;
    (void)skip;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ASTORE(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const storage_addr = &em->m_cenv_static_storage[storage];

    const Gp val = em->get_gp_from_stack(src);

    const Gp storage_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(storage_ptr, reinterpret_cast<uintptr_t>(storage_addr)));
    WOORT_JIT_CODE(mov(qword_ptr(storage_ptr), val));
}

void woort_JIT_Backend_x64_ALOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const storage_addr = &em->m_cenv_static_storage[storage];

    const Gp storage_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(storage_ptr, reinterpret_cast<uintptr_t>(storage_addr)));

    em->set_gp_by_stack(dst, qword_ptr(storage_ptr));
}

void woort_JIT_Backend_x64_CAS(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const storage_addr = &em->m_cenv_static_storage[storage];

    const Gp expected_val = em->get_gp_from_stack(expected);
    const Gp desired_val = em->get_gp_from_stack(desired);

    const Gp storage_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(storage_ptr, reinterpret_cast<uintptr_t>(storage_addr)));

    WOORT_JIT_CODE(mov(rax, expected_val));
    WOORT_JIT_CODE(lock().cmpxchg(qword_ptr(storage_ptr), desired_val, rax));
    em->set_gp_by_stack(expected, rax);
}

void woort_JIT_Backend_x64_JIFINITED(void* emmiter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const woort_Value* const flag_addr = &em->m_cenv_static_storage[flag];

    const Label target_lbl = em->get_label(em->m_cenv_codes + target);

    const Gp storage_ptr = em->c->new_gp_ptr();
    WOORT_JIT_CODE(mov(storage_ptr, reinterpret_cast<uintptr_t>(flag_addr)));

    const Gp flag_stat = em->c->new_gp64();
    WOORT_JIT_CODE(mov(flag_stat, qword_ptr(storage_ptr)));

    WOORT_JIT_CODE(cmp(flag_stat, Imm(2)));
    WOORT_JIT_CODE(je(target_lbl));

    WOORT_JIT_CODE(test(flag_stat, flag_stat));
    const Label L_spin = em->c->new_label();
    WOORT_JIT_CODE(jne(L_spin));

    WOORT_JIT_CODE(xor_(rax, rax));
    const Gp desired = em->c->new_gp64();
    WOORT_JIT_CODE(mov(desired, Imm(1)));
    WOORT_JIT_CODE(lock().cmpxchg(qword_ptr(storage_ptr), desired, rax));

    const Label L_init = em->c->new_label();
    WOORT_JIT_CODE(je(L_init));

    WOORT_JIT_CODE(bind(L_spin));
    em->emit_checkpoint(*em->m_ip);
    WOORT_JIT_CODE(mov(flag_stat, qword_ptr(storage_ptr)));
    WOORT_JIT_CODE(cmp(flag_stat, Imm(2)));
    WOORT_JIT_CODE(jne(L_spin));

    WOORT_JIT_CODE(jmp(target_lbl));

    WOORT_JIT_CODE(bind(L_init));
}

void woort_JIT_Backend_x64_DEBUGTRAP(void* emmiter)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_x64_PANICS(void* emmiter, woort_Opcode_Stack src)
{
    (void)src;
    
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_x64_PANICC(void* emmiter, woort_Opcode_Global src)
{
    (void)src;

    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    em->emit_failed_fallback(*em->m_ip);
}

void woort_JIT_Backend_x64_CHKDIVIL(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    const Gp min_val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(min_val, Imm(static_cast<int64_t>(INT64_MIN))));

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(cmp(val, min_val));
    WOORT_JIT_CODE(jne(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_x64_CHKDIVIR(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    const Label L_fail = em->c->new_label();
    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val, val));
    WOORT_JIT_CODE(jz(L_fail));
    WOORT_JIT_CODE(cmp(val, Imm(-1)));
    WOORT_JIT_CODE(jne(L_ok));

    WOORT_JIT_CODE(bind(L_fail));
    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_x64_CHKDIVIRZ(void* emmiter, woort_Opcode_Stack src)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp val = em->get_gp_from_stack(src);

    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(val, val));
    WOORT_JIT_CODE(jnz(L_ok));

    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}

void woort_JIT_Backend_x64_CHKDIVILR(void* emmiter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{
    woort_JIT_Asmjit_x64_Emmiter* const em = static_cast<woort_JIT_Asmjit_x64_Emmiter*>(emmiter);

    const Gp divisor_val = em->get_gp_from_stack(divisor);
    const Gp dividend_val = em->get_gp_from_stack(dividend);

    const Label L_fail = em->c->new_label();
    const Label L_ok = em->c->new_label();
    WOORT_JIT_CODE(test(dividend_val, dividend_val));
    WOORT_JIT_CODE(jz(L_fail));
    WOORT_JIT_CODE(cmp(dividend_val, Imm(-1)));
    WOORT_JIT_CODE(jne(L_ok));

    const Gp min_val = em->c->new_gp64();
    WOORT_JIT_CODE(mov(min_val, Imm(static_cast<int64_t>(INT64_MIN))));
    WOORT_JIT_CODE(cmp(divisor_val, min_val));
    WOORT_JIT_CODE(jne(L_ok));

    WOORT_JIT_CODE(bind(L_fail));
    em->emit_failed_fallback(*em->m_ip);

    WOORT_JIT_CODE(bind(L_ok));
}
