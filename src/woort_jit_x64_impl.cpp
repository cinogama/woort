#include "woort_jit_x64_bridge.h"
#include "woort_jit_bridge.h"
#include "woort_value_types.h"

#include "woomem.h"

#include "asmjit/x86.h"

#include <new>
#include <memory>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <optional>

using namespace std;
using namespace asmjit;
using namespace asmjit::x86;

#define WOORT_JIT_CODE(CMD) em->update_last_error(em->c->CMD)

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
    (void)emmiter;
    (void)dst;
    (void)target;
    (void)src;
    abort();
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
        em->emit_checkpoint(*em->m_ip + 1);
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
    (void)emmiter;
    (void)cond;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JFWDZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JFWDEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JFWDNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKNZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKZ(void* emmiter, woort_Opcode_Stack cond, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)cond;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKNEQ(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JFWDLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JFWDGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
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
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKLT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKGT(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKEL(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

void woort_JIT_Backend_x64_JBCKEG(void* emmiter, woort_Opcode_Stack a, woort_Opcode_Stack b, woort_Opcode_CodeDiff off)
{
    (void)emmiter;
    (void)a;
    (void)b;
    (void)off;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_MKVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
    abort();
}

void woort_JIT_Backend_x64_MKMAP(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
    abort();
}

void woort_JIT_Backend_x64_MKSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n)
{
    (void)emmiter;
    (void)dst;
    (void)n;
    abort();
}

void woort_JIT_Backend_x64_MKUNION(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)idx;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_MKCLOSURE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count n, woort_Opcode_Global tmpl)
{
    (void)emmiter;
    (void)dst;
    (void)n;
    (void)tmpl;
    abort();
}

void woort_JIT_Backend_x64_BOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_UNBOXDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CHECKDYN(void* emmiter, woort_Opcode_Stack dst, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)type;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_PUSHBOXDYN(void* emmiter, woort_BoxValueType type, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)type;
    (void)src;
    abort();
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
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_LTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_GTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_GEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_EQI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_NEI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_SUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_MULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_DIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_MODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_NEGR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_LTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_GTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_GER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_EQR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_NER(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_ADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_GTS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_GES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_EQS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_NES(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack a, woort_Opcode_Stack b)
{
    (void)emmiter;
    (void)dst;
    (void)a;
    (void)b;
    abort();
}

void woort_JIT_Backend_x64_LNOT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
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
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CMULI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CDIVI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CADDR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CSUBR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CMULR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CDIVR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CVADDS(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CMODI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CMODR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CLAND(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CLOR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CLNOT(void* emmiter, woort_Opcode_Stack dst)
{
    (void)emmiter;
    (void)dst;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_MKPVALUE(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)dst;
    (void)src;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_LDIDXVEC(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)vec;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXVECX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack vec, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)vec;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDSTRUCT(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)dst;
    (void)idx;
    (void)obj;
    abort();
}

void woort_JIT_Backend_x64_LDIDSTRING(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack str, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)str;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTI(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTR(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTB(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTIX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTRX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTBX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

void woort_JIT_Backend_x64_LDIDXDICTXX(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Stack map, woort_Opcode_Stack idx)
{
    (void)emmiter;
    (void)dst;
    (void)map;
    (void)idx;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_STIDXVECI(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXVECR(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXVECB(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXVECX(void* emmiter, woort_Opcode_Stack vec, woort_Opcode_Stack idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)vec;
    (void)idx;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXDICTXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPII(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPIR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPIB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPIX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPRI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPRR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPRB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPRX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPBI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPBR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPBB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPBX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPXI(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPXR(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPXB(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDXMAPXX(void* emmiter, woort_Opcode_Stack map, woort_Opcode_Stack key, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)map;
    (void)key;
    (void)src;
    abort();
}

/* -------------------------------------------------------------------------- */

void woort_JIT_Backend_x64_STIDSTRUCT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)obj;
    (void)idx;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_STIDSTRUCTEXT(void* emmiter, woort_Opcode_Stack obj, woort_Opcode_Count idx, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)obj;
    (void)idx;
    (void)src;
    abort();
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

void woort_JIT_Backend_x64_PUSHIDXSTRUCT(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
    abort();
}

void woort_JIT_Backend_x64_PUSHIDXSTBOXI(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
    abort();
}

void woort_JIT_Backend_x64_PUSHIDXSTBOXR(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
    abort();
}

void woort_JIT_Backend_x64_PUSHIDXSTBOXB(void* emmiter, woort_Opcode_Count idx, woort_Opcode_Stack obj)
{
    (void)emmiter;
    (void)idx;
    (void)obj;
    abort();
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
    (void)emmiter;
    (void)storage;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_ALOAD(void* emmiter, woort_Opcode_Stack dst, woort_Opcode_Global storage)
{
    (void)emmiter;
    (void)dst;
    (void)storage;
    abort();
}

void woort_JIT_Backend_x64_CAS(void* emmiter, woort_Opcode_Global storage, woort_Opcode_Stack desired, woort_Opcode_Stack expected)
{
    (void)emmiter;
    (void)storage;
    (void)desired;
    (void)expected;
    abort();
}

void woort_JIT_Backend_x64_JIFINITED(void* emmiter, woort_Opcode_Global flag, woort_Opcode_CodeAbs target)
{
    (void)emmiter;
    (void)flag;
    (void)target;
    abort();
}

void woort_JIT_Backend_x64_DEBUGTRAP(void* emmiter)
{
    (void)emmiter;
    abort();
}

void woort_JIT_Backend_x64_PANICS(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_PANICC(void* emmiter, woort_Opcode_Global src)
{
    (void)emmiter;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CHKDIVIL(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CHKDIVIR(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CHKDIVIRZ(void* emmiter, woort_Opcode_Stack src)
{
    (void)emmiter;
    (void)src;
    abort();
}

void woort_JIT_Backend_x64_CHKDIVILR(void* emmiter, woort_Opcode_Stack divisor, woort_Opcode_Stack dividend)
{
    (void)emmiter;
    (void)divisor;
    (void)dividend;
    abort();
}
