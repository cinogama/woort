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
#include "woort_gc_units.h"
#include "woort_gc_string.h"
#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc_closure.h"
#include "woort_utf8.h"
#include "woort_vm_debugger.h"
#include "woort_disassembly.h"

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <stdio.h>
#include <inttypes.h>

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


WOORT_NODISCARD bool _woort_VMRuntime_extern_stack(woort_VMRuntime* vm)
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
    memcpy(new_stack + current_stack_size, vm->m_stack, current_stack_size * sizeof(woort_Value));
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

WOORT_NODISCARD woort_VmCallStatus _woort_VMRuntime_dispatch(
    woort_VMRuntime* vm);

WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func)
{
    /*
    注意，此处 woort_CodeEnv_find 在以下情况是安全的：
        1）CodeEnv 尚未被 Drop
        2）func 出自一个其他虚拟机依然持有的地址，这意味着 func 对
            应的 CodeEnv 能够被标记
    */
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

    woort_VMRuntime* const last_running_vm =
        woort_VMRuntime_swap(vm);

    // Reserve sp
    if (vm->m_sp - 2 < vm->m_stack)
    {
        // Stack size not enough.
        while (!_woort_VMRuntime_extern_stack(vm))
        {
            woort_panic(
                WOORT_PANIC_STACK_OVERFLOW,
                "Stack overflow.");
        }
    }

    vm->m_sp -= 2;

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

    woort_VmCallStatus r = _woort_VMRuntime_dispatch(vm);
    (void)woort_VMRuntime_swap(last_running_vm);

    return r;
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

    woort_CodeEnv* rt_env = vm->m_env;
    const woort_Bytecode* rt_env_code = rt_env->m_code_begin;
    const woort_Bytecode* rt_env_code_end = rt_env->m_code_end;
    woort_Value* rt_env_data = rt_env->m_data_begin;

    woort_Value* rt_stack = vm->m_stack;
    woort_Value* rt_stack_end = vm->m_stack_end;
    woort_Value* rt_sp = vm->m_sp;
    woort_Value* rt_sb = vm->m_sb;

    // Ok
_label_continue_execution:
    WOORT_VM_CHECKPOINT();

    for (;;)
    {
#define WOORT_VM_CASE_OP6_M2(CODE, MODE)    \
    (woort_OpcodeFormal_OP6_M2_cons(CODE, MODE) >> 24)
#define WOORT_VM_CASE_OP6(CODE)             \
    WOORT_VM_CASE_OP6_M2(CODE, 0):          \
    case WOORT_VM_CASE_OP6_M2(CODE, 1):     \
    case WOORT_VM_CASE_OP6_M2(CODE, 2):     \
    case WOORT_VM_CASE_OP6_M2(CODE, 3)

        register const woort_Bytecode c = *rt_ip;

    _label_vm_dispatch_reentry_for_debug_trap:
        switch ((uint8_t)(c >> 24))
        {
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_NOP):
        {
            break;
        }
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
            woort_Value src = rt_sb[(int8_t)WOORT_BYTECODE(C8, c)];
            woort_GC_mixed_write_barrier_value(
                &rt_env_data[WOORT_BYTECODE(MAB18, c)], src);
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
            woort_Value src = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
            woort_GC_mixed_write_barrier_value(
                &rt_env_data[rt_ip[1]], src);

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
            if (rt_sp > rt_stack)
            {
                *(rt_sp--) = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHCCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHCHK, 2):
        {
            if (rt_sp > rt_stack)
            {
                *(rt_sp--) = rt_env_data[WOORT_BYTECODE(ABC24, c)];
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHCCHKEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHCHK, 3):
        {
            if (rt_sp > rt_stack)
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
            assert(rt_sp > rt_stack);

            *(rt_sp--) = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)];
            break;
        }
        // PUSHCCHK
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 2):
        {
            assert(rt_sp > rt_stack);

            *(rt_sp--) = rt_env_data[WOORT_BYTECODE(ABC24, c)];
            break;
        }
        // PUSHCCHKEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSH, 3):
        {
            assert(rt_sp > rt_stack);

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
            woort_Value src = *(++rt_sp);
            woort_GC_mixed_write_barrier_value(
                &rt_env_data[WOORT_BYTECODE(ABC24, c)], src);

            assert(rt_sp <= rt_sb);
            break;
        }
        // POPCEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_POP, 3):
        {
            woort_Value src = *(++rt_sp);
            woort_GC_mixed_write_barrier_value(
                &rt_env_data[rt_ip[1]], src);

            assert(rt_sp <= rt_sb);

            rt_ip += 2;
            continue;
        }
        // ITORST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTI, 0):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real =
                (woort_Real)rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // ITORLD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTI, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real =
                (woort_Real)rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;
            break;
        }
        // ITOSST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTI, 2):
        {
            const woort_Int int_val = rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string =
                woort_GCString_from_integer(int_val);
            break;
        }
        // ITOSLD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTI, 3):
        {
            const woort_Int int_val = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;
            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string =
                woort_GCString_from_integer(int_val);
            break;
        }
        // RTOIST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTR, 0):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer =
                (woort_Int)rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            break;
        }
        // RTOILD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTR, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer =
                (woort_Int)rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real;
            break;
        }
        // RTOSST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTR, 2):
        {
            const woort_Real real_val = rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string =
                woort_GCString_from_real(real_val);
            break;
        }
        // RTOSLD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTR, 3):
        {
            const woort_Real real_val = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real;
            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string =
                woort_GCString_from_real(real_val);
            break;
        }
        // STOIST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTS, 0):
        {
            const woort_GCString* str_val = rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string;
            woort_Int int_result;
            if (woort_GCString_to_integer(str_val, &int_result))
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer = int_result;
            else
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer = 0;
            break;
        }
        // STOILD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTS, 1):
        {
            const woort_GCString* str_val = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string;
            woort_Int int_result;
            if (woort_GCString_to_integer(str_val, &int_result))
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer = int_result;
            else
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer = 0;
            break;
        }
        // STORST
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTS, 2):
        {
            const woort_GCString* str_val = rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string;
            woort_Real real_result;
            if (woort_GCString_to_real(str_val, &real_result))
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real = real_result;
            else
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real = 0.0;
            break;
        }
        // STORLD
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CASTS, 3):
        {
            const woort_GCString* str_val = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string;
            woort_Real real_result;
            if (woort_GCString_to_real(str_val, &real_result))
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real = real_result;
            else
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real = 0.0;
            break;
        }

        // CALLNWO
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_CALLNWO):
        {
            rt_sp -= 2;
            if (rt_sp >= rt_stack)
            {
                /*
                CALLNWO 绝不发生 FAR_CALL，所以直接处理即可
                */
                rt_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_NEAR;
                rt_sp[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                rt_sp[2].m_ret_addr = rt_ip + 1;

                rt_sb = rt_sp;

                rt_ip = rt_env_data[WOORT_BYTECODE(MABC26, c)].m_script_function;

                WOORT_VM_CHECKPOINT();
                continue;
            }

            rt_sp += 2;
            WOORT_VM_THROW(stack_overflow);
        }
        // CALLNFP
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_CALLNFP):
        {
            woort_Value* const new_sp = rt_sp - 2;
            if (new_sp >= rt_stack)
            {
                /*
                此处保存到状态仅供调试等目的使用，这些状态实际上不被运行时使用
                */
                new_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_NEAR;
                new_sp[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                new_sp[2].m_ret_addr = /* Update rt_ip to return place. */ ++rt_ip;

                const woort_NativeFunction native_function =
                    rt_env_data[WOORT_BYTECODE(MABC26, c)].m_native_function;

                // No need to WOORT_VM_SYNC_STATE(), we will do it manually.
                vm->m_sb = vm->m_sp = new_sp;
                vm->m_ip = (const woort_Bytecode*)native_function;

                const uint32_t stack_version_before_native_call =
                    vm->m_stack_realloc_version;

                const woort_VmCallStatus status = native_function(vm);

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

                // Donot need to restore any status.
                if (status == WOORT_VM_CALL_STATUS_RESYNC)
                {
                    WOORT_VM_RESYNC_STATE();
                    WOORT_VM_CHECKPOINT();
                }
                else
                    assert(status == WOORT_VM_CALL_STATUS_NORMAL);

                // Ok, continue execute.
                continue;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // CALLNJIT
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_CALLNJIT):
        {
            woort_Value* const new_sp = rt_sp - 2;
            if (new_sp >= rt_stack)
            {
                new_sp[1].m_ret_bp.m_way = WOORT_CALL_WAY_FAR;
                new_sp[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                new_sp[2].m_ret_addr = ++rt_ip;

                const woort_JitFunction jit_function =
                    rt_env_data[WOORT_BYTECODE(MABC26, c)].m_jit_function;

                const woort_VmCallStatus status =
                    jit_function(vm, rt_sp + 1);

                if (status == WOORT_VM_CALL_STATUS_RESYNC)
                {
                    WOORT_VM_RESYNC_STATE();
                    WOORT_VM_CHECKPOINT();
                }
                else
                    assert(status == WOORT_VM_CALL_STATUS_NORMAL);

                // Ok, continue execute.
                continue;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // CALLS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CALL, 0):
        {
            goto _label_vm_call_impl_calls;
        }
        // CALLC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CALL, 1):
        {
            const woort_GCClosure* target;
            if (1)
                target = rt_env_data[WOORT_BYTECODE(ABC24, c)].m_closure;
            else
            {
            _label_vm_call_impl_calls:
                target = rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_closure;
            }

            woort_Value* const new_sb = rt_sp - 2;
            woort_Value* const new_sp = new_sb - target->m_size;
            if (new_sp >= rt_stack)
            {
                if (target->m_size != 0)
                    memcpy(
                        new_sp + 1,
                        target->m_datas,
                        sizeof(woort_Value) * target->m_size);

                if (target->m_script_function != NULL)
                {
                    if (target->m_jit_function != NULL)
                    {
                        // Is JIT function.
                        new_sb[1].m_ret_bp.m_way = WOORT_CALL_WAY_FAR;
                        new_sb[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                        new_sb[2].m_ret_addr = ++rt_ip;

                        const woort_VmCallStatus status =
                            target->m_jit_function(vm, rt_sp + 1);

                        if (status == WOORT_VM_CALL_STATUS_RESYNC)
                        {
                            WOORT_VM_RESYNC_STATE();
                            WOORT_VM_CHECKPOINT();
                        }
                        else
                            assert(status == WOORT_VM_CALL_STATUS_NORMAL);
                    }
                    else
                    {
                        // Is script function.
                        /* CALL 可能发生 FAR_CALL，需要在跳转完成之后检查是否是 FAR CALL */
                        new_sb[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                        new_sb[2].m_ret_addr = rt_ip + 1;

                        rt_sp = new_sp;
                        rt_sb = new_sb;

                        rt_ip = target->m_script_function;

                        if (rt_ip >= rt_env_code_end || rt_ip < rt_env_code)
                        {
                            // 已经跳出当前 env 的代码段，触发一个 env_updated.
                            rt_sb[1].m_ret_bp.m_way = WOORT_CALL_WAY_FAR;
                            // Update env.
                            WOORT_VM_THROW(env_updated);
                        }

                        rt_sb[1].m_ret_bp.m_way = WOORT_CALL_WAY_NEAR;
                        WOORT_VM_CHECKPOINT();
                    }
                }
                else
                {
                    // Is native function.
                    /*
                    此处保存到状态仅供调试等目的使用，这些状态实际上不被运行时使用
                    */
                    new_sb[1].m_ret_bp.m_way = WOORT_CALL_WAY_NEAR;
                    new_sb[1].m_ret_bp.m_bp_offset = (uint32_t)(rt_stack_end - rt_sb);
                    new_sb[2].m_ret_addr = /* Update rt_ip to return place. */ ++rt_ip;

                    // No need to WOORT_VM_SYNC_STATE(), we will do it manually.
                    rt_sp = new_sp;
                    rt_sb = new_sb;
                    vm->m_ip = (const woort_Bytecode*)target->m_native_function;

                    const uint32_t stack_version_before_native_call =
                        vm->m_stack_realloc_version;

                    const woort_VmCallStatus status =
                        target->m_native_function(vm);

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

                    // Donot need to restore any status.
                    if (status == WOORT_VM_CALL_STATUS_RESYNC)
                    {
                        WOORT_VM_RESYNC_STATE();
                        WOORT_VM_CHECKPOINT();
                    }
                    else
                        assert(status == WOORT_VM_CALL_STATUS_NORMAL);
                }

                // Ok, continue execute.
                continue;
            }

            // Stack overflow.
            WOORT_VM_THROW(stack_overflow);
        }
        // RET
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 0):
        {
            rt_sp = rt_sb + 2;
            rt_sb = rt_stack_end - rt_sp[-1].m_ret_bp.m_bp_offset;
            rt_ip = rt_sp[0].m_ret_addr;

            switch (rt_sp[-1].m_ret_bp.m_way)
            {
            case WOORT_CALL_WAY_NEAR:
                break;
            case WOORT_CALL_WAY_FROM_NATIVE:
                WOORT_VM_SYNC_STATE();
                return WOORT_VM_CALL_STATUS_NORMAL;
            case WOORT_CALL_WAY_FAR:
            {
                // Try resync far ip.
                if (rt_ip < rt_env_code || rt_ip >= rt_env_code_end)
                    WOORT_VM_THROW(env_updated);
                break;
            }
            default:
                // Cannot be here.
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_CALLSTACK,
                    "Bad callstack, unexpected call way(%x).",
                    (uint32_t)rt_sp[-1].m_ret_bp.m_way);
            }
            continue;
        }
        // RETVS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 1):
        {
            rt_sp = rt_sb + 2;
            rt_sb = rt_stack_end - rt_sp[-1].m_ret_bp.m_bp_offset;
            rt_ip = rt_sp[0].m_ret_addr;

            /* 此处使用 rt_sp 寻址，因为这是上一层调用栈的 bp */
            rt_sp[0] = rt_sp[(int16_t)WOORT_BYTECODE(BC16, c) - 2];

            switch (rt_sp[-1].m_ret_bp.m_way)
            {
            case WOORT_CALL_WAY_NEAR:
                break;
            case WOORT_CALL_WAY_FROM_NATIVE:
                WOORT_VM_SYNC_STATE();
                return WOORT_VM_CALL_STATUS_NORMAL;
            case WOORT_CALL_WAY_FAR:
            {
                // Try resync far ip.
                if (rt_ip < rt_env_code || rt_ip >= rt_env_code_end)
                    WOORT_VM_THROW(env_updated);
                break;
            }
            default:
                // Cannot be here.
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_CALLSTACK,
                    "Bad callstack, unexpected call way(%x).",
                    (uint32_t)rt_sp[-1].m_ret_bp.m_way);
            }
            continue;
        }
        // RETVC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 2):
        {
            rt_sp = rt_sb + 2;
            rt_sb = rt_stack_end - rt_sp[-1].m_ret_bp.m_bp_offset;
            rt_ip = rt_sp[0].m_ret_addr;

            rt_sp[0] = rt_env_data[WOORT_BYTECODE(ABC24, c)];

            switch (rt_sp[-1].m_ret_bp.m_way)
            {
            case WOORT_CALL_WAY_NEAR:
                break;
            case WOORT_CALL_WAY_FROM_NATIVE:
                WOORT_VM_SYNC_STATE();
                return WOORT_VM_CALL_STATUS_NORMAL;
            case WOORT_CALL_WAY_FAR:
            {
                // Try resync far ip.
                if (rt_ip < rt_env_code || rt_ip >= rt_env_code_end)
                    WOORT_VM_THROW(env_updated);
                break;
            }
            default:
                // Cannot be here.
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_CALLSTACK,
                    "Bad callstack, unexpected call way(%x).",
                    (uint32_t)rt_sp[-1].m_ret_bp.m_way);
            }
            continue;
        }
        // POPRS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_RET, 3):
        {
            // NOTE: Cannot be negative.
            rt_sp +=
                (size_t)rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            assert(rt_sp <= rt_sb);
            break;
        }
        // RESULT
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_RESULT):
        {
            const int16_t v = (int16_t)WOORT_BYTECODE(BC16, c);

            rt_sb[v] = rt_sp[0];
            rt_sp += WOORT_BYTECODE(MA10, c);

            assert(rt_sp <= rt_sb);

            break;
        }
        // JMPF
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_JFWD):
        {
            rt_ip = rt_env_code + WOORT_BYTECODE(MABC26, c);
            continue;
        }
        // JMPB
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_JBCK):
        {
            rt_ip = rt_env_code + WOORT_BYTECODE(MABC26, c);
            WOORT_VM_CHECKPOINT();
            continue;
        }
        // JFWDNZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFWDCND, 0):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer != 0)
            {
                rt_ip += WOORT_BYTECODE(BC16, c);
                continue;
            }
            break;
        }
        // JFWDZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFWDCND, 1):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer == 0)
            {
                rt_ip += WOORT_BYTECODE(BC16, c);
                continue;
            }
            break;
        }
        // JFWDEQ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFWDCND, 2):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                == rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JFWDNEQ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFWDCND, 3):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                != rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JBCKNZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCND, 0):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer != 0)
            {
                rt_ip -= WOORT_BYTECODE(BC16, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCKZ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCND, 1):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer == 0)
            {
                rt_ip -= WOORT_BYTECODE(BC16, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCKEQ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCND, 2):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                == rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCKNEQ
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCND, 3):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                != rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JFWDLT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFDCMP, 0):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                < rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JFWDGT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFDCMP, 1):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                > rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JFWDEL
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFDCMP, 2):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                <= rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JFWDEG
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JFDCMP, 3):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                >= rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip += WOORT_BYTECODE(C8, c);
                continue;
            }
            break;
        }
        // JBCKLT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCMP, 0):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                < rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCKGT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCMP, 1):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                > rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCKEL
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCMP, 2):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                <= rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // JBCKEG
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_JBCKCMP, 3):
        {
            if (rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                >= rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer)
            {
                rt_ip -= WOORT_BYTECODE(C8, c);
                WOORT_VM_CHECKPOINT();
                continue;
            }
            break;
        }
        // MKVEC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CONS, 0):
        {
            const size_t size = WOORT_BYTECODE(A8, c);

            woort_GCVec* const gcvec = woort_GCVec_new();
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_vec = gcvec;

            // NOTE: 此处不同步虚拟机状态直接分配是没有问题的，如果分配失败
            //      会假定整个栈空间都在被使用中，肯定能标记到 gcvec 实例
            woort_GCVec_resize(gcvec, size);

            for (size_t i = 1; i <= size; ++i)
                woort_GC_mixed_write_barrier_dynbox(
                    &gcvec->m_datas[size - i], rt_sp[i].m_dynamic);

            rt_sp += size;
            break;
        }
        // MKMAP
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CONS, 1):
        {
            const size_t size = WOORT_BYTECODE(A8, c);

            woort_GCMap* const gcmap = woort_GCMap_new();
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_map = gcmap;

            woort_GCMap_reserve(gcmap, size);

            for (size_t i = 0; i < size; ++i)
            {
                woort_DynBox val = rt_sp[1 + i * 2].m_dynamic;
                woort_DynBox key = rt_sp[2 + i * 2].m_dynamic;
                woort_GCMap_set_or_insert(gcmap, key, val);
            }

            rt_sp += size * 2;
            break;
        }

        // MKSTRUCT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CONS, 2):
        {
            const size_t size = WOORT_BYTECODE(A8, c);

            woort_GCStruct* const gcstruct = woort_GCStruct_new(size);
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct = gcstruct;

            for (size_t i = 1; i <= size; ++i)
                woort_GC_mixed_write_barrier_value(
                    &gcstruct->m_datas[size - i], rt_sp[i]);

            rt_sp += size;
            break;
        }

        // MKVECEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CONSEX, 0):
        {
            const size_t size = rt_ip[1];

            woort_GCVec* const gcvec = woort_GCVec_new();
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_vec = gcvec;

            woort_GCVec_resize(gcvec, size);

            for (size_t i = 1; i <= size; ++i)
                woort_GC_mixed_write_barrier_dynbox(
                    &gcvec->m_datas[size - i], rt_sp[i].m_dynamic);

            rt_sp += size;
            rt_ip += 2;
            continue;
        }
        // MKMAPEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CONSEX, 1):
        {
            const size_t size = rt_ip[1];

            woort_GCMap* const gcmap = woort_GCMap_new();
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_map = gcmap;

            woort_GCMap_reserve(gcmap, size);

            for (size_t i = 0; i < size; ++i)
            {
                woort_DynBox val = rt_sp[1 + i * 2].m_dynamic;
                woort_DynBox key = rt_sp[2 + i * 2].m_dynamic;
                woort_GCMap_set_or_insert(gcmap, key, val);
            }

            rt_sp += size * 2;
            rt_ip += 2;
            continue;
        }
        // MKSTRUCTEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_CONSEX, 2):
        {
            const size_t size = rt_ip[1];

            woort_GCStruct* const gcstruct = woort_GCStruct_new(size);
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct = gcstruct;

            for (size_t i = 1; i <= size; ++i)
                woort_GC_mixed_write_barrier_value(
                    &gcstruct->m_datas[size - i], rt_sp[i]);

            rt_sp += size;
            rt_ip += 2;
            continue;
        }
        // MKCLOSURE
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_MKCLOSURE):
        {
            const size_t size = WOORT_BYTECODE(MA10, c);
            const uint32_t const_idx = rt_ip[1];

            woort_GCClosure* const gcclosure =
                woort_GCClosure_new(rt_env_data[const_idx].m_closure, size);

            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_closure = gcclosure;

            for (size_t i = 1; i <= size; ++i)
                woort_GC_mixed_write_barrier_value(
                    &gcclosure->m_datas[size - i], rt_sp[i]);

            rt_sp += size;
            rt_ip += 2;
            continue;
        }

        // BOXDYN
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic = woort_DynBox_box(
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)],
                (woort_BoxValueType)WOORT_BYTECODE(A8, c));
            break;
        }
        // UNBOXDYN
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 1):
        {
            if (!woort_DynBox_unbox(
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic,
                (woort_BoxValueType)WOORT_BYTECODE(A8, c),
                &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]))
            {
                WOORT_VM_THROW(bad_type);
            }
            break;
        }
        // CHECKDYN
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_DynBox_check(
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic,
                    (woort_BoxValueType)WOORT_BYTECODE(A8, c)) ? 1 : 0;
            break;
        }
        // PUSHBOXDYN
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_DYN, 3):
        {
            if (rt_sp > rt_stack)
            {
                rt_sp->m_dynamic = woort_DynBox_box(
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)],
                    (woort_BoxValueType)WOORT_BYTECODE(A8, c));
                --rt_sp;
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // ADDI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer +
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // SUBI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer -
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // MULI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer *
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // DIVI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIASMD, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer /
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // MODI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIONLG, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer %
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // NEGI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIONLG, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer =
                -rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // LTI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIONLG, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer <
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // GTI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPIONLG, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer >
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // LEI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPISREN, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer <=
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // GEI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPISREN, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer >=
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // EQI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPISREN, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer ==
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // NEI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPISREN, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer !=
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // ADDR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRASMD, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real +
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // SUBR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRASMD, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real -
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // MULR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRASMD, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real *
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // DIVR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRASMD, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real /
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // MODR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRONLG, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real =
                fmod(rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);
            break;
        }
        // NEGR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRONLG, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real =
                -rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            break;
        }
        // LTR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRONLG, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real <
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // GTR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRONLG, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real >
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // LER
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRSREN, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real <=
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // GER
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRSREN, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real >=
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // EQR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRSREN, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real ==
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // NER
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPRSREN, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real !=
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real;
            break;
        }
        // ADDS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSALGS, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_string =
                woort_GCString_add_string(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string);
            break;
        }
        // LTS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSALGS, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_GCString_compare(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string) < 0;
            break;
        }
        // GTS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSALGS, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_GCString_compare(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string) > 0;
            break;
        }
        // LES
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSALGS, 3):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_GCString_compare(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string) <= 0;
            break;
        }
        // GES
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSREN, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_GCString_compare(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string) >= 0;
            break;
        }
        // EQS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSREN, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_GCString_compare(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string) == 0;
            break;
        }
        // NES
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPSREN, 2):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                woort_GCString_compare(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string) != 0;
            break;
        }

        // LAND
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPLAONI, 0):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                && rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // LOR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPLAONI, 1):
        {
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer
                || rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer;
            break;
        }
        // LNOT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPLAONI, 2):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer =
                !rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CADDI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCIASMD, 0):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer +=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CSUBI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCIASMD, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer -=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CMULI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCIASMD, 2):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer *=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CDIVI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCIASMD, 3):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer /=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CADDR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCRASMD, 0):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real +=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            break;
        }
        // CSUBR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCRASMD, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real -=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            break;
        }
        // CMULR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCRASMD, 2):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real *=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            break;
        }
        // CDIVR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCRASMD, 3):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real /=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;
            break;
        }
        // CADDS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCSAIOO, 0):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string =
                woort_GCString_add_string(
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string,
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string);
            break;
        }
        // CVADDS
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCSAIOO, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string =
                woort_GCString_add_string(
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_string,
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_string);
            break;
        }
        // CMODI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCSAIOO, 2):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer %=
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CMODR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCSAIOO, 3):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real =
                fmod(rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real,
                    rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real);
            break;
        }
        // CLAND
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCLAON, 0):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer
                && rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CLOR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCLAON, 1):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer
                || rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;
            break;
        }
        // CLNOT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_OPCLAON, 2):
        {
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer =
                !rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;
            break;
        }
        // LDIDXVEC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDX, 0):
        {
            const size_t index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            woort_GCVec* const gcvec =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_vec;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            woort_DynBox_unbox_no_check(
                gcvec->m_datas[index],
                &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]);

            break;
        }
        // LDIDXVECX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDX, 1):
        {
            const size_t index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            woort_GCVec* const gcvec =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_vec;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic =
                gcvec->m_datas[index];

            break;
        }
        // LDIDSTRUCT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDX, 2):
        {
            const size_t index =
                (size_t)WOORT_BYTECODE(A8, c);

            woort_GCStruct* const gcstruct =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_struct;

            assert(index < gcstruct->m_size);

            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)] =
                gcstruct->m_datas[index];

            break;
        }
        // LDIDSTRING
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDX, 3):
        {
            const size_t char_index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            const woort_GCString* const gcstr =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_string;

            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer =
                (woort_Int)woort_u8stridx(
                    gcstr->m_content, gcstr->m_length, char_index);

            break;
        }
        // LDIDXDICTI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICT, 0):
        {
            const woort_Int index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_int(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(
                *val, &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]);
            break;
        }
        // LDIDXDICTR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICT, 1):
        {
            const woort_Real index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_real(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(
                *val, &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]);
            break;
        }
        // LDIDXDICTB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICT, 2):
        {
            const bool index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_bool(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(
                *val, &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]);
            break;
        }
        // LDIDXDICTX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICT, 3):
        {
            const woort_DynBox index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_dynamic;

            // NOTE: LDIDXDICTX 用于索引类型为 dynamic 或者 gcunit 的情况
            //      考虑到 m_boxed_gc_unit 和 m_gcinstance 应当使用了相同
            //      的存储方式，所以此处直接传入 gc_unit 应当也能够正确工作
            // 
            // TODO: 需要额外的断言检查

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox val;
            if (!woort_GCMap_get(gcmap, index, &val))
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(
                val, &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]);
            break;
        }
        // LDIDXDICTIX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTX, 0):
        {
            const woort_Int index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_int(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic = *val;
            break;
        }
        // LDIDXDICTRX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTX, 1):
        {
            const woort_Real index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_real;

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_real(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic = *val;
            break;
        }
        // LDIDXDICTBX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTX, 2):
        {
            const bool index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer;

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_bool(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic = *val;
            break;
        }
        // LDIDXDICTXX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTX, 3):
        {
            const woort_DynBox index =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_dynamic;

            // NOTE: LDIDXDICTXX 用于索引类型为 dynamic 或者 gcunit 的情况
            //      考虑到 m_boxed_gc_unit 和 m_gcinstance 应当使用了相同
            //      的存储方式，所以此处直接传入 gc_unit 应当也能够正确工作
            // 
            // TODO: 需要额外的断言检查

            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_map;

            if (!woort_GCMap_get(
                gcmap,
                index,
                &rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic))
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            break;
        }
        // LDIDXVECEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXEX, 0):
        {
            const size_t index =
                (size_t)rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t vec_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCVec* const gcvec = rt_sb[vec_reg].m_vec;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            woort_DynBox_unbox_no_check(
                gcvec->m_datas[index],
                &rt_sb[result_reg]);

            rt_ip += 2;
            continue;
        }
        // LDIDXVECXEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXEX, 1):
        {
            const size_t index =
                (size_t)rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t vec_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCVec* const gcvec = rt_sb[vec_reg].m_vec;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            rt_sb[result_reg].m_dynamic = gcvec->m_datas[index];

            rt_ip += 2;
            continue;
        }
        // LDIDSTRUCTEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXEX, 2):
        {
            const size_t index = (size_t)WOORT_BYTECODE(ABC24, c);

            const int16_t struct_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCStruct* const gcstruct = rt_sb[struct_reg].m_struct;

            assert(index < gcstruct->m_size);

            rt_sb[result_reg] = gcstruct->m_datas[index];

            rt_ip += 2;
            continue;
        }
        // LDIDSTRINGEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXEX, 3):
        {
            const size_t char_index =
                (size_t)rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t str_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            const woort_GCString* const gcstr = rt_sb[str_reg].m_string;

            rt_sb[result_reg].m_integer =
                (woort_Int)woort_u8stridx(
                    gcstr->m_content, gcstr->m_length, char_index);

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTIEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEX, 0):
        {
            const woort_Int index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_int(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(*val, &rt_sb[result_reg]);

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTREXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEX, 1):
        {
            const woort_Real index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_real(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(*val, &rt_sb[result_reg]);

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTBEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEX, 2):
        {
            const bool index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_bool(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(*val, &rt_sb[result_reg]);

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTXEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEX, 3):
        {
            const woort_DynBox index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_dynamic;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox val;
            if (!woort_GCMap_get(gcmap, index, &val))
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_unbox_no_check(val, &rt_sb[result_reg]);

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTIXEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEXX, 0):
        {
            const woort_Int index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_int(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            rt_sb[result_reg].m_dynamic = *val;

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTRXEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEXX, 1):
        {
            const woort_Real index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_real;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_real(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            rt_sb[result_reg].m_dynamic = *val;

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTBXEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEXX, 2):
        {
            const bool index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_integer;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* const val = woort_GCMap_get_bucket_val_by_bool(gcmap, index);
            if (val == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }
            rt_sb[result_reg].m_dynamic = *val;

            rt_ip += 2;
            continue;
        }
        // LDIDXDICTXXEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_LDIDXDICTEXX, 3):
        {
            const woort_DynBox index =
                rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_dynamic;

            const int16_t map_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t result_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            if (!woort_GCMap_get(gcmap, index, &rt_sb[result_reg].m_dynamic))
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            rt_ip += 2;
            continue;
        }
        // STIDXVECI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXVEC, 0):
        {
            woort_GCVec* const gcvec =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_vec;

            const size_t index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            woort_GC_mixed_write_barrier_dynbox(
                &gcvec->m_datas[index],
                woort_DynBox_box_int(rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer));

            break;
        }
        // STIDXVECR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXVEC, 1):
        {
            woort_GCVec* const gcvec =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_vec;

            const size_t index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            woort_GC_mixed_write_barrier_dynbox(
                &gcvec->m_datas[index],
                woort_DynBox_box_real(rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real));

            break;
        }
        // STIDXVECB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXVEC, 2):
        {
            woort_GCVec* const gcvec =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_vec;

            const size_t index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            woort_GC_mixed_write_barrier_dynbox(
                &gcvec->m_datas[index],
                woort_DynBox_box_bool(rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer));

            break;
        }
        // STIDXVECX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXVEC, 3):
        {
            woort_GCVec* const gcvec =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_vec;

            const size_t index =
                (size_t)rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            // NOTE: STIDXVECX 用于索引类型为 dynamic 或者 gcunit 的情况
            //      考虑到 m_boxed_gc_unit 和 m_gcinstance 应当使用了相同
            //      的存储方式，所以此处直接传入 gc_unit 应当也能够正确工作
            // 
            // TODO: 需要额外的断言检查

            woort_GC_mixed_write_barrier_dynbox(
                &gcvec->m_datas[index],
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXDICTII
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTI, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_int(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTIR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTI, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_int(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXDICTIB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTI, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_int(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTIX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTI, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_int(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            // NOTE: STIDXDICTIX 用于值类型为 dynamic 或者 gcunit 的情况
            //      考虑到 m_boxed_gc_unit 和 m_gcinstance 应当使用了相同
            //      的存储方式，所以此处直接传入 gc_unit 应当也能够正确工作
            //
            // TODO: 需要额外的断言检查
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXDICTRI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTR, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_real(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTRR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTR, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_real(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXDICTRB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTR, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_real(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTRX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTR, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_real(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            // NOTE: STIDXDICTRX 用于值类型为 dynamic 或者 gcunit 的情况
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXDICTBI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTB, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_bool(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTBR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTB, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_bool(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXDICTBB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTB, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_bool(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTBX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTB, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_bool(gcmap, key);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            // NOTE: STIDXDICTBX 用于值类型为 dynamic 或者 gcunit 的情况
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXDICTXI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTX, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTXR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTX, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXDICTXB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTX, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXDICTXX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXDICTX, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            // NOTE: STIDXDICTXX 用于键和值类型均为 dynamic 或者 gcunit 的情况
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXMAPII
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPI, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_int(gcmap, key);

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPIR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPI, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_int(gcmap, key);

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXMAPIB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPI, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_int(gcmap, key);

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPIX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPI, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Int key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_int(gcmap, key);

            // NOTE: STIDXMAPIX 用于值类型为 dynamic 或者 gcunit 的情况
            //      考虑到 m_boxed_gc_unit 和 m_gcinstance 应当使用了相同
            //      的存储方式，所以此处直接传入 gc_unit 应当也能够正确工作
            //
            // TODO: 需要额外的断言检查
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXMAPRI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPR, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_real(gcmap, key);

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPRR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPR, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_real(gcmap, key);

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXMAPRB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPR, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_real(gcmap, key);

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPRX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPR, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const woort_Real key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_real;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_real(gcmap, key);

            // NOTE: STIDXMAPRX 用于值类型为 dynamic 或者 gcunit 的情况
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXMAPBI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPB, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_bool(gcmap, key);

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPBR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPB, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_bool(gcmap, key);

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXMAPBB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPB, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_bool(gcmap, key);

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPBX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPB, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            const bool key =
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_integer != 0;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_bool(gcmap, key);

            // NOTE: STIDXMAPBX 用于值类型为 dynamic 或者 gcunit 的情况
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDXMAPXI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPX, 0):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            woort_DynBox_box_int_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPXR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPX, 1):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            woort_DynBox_box_real_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_real);

            break;
        }
        // STIDXMAPXB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPX, 2):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            woort_DynBox_box_bool_with_barrier(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_integer);

            break;
        }
        // STIDXMAPXX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXMAPX, 3):
        {
            woort_GCMap* const gcmap =
                rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_map;

            woort_DynBox* const val_ptr =
                woort_GCMap_get_or_create_bucket_val_by_dynbox(
                    gcmap, rt_sb[(int8_t)WOORT_BYTECODE(C8, c)].m_dynamic);

            // NOTE: STIDXMAPXX 用于键和值类型均为 dynamic 或者 gcunit 的情况
            woort_GC_mixed_write_barrier_dynbox(
                val_ptr, rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_dynamic);

            break;
        }
        // STIDSTRUCT
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_STIDSTRUCT):
        {
            const size_t index = (size_t)WOORT_BYTECODE(MA10, c);

            woort_GCStruct* const gcstruct =
                rt_sb[(int8_t)WOORT_BYTECODE(B8, c)].m_struct;

            assert(index < gcstruct->m_size);

            woort_GC_mixed_write_barrier_value(
                &gcstruct->m_datas[index],
                rt_sb[(int8_t)WOORT_BYTECODE(C8, c)]);

            break;
        }
        // STIDXVECEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXEX, 0):
        {
            const uint8_t value_type = (uint8_t)WOORT_BYTECODE(A8, c);

            const int16_t vec_reg = (int16_t)WOORT_BYTECODE(BC16, c);
            const int16_t index_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t value_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCVec* const gcvec = rt_sb[vec_reg].m_vec;

            const size_t index = (size_t)rt_sb[index_reg].m_integer;

            if (index >= gcvec->m_length)
                WOORT_VM_THROW(index_out_of_range);

            switch (value_type)
            {
            case 0: // I
                woort_GC_mixed_write_barrier_dynbox(
                    &gcvec->m_datas[index],
                    woort_DynBox_box_int(rt_sb[value_reg].m_integer));
                break;
            case 1: // R
                woort_GC_mixed_write_barrier_dynbox(
                    &gcvec->m_datas[index],
                    woort_DynBox_box_real(rt_sb[value_reg].m_real));
                break;
            case 2: // B
                woort_GC_mixed_write_barrier_dynbox(
                    &gcvec->m_datas[index],
                    woort_DynBox_box_bool(rt_sb[value_reg].m_integer));
                break;
            case 3: // X
                // NOTE: STIDXVECX 用于值类型为 dynamic 或者 gcunit 的情况
                woort_GC_mixed_write_barrier_dynbox(
                    &gcvec->m_datas[index],
                    rt_sb[value_reg].m_dynamic);
                break;
            default:
                WOORT_VM_THROW(bad_command);
            }

            rt_ip += 2;
            continue;
        }
        // STIDXDICTEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXEX, 1):
        {
            const uint8_t key_type = (uint8_t)((c >> 20) & 0xFu);
            const uint8_t value_type = (uint8_t)((c >> 16) & 0xFu);

            const int16_t map_reg = (int16_t)WOORT_BYTECODE(BC16, c);
            const int16_t key_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t value_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* val_ptr = NULL;

            switch (key_type)
            {
            case 0: // I
                val_ptr = woort_GCMap_get_bucket_val_by_int(
                    gcmap, rt_sb[key_reg].m_integer);
                break;
            case 1: // R
                val_ptr = woort_GCMap_get_bucket_val_by_real(
                    gcmap, rt_sb[key_reg].m_real);
                break;
            case 2: // B
                val_ptr = woort_GCMap_get_bucket_val_by_bool(
                    gcmap, rt_sb[key_reg].m_integer != 0);
                break;
            case 3: // X
                val_ptr = woort_GCMap_get_bucket_val_by_dynbox(
                    gcmap, rt_sb[key_reg].m_dynamic);
                break;
            default:
                WOORT_VM_THROW(bad_command);
            }

            if (val_ptr == NULL)
            {
                WOORT_VM_THROW(index_out_of_range);
            }

            switch (value_type)
            {
            case 0: // I
                woort_DynBox_box_int_with_barrier(
                    val_ptr, rt_sb[value_reg].m_integer);
                break;
            case 1: // R
                woort_DynBox_box_real_with_barrier(
                    val_ptr, rt_sb[value_reg].m_real);
                break;
            case 2: // B
                woort_DynBox_box_bool_with_barrier(
                    val_ptr, rt_sb[value_reg].m_integer);
                break;
            case 3: // X
                // NOTE: STIDXDICT*X 用于值类型为 dynamic 或者 gcunit 的情况
                woort_GC_mixed_write_barrier_dynbox(
                    val_ptr, rt_sb[value_reg].m_dynamic);
                break;
            default:
                WOORT_VM_THROW(bad_command);
            }

            rt_ip += 2;
            continue;
        }
        // STIDXMAPEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXEX, 2):
        {
            const uint8_t key_type = (uint8_t)((c >> 20) & 0xFu);
            const uint8_t value_type = (uint8_t)((c >> 16) & 0xFu);

            const int16_t map_reg = (int16_t)WOORT_BYTECODE(BC16, c);
            const int16_t key_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t value_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCMap* const gcmap = rt_sb[map_reg].m_map;

            woort_DynBox* val_ptr = NULL;

            switch (key_type)
            {
            case 0: // I
                val_ptr = woort_GCMap_get_or_create_bucket_val_by_int(
                    gcmap, rt_sb[key_reg].m_integer);
                break;
            case 1: // R
                val_ptr = woort_GCMap_get_or_create_bucket_val_by_real(
                    gcmap, rt_sb[key_reg].m_real);
                break;
            case 2: // B
                val_ptr = woort_GCMap_get_or_create_bucket_val_by_bool(
                    gcmap, rt_sb[key_reg].m_integer != 0);
                break;
            case 3: // X
                val_ptr = woort_GCMap_get_or_create_bucket_val_by_dynbox(
                    gcmap, rt_sb[key_reg].m_dynamic);
                break;
            default:
                WOORT_VM_THROW(bad_command);
            }

            switch (value_type)
            {
            case 0: // I
                woort_DynBox_box_int_with_barrier(
                    val_ptr, rt_sb[value_reg].m_integer);
                break;
            case 1: // R
                woort_DynBox_box_real_with_barrier(
                    val_ptr, rt_sb[value_reg].m_real);
                break;
            case 2: // B
                woort_DynBox_box_bool_with_barrier(
                    val_ptr, rt_sb[value_reg].m_integer);
                break;
            case 3: // X
                // NOTE: STIDXMAP*X 用于值类型为 dynamic 或者 gcunit 的情况
                woort_GC_mixed_write_barrier_dynbox(
                    val_ptr, rt_sb[value_reg].m_dynamic);
                break;
            default:
                WOORT_VM_THROW(bad_command);
            }

            rt_ip += 2;
            continue;
        }
        // STIDSTRUCTEXT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_STIDXEX, 3):
        {
            const size_t index = (size_t)WOORT_BYTECODE(ABC24, c);

            const int16_t struct_reg = (int16_t)(rt_ip[1] >> 16);
            const int16_t value_reg = (int16_t)(rt_ip[1] & 0xFFFFu);

            woort_GCStruct* const gcstruct = rt_sb[struct_reg].m_struct;

            assert(index < gcstruct->m_size);

            woort_GC_mixed_write_barrier_value(
                &gcstruct->m_datas[index],
                rt_sb[value_reg]);

            rt_ip += 2;
            continue;
        }
        // UNPACKSTRUCT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_UNPACK, 0):
        {
            const int16_t struct_reg = (int16_t)WOORT_BYTECODE(BC16, c);

            woort_GCStruct* const gcstruct = rt_sb[struct_reg].m_struct;
            const size_t size = gcstruct->m_size;

            // 检查栈空间
            if ((size_t)(rt_sp - rt_stack) < size)
                WOORT_VM_THROW(stack_overflow);

            // 将结构体的元素压入栈中（保持顺序）
            for (size_t i = 0; i < size; ++i)
                rt_sp[-(ptrdiff_t)i] = gcstruct->m_datas[size - 1 - i];

            rt_sp -= size;
            break;
        }
        // UNPACKVEC
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_UNPACK, 1):
        {
            const int16_t vec_reg = (int16_t)WOORT_BYTECODE(BC16, c);

            woort_GCVec* const gcvec = rt_sb[vec_reg].m_vec;
            const size_t size = gcvec->m_length;

            // 检查栈空间
            if ((size_t)(rt_sp - rt_stack) < size)
            {
                WOORT_VM_THROW(stack_overflow);
            }


            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer = (woort_Int)size;

            // 将向量的元素解包并压入栈中（保持顺序）
            for (size_t i = 0; i < size; ++i)
            {
                woort_DynBox_unbox_no_check(
                    gcvec->m_datas[size - 1 - i],
                    &rt_sp[-(ptrdiff_t)i]);
            }

            rt_sp -= size;
            break;
        }
        // UNPACKVECX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_UNPACK, 2):
        {
            const int16_t vec_reg = (int16_t)WOORT_BYTECODE(BC16, c);

            woort_GCVec* const gcvec = rt_sb[vec_reg].m_vec;
            const size_t size = gcvec->m_length;

            // 检查栈空间
            if ((size_t)(rt_sp - rt_stack) < size)
                WOORT_VM_THROW(stack_overflow);

            rt_sb[(int8_t)WOORT_BYTECODE(A8, c)].m_integer = (woort_Int)size;

            // 将向量的元素作为 DynBox 压入栈中（保持顺序）
            for (size_t i = 0; i < size; ++i)
                rt_sp[-(ptrdiff_t)i].m_dynamic = gcvec->m_datas[size - 1 - i];

            rt_sp -= size;
            break;
        }
        // PUSHIDXSTRUCT
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_UNPACK, 3):
        {
            if (rt_sp > rt_stack)
            {
                const size_t index = (size_t)WOORT_BYTECODE(A8, c);
                woort_GCStruct* const gcstruct =
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct;

                assert(index < gcstruct->m_size);

                *(rt_sp--) = gcstruct->m_datas[index];
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHIDXSTBOXI
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHIDXSTBOX, 0):
        {
            if (rt_sp > rt_stack)
            {
                const size_t index = WOORT_BYTECODE(A8, c);
                woort_GCStruct* const gcstruct =
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct;

                assert(index < gcstruct->m_size);

                rt_sp->m_dynamic = woort_DynBox_box_int(
                    gcstruct->m_datas[index].m_integer);
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHIDXSTBOXR
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHIDXSTBOX, 1):
        {
            if (rt_sp > rt_stack)
            {
                const size_t index = WOORT_BYTECODE(A8, c);
                woort_GCStruct* const gcstruct =
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct;

                assert(index < gcstruct->m_size);

                rt_sp->m_dynamic = woort_DynBox_box_real(
                    gcstruct->m_datas[index].m_real);
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHIDXSTBOXB
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHIDXSTBOX, 2):
        {
            if (rt_sp > rt_stack)
            {
                const size_t index = WOORT_BYTECODE(A8, c);
                woort_GCStruct* const gcstruct =
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct;

                assert(index < gcstruct->m_size);

                rt_sp->m_dynamic = woort_DynBox_box_bool(
                    gcstruct->m_datas[index].m_integer);
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PUSHIDXSTBOXX
        case WOORT_VM_CASE_OP6_M2(WOORT_OPCODE_PUSHIDXSTBOX, 3):
        {
            if (rt_sp > rt_stack)
            {
                const size_t index = WOORT_BYTECODE(A8, c);
                woort_GCStruct* const gcstruct =
                    rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_struct;

                assert(index < gcstruct->m_size);

                rt_sp->m_dynamic = gcstruct->m_datas[index].m_dynamic;
                break;
            }
            WOORT_VM_THROW(stack_overflow);
        }
        // PACKARG
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_PACKARG):
        {
            const woort_Value* const argument_to_pack = &rt_sb[
                3 /* First argument place */
                    + 1 /* Argument count for variadic function */
                    + WOORT_BYTECODE(MA10, c) /* Skip count */];

            woort_GCVec* const gcvec = woort_GCVec_new();
            rt_sb[(int16_t)WOORT_BYTECODE(BC16, c)].m_vec = gcvec;

            const size_t pack_argc = rt_sb[3].m_integer;

            // NOTE: 此处不同步虚拟机状态直接分配是没有问题的，如果分配失败
            //      会假定整个栈空间都在被使用中，肯定能标记到 gcvec 实例
            woort_GCVec_resize(gcvec, pack_argc);

            // NOTE: PACKARG 指令被用于收集变长的参数，因此栈中的值预期均为
            //      DynBox, 直接使用 memcpy 复制到新的数组实例中完成装箱
            memcpy(gcvec->m_datas, argument_to_pack, pack_argc * sizeof(woort_DynBox));

            break;
        }
        case WOORT_VM_CASE_OP6(WOORT_OPCODE_TRAP):
        {
            if (!woort_VMRuntime_Debugger_try_trap())
            {
                /* TODO: 如果没有调试器，但是陷入了 TRAP 指令，通知 CodeEnv 清空 Trap */
            }
            // 通过 CodeEnv 查询 Trap，获取原本的指令，然后重新执行

            goto _label_vm_dispatch_reentry_for_debug_trap;
        }
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
        const uint32_t request_mask = woort_atomic_load_explicit(
            &vm->m_check_request_mask,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        if (request_mask != 0)
        {
            if (request_mask & WOORT_VMRUNTIME_CHECK_REQUEST_ABORT)
            {
                woort_panic(
                    WOORT_PANIC_ABORTED,
                    "Aborted vm.",
                    request_mask);

                return WOORT_VM_CALL_STATUS_ABORTED;
            }
            else if (request_mask
                & (WOORT_VMRUNTIME_CHECK_REQUEST_GC_CHECK
                    | WOORT_VMRUNTIME_CHECK_REQUEST_GC_PROCESSING))
            {
                woort_VMRuntime_handle_gc_check_request_and_mark(vm);
            }
            else if (request_mask
                & WOORT_VMRUNTIME_CHECK_REQUEST_STACK_OCCUPYING)
            {
                // TODO: 栈占用的等待机制需要重做，用 hangup 不大合适
                if (woort_VMRuntime_request_accept(
                    vm,
                    WOORT_VMRUNTIME_CHECK_REQUEST_STACK_OCCUPYING))
                {
                    woort_VMRuntime_hangup(vm);
                }
            }
            else if (request_mask
                & WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK)
            {
                if (woort_VMRuntime_Debugger_try_trap())
                {
                    (void)woort_VMRuntime_request_accept(
                        vm,
                        WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK);
                }
            }
            else
            {
                WOORT_VM_SYNC_STATE_AND_PANIC(
                    WOORT_PANIC_BAD_VM_REQUEST,
                    "Bad vm request: %x",
                    request_mask);
            }
        }
        WOORT_VM_HANDLED();
    }
    WOORT_VM_EXCEPTION_LABEL(index_out_of_range) :
    {
        WOORT_VM_SYNC_STATE_AND_PANIC(
            WOORT_PANIC_INDEX_OUT_OF_RANGE,
            "Index out of range.");
        WOORT_VM_HANDLED();
    }
    WOORT_VM_EXCEPTION_LABEL(stack_overflow) :
    {
        // Stack used up, try extern.
        while (/* UNLIKELY */ !_woort_VMRuntime_extern_stack(vm))
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
        &vm->m_check_request_mask, check_mask, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL));
}

WOORT_NODISCARD bool woort_VMRuntime_request_check(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask)
{
    return 0 != (check_mask & woort_atomic_load_explicit(
        &vm->m_check_request_mask, WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE));
}

WOORT_NODISCARD bool woort_VMRuntime_request_accept(
    woort_VMRuntime* vm, woort_VMRuntime_CheckRequestMask check_mask)
{
    return 0 != (check_mask & woort_atomic_fetch_and_explicit(
        &vm->m_check_request_mask, ~check_mask, WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL));
}

void woort_VMRuntime_mark_vm_after_sync(woort_VMRuntime* vm)
{
    woomem_mark_unit_head(vm->m_env);

    // TODO: Optimize for fast marking.
    for (const void** p = (void**)vm->m_sp; p != (void**)vm->m_stack_end; ++p)
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

WOORT_NODISCARD /* OPTIONAL */ woort_VMRuntime* woort_VMRuntime_swap(
    /* OPTIONAL */ woort_VMRuntime* vm)
{
    if (WOORT_t_this_thread_vm == vm)
        return vm;

    woort_VMRuntime* const last_vm = WOORT_t_this_thread_vm;

    if (last_vm != NULL)
    {
        const bool r = woort_VMRuntime_request_set(
            last_vm,
            WOORT_VMRUNTIME_CHECK_REQUEST_GC_LEAVE);

        (void)r;
        assert(r);
    }
    WOORT_t_this_thread_vm = vm;
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
