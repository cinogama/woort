#include "woort.h"

#include "woomem.h"

#include "woort_vm.h"
#include "woort_threads.h"
#include "woort_value.h"
#include "woort_log.h"
#include "woort_codeenv.h"
#include "woort_opcode.h"
#include "woort_hashmap.h"
#include "woort_gc.h"

#include <assert.h>
#include <stdlib.h>
#include <memory.h>

WOORT_THREAD_LOCAL woort_VMRuntime* t_this_thread_vm = NULL;

const size_t WOORT_VM_DEFAULT_STACK_BEGIN_SIZE = 32;
const size_t WOORT_VM_MAX_STACK_SIZE = 1024 * 1024 * 1024 / 8;

void _woort_VMRuntime_destroy(woort_VMRuntime* vm)
{
    if (vm->m_stack != NULL)
        free(vm->m_stack);

    if (vm->m_hangup_mx != NULL)
        woort_mutex_destroy(vm->m_hangup_mx);

    if (vm->m_hangup_cv != NULL)
        woort_condition_variable_destroy(vm->m_hangup_cv);
}

WOORT_NODISCARD bool woort_VMRuntime_create(woort_VMRuntime** out_vm)
{
    woort_VMRuntime* vm = malloc(sizeof(woort_VMRuntime));
    if (vm == NULL)
    {
        WOORT_DEBUG("Out of memory");
        return false;
    }

    vm->m_hangup_c = 0;

    if (!woort_mutex_create(&vm->m_hangup_mx))
        vm->m_hangup_mx = NULL;

    if (!woort_condition_variable_create(&vm->m_hangup_cv))
        vm->m_hangup_cv = NULL;

    // Init stack state.
    vm->m_stack_realloc_version = 0;
    vm->m_stack =
        malloc(WOORT_VM_DEFAULT_STACK_BEGIN_SIZE * sizeof(woort_Value));

    if (vm->m_stack == NULL
        || vm->m_hangup_mx == NULL
        || vm->m_hangup_cv == NULL)
    {
        WOORT_DEBUG("Out of memory");
        goto _label_failed_to_init;
    }

    vm->m_stack_end = vm->m_stack + WOORT_VM_DEFAULT_STACK_BEGIN_SIZE;
    vm->m_sb = vm->m_sp = vm->m_stack_end - 1;

    // Init runtime state.
    vm->m_ip = NULL;
    vm->m_env = NULL;
    woort_atomic_store_explicit(
        &vm->m_check_request_mask,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    if (!woort_GC_register_root_vm(vm))
        // Failed to register root.
        goto _label_failed_to_init;

    *out_vm = vm;
    return true;

_label_failed_to_init:
    _woort_VMRuntime_destroy(vm);
    return false;
}
void woort_VMRuntime_destroy(woort_VMRuntime* vm)
{
    woort_GC_unregister_root_vm(vm);
    _woort_VMRuntime_destroy(vm);
}

WOORT_NODISCARD woort_VmCallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm);

WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func)
{
    if (!woort_CodeEnv_find(func, &vm->m_env))
        return WOORT_VM_CALL_STATUS_ABORTED;

    // Push call stack info here.
    /*
        [  SP AFTER CALL ]
        [  CALL CONTEXT  ] ==> {
        [ CLOSUER UNPACK ]          [   RETURN ADDRESS    ]
        [   ARGUMENTS    ]          [ CALLSTACK TYPE & BP ]
                                }
    */

    // Reserve sp
    vm->m_sp -= 3;

    // Set call way and bp offset.
    vm->m_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_FROM_NATIVE;
    vm->m_sp[1].m_ret_bp.m_bp_offset =
        (uint32_t)(vm->m_stack_end - vm->m_sb);

    // Set ret addr (Only for trace).
    vm->m_sp[2].m_ret_addr = vm->m_ip /* trace from current. */;

    // Sync bp to sp.
    vm->m_sb = vm->m_sp;

    // Set target ip.
    vm->m_ip = func;

    return _woort_VMRuntime_dispatch(vm);
}

void _woort_VMRuntime_request_checkpoint(woort_VMRuntime* vm)
{
    const uint32_t request_mask = woort_atomic_load_explicit(
        &vm->m_check_request_mask,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);

    if (request_mask != 0)
    {
        if (request_mask & WOORT_VMRUNTIME_CHECK_REQUEST_ABORT)
        {
            TODO;
        }
        else
        {
            woort_panic(
                WOORT_PANIC_BAD_VM_REQUEST,
                "Bad vm request: %x",
                request_mask);
        }
    }
}

bool _woort_VMRuntime_extern_stack(woort_VMRuntime* vm)
{
    const size_t current_stack_size = vm->m_stack_end - vm->m_stack;
    if (current_stack_size >= WOORT_VM_MAX_STACK_SIZE)
    {
        // Too big...
        WOORT_DEBUG("Cannot extern stack, too big.");
        return false;
    }

    const size_t new_stack_size = current_stack_size * 2;
    woort_Value* const new_stack =
        malloc(new_stack_size * sizeof(woort_Value));

    if (new_stack == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }

    // Move stack data from head to tail.
    memcpy(new_stack + current_stack_size, vm->m_stack, current_stack_size);
    free(vm->m_stack);

    // Update vm state.
    woort_Value* const new_stack_end = new_stack + new_stack_size;
    vm->m_sp = new_stack_end - (vm->m_stack_end - vm->m_sp);
    vm->m_sb = new_stack_end - (vm->m_stack_end - vm->m_sb);
    vm->m_stack = new_stack;
    vm->m_stack_end = new_stack_end;

    // Update stack version.
    ++vm->m_stack_realloc_version;

    return true;
}

void woort_VMRuntime_hangup(woort_VMRuntime* vm)
{
    woort_mutex_lock(vm->m_hangup_mx);
    ++vm->m_hangup_c;
    do
    {
        if (0 == vm->m_hangup_c)
            break;

        woort_condition_variable_wait(vm->m_hangup_cv, vm->m_hangup_mx);
    } while (true);
    woort_mutex_unlock(vm->m_hangup_mx);
}

void woort_VMRuntime_wakeup(woort_VMRuntime* vm)
{
    woort_mutex_lock(vm->m_hangup_mx);
    do
    {
        --vm->m_hangup_c;
        woort_condition_variable_signal(vm->m_hangup_cv);

    } while (0);
    woort_mutex_unlock(vm->m_hangup_mx);
}

WOORT_NODISCARD woort_VmCallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm)
{
    assert(vm->m_ip != NULL);

    /*
    SYNC (正同步):
    正同步，即将当前执行状态同步到虚拟机实例上，以便外部观测。
    以下情况发生时，需要执行一次正同步：
        1) 脱离当前虚拟机循环（任务中断，或者结束）
        2) 虚拟机发生异常
        3）调用非 JIT 的本机函数
        4) 本机函数（包含 JIT和非JIT 函数）在返回 RESYNC 请求之前
    执行正同步时，需要同步 ip, sb, sp；

    RESYNC（反同步）:
    反同步，放弃当前虚拟机状态，从虚拟机实例上重新提取。
    以下情况发生时，需要执行反同步：
        1) 调用本机函数（包含 JIT和非JIT 函数）返回 RESYNC 请求
    执行反同步时，需要从实例获取 ip, sb, sp 和 env，同时，更新
    rt_env_code，rt_env_code_end 和 rt_env_data
    */
#define WOORT_VM_SYNC_STATE()                   \
    do{                                         \
        vm->m_ip = rt_ip;                       \
        vm->m_sp = rt_sp;                       \
        vm->m_sb = rt_sb;                       \
    }while(0)
#define WOORT_VM_RESYNC_STATE()                 \
    do{                                         \
        rt_ip = vm->m_ip;                       \
        rt_stack = vm->m_stack;                 \
        rt_stack_end = vm->m_stack_end;         \
        rt_sp = vm->m_sp;                       \
        rt_sb = vm->m_sb;                       \
        rt_env = vm->m_env;                     \
        rt_env_code = rt_env->m_code_begin;     \
        rt_env_code_end = rt_env->m_code_end;   \
        rt_env_data = rt_env->m_data_begin;     \
    }while(0)
#define WOORT_VM_SYNC_STATE_AND_PANIC(...)  \
    do{                                     \
        WOORT_VM_SYNC_STATE();              \
        woort_panic(__VA_ARGS__);           \
    }while(0)
#define WOORT_VM_CHECK_STACK_VERSION_AND_RESYNC_STACK_STATE(OLD_VERSION)    \
    do{                                                                     \
        if (/* Unlikely */ OLD_VERSION != vm->m_stack_realloc_version)      \
        {                                                                   \
            /* Stack updated during native function. */                 \
            rt_sp = vm->m_stack_end - (rt_stack_end - rt_sp);               \
            rt_sb = vm->m_stack_end - (rt_stack_end - rt_sb);               \
            rt_stack = vm->m_stack;                                         \
            rt_stack_end = vm->m_stack_end;                                 \
        }                                                                   \
    }while(0)

#define WOORT_VM_THROW(NAME)                    \
    do{                                         \
        WOORT_VM_SYNC_STATE();                  \
        goto _label_exception_handler_##NAME;   \
    }while(0)
#define WOORT_VM_HANDLED() \
    do{                                         \
        WOORT_VM_RESYNC_STATE();                \
        goto _label_continue_execution;         \
    }while(0)
#define WOORT_VM_CHECKPOINT()                               \
    do {                                                    \
        if (/* Unlikely */ 0 != woort_atomic_load_explicit( \
            &vm->m_check_request_mask,                      \
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED))             \
        {                                                   \
            WOORT_VM_THROW(checkpoint);                     \
        }                                                   \
    } while (0)

    const woort_Bytecode* rt_ip = vm->m_ip;

    const woort_CodeEnv* rt_env = vm->m_env;
    const woort_Bytecode* rt_env_code = rt_env->m_code_begin;
    const woort_Bytecode* rt_env_code_end = rt_env->m_code_end;
    woort_Value* rt_env_data = rt_env->m_data_begin;

    woort_Value* rt_stack = vm->m_stack;
    woort_Value* rt_stack_end = vm->m_stack_end;
    woort_Value* rt_sp = vm->m_sp;
    woort_Value* rt_sb = vm->m_sb;

    WOORT_VM_CHECKPOINT();

    // Ok
_label_continue_execution:
    for (;;)
    {
#define WOORT_VM_CASE_OP6_M2(CODE, MODE)    \
    woort_OpcodeFormal_OP6_M2_cons(CODE, MODE)
#define WOORT_VM_CASE_OP6(CODE)             \
    WOORT_VM_CASE_OP6_M2(CODE, 0):          \
    case WOORT_VM_CASE_OP6_M2(CODE, 1):     \
    case WOORT_VM_CASE_OP6_M2(CODE, 2):     \
    case WOORT_VM_CASE_OP6_M2(CODE, 3)

        register const woort_Bytecode c = *rt_ip;
        switch (WOORT_BYTECODE_OPM8_MASK & c)
        {
            // LOAD
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_LOAD):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)] =
                rt_env_data[WOORT_BYTECODE(MAB18, c)];
            break;
        }
        // STORE
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_STORE):
        {
            rt_env_data[WOORT_BYTECODE(MAB18, c)] =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)];
            break;
        }
        // LOADEX
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_LOADEX):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)] =
                rt_env_data[rt_ip[1]];

            rt_ip += 2;
            continue;
        }
        // STOREEX
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_STOREEX):
        {
            rt_env_data[rt_ip[1]] =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];

            rt_ip += 2;
            continue;
        }
        // MOVLD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_MOV, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)]
                = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
            break;
        }
        // MOVST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_MOV, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)]
                = rt_sb[(int8_t)WOORT_BYTECODE(A8, c)];
            break;
        }
        // MOVLDEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_MOV, 2):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)]
                = rt_sb[(int32_t)rt_ip[1]];

            rt_ip += 2;
            continue;
        }
        // MOVSTEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_MOV, 3):
        {
            rt_sb[(int32_t)rt_ip[1]]
                = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];

            rt_ip += 2;
            continue;
        }
        // PUSHRCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHCHK, 0):
        {
            // PUSH RESERVE STACK
            const uint32_t reserve_stack_sz = WOORT_BYTECODE(ABC24, c);

            rt_sp -= reserve_stack_sz;
            if (rt_sp >= rt_stack)
                break;

            rt_sp += reserve_stack_sz;
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHSCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHCHK, 1):
        {
            if (rt_sp >= rt_stack)
            {
                *(rt_sp--) = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHCCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHCHK, 2):
        {
            if (rt_sp >= rt_stack)
            {
                *(rt_sp--) = rt_env_data[WOORT_BYTECODE(ABC24, c)];
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHCCHKEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHCHK, 3):
        {
            if (rt_sp >= rt_stack)
            {
                *(rt_sp--) = rt_env_data[rt_ip[1]];

                rt_ip += 2;
                continue;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // ASSURESSZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 0):
        {
            if (rt_sp - WOORT_BYTECODE(ABC24, c) >= rt_stack)
                break;

            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHSCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 1):
        {
            assert(rt_sp >= rt_stack);

            *(rt_sp--) = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
            break;
        }
        // PUSHCCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 2):
        {
            assert(rt_sp >= rt_stack);

            *(rt_sp--) = rt_env_data[WOORT_BYTECODE(ABC24, c)];
            break;
        }
        // PUSHCCHKEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 3):
        {
            assert(rt_sp >= rt_stack);

            *(rt_sp--) = rt_env_data[rt_ip[1]];

            rt_ip += 2;
            continue;
        }
        // POPR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 0):
        {
            rt_sp += WOORT_BYTECODE(ABC24, c);

            assert(rt_sp <= rt_sb);
            break;
        }
        // POPS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)] = *(++rt_sp);

            assert(rt_sp <= rt_sb);
            break;
        }
        // POPC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 2):
        {
            rt_env_data[WOORT_BYTECODE(ABC24, c)] = *(++rt_sp);

            assert(rt_sp <= rt_sb);
            break;
        }
        // POPCEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 3):
        {
            rt_env_data[rt_ip[1]] = *(++rt_sp);

            assert(rt_sp <= rt_sb);

            rt_ip += 2;
            continue;
        }
        // TODO: WOORT_OPCODE_CASTI
        // TODO: WOORT_OPCODE_CASTR
        // TODO: WOORT_OPCODE_CASTS

        // CALLNWO
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_CALLNWO):
        {
            rt_sp -= 2;
            if (rt_sp >= rt_stack)
            {
                rt_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_NEAR;
                rt_sp[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                rt_sp[2].m_ret_addr = rt_ip + 1;

                rt_sb = rt_sp;

                rt_ip = rt_env_data[WOORT_BYTECODE(MABC26, c)].m_script_function;
                continue;
            }

            rt_sp += 2;
            WOORT_VM_THROW(stack_overflow);
        }
        // CALLNFP
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_CALLNFP):
        {
            rt_sp -= 2;
            if (rt_sp >= rt_stack)
            {
                rt_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_NEAR;
                rt_sp[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                rt_sp[2].m_ret_addr = rt_ip + 1;

                rt_sb = rt_sp;

                const woort_NativeFunction native_function =
                    rt_env_data[WOORT_BYTECODE(MABC26, c)].m_native_or_jit_function;

                // 设置 rt_ip，这样可以追踪到完整的调用栈
                rt_ip = (const woort_Bytecode*)native_function;

                WOORT_VM_SYNC_STATE();

                const uint32_t stack_version_before_native_call =
                    vm->m_stack_realloc_version;

                const woort_VmCallStatus status = native_function(
                    vm, (woort_value*)(rt_sp + 3));

                /*
                ATTENTION:
                    本机调用发生之后，只可能返回到当前调用栈所在的虚拟机函数；
                不必考虑 rt_env 改变的情况，因为即便 rt_env 发生改变，回
                到此处时，也应当回到旧的 rt_env，所以不需要更新它们。

                    但是，栈空间完全可能在本机调用期间发生改变，在旧版本（1.15
                之前）的 Woolang 中，栈空间的更新由调用方负责检查和标记：
                现在这部分工作由被调用方负责。
                */
                WOORT_VM_CHECK_STACK_VERSION_AND_RESYNC_STACK_STATE(
                    stack_version_before_native_call);

                // Restore return place.
                rt_ip = rt_sb[2].m_ret_addr;

                if (status == WOORT_VM_CALL_STATUS_NORMAL)
                {
                    // Ok, continue execute.
                    continue;
                }
                return status;
            }

            rt_sp += 2;
            WOORT_VM_THROW(stack_overflow);
        }
        // CALLNJIT
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_CALLNJIT):
        {
            rt_sp -= 2;
            if (rt_sp >= rt_stack)
            {
                rt_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_FAR;
                rt_sp[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                rt_sp[2].m_ret_addr = rt_ip + 1;

                rt_sb = rt_sp;

                const woort_NativeFunction jit_function =
                    rt_env_data[WOORT_BYTECODE(MABC26, c)].m_native_or_jit_function;

                const woort_VmCallStatus status =
                    jit_function(vm, (woort_value*)(rt_sp + 3));

                switch (status)
                {
                case WOORT_VM_CALL_STATUS_RESYNC:
                    WOORT_VM_RESYNC_STATE();
                    break;
                case WOORT_VM_CALL_STATUS_NORMAL:
                    break;
                default:
                    return status;
                }

                // Ok, continue execute.
                break;
            }

            rt_sp += 2;
            WOORT_VM_THROW(stack_overflow);
        }
        // TODO: WOORT_OPCODE_CALL

        // RET
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 0):
        {
            rt_sp = rt_sb;
            rt_sb = rt_stack_end - rt_sp[1].m_ret_bp.m_bp_offset;
            rt_ip = rt_sp[2].m_ret_addr;

            switch (rt_sp[1].m_ret_bp.m_way)
            {
            case WOORT_CALL_WAY_NEAR:
                break;
            case WOORT_CALL_WAY_FROM_NATIVE:
                return WOORT_VM_CALL_STATUS_NORMAL;
            case WOORT_CALL_WAY_FAR:
            {
                // Try resync far ip.
                if (rt_ip < rt_env_code || rt_ip >= rt_env_code_end)
                {
                    ++rt_ip;
                    WOORT_VM_THROW(env_updated);
                }
                break;
            }
            default:
                // Cannot be here.
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_CALLSTACK,
                    "Bad callstack, unexpected call way(%x).",
                    (uint32_t)rt_sp[1].m_ret_bp.m_way);
            }
            break;
        }
        // RETVS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 1):
        {
            rt_sp = rt_sb;
            rt_sb = rt_stack_end - rt_sp[1].m_ret_bp.m_bp_offset;
            rt_ip = rt_sp[2].m_ret_addr;

            /* 此处使用 rt_sp 寻址，因为这是上一层调用栈的 bp */
            rt_sp[2] = rt_sp[(int16_t)WOORT_BYTECODE(BC16, c)];

            switch (rt_sp[1].m_ret_bp.m_way)
            {
            case WOORT_CALL_WAY_NEAR:
                break;
            case WOORT_CALL_WAY_FROM_NATIVE:
                return WOORT_VM_CALL_STATUS_NORMAL;
            case WOORT_CALL_WAY_FAR:
            {
                // Try resync far ip.
                if (rt_ip < rt_env_code || rt_ip >= rt_env_code_end)
                {
                    ++rt_ip;
                    WOORT_VM_THROW(env_updated);
                }
                break;
            }
            default:
                // Cannot be here.
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_CALLSTACK,
                    "Bad callstack, unexpected call way(%x).",
                    (uint32_t)rt_sp[1].m_ret_bp.m_way);
            }
            break;
        }
        // RETVC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 2):
        {
            rt_sp = rt_sb;
            rt_sb = rt_stack_end - rt_sp[1].m_ret_bp.m_bp_offset;
            rt_ip = rt_sp[2].m_ret_addr;

            rt_sp[2] = rt_env_data[WOORT_BYTECODE(ABC24, c)];

            switch (rt_sp[1].m_ret_bp.m_way)
            {
            case WOORT_CALL_WAY_NEAR:
                break;
            case WOORT_CALL_WAY_FROM_NATIVE:
                return WOORT_VM_CALL_STATUS_NORMAL;
            case WOORT_CALL_WAY_FAR:
            {
                // Try resync far ip.
                if (rt_ip < rt_env_code || rt_ip >= rt_env_code_end)
                {
                    ++rt_ip;
                    WOORT_VM_THROW(env_updated);
                }
                break;
            }
            default:
                // Cannot be here.
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_CALLSTACK,
                    "Bad callstack, unexpected call way(%x).",
                    (uint32_t)rt_sp[1].m_ret_bp.m_way);
            }
            break;
        }
        // RESULT
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_RESULT):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)] = rt_sp[2];
            rt_sp += 2 + WOORT_BYTECODE(MA10, c);

            assert(rt_sp <= rt_sb);

            break;
        }
        // JMPF
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_JMP):
        {
            rt_ip = rt_env_code + WOORT_BYTECODE(MABC26, c);
            continue;
        }
        // JMPB
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_JMPGC):
        {
            rt_ip = rt_env_code + WOORT_BYTECODE(MABC26, c);
            WOORT_VM_CHECKPOINT();
            continue;
        }
        // JFCONDNZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCOND, 0):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer != 0)
            {
                rt_ip += WOORT_BYTECODE(BC16, c);
                continue;
            }
            break;
        }
        // JFCONDZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCOND, 1):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer == 0)
            {
                rt_ip += WOORT_BYTECODE(BC16, c);
                continue;
            }
            break;
        }
        // JFCONDEQ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCOND, 2):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                == rt_sp[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JFCONDNE
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCOND, 3):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                != rt_sp[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JBCONDNZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCONDGC, 0):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer != 0)
            {
                rt_ip -= WOORT_BYTECODE(BC16, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCONDZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCONDGC, 1):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer == 0)
            {
                rt_ip -= WOORT_BYTECODE(BC16, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCONDEQ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCONDGC, 2):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                == rt_sp[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCONDNE
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JCONDGC, 3):
        {
            if (rt_sp[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                != rt_sp[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // TODO: WOORT_OPCODE_CONS
        // TODO: WOORT_OPCODE_CONSEX
        // TODO: WOORT_OPCODE_MKCLOS

        //// BOXDYN
        //case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 0):
        //{
        //    woort_DynBox_box(
        //        (woort_DynBox_ValueType)WOORT_BYTECODE(A8, c),
        //        rt_sb[(int8_t)WOORT_BYTECODE(B8, c)],
        //        &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

        //    break;
        //}
        //// UNBOXDYN
        //case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 1):
        //{
        //    if (woort_DynBox_try_unbox(
        //        (woort_DynBox_ValueType)WOORT_BYTECODE(A8, c),
        //        rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic,
        //        &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]))
        //    {
        //        // Type matched.
        //        break;
        //    }

        //    // Bad type.
        //    WOORT_VM_THROW(bad_type);
        //}
        //// CHECKDYN
        //case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 2):
        //{
        //    rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
        //        woort_DynBox_check(
        //            rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic,
        //            (woort_DynBox_ValueType)WOORT_BYTECODE(A8, c));
        //    break;
        //}
        //// PUSHDYN
        //case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 3):
        //{
        //    woort_DynBox_box(
        //        (woort_DynBox_ValueType)WOORT_BYTECODE(A8, c),
        //        rt_sb[(int8_t)WOORT_BYTECODE(B8, c)],
        //        rt_sp++);

        //    break;
        //}
        default:
            // Unknown bytecode command.
            WOORT_VM_THROW(bad_command);
        }

        // Move forward to next command.
        ++rt_ip;
    }

    // Ok, finished.
    WOORT_VM_SYNC_STATE();
    return WOORT_VM_CALL_STATUS_NORMAL;

#define WOORT_VM_EXCEPTION_LABEL(NAME) _label_exception_handler_##NAME
    WOORT_VM_EXCEPTION_LABEL(checkpoint) :
    {
        _woort_VMRuntime_request_checkpoint(vm);
        WOORT_VM_HANDLED();
    }
    WOORT_VM_EXCEPTION_LABEL(stack_overflow) :
    {
        // Stack used up, try extern.
        if (/* UNLIKELY */ !_woort_VMRuntime_extern_stack(vm))
        {
            WOORT_VM_SYNC_STATE_AND_PANIC(
                WOORT_PANIC_STACK_OVERFLOW,
                "Stack overflow.");
        }
        WOORT_VM_HANDLED();
    }
    WOORT_VM_EXCEPTION_LABEL(env_updated) :
    {
        if (/* UNLIKELY */ !woort_CodeEnv_find(vm->m_ip, &vm->m_env))
        {
            WOORT_VM_SYNC_STATE_AND_PANIC(
                WOORT_PANIC_CODE_ENV_NOT_FOUND,
                "Cannot find code environment from `%p`.", vm->m_ip);
        }
        WOORT_VM_HANDLED();
    }
    WOORT_VM_EXCEPTION_LABEL(bad_type) :
    {
        // Bad command.
        WOORT_VM_SYNC_STATE_AND_PANIC(
            WOORT_PANIC_BAD_TYPE,
            "Bad type.");
        return WOORT_VM_CALL_STATUS_ABORTED;
    }
    WOORT_VM_EXCEPTION_LABEL(bad_command) :
    {
        // Bad command.
        WOORT_VM_SYNC_STATE_AND_PANIC(
            WOORT_PANIC_BAD_BYTE_CODE,
            "Bad command(%x).",
            *(uint32_t*)rt_ip);
        return WOORT_VM_CALL_STATUS_ABORTED;
    }
#undef WOORT_VM_EXCEPTION_LABEL
}

WOORT_NODISCARD bool woort_VMRuntime_request_set(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask)
{
    return 0 == (check_mask & woort_atomic_fetch_or_explicit(
        &vm->m_check_request_mask, check_mask, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));
}

WOORT_NODISCARD bool woort_VMRuntime_request_check(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask)
{
    return 0 != (check_mask & woort_atomic_load_explicit(
        &vm->m_check_request_mask, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));
}

WOORT_NODISCARD bool woort_VMRuntime_request_accept(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask)
{
    return 0 != (check_mask & woort_atomic_fetch_and_explicit(
        &vm->m_check_request_mask, ~check_mask, WOORT_ATOMIC_MEMORY_ORDER_RELAXED));
}

void woort_VMRuntime_mark_vm_after_sync(woort_VMRuntime* vm)
{
    // Make sure all write to stack visable.
    woort_atomic_thread_fence(
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    woomem_try_mark_unit((intptr_t)vm->m_env);

    // TODO: Optimize for fast marking.
    for (const void** p = (void**)(vm->m_sp + 1); p < (void**)vm->m_stack_end; ++p)
        woomem_try_mark_unit((intptr_t)*p);
}

void woort_VMRuntime_handle_gc_check_request_and_mark(woort_VMRuntime* vm)
{
    if (woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
    {
        const bool self_marking = woort_VMRuntime_request_accept(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK);

        if (self_marking)
        {
            // Mark vm by it self.
            woort_VMRuntime_mark_vm_after_sync(vm);
        }
        // else: VM has been marked, do nothing.

        if (!woort_VMRuntime_request_accept(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
        {
            if (!self_marking)
            {
                // NOTE: 有非常非常微小的概率，上一轮的 GC 检查点执行到此处时，下一轮
                //      的 GC 已经开始并接收到 GC_PROCESSING 正在阻塞等待。
                //      此处需要执行重标记，然后唤起 GC 工作线程

                // Mark vm by it self.
                woort_VMRuntime_mark_vm_after_sync(vm);
            }
            woort_VMRuntime_wakeup(vm);
        }
    }
    else
    {
        // GC Work thread is processing this vm mark.
        if (woort_VMRuntime_request_accept(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
        {
            // Wait until processing end.
            woort_VMRuntime_hangup(vm);
        }
    }
}

void woort_VMRuntime_gc_checkpoint(woort_VMRuntime* vm)
{
    if (woort_VMRuntime_request_check(
        vm,
        WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING
        | WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK))
    {
        woort_VMRuntime_handle_gc_check_request_and_mark(vm);
    }
}

WOORT_NODISCARD /* OPTIONAL */ woort_VMRuntime* woort_VMRuntime_swap_running_vm(
    /* OPTIONAL */ woort_VMRuntime* vm)
{
    if (t_this_thread_vm == vm)
        return vm;

    woort_VMRuntime* const last_vm = t_this_thread_vm;

    if (last_vm != NULL)
    {
        const bool r = woort_VMRuntime_request_set(
            last_vm,
            WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE);

        (void)r;
        assert(r);
    }
    t_this_thread_vm = vm;
    if (vm != NULL)
    {
        woort_VMRuntime_gc_checkpoint(vm);
        const bool r = woort_VMRuntime_request_accept(
            vm,
            WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE);

        if (!r)
        {
            woort_panic(WOORT_PANIC_REENTRY_GC_SCOPE,
                "VM %p already in running, cannot entry it again.", vm);
        }
    }
    return last_vm;
}
