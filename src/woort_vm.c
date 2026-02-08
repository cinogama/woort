#include "woort.h"

#include "woort_vm.h"
#include "woort_threads.h"
#include "woort_value.h"
#include "woort_log.h"
#include "woort_codeenv.h"
#include "woort_opcode.h"

#include <assert.h>
#include <stdlib.h>
#include <memory.h>

WOORT_THREAD_LOCAL woort_VMRuntime* t_this_thread_vm = NULL;

const size_t WOORT_VM_DEFAULT_STACK_BEGIN_SIZE = 32;
const size_t WOORT_VM_MAX_STACK_SIZE = 1024 * 1024 * 1024 / 8;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm)
{
    // Init stack state.
    vm->m_stack_realloc_version = 0;
    vm->m_stack = malloc(
        WOORT_VM_DEFAULT_STACK_BEGIN_SIZE * sizeof(woort_Value));

    if (vm->m_stack == NULL)
    {
        WOORT_DEBUG("Out of memory");
        return false;
    }

    vm->m_stack_end = vm->m_stack + WOORT_VM_DEFAULT_STACK_BEGIN_SIZE;
    vm->m_sb = vm->m_sp = vm->m_stack_end - 1;

    // Init runtime state.
    vm->m_ip = NULL;
    vm->m_env = NULL;

    return true;
}
void woort_VMRuntime_deinit(woort_VMRuntime* vm)
{
    if (vm->m_stack != NULL)
    {
        free(vm->m_stack);
    }
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
    woort_Value* const new_stack = realloc(vm->m_stack, new_stack_size * sizeof(woort_Value));
    if (new_stack == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }

    // Move stack data from head to tail.
    memcpy(new_stack + current_stack_size, new_stack, current_stack_size);

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

    const woort_Bytecode* rt_ip = vm->m_ip;

    const woort_CodeEnv* rt_env = vm->m_env;
    const woort_Bytecode* rt_env_code = rt_env->m_code_begin;
    const woort_Bytecode* rt_env_code_end = rt_env->m_code_end;
    woort_Value* rt_env_data = rt_env->m_data_begin;

    woort_Value* rt_stack = vm->m_stack;
    woort_Value* rt_stack_end = vm->m_stack_end;
    woort_Value* rt_sp = vm->m_sp;
    woort_Value* rt_sb = vm->m_sb;

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

                // Check for assuring invoke script function.
                assert(rt_env_data[WOORT_BYTECODE(MABC26, c)].m_function.m_type ==
                    WOORT_FUNCTION_TYPE_SCRIPT);

                rt_ip = (const woort_Bytecode*)rt_env_data[
                    WOORT_BYTECODE(MABC26, c)].m_function.m_address;
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

                // Check for assuring invoke native function.
                assert(rt_env_data[WOORT_BYTECODE(MABC26, c)].m_function.m_type ==
                    WOORT_FUNCTION_TYPE_NATIVE);

                const woort_NativeFunction function =
                    (woort_NativeFunction)rt_env_data[
                        WOORT_BYTECODE(MABC26, c)].m_function.m_address;

                WOORT_VM_SYNC_STATE();

                const uint32_t stack_version_before_native_call = vm->m_stack_realloc_version;
                const woort_VmCallStatus status = function(vm, rt_sp + 3);
                /*
                ATTENTION:
                        本机调用发生之后，只可能返回到当前调用栈所在的虚拟机函数；
                    不必考虑 rt_env 改变的情况，因为即便 rt_env 发生改变，回
                    到此处时，也应当回到旧的 rt_env，所以不需要更新它们。

                        但是，栈空间完全可能在本机调用期间发生改变，在旧版本（1.15
                    之前）的 Woolang 中，栈空间的更新由调调用方负责检查和标记：
                    现在这部分工作由被用方负责。
                */

                WOORT_VM_CHECK_STACK_VERSION_AND_RESYNC_STACK_STATE(
                    stack_version_before_native_call);

                if (status == WOORT_VM_CALL_STATUS_NORMAL)
                {
                    // Ok, continue execute.
                    break;
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

                // Check for assuring invoke jit function.
                assert(rt_env_data[WOORT_BYTECODE(MABC26, c)].m_function.m_type ==
                    WOORT_FUNCTION_TYPE_JIT);

                const woort_NativeFunction jit_function =
                    (woort_NativeFunction)rt_env_data[
                        WOORT_BYTECODE(MABC26, c)].m_function.m_address;

                const woort_VmCallStatus status = jit_function(vm, rt_sp + 3);
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
                ++rt_ip;
                WOORT_VM_THROW(env_updated);
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
                ++rt_ip;
                WOORT_VM_THROW(env_updated);
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
                ++rt_ip;
                WOORT_VM_THROW(env_updated);
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
            rt_sb[WOORT_BYTECODE(BC16, c)] = rt_sp[2];
            rt_sp += 2 + WOORT_BYTECODE(MA10, c);

            assert(rt_sp <= rt_sb);

            break;
        }
        default:
            // Unknown bytecode command.
            WOORT_VM_THROW(bad_command);
        }

        // Move forward to next command.
        ++rt_ip;
    }
    // Ok
    return WOORT_VM_CALL_STATUS_NORMAL;

_label_exception_handler_stack_overflow:
    // Stack used up, try extern.
    if (/* UNLIKELY */ !_woort_VMRuntime_extern_stack(vm))
    {
        WOORT_VM_SYNC_STATE_AND_PANIC(
            WOORT_PANIC_STACK_OVERFLOW,
            "Stack overflow.");
    }
    WOORT_VM_HANDLED();

_label_exception_handler_env_updated:
    if (/* UNLIKELY */ !woort_CodeEnv_find(vm->m_ip, &vm->m_env))
    {
        WOORT_VM_SYNC_STATE_AND_PANIC(
            WOORT_PANIC_CODE_ENV_NOT_FOUND,
            "Cannot find code environment from `%p`.", vm->m_ip);
    }
    WOORT_VM_HANDLED();

_label_exception_handler_bad_command:
    // Bad command.
    WOORT_VM_SYNC_STATE_AND_PANIC(
        WOORT_PANIC_BAD_BYTE_CODE,
        "Bad command(%x).",
        *(uint32_t*)rt_ip);
    return WOORT_VM_CALL_STATUS_ABORTED;
}
