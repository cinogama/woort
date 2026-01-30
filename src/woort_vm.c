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

WOORT_NODISCARD woort_VMRuntime_CallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm);

WOORT_NODISCARD woort_VMRuntime_CallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func)
{
    if (!woort_CodeEnv_find(func, &vm->m_env))
        return WOORT_VM_CALL_STATUS_TBD_BAD_STATUS;

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

    return true;
}

WOORT_NODISCARD woort_VMRuntime_CallStatus _woort_VMRuntime_dispatch(
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
    执行正同步时，需要同步 ip, sb, sp 和 env；

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
        vm->m_stack = rt_stack;                 \
        vm->m_sp = rt_sp;                       \
        vm->m_sb = rt_sb;                       \
        vm->m_env = rt_env;                     \
    }while(0)
#define WOORT_VM_RESYNC_STATE()                 \
    do{                                         \
        rt_ip = vm->m_ip;                       \
        rt_stack = vm->m_stack;                 \
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

    const woort_CodeEnv* rt_env = vm->m_env;
    const woort_Bytecode* rt_env_code = rt_env->m_code_begin;
    const woort_Bytecode* rt_env_code_end = rt_env->m_code_end;
    woort_Value* rt_env_data = rt_env->m_data_begin;

    const woort_Bytecode* rt_ip = vm->m_ip;
    woort_Value* rt_stack = vm->m_stack;
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
        // PUSH M2
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 0):
        {
            // PUSH RESERVE STACK
            const uint32_t reserve_stack_sz = WOORT_BYTECODE(ABC24, c);

            rt_sp -= reserve_stack_sz;
            if (/* UNLIKELY */ rt_sp < rt_stack)
            {
                rt_sp += reserve_stack_sz;
                WOORT_VM_THROW(stack_overflow);
            }
            ++rt_ip;
            break;
        }
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 1):
        {
            // PUSH C24
            if (rt_sp >= rt_stack)
            {
                *(rt_sp--) = rt_sb[WOORT_BYTECODE(ABC24, c)];
                ++rt_ip;
                break;
            }
            else
                WOORT_VM_THROW(stack_overflow);
        }
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 2):
        {
            // PUSH R16
            if (rt_sp >= rt_stack)
            {
                *(rt_sp--) = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
                ++rt_ip;
                break;
            }
            else
                WOORT_VM_THROW(stack_overflow);
        }
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 3):
        {
            // PUSH C24 EX_C26
            if (rt_sp >= rt_stack)
            {
                *(rt_sp--) = rt_sb[
                    (((uint64_t)WOORT_BYTECODE(ABC24, c)) << 26) 
                        | (uint64_t)WOORT_BYTECODE(MABC26, rt_ip[1])];
                rt_ip += 2;
                break;
            }
            else
                WOORT_VM_THROW(stack_overflow);
        }
        // POP M2
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 0):
        {
            // POP N
            rt_sp += WOORT_BYTECODE(ABC24, c);

            ++rt_ip;
            break;
        }
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 2):
        {
            // POP R16
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)] = *(++rt_sp);
            ++rt_ip;
            break;
        }

        // LOAD
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_LOAD):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)] = 
                rt_env_data[WOORT_BYTECODE(MAB18, c)];
            ++rt_ip;
            break;
        }
        // STORE
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_STORE):
        {
            rt_env_data[WOORT_BYTECODE(MAB18, c)] = 
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)];
            ++rt_ip;
            break;
        }
        // ADDI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 0):
        {
            // ADDI
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                + rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;

            ++rt_ip;
            break;
        }
        // SUBI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 1):
        {
            // SUBI
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                - rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;

            ++rt_ip;
            break;
        }
        // MULI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 2):
        {
            // MULI
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                * rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;

            ++rt_ip;
            break;
        }
        // DIVI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 3):
        {
            // DIVI
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                / rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;

            ++rt_ip;
            break;
        }
        // JMP
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_JMP):
        {
            rt_ip += WOORT_BYTECODE(MABC26, c);
            break;
        }
        // JMPBACK
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_JMPGC):
        {
            rt_ip -= WOORT_BYTECODE(MABC26, c);
            break;
        }
        // NOP(EXT DATA)
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_NOP):
        {
            ++rt_ip;
            break;
        }
        default:
            // Unknown bytecode command.
            WOORT_VM_THROW(bad_command);
        }
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

_label_exception_handler_bad_command:
    // Bad command.
    WOORT_VM_SYNC_STATE_AND_PANIC(
        WOORT_PANIC_BAD_BYTE_CODE,
        "Bad command(%x).",
        *(uint32_t*)rt_ip);
    return WOORT_VM_CALL_STATUS_TBD_BAD_STATUS;
}
