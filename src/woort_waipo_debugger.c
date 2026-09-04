#include "woort.h"
#include "woort_waipo_debugger.h"
#include "woort_vm_debugger_api.h"
#include "woort_threads.h"
#include "woort_hashmap.h"
#include "woort_util.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_opcode.h"
#include "woort_atomic.h"
#include "woort_value.h"
#include "woort_mem.h"
#include "woort_gc.h"
#include "woort_gc_closure.h"
#include "woort_spin.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*
Watch And Inspect Program Operation
*/

static void _woort_WAIPO_BreakpointCollection_init(
    woort_WAIPO_BreakpointCollection* collection)
{
    woort_hashmap_init(
        &collection->m_breakpoints,
        sizeof(woort_Bytecode*),
        sizeof(size_t),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    woort_hashmap_init(
        &collection->m_debug_breakpoints,
        sizeof(woort_Bytecode*),
        sizeof(size_t),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    woort_vector_init(
        &collection->m_user_breakpoints,
        sizeof(woort_WAIPO_UserBreakpoint));

    collection->m_next_breakpoint_id = 1;

    woort_rwspinlock_init(&collection->m_rwspin);
}

static void _woort_WAIPO_BreakpointCollection_deinit(
    woort_WAIPO_BreakpointCollection* collection)
{
    for (size_t i = 0; i < collection->m_user_breakpoints.m_size; ++i)
    {
        woort_WAIPO_UserBreakpoint* ub =
            (woort_WAIPO_UserBreakpoint*)woort_vector_at(
                &collection->m_user_breakpoints, i);
        woort_vector_deinit(&ub->m_ips);
    }
    woort_hashmap_deinit(&collection->m_breakpoints);
    woort_hashmap_deinit(&collection->m_debug_breakpoints);
    woort_vector_deinit(&collection->m_user_breakpoints);

    woort_rwspinlock_deinit(&collection->m_rwspin);
}

/*
 * 占用 ip 处的 TRAP（地址计数，同一地址可被多条断点与步进断点共享），
 * 写入失败（地址不属于任何 CodeEnv 或内存不足）返回 false。
 */
WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_break_at_without_lock(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    woort_CodeEnv* cenv;
    if (woort_CodeEnv_find(ip, &cenv))
    {
        size_t* counter;
        switch (woort_hashmap_get_or_emplace(&collection->m_breakpoints, &ip, (void**)&counter))
        {
        case WOORT_HASHMAP_RESULT_OK:
            *counter = 1;
            /*
            NOTE: 因为之前的调试器实例遗留的 Trap，woort_CodeEnv_set_trap 可能会失败。不过
                我们不在意，此处直接假装是我们设置的即可。
            */
            (void)woort_CodeEnv_set_trap(cenv, (woort_Bytecode*)ip);
            return true;
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
            ++*counter;
            return true;
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            break;
        }
    }
    return false;
}

static void _woort_WAIPO_BreakpointCollection_cancel_break_at_without_lock(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    size_t* counter;
    if (woort_hashmap_find(&collection->m_breakpoints, &ip, (void**)&counter))
    {
        --*counter;
        if (*counter == 0)
        {
            (void)woort_hashmap_remove(&collection->m_breakpoints, &ip);

            woort_CodeEnv* cenv;
            if (woort_CodeEnv_find(ip, &cenv))
                (void)woort_CodeEnv_clear_trap(cenv, (woort_Bytecode*)ip);
        }
    }
}

/*
 * 以用户断点身份占用 ip：除 m_breakpoints 的 TRAP 地址计数外，同时在
 * m_debug_breakpoints 中累计该地址的无条件断点持有数——与步进断点不同，
 * 用户断点对任意 VM（无论是否处于关注中）命中即中断。
 * 写入失败（TRAP 占用失败或内存不足）返回 false；后者会回滚刚占用的
 * TRAP 计数，保持两张表一致。
 */
WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_break_debug_at_without_lock(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    if (!_woort_WAIPO_BreakpointCollection_break_at_without_lock(collection, ip))
        return false;

    size_t* debug_counter;
    switch (woort_hashmap_get_or_emplace(
        &collection->m_debug_breakpoints, &ip, (void**)&debug_counter))
    {
    case WOORT_HASHMAP_RESULT_OK:
        *debug_counter = 1;
        return true;
    case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        ++*debug_counter;
        return true;
    case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
        break;
    }

    /* 无条件断点记录失败，回滚刚占用的 TRAP 计数 */
    _woort_WAIPO_BreakpointCollection_cancel_break_at_without_lock(collection, ip);
    return false;
}

/*
 * 释放用户断点对 ip 的占用：递减 m_debug_breakpoints 的持有数，归零时摘除
 * 条目（地址不再无条件中断），并释放对应的 TRAP 地址计数。
 */
static void _woort_WAIPO_BreakpointCollection_cancel_debug_break_at_without_lock(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    size_t* debug_counter;
    if (woort_hashmap_find(&collection->m_debug_breakpoints, &ip, (void**)&debug_counter))
    {
        --*debug_counter;
        if (*debug_counter == 0)
            (void)woort_hashmap_remove(&collection->m_debug_breakpoints, &ip);
    }

    _woort_WAIPO_BreakpointCollection_cancel_break_at_without_lock(collection, ip);
}

/*
 * 向断点集合添加一条用户断点：占用 ips 给出的全部地址并记入无条件断点表
 * （m_debug_breakpoints，任意 VM 命中即中断），任一地址写入失败则整体回滚
 * （返回 false）。desc_fmt 为 NULL 时描述留空。
 * line 为 0 起始源码行号，WOORT_WAIPO_DEBUGGER_BREAKPOINT_NO_LINE 表示无行号信息（函数名断点）。
 * 成功时分配稳定递增的断点编号，经 out_id（可选）返回给调用方显示。
 */
WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_add_user_breakpoint_impl_without_lock(
    woort_WAIPO_BreakpointCollection* collection,
    const woort_Bytecode* const* ips,
    size_t ip_count,
    size_t line,
    /* OPTIONAL */ woort_WAIPO_Debugger_BreakpointId* out_id,
    /* OPTIONAL */ const char* desc_fmt,
    va_list args)
{
    assert(ips != NULL);

    if (ip_count == 0)
        return false;

    woort_WAIPO_UserBreakpoint ub;
    /* 编号先取自计数器，仅在整体成功后才推进计数器（失败不烧号） */
    ub.m_id = collection->m_next_breakpoint_id;
    ub.m_line = line;
    woort_vector_init(&ub.m_ips, sizeof(const woort_Bytecode*));

    if (desc_fmt != NULL)
        (void)vsnprintf(ub.m_desc, sizeof(ub.m_desc), desc_fmt, args);
    else
        ub.m_desc[0] = '\0';

    woort_WAIPO_UserBreakpoint* emplaced;
    if (!woort_vector_emplace_back(&collection->m_user_breakpoints, 1, (void**)&emplaced))
    {
        woort_vector_deinit(&ub.m_ips);
        return false;
    }

    *emplaced = ub;

    for (size_t i = 0; i < ip_count; ++i)
    {
        const bool applied =
            _woort_WAIPO_BreakpointCollection_break_debug_at_without_lock(collection, ips[i]);

        if (applied && woort_vector_push_back(&emplaced->m_ips, 1, &ips[i]))
            continue;

        if (applied)
            _woort_WAIPO_BreakpointCollection_cancel_debug_break_at_without_lock(
                collection, ips[i]);

        /* 回滚：取消已生效的断点并移除条目 */
        for (size_t j = 0; j < emplaced->m_ips.m_size; ++j)
            _woort_WAIPO_BreakpointCollection_cancel_debug_break_at_without_lock(
                collection,
                *(const woort_Bytecode**)woort_vector_at(&emplaced->m_ips, j));

        woort_vector_deinit(&emplaced->m_ips);
        (void)woort_vector_erase_at(
            &collection->m_user_breakpoints,
            collection->m_user_breakpoints.m_size - 1);
        return false;
    }

    ++collection->m_next_breakpoint_id;
    if (out_id != NULL)
        *out_id = ub.m_id;

    return true;
}

WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    woort_rwspinlock_write_lock(&collection->m_rwspin);
    const bool result = _woort_WAIPO_BreakpointCollection_break_at_without_lock(collection, ip);
    woort_rwspinlock_write_unlock(&collection->m_rwspin);

    return result;
}

static void _woort_WAIPO_BreakpointCollection_cancel_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    woort_rwspinlock_write_lock(&collection->m_rwspin);
    _woort_WAIPO_BreakpointCollection_cancel_break_at_without_lock(collection, ip);
    woort_rwspinlock_write_unlock(&collection->m_rwspin);
}

WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_add_user_breakpoint(
    woort_WAIPO_BreakpointCollection* collection,
    const woort_Bytecode* const* ips,
    size_t ip_count,
    size_t line,
    /* OPTIONAL */ woort_WAIPO_Debugger_BreakpointId* out_id,
    /* OPTIONAL */ const char* desc_fmt,
    ...)
{
    va_list args;
    va_start(args, desc_fmt);

    woort_rwspinlock_write_lock(&collection->m_rwspin);

    const bool result =
        _woort_WAIPO_BreakpointCollection_add_user_breakpoint_impl_without_lock(
            collection,
            ips,
            ip_count,
            line,
            out_id,
            desc_fmt,
            args);

    woort_rwspinlock_write_unlock(&collection->m_rwspin);

    va_end(args);
    return result;
}

/*
 * 查询 ip 处是否有断点持有的 TRAP（用户断点与步进断点共用同一套地址计数）。
 */
WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_contains_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    woort_rwspinlock_read_lock(&collection->m_rwspin);
    const bool result = woort_hashmap_contains(&collection->m_breakpoints, &ip);
    woort_rwspinlock_read_unlock(&collection->m_rwspin);

    return result;
}

WOORT_NODISCARD bool _woort_WAIPO_BreakpointCollection_contains_debug_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip)
{
    woort_rwspinlock_read_lock(&collection->m_rwspin);
    const bool result = woort_hashmap_contains(&collection->m_debug_breakpoints, &ip);
    woort_rwspinlock_read_unlock(&collection->m_rwspin);

    return result;
}

/*
 * 按下标取用户断点记录（下标即创建序）
 */
WOORT_NODISCARD static const woort_WAIPO_UserBreakpoint*
_woort_WAIPO_BreakpointCollection_get_user_breakpoint_at_without_lock(
    woort_WAIPO_BreakpointCollection* collection, size_t index)
{
    assert(index < collection->m_user_breakpoints.m_size);

    const woort_WAIPO_UserBreakpoint* const result =
        (const woort_WAIPO_UserBreakpoint*)woort_vector_at(
            &collection->m_user_breakpoints, index);

    return result;
}

/*
 * 从用户断点记录填充公开的断点信息。m_filename 指向断点记录自带的描述缓冲
 * （该断点被删除或调试器卸载前有效），描述为空时置 NULL；m_line 为 0 起始
 * 源码行号，无行号信息（函数名断点）时为 WOORT_WAIPO_DEBUGGER_BREAKPOINT_NO_LINE。
 */
static void _woort_WAIPO_Debugger_set_breakpoint_info(
    woort_WAIPO_Debugger_BreakpointInfo* info,
    const woort_WAIPO_UserBreakpoint* ub)
{
    info->m_id = ub->m_id;
    
    _Static_assert(
        WOORT_WAIPO_DEBUGGER_BREAKPOINT_INFO_NAME_LEN == sizeof(info->m_name)
        && sizeof(info->m_name) == sizeof(ub->m_desc), "Size must be match.");
    memcpy(info->m_name, ub->m_desc, sizeof(info->m_name));

    info->m_line = ub->m_line;
}

WOORT_NODISCARD bool
_woort_WAIPO_BreakpointCollection_find_user_breakpoint_and_get_info(
    woort_WAIPO_BreakpointCollection* collection,
    woort_WAIPO_Debugger_BreakpointId id,
    woort_WAIPO_Debugger_BreakpointInfo* out_breakinfo)
{
    woort_rwspinlock_read_lock(&collection->m_rwspin);

    /* 编号稳定且不复用，按编号线性查找而非下标 */
    bool found = false;

    const size_t count = collection->m_user_breakpoints.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        const woort_WAIPO_UserBreakpoint*  user_breakpoint =
            _woort_WAIPO_BreakpointCollection_get_user_breakpoint_at_without_lock(
                collection, i);

        if (user_breakpoint->m_id == id)
        {
            _woort_WAIPO_Debugger_set_breakpoint_info(
                out_breakinfo, user_breakpoint);

            found = true;
            break;
        }
    }
    woort_rwspinlock_read_unlock(&collection->m_rwspin);

    return found;
}

/*
 * 移除第 index 条用户断点：取消其持有的全部地址并释放记录。
 * 同一地址可能被多条断点共享，cancel 仅递减计数，最后一条才真正摘除 TRAP
 * 与无条件断点标记。
 */
static void _woort_WAIPO_BreakpointCollection_remove_user_breakpoint_at_without_lock(
    woort_WAIPO_BreakpointCollection* collection, size_t index)
{
    woort_WAIPO_UserBreakpoint* const ub =
        (woort_WAIPO_UserBreakpoint*)woort_vector_at(
            &collection->m_user_breakpoints, index);

    for (size_t k = 0; k < ub->m_ips.m_size; ++k)
    {
        _woort_WAIPO_BreakpointCollection_cancel_debug_break_at_without_lock(
            collection,
            *(const woort_Bytecode**)woort_vector_at(&ub->m_ips, k));
    }

    woort_vector_deinit(&ub->m_ips);

    (void)woort_vector_erase_at(&collection->m_user_breakpoints, index);
}

WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_delete_user_breakpoint(
    woort_WAIPO_BreakpointCollection* collection,
    woort_WAIPO_Debugger_BreakpointId id)
{
    woort_rwspinlock_write_lock(&collection->m_rwspin);

    /* 编号稳定且不复用，按编号线性查找而非下标 */
    const size_t count = collection->m_user_breakpoints.m_size;
    bool found = false;

    for (size_t i = 0; i < count; ++i)
    {
        const woort_WAIPO_UserBreakpoint* const ub =
            _woort_WAIPO_BreakpointCollection_get_user_breakpoint_at_without_lock(collection, i);

        if (ub->m_id == id)
        {
            _woort_WAIPO_BreakpointCollection_remove_user_breakpoint_at_without_lock(collection, i);
            found = true;
            break;
        }
    }
    woort_rwspinlock_write_unlock(&collection->m_rwspin);

    return found;
}

static void _woort_WAIPO_BreakpointCollection_clear_user_breakpoints(
    woort_WAIPO_BreakpointCollection* collection)
{
    woort_rwspinlock_write_lock(&collection->m_rwspin);

    /* 从尾部向前逐条移除，erase_at 无需搬移后续元素；编号分配器不重置，
       清空后新设断点仍不与历史编号冲突 */
    while (collection->m_user_breakpoints.m_size != 0)
    {
        _woort_WAIPO_BreakpointCollection_remove_user_breakpoint_at_without_lock(
            collection,
            collection->m_user_breakpoints.m_size - 1);
    }
    woort_rwspinlock_write_unlock(&collection->m_rwspin);
}

/*
 * 按创建序枚举全部用户断点，逐条转换为公开的断点信息后经 callback 回调
 * （userdata 透传）；回调返回 false 时提前终止枚举并整体返回 false，
 * 全部枚举完毕返回 true。
 *
 * 回调在持有 m_rwspin 读锁期间被调用，而该读写自旋锁不可重入：
 * 回调内（同线程）不得再调用任何断点接口（set / delete / clear /
 * query / get_breakpoint_info，含经其他函数间接调用）——写接口会等待
 * 本线程尚未释放的读锁，必然自死锁；读接口在恰有其他线程等待写入时
 * 也会与之互旋死锁。需要对断点做其他操作时，只能在枚举返回前后进行。
 */
WOORT_NODISCARD static bool _woort_WAIPO_BreakpointCollection_query_breakpoints(
    woort_WAIPO_BreakpointCollection* collection,
    woort_WAIPO_Debugger_QueryBreakpointCallback callback,
    void* userdata)
{
    woort_rwspinlock_read_lock(&collection->m_rwspin);

    const size_t count = collection->m_user_breakpoints.m_size;
    bool canceled = false;

    for (size_t i = 0; i < count; ++i)
    {
        const woort_WAIPO_UserBreakpoint* const ub =
            _woort_WAIPO_BreakpointCollection_get_user_breakpoint_at_without_lock(collection, i);

        woort_WAIPO_Debugger_BreakpointInfo info;
        _woort_WAIPO_Debugger_set_breakpoint_info(&info, ub);

        if (!callback(&info, userdata))
        {
            canceled = true;
            break;
        }
    }
    woort_rwspinlock_read_unlock(&collection->m_rwspin);

    return !canceled;
}

static bool _woort_WAIPO_collect_debug_break_ip_callback(
    const void* key, void* value, void* user_data)
{
    (void)value; /* 持有计数不导出，仅需地址 */

    woort_Vector /* const woort_Bytecode* */* const ips =
        (woort_Vector*)user_data;

    const woort_Bytecode* const ip = *(const woort_Bytecode* const*)key;
    return woort_vector_push_back(ips, 1, &ip);
}

/*
 * 收集 m_debug_breakpoints 中全部无条件断点的指令地址，追加到
 * modify_break_ips（元素类型 const woort_Bytecode*，由调用方初始化/释放）。
 * 哈希表键唯一，结果不含重复地址；仅内存不足时会截断。
 */
void _woort_WAIPO_BreakpointCollection_collect_debug_breakpoints(
    woort_WAIPO_BreakpointCollection* collection,
    woort_Vector /* const woort_Bytecode* */* modify_break_ips)
{
    woort_rwspinlock_read_lock(&collection->m_rwspin);
    (void)woort_hashmap_foreach(
        &collection->m_debug_breakpoints,
        &_woort_WAIPO_collect_debug_break_ip_callback,
        modify_break_ips);
    woort_rwspinlock_read_unlock(&collection->m_rwspin);
}

typedef struct _woort_WAIPO_CollectBreakIpsContext
{
    woort_CodeEnv* m_cenv;
    woort_Vector /* const woort_Bytecode* */* m_ips;
} _woort_WAIPO_CollectBreakIpsContext;

static bool _woort_WAIPO_collect_break_ip_callback(
    uint32_t bytecode_offset, void* user_data)
{
    _woort_WAIPO_CollectBreakIpsContext* ctx =
        (_woort_WAIPO_CollectBreakIpsContext*)user_data;

    const woort_Bytecode* ip = ctx->m_cenv->m_code_begin + bytecode_offset;

    /* 同一偏移只记录一次 */
    for (size_t k = 0; k < ctx->m_ips->m_size; ++k)
    {
        if (*(const woort_Bytecode**)woort_vector_at(ctx->m_ips, k) == ip)
            return true;
    }

    return woort_vector_push_back(ctx->m_ips, 1, &ip);
}

/*
 * 收集 CodeEnv 中指定源码行（srcloc 行号，0 起始）关联的全部指令地址，
 * 同一偏移只记录一次。out_ips 由调用方初始化/释放。
 * 返回值语义与 woort_CodeEnv_foreach_offset_by_srcloc 一致。
 */
WOORT_NODISCARD static bool _woort_WAIPO_collect_line_break_ips(
    woort_CodeEnv* cenv,
    const char* filepath,
    uint32_t line /* srcloc 行号，0 起始 */,
    woort_Vector /* const woort_Bytecode* */* out_ips)
{
    _woort_WAIPO_CollectBreakIpsContext ctx;
    ctx.m_cenv = cenv;
    ctx.m_ips = out_ips;

    return woort_CodeEnv_foreach_offset_by_srcloc(
        cenv, filepath, line,
        &_woort_WAIPO_collect_break_ip_callback, &ctx);
}

typedef struct woort_WAIPO_VMLocalContext
{
    woort_WAIPO_BreakpointCollection* m_breakpoint_collection;

    /* m_step_breakpoints 用于进一步筛选是否是当前 VM 的步进断点 */
    /* OPTIONAL */ const woort_Bytecode* m_step_breakpoints[2];

    /* 源码行级步进状态 */
    bool m_is_source_step;
    /* OPTIONAL */ const char* m_step_source_file;
    size_t m_step_source_line;
    size_t m_step_source_begin_column;
    size_t m_step_source_end_line;
    size_t m_step_source_end_column;

    /* "next" 步进状态 */
    bool m_is_source_next;
    size_t m_step_target_depth;

    /* "return" 步进状态 */
    bool m_is_source_return;

}woort_WAIPO_VMLocalContext;

static void _woort_WAIPO_VMLocalContext_init(
    woort_WAIPO_VMLocalContext* vmcontext, woort_WAIPO_BreakpointCollection* collection)
{
    vmcontext->m_breakpoint_collection = collection;
    vmcontext->m_step_breakpoints[0] = NULL;
    vmcontext->m_step_breakpoints[1] = NULL;
    vmcontext->m_is_source_step = false;
    vmcontext->m_step_source_file = NULL;
    vmcontext->m_step_source_line = 0;
    vmcontext->m_step_source_begin_column = 0;
    vmcontext->m_step_source_end_line = 0;
    vmcontext->m_step_source_end_column = 0;
    vmcontext->m_is_source_next = false;
    vmcontext->m_step_target_depth = 0;
    vmcontext->m_is_source_return = false;
}

WOORT_NODISCARD static bool _woort_WAIPO_VMLocalContext_set_stepir_breakpoint(
    woort_WAIPO_VMLocalContext* vmcontext, const woort_Bytecode* breakdown_ip)
{
    for (size_t i = 0; i < 2; ++i)
    {
        if (vmcontext->m_step_breakpoints[i] == NULL)
        {
            if (_woort_WAIPO_BreakpointCollection_break_at(
                vmcontext->m_breakpoint_collection, breakdown_ip))
            {
                vmcontext->m_step_breakpoints[i] = breakdown_ip;
                return true;
            }
            break;
        }
    }
    return false;
}

static void _woort_WAIPO_VMLocalContext_set_source_step(
    woort_WAIPO_VMLocalContext* vmcontext,
    /* OPTIONAL */ const char* filepath,
    size_t line,
    size_t begin_column,
    size_t end_line,
    size_t end_column)
{
    vmcontext->m_is_source_step = true;
    vmcontext->m_step_source_file = filepath;
    vmcontext->m_step_source_line = line;
    vmcontext->m_step_source_begin_column = begin_column;
    vmcontext->m_step_source_end_line = end_line;
    vmcontext->m_step_source_end_column = end_column;
}

static void _woort_WAIPO_VMLocalContext_clean_step_breakpoint(
    woort_WAIPO_VMLocalContext* vmcontext)
{
    for (size_t i = 0; i < 2; ++i)
    {
        if (vmcontext->m_step_breakpoints[i] != NULL)
        {
            _woort_WAIPO_BreakpointCollection_cancel_break_at(
                vmcontext->m_breakpoint_collection,
                vmcontext->m_step_breakpoints[i]);

            vmcontext->m_step_breakpoints[i] = NULL;
        }
    }
    vmcontext->m_is_source_step = false;
    vmcontext->m_step_source_file = NULL;
    vmcontext->m_step_source_line = 0;
    vmcontext->m_step_source_begin_column = 0;
    vmcontext->m_step_source_end_line = 0;
    vmcontext->m_step_source_end_column = 0;
    vmcontext->m_is_source_next = false;
    vmcontext->m_step_target_depth = 0;
    vmcontext->m_is_source_return = false;
}

static void _woort_WAIPO_VMLocalContext_deinit(woort_WAIPO_VMLocalContext* vmcontext)
{
    _woort_WAIPO_VMLocalContext_clean_step_breakpoint(vmcontext);
}

static bool _woort_WAIPO_VMLocalContext_meet_step_breakdown(
    woort_WAIPO_VMLocalContext* vmcontext, const woort_Bytecode* ip)
{
    return vmcontext->m_step_breakpoints[0] == ip
        || vmcontext->m_step_breakpoints[1] == ip;
}

static size_t _woort_WAIPO_get_current_callstack_depth(woort_VMRuntime* vm)
{
    woort_VMRuntime_TraceCallstack_Iter iter;
    woort_VMRuntime_trace_begin(vm, &iter);
    size_t depth = 0;
    while (woort_VMRuntime_trace_next(&iter, NULL))
    {
        ++depth;
    }
    return depth;
}

WOORT_NODISCARD bool _woort_WAIPO_trace_to_depth(
    woort_VMRuntime* vm,
    woort_WAIPO_Debugger_FrameId target_depth,
    /* OPTIONAL */ woort_VMRuntime_TraceCallstack* out_trace)
{
    woort_VMRuntime_TraceCallstack_Iter trace_iter;
    woort_VMRuntime_TraceCallstack trace;

    woort_VMRuntime_trace_begin(vm, &trace_iter);

    size_t depth = 0;
    while (woort_VMRuntime_trace_next(&trace_iter, out_trace == NULL ? NULL : &trace))
    {
        if (depth == target_depth)
        {
            if (out_trace != NULL)
                *out_trace = trace;
            return true;
        }
        ++depth;
    }

    return false;
}

WOORT_NODISCARD bool _woort_WAIPO_VMLocalContext_set_stepin_breakpoint(
    woort_WAIPO_VMLocalContext* vmcontext,
    woort_VMRuntime* vm,
    const woort_Bytecode* breakdown_ip)
{
    const uint32_t code_offset = (uint32_t)(vm->m_ip - vm->m_env->m_code_begin);

    woort_SourceLocation src_loc;
    if (woort_CodeEnv_find_srcloc_by_offset(vm->m_env, code_offset, &src_loc))
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext,
            src_loc.m_filepath,
            (size_t)src_loc.m_begin_line,
            (size_t)src_loc.m_begin_column,
            (size_t)src_loc.m_end_line,
            (size_t)src_loc.m_end_column);
    }
    else
    {
        _woort_WAIPO_VMLocalContext_set_source_step(
            vmcontext, NULL, 0, 0, 0, 0);
    }

    return _woort_WAIPO_VMLocalContext_set_stepir_breakpoint(vmcontext, breakdown_ip);
}

WOORT_NODISCARD bool _woort_WAIPO_VMLocalContext_set_stepover_breakpoint(
    woort_WAIPO_VMLocalContext* vmcontext,
    woort_VMRuntime* vm,
    const woort_Bytecode* breakdown_ip)
{
    if (!_woort_WAIPO_VMLocalContext_set_stepin_breakpoint(
        vmcontext, vm, breakdown_ip))
    {
        /* Failed to set step breakpoint. */
        return false;
    }

    vmcontext->m_is_source_next = true;
    vmcontext->m_step_target_depth = _woort_WAIPO_get_current_callstack_depth(vm);

    return true;
}

WOORT_NODISCARD bool _woort_WAIPO_VMLocalContext_set_stepout_breakpoint(
    woort_WAIPO_VMLocalContext* vmcontext,
    woort_VMRuntime* vm,
    const woort_Bytecode* breakdown_ip)
{
    if (!_woort_WAIPO_VMLocalContext_set_stepin_breakpoint(
        vmcontext, vm, breakdown_ip))
    {
        /* Failed to set step breakpoint. */
        return false;
    }

    vmcontext->m_is_source_return = true;
    vmcontext->m_step_target_depth = _woort_WAIPO_get_current_callstack_depth(vm);

    return true;
}

WOORT_NODISCARD static bool _woort_WAIPO_Debugger_focus_on(
    woort_WAIPO_Debugger* debugger_instance,
    woort_VMRuntime* vm,
    woort_WAIPO_VMLocalContext** out_local_context)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    switch (woort_hashmap_get_or_emplace(
        &debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
    {
    case WOORT_HASHMAP_RESULT_OK:
        _woort_WAIPO_VMLocalContext_init(vmcontext, &debugger_instance->m_breakpoint_collection);
        break;
    case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        break;
    case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
        /* Emm... */
        return false;
    }
    *out_local_context = vmcontext;
    return true;
}

static void _woort_WAIPO_Debugger_out_of_focus(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    woort_WAIPO_VMLocalContext* vmcontext;
    if (woort_hashmap_find(&debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
    {
        _woort_WAIPO_VMLocalContext_deinit(vmcontext);
        (void)woort_hashmap_remove(&debugger_instance->m_focusing_vms, &vm);
    }
}

static int _woort_WAIPO_empty_cb(const char* fmt, ...)
{
    (void)fmt;
    return 0;
}

/*
 * 根据当前指令和 VM 状态计算下一条指令的地址（用于单步执行）。
 * 考虑跳转、调用、返回等所有控制流转移情况。
 * 返回 false 表示无法确定下一条指令（如从 native 函数返回）。
 */
WOORT_NODISCARD static bool _woort_WAIPO_get_next_ip(
    const woort_Bytecode* ip,
    woort_CodeEnv* cenv,
    const woort_Value* sb,
    woort_VMRuntime* vm,
    /* OPTIONAL */ const woort_Bytecode** out_next_ip)
{
    assert(ip != NULL && cenv != NULL && sb != NULL);

    if (out_next_ip == NULL)
        return false;

    if (ip < cenv->m_code_begin || ip >= cenv->m_code_end)
        return false;

    const woort_Bytecode bc = woort_CodeEnv_raw_trap(cenv, ip);
    const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
    const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

    switch ((woort_Opcode)op6)
    {
    case WOORT_OPCODE_JFWD:
    case WOORT_OPCODE_JBCK:
    {
        *out_next_ip = cenv->m_code_begin + WOORT_BYTECODE(MABC26, bc);
        return true;
    }

    case WOORT_OPCODE_JFWDCND:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        switch (m2)
        {
        case 0: /* JFWDNZ */
            if (sb[a_offset].m_integer != 0)
            {
                *out_next_ip = ip + (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 1: /* JFWDZ */
            if (sb[a_offset].m_integer == 0)
            {
                *out_next_ip = ip + (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 2: /* JFWDEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer == sb[b_offset].m_integer)
            {
                *out_next_ip = ip + (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        case 3: /* JFWDNEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer != sb[b_offset].m_integer)
            {
                *out_next_ip = ip + (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_JBCKCND:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        switch (m2)
        {
        case 0: /* JBCKNZ */
            if (sb[a_offset].m_integer != 0)
            {
                *out_next_ip = ip - (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 1: /* JBCKZ */
            if (sb[a_offset].m_integer == 0)
            {
                *out_next_ip = ip - (int16_t)WOORT_BYTECODE(BC16, bc);
                return true;
            }
            break;
        case 2: /* JBCKEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer == sb[b_offset].m_integer)
            {
                *out_next_ip = ip - (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        case 3: /* JBCKNEQ */
        {
            const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
            if (sb[a_offset].m_integer != sb[b_offset].m_integer)
            {
                *out_next_ip = ip - (int8_t)WOORT_BYTECODE(C8, bc);
                return true;
            }
            break;
        }
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_JFDCMP:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
        bool taken = false;
        switch (m2)
        {
        case 0: taken = (sb[a_offset].m_integer < sb[b_offset].m_integer); break;
        case 1: taken = (sb[a_offset].m_integer > sb[b_offset].m_integer); break;
        case 2: taken = (sb[a_offset].m_integer <= sb[b_offset].m_integer); break;
        case 3: taken = (sb[a_offset].m_integer >= sb[b_offset].m_integer); break;
        }
        if (taken)
        {
            *out_next_ip = ip + (int8_t)WOORT_BYTECODE(C8, bc);
            return true;
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_JBCKCMP:
    {
        const int8_t a_offset = (int8_t)WOORT_BYTECODE(A8, bc);
        const int8_t b_offset = (int8_t)WOORT_BYTECODE(B8, bc);
        bool taken = false;
        switch (m2)
        {
        case 0: taken = (sb[a_offset].m_integer < sb[b_offset].m_integer); break;
        case 1: taken = (sb[a_offset].m_integer > sb[b_offset].m_integer); break;
        case 2: taken = (sb[a_offset].m_integer <= sb[b_offset].m_integer); break;
        case 3: taken = (sb[a_offset].m_integer >= sb[b_offset].m_integer); break;
        }
        if (taken)
        {
            *out_next_ip = ip - (int8_t)WOORT_BYTECODE(C8, bc);
            return true;
        }
        *out_next_ip = ip + 1;
        return true;
    }

    case WOORT_OPCODE_CALLNWO:
    {
        *out_next_ip = cenv->m_data_begin[WOORT_BYTECODE(MABC26, bc)].m_script_function;
        return true;
    }
    case WOORT_OPCODE_CALLNFP:
    case WOORT_OPCODE_CALLNJIT:
    {
        (void)woort_VMRuntime_request_set(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);
        *out_next_ip = ip + 1;
        return true;
    }
    case WOORT_OPCODE_CALL:
    {
        const woort_GCClosure* target;
        if (m2 == 0) /* CALLS */
        {
            target = sb[(int16_t)WOORT_BYTECODE(BC16, bc)].m_closure;
        }
        else /* m2 == 1, CALLC */
        {
            target = cenv->m_data_begin[WOORT_BYTECODE(ABC24, bc)].m_closure;
        }

        // Assure invoking closure is valid.
        const woort_GCClosure* const invoked_closure_instance =
            woort_mem_validate_addr_head((void*)target);

        if (invoked_closure_instance != NULL
            && invoked_closure_instance == target
            && invoked_closure_instance->m_gc_unit.m_proxy == &WOORT_GCCLOSURE_UNIT_PROXY)
        {
            /*
            The minimum unit of memory allocation in Woomem is 8 bytes. We need to
            verify the type of the unit here, and the type information happens to
            fall within the first eight bytes; therefore, reading the first 8 bytes
            of the unit is safe.
            */
            _Static_assert(
                offsetof(woort_GCClosure, m_gc_unit)
                + sizeof(invoked_closure_instance->m_gc_unit) <= 8,
                "woort_GCUnit is too large/far to safely verify its type.");

            if (invoked_closure_instance->m_script_function != NULL)
                *out_next_ip = invoked_closure_instance->m_script_function;
            else
                *out_next_ip = ip + 1;

            return true;
        }
        else
            /* Bad closure instance */
            return false;
    }
    case WOORT_OPCODE_RET:
    {
        if (m2 == 3)
        {
            /* Is POPRS. not ret. */
            goto label_fall_to_default;
        }

        const woort_Value* trace_sb = sb;
        while (trace_sb[1].m_ret_bp.m_way == WOORT_CALL_WAY_FROM_NATIVE)
        {
            trace_sb = trace_sb + 2 + trace_sb[1].m_ret_bp.m_bp_offset;
            if (vm->m_stack_end - trace_sb < 3)
                return false;
        }
        *out_next_ip = (const woort_Bytecode*)trace_sb[2].m_ret_addr;
        return true;
    }
    case WOORT_OPCODE_JIFINITED:
    {
        woort_AtomicInt64* const flag = &cenv->m_data_begin[ip[1]].m_atomic_i64;
        const int64_t flag_stat = woort_atomic_load_explicit(
            (woort_AtomicInt64*)flag,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

        if (flag_stat == 2)
            *out_next_ip = cenv->m_code_begin + WOORT_BYTECODE(MABC26, bc);
        else
            *out_next_ip = ip + 2;

        return true;
    }
    case WOORT_OPCODE_TRAP:
    {
        if (m2 != 0)
            return false;
        *out_next_ip = ip + 1;
        return true;
    }
    default:
    {
    label_fall_to_default:
        *out_next_ip = woort_disassembly(ip, &_woort_WAIPO_empty_cb);
        return true;
    }
    }
}

static bool _woort_WAIPO_Debugger_is_focus_vm(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    return woort_hashmap_contains(&debugger_instance->m_focusing_vms, &vm);
}

typedef struct _woort_WAIPO_SavedStepBreakContext
{
    const char* m_file;
    size_t m_line;
    size_t m_col;
    size_t m_end_line;
    size_t m_end_col;
    bool m_is_stepover;
    bool m_is_stepout;
    size_t m_target_depth;

}_woort_WAIPO_SavedStepBreakContext;

static bool _woort_WAIPO_Debugger_meet_breakpoint(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm)
{
    const woort_Bytecode* current_ip = vm->m_ip;

    bool breakdown = false;
    /* OPTIONAL */ woort_CodeEnv* step_break_trapped_env_for_refill = NULL;
    _woort_WAIPO_SavedStepBreakContext saved_step_break_context_for_restoring;

    if (_woort_WAIPO_BreakpointCollection_contains_break_at(
        &debugger_instance->m_breakpoint_collection, current_ip))
    {
        /* Might be next, return, step or step ir? */
        /* Check, we may need to clear step breakpoint. */
        woort_WAIPO_VMLocalContext* vmcontext;
        if (woort_hashmap_find(&debugger_instance->m_focusing_vms, &vm, (void**)&vmcontext))
        {
            if (_woort_WAIPO_VMLocalContext_meet_step_breakdown(vmcontext, current_ip))
            {
                breakdown = true;

                /*
                无论是以外部请求还是 TRAP 进入调试器的 VM 应当进行过一次正同步
                我们应当认为 vm 当前的 env 能与当前 ip 对应；
                */
                woort_CodeEnv* const cenv = vm->m_env;

                /*
                如果遇到 next/return，就不能简单停在当前步进断点处；
                继续检查，如果确实不满足步进断点结束条件，将 step_break_trapped_but_need_to_continue
                设置为 true 以便继续填装步进断点
                */
                if (vmcontext->m_is_source_return)
                {
                    if (_woort_WAIPO_get_current_callstack_depth(vm)
                        >= vmcontext->m_step_target_depth)
                    {
                        /* 仍在当前函数或更深层，继续步进 */
                        breakdown = false;
                    }
                }
                else if (vmcontext->m_is_source_step)
                {
                    const uint32_t code_offset =
                        (uint32_t)(current_ip - cenv->m_code_begin);
                    woort_SourceLocation src_loc;

                    if (woort_CodeEnv_find_srcloc_by_offset(
                        cenv, code_offset, &src_loc))
                    {
                        const bool file_changed =
                            (vmcontext->m_step_source_file == NULL
                                || src_loc.m_filepath == NULL)
                            ? (vmcontext->m_step_source_file != src_loc.m_filepath)
                            : (strcmp(vmcontext->m_step_source_file,
                                src_loc.m_filepath) != 0);

                        const bool loc_changed =
                            file_changed
                            || src_loc.m_begin_line
                            != vmcontext->m_step_source_line
                            || (size_t)src_loc.m_begin_column
                            != vmcontext->m_step_source_begin_column
                            || (size_t)src_loc.m_end_line
                            != vmcontext->m_step_source_end_line
                            || (size_t)src_loc.m_end_column
                            != vmcontext->m_step_source_end_column;

                        if (vmcontext->m_is_source_next)
                        {
                            if (!loc_changed
                                || _woort_WAIPO_get_current_callstack_depth(vm) > vmcontext->m_step_target_depth)
                            {
                                /* 位置未变化，或者在更深层的函数调用内部，继续步进 */
                                breakdown = false;
                            }
                        }
                        else if (!loc_changed)
                        {
                            /* 源码位置未变动，继续步进 */
                            breakdown = false;
                        }
                    }
                    /* else: 当前指令无源码信息，中断 */
                }
                /* else: IR 级步进：直接中断 */

                if (!breakdown)
                {
                    /* 中断操作被取消了，继续步进，准备重新填装步进断点 */
                    step_break_trapped_env_for_refill = cenv;

                    saved_step_break_context_for_restoring.m_file = vmcontext->m_step_source_file;
                    saved_step_break_context_for_restoring.m_line = vmcontext->m_step_source_line;
                    saved_step_break_context_for_restoring.m_col = vmcontext->m_step_source_begin_column;
                    saved_step_break_context_for_restoring.m_end_line = vmcontext->m_step_source_end_line;
                    saved_step_break_context_for_restoring.m_end_col = vmcontext->m_step_source_end_column;
                    saved_step_break_context_for_restoring.m_is_stepover = vmcontext->m_is_source_next;
                    saved_step_break_context_for_restoring.m_is_stepout = vmcontext->m_is_source_return;
                    saved_step_break_context_for_restoring.m_target_depth = vmcontext->m_step_target_depth;
                }

                /* Step break point trapped, we need to clear the breakpoint trap. */
                _woort_WAIPO_VMLocalContext_clean_step_breakpoint(vmcontext);
            }
        }

        /* May be step debug point? */
        if (!breakdown && _woort_WAIPO_BreakpointCollection_contains_debug_break_at(
            &debugger_instance->m_breakpoint_collection, current_ip))
        {
            breakdown = true;
        }

        /* 检查是否需要重新填装步进断点 */
        if (!breakdown && step_break_trapped_env_for_refill != NULL)
        {
            /* 仍在同一源码位置（或 next 中尚未返回目标深度），继续步进 */
            const woort_Bytecode* next_ip = NULL;
            if (_woort_WAIPO_get_next_ip(
                current_ip, step_break_trapped_env_for_refill, vm->m_sb, vm, &next_ip))
            {
                _woort_WAIPO_VMLocalContext_set_source_step(
                    vmcontext,
                    saved_step_break_context_for_restoring.m_file,
                    saved_step_break_context_for_restoring.m_line,
                    saved_step_break_context_for_restoring.m_col,
                    saved_step_break_context_for_restoring.m_end_line,
                    saved_step_break_context_for_restoring.m_end_col);

                vmcontext->m_is_source_next = saved_step_break_context_for_restoring.m_is_stepover;
                vmcontext->m_is_source_return = saved_step_break_context_for_restoring.m_is_stepout;
                vmcontext->m_step_target_depth = saved_step_break_context_for_restoring.m_target_depth;

                if (_woort_WAIPO_VMLocalContext_set_stepir_breakpoint(
                    vmcontext, next_ip))
                {
                    /* 成功设置下一步断点，不中断 */
                }
                else
                {
                    /* 设置断点失败，中断 */
                    breakdown = true;
                }
            }
            else
            {
                /* 无法确定下一条指令，中断 */
                breakdown = true;
            }
        }

    }
    return breakdown;
}

static void woort_WAIPO_Debugger_active(woort_VMRuntime* vm, void* instance, bool trap_by_request)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;

    if (woort_hashmap_is_empty(&debugger_instance->m_focusing_vms)
        || _woort_WAIPO_Debugger_meet_breakpoint(debugger_instance, vm)
        || trap_by_request)
    {
        do
        {
            assert(debugger_instance->m_current_vm == NULL);

            /* Apply current VM. */
            debugger_instance->m_current_vm = vm;

            const woort_WAIPO_TrapEndBehavior behavior =
                debugger_instance->m_trap_callback(debugger_instance, vm);

            /* Reset current VM to NULL. */
            debugger_instance->m_current_vm = NULL;

            if (behavior == WOORT_WAIPO_TRAP_CONTINUE)
            {
                /* Un focus this vm, continue. */
                _woort_WAIPO_Debugger_out_of_focus(debugger_instance, vm);
                break;
            }
            else
            {
                const woort_Bytecode* next_ip;
                if (!_woort_WAIPO_get_next_ip(vm->m_ip, vm->m_env, vm->m_sb, vm, &next_ip))
                {
                    (void)printf(WOORT_ANSI_HIR "Cannot determine next instruction.\n" WOORT_ANSI_RST);
                    continue;
                }

                woort_WAIPO_VMLocalContext* local_context;
                if (!_woort_WAIPO_Debugger_focus_on(debugger_instance, vm, &local_context))
                {
                    (void)printf(WOORT_ANSI_HIR "Failed to focus on VM.\n" WOORT_ANSI_RST);
                    continue;
                }

                bool breakpoint_set_successfully;
                switch (behavior)
                {
                case WOORT_WAIPO_TRAP_STEPIR:
                    breakpoint_set_successfully =
                        _woort_WAIPO_VMLocalContext_set_stepir_breakpoint(local_context, next_ip);
                    break;
                case WOORT_WAIPO_TRAP_STEPIN:
                    breakpoint_set_successfully =
                        _woort_WAIPO_VMLocalContext_set_stepin_breakpoint(local_context, vm, next_ip);
                    break;
                case WOORT_WAIPO_TRAP_STEPOVER:
                    breakpoint_set_successfully =
                        _woort_WAIPO_VMLocalContext_set_stepover_breakpoint(local_context, vm, next_ip);
                    break;
                case WOORT_WAIPO_TRAP_STEPOUT:
                    breakpoint_set_successfully =
                        _woort_WAIPO_VMLocalContext_set_stepout_breakpoint(local_context, vm, next_ip);
                    break;
                default:
                    /* Unknown or unexpected behavior. */
                    abort();
                }

                if (breakpoint_set_successfully)
                    break;

                (void)printf(WOORT_ANSI_HIR "Failed to set step breakpoint." WOORT_ANSI_RST);
            }
        } while (1);
    }
}

static bool _woort_WAIPO_VMLocalContext_deinit_callback(
    const void* key,
    void* value,
    void* user_data)
{
    (void)key;
    (void)user_data;
    _woort_WAIPO_VMLocalContext_deinit((woort_WAIPO_VMLocalContext*)value);
    return true;
}

static void _woort_WAIPO_Debugger_close(void* instance)
{
    woort_WAIPO_Debugger* const debugger_instance = instance;

    (void)woort_hashmap_foreach(
        &debugger_instance->m_focusing_vms,
        &_woort_WAIPO_VMLocalContext_deinit_callback,
        NULL);

    woort_hashmap_deinit(&debugger_instance->m_focusing_vms);
    _woort_WAIPO_BreakpointCollection_deinit(&debugger_instance->m_breakpoint_collection);

    free(debugger_instance);
}

WOORT_NODISCARD woort_DebuggerAttachResult woort_WAIPO_Debugger_attach(
    /* OPTIONAL */ woort_WAIPO_Debugger_TrapCallback breakdown_callback,
    /* OPTIONAL */ woort_WAIPO_Debugger** out_debugger)
{
    woort_WAIPO_Debugger* const debugger_instance =
        malloc(sizeof(woort_WAIPO_Debugger));

    if (debugger_instance == NULL)
        return WOORT_DEBUGGER_ATTACH_RESULT_FAILED;

    woort_hashmap_init(
        &debugger_instance->m_focusing_vms,
        sizeof(woort_VMRuntime*),
        sizeof(woort_WAIPO_VMLocalContext),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    _woort_WAIPO_BreakpointCollection_init(
        &debugger_instance->m_breakpoint_collection);

    debugger_instance->m_first_breakdown = true;
    debugger_instance->m_last_command[0] = '\0';
    debugger_instance->m_current_frame_depth = 0;
    debugger_instance->m_current_vm = NULL;

    if (breakdown_callback != NULL)
        debugger_instance->m_trap_callback = breakdown_callback;
    else
    {
        debugger_instance->m_trap_callback =
            &woort_WAIPO_Debugger_process_cmdline;
    }

    const woort_DebuggerAttachResult result =
        woort_VMRuntime_Debugger_attach(
            &woort_WAIPO_Debugger_active,
            debugger_instance,
            &_woort_WAIPO_Debugger_close);

    if (result == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS && out_debugger != NULL)
        *out_debugger = debugger_instance;

    return result;
}

/* ========================================================== */

WOORT_NODISCARD woort_VMRuntime* woort_WAIPO_Debugger_do_get_current_vm(
    woort_WAIPO_Debugger* debugger)
{
    return debugger->m_current_vm;
}

WOORT_NODISCARD
woort_WAIPO_Debugger_FrameId woort_WAIPO_Debugger_do_get_current_frame(
    woort_WAIPO_Debugger* debugger)
{
    return debugger->m_current_frame_depth;
}
WOORT_NODISCARD bool woort_WAIPO_Debugger_do_switch_trace_frame(
    woort_WAIPO_Debugger* debugger,
    woort_WAIPO_Debugger_FrameId frame_id,
    /* OPTIONAL */ woort_VMRuntime_TraceCallstack* out_tracestack)
{
    woort_VMRuntime* const current_vm = woort_WAIPO_Debugger_do_get_current_vm(debugger);

    if (!_woort_WAIPO_trace_to_depth(current_vm, frame_id, out_tracestack))
    {
        /* No such frame. */
        return false;
    }
    debugger->m_current_frame_depth = frame_id;
    return true;
}

WOORT_NODISCARD static bool _woort_WAIPO_Debugger_get_locals_from_trace_frame(
    woort_WAIPO_Debugger* debugger,
    woort_WAIPO_Debugger_FrameId frame,
    woort_VMRuntime_TraceCallstack* out_trace,
    woort_CodeEnv** out_cenv /* If trace failed or failed to find codeenv fill with NULL. */,
    const woort_Vector/* woort_LocalVarDebugInfo */** out_locals)
{
    woort_VMRuntime* const current_vm = woort_WAIPO_Debugger_do_get_current_vm(debugger);
    if (!_woort_WAIPO_trace_to_depth(current_vm, frame, out_trace))
    {
        /* Bad frame. */
        *out_cenv = NULL;
        return false;
    }

    if (!woort_CodeEnv_find(out_trace->m_code_addr, out_cenv))
    {
        /* Unknown function. */
        *out_cenv = NULL;
        return false;
    }

    woort_CodeEnv* const cenv = *out_cenv;
    assert(cenv != NULL);

    const uint32_t frame_ip_offset =
        (uint32_t)(out_trace->m_code_addr - cenv->m_code_begin);

    const woort_FunctionBoundary* const function_boundary
        = woort_CodeEnv_find_function_boundary_by_offset(cenv, frame_ip_offset);

    if (function_boundary == NULL
        || !woort_CodeEnv_find_local_vars_by_boundary(
            cenv, function_boundary, out_locals))
    {
        /* No debug info for this function. */
        return false;
    }

    assert(*out_cenv != NULL);
    return true;
}

WOORT_NODISCARD size_t woort_WAIPO_Debugger_do_get_local_count(
    woort_WAIPO_Debugger* debugger)
{
    woort_VMRuntime_TraceCallstack trace;
    woort_CodeEnv* cenv;
    const woort_Vector* local_vars;
    if (!_woort_WAIPO_Debugger_get_locals_from_trace_frame(
        debugger, debugger->m_current_frame_depth, &trace, &cenv, &local_vars))
    {
        /* Failed to find debug info for this frame. */
        return 0;
    }

    (void)trace;
    (void)cenv;
    return local_vars->m_size;
}

static void _woort_WAIPO_Debugger_set_local_variable_info(
    woort_WAIPO_Debugger_VariableInfo* info,
    const woort_LocalVarDebugInfo* local_var_info,
    woort_Value* bp_addr)
{
    info->m_name = local_var_info->m_name;
    info->m_is_local = true;
    info->m_location.m_stack_frame_bp_offset = local_var_info->m_stack_offset;
    info->m_value = &bp_addr[local_var_info->m_stack_offset];
}

static void _woort_WAIPO_Debugger_set_static_variable_info(
    woort_WAIPO_Debugger_VariableInfo* info,
    const woort_StaticVarDebugInfo* static_var_info,
    size_t const_count_in_env,
    woort_Value* const_static_data_buffer)
{
    info->m_name = static_var_info->m_name;
    info->m_is_local = false;
    info->m_location.m_static_constant_index =
        (uint32_t)(const_count_in_env + static_var_info->m_static_idx);
    info->m_value = &const_static_data_buffer[info->m_location.m_static_constant_index];
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_do_query_local(
    woort_WAIPO_Debugger* debugger,
    size_t index,
    woort_WAIPO_Debugger_VariableInfo* out_local_info)
{
    woort_VMRuntime_TraceCallstack trace;
    woort_CodeEnv* cenv;
    const woort_Vector* local_vars;
    if (!_woort_WAIPO_Debugger_get_locals_from_trace_frame(
        debugger, debugger->m_current_frame_depth, &trace, &cenv, &local_vars))
    {
        /* Failed to find debug info for this frame. */
        return false;
    }

    (void)cenv;
    woort_LocalVarDebugInfo* local_var_debug_info;
    if (!woort_vector_index(local_vars, index, (void**)&local_var_debug_info))
        /* Out of range. */
        return false;

    woort_VMRuntime* const current_vm = woort_WAIPO_Debugger_do_get_current_vm(debugger);

    woort_Value* const sb_addr =
        current_vm->m_stack_end - trace.m_callstack_offset_of_base;

    _woort_WAIPO_Debugger_set_local_variable_info(
        out_local_info, local_var_debug_info, sb_addr);

    return true;
}
WOORT_NODISCARD bool woort_WAIPO_Debugger_do_get_variable_by_name(
    woort_WAIPO_Debugger* debugger,
    const char* name,
    woort_WAIPO_Debugger_QueryVariableCallback callback,
    void* userdata)
{
    woort_WAIPO_Debugger_VariableInfo variable_info;

    woort_VMRuntime_TraceCallstack trace;
    /* OPTIONAL */ woort_CodeEnv* cenv;
    const woort_Vector* local_vars;
    if (_woort_WAIPO_Debugger_get_locals_from_trace_frame(
        debugger, debugger->m_current_frame_depth, &trace, &cenv, &local_vars))
    {
        /*
        Search for local variables.
        */

        woort_VMRuntime* const current_vm = woort_WAIPO_Debugger_do_get_current_vm(debugger);
        woort_Value* const sb_addr =
            current_vm->m_stack_end - trace.m_callstack_offset_of_base;

        for (size_t i = 0; i < local_vars->m_size; ++i)
        {
            const woort_LocalVarDebugInfo* info =
                (const woort_LocalVarDebugInfo*)woort_vector_at(
                    (woort_Vector*)local_vars, i);

            if (strcmp(info->m_name, name) != 0)
                continue;

            _woort_WAIPO_Debugger_set_local_variable_info(&variable_info, info, sb_addr);
            if (!callback(&variable_info, userdata))
                return false;
        }
    }

    if (cenv != NULL)
    {
        /*
         * 搜索当前 CodeEnv 中的静态变量。
         */
        for (size_t i = 0; i < cenv->m_pdb.m_static_var_debug_info.m_size; ++i)
        {
            const woort_StaticVarDebugInfo* info =
                (const woort_StaticVarDebugInfo*)woort_vector_at(
                    (woort_Vector*)&cenv->m_pdb.m_static_var_debug_info, i);

            if (strcmp(info->m_name, name) != 0)
                continue;

            _woort_WAIPO_Debugger_set_static_variable_info(
                &variable_info,
                info,
                cenv->m_const_records.m_size,
                cenv->m_data_begin);

            if (!callback(&variable_info, userdata))
                return false;
        }
    }
    return true;
}

typedef struct _woort_WAIPO_BreakByFileLineContext
{
    const char* m_filepath;
    uint32_t m_line;
    woort_Vector m_ips;
} _woort_WAIPO_BreakByFileLineContext;

static bool _woort_WAIPO_break_by_file_line_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_BreakByFileLineContext* ctx =
        (_woort_WAIPO_BreakByFileLineContext*)user_data;

    /*
     * 一行源码可能对应多条指令（如 for 头部生成初始化、条件、增量三段代码），
     * 枚举该行全部关联偏移，逐个设置断点。
     */
    (void)_woort_WAIPO_collect_line_break_ips(
        cenv, ctx->m_filepath, ctx->m_line, &ctx->m_ips);

    return true;
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_set_source_breakpoint(
    woort_WAIPO_Debugger* debugger,
    const char* path,
    uint32_t line,
    woort_WAIPO_Debugger_BreakpointId* out_id)
{
    _woort_WAIPO_BreakByFileLineContext ctx;
    ctx.m_filepath = path;
    /* line 与 srcloc 行号一致（0 起始），原样存入断点记录 */
    ctx.m_line = line;
    woort_vector_init(&ctx.m_ips, sizeof(const woort_Bytecode*));

    woort_CodeEnv_foreach(&_woort_WAIPO_break_by_file_line_callback, &ctx);

    if (ctx.m_ips.m_size == 0)
    {
        woort_vector_deinit(&ctx.m_ips);
        return false;
    }

    const bool added = _woort_WAIPO_BreakpointCollection_add_user_breakpoint(
        &debugger->m_breakpoint_collection,
        (const woort_Bytecode* const*)ctx.m_ips.m_data,
        ctx.m_ips.m_size,
        line,
        out_id,
        "%s",
        path);

    woort_vector_deinit(&ctx.m_ips);

    return added;
}

typedef struct _woort_WAIPO_BreakByFuncContext
{
    const char* m_funcname;
    woort_Vector m_ips;
} _woort_WAIPO_BreakByFuncContext;

static bool _woort_WAIPO_break_by_func_callback(
    woort_CodeEnv* cenv, void* user_data)
{
    _woort_WAIPO_BreakByFuncContext* ctx =
        (_woort_WAIPO_BreakByFuncContext*)user_data;

    /*
     * 函数名按子串匹配，可能命中多个函数（含跨 CodeEnv 的同名函数），
     * 枚举全部命中入口，合并为同一个断点。
     */
    for (size_t i = 0; i < cenv->m_function_boundaries.m_size; ++i)
    {
        const woort_FunctionBoundary* boundary =
            (const woort_FunctionBoundary*)woort_vector_at(
                (woort_Vector*)&cenv->m_function_boundaries, i);

        if (boundary->m_name == NULL)
            continue;
        if (strstr(boundary->m_name, ctx->m_funcname) == NULL)
            continue;

        const woort_Bytecode* target_ip =
            cenv->m_code_begin + boundary->m_offset_begin;

        if (!woort_vector_push_back(&ctx->m_ips, 1, &target_ip))
            return false;
    }
    return true;
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_set_function_breakpoint(
    woort_WAIPO_Debugger* debugger,
    const char* function_name,
    woort_WAIPO_Debugger_BreakpointId* out_id)
{
    _woort_WAIPO_BreakByFuncContext ctx;
    ctx.m_funcname = function_name;
    woort_vector_init(&ctx.m_ips, sizeof(const woort_Bytecode*));

    woort_CodeEnv_foreach(&_woort_WAIPO_break_by_func_callback, &ctx);

    if (ctx.m_ips.m_size == 0)
    {
        woort_vector_deinit(&ctx.m_ips);
        return false;
    }

    const bool added = _woort_WAIPO_BreakpointCollection_add_user_breakpoint(
        &debugger->m_breakpoint_collection,
        (const woort_Bytecode* const*)ctx.m_ips.m_data,
        ctx.m_ips.m_size,
        WOORT_WAIPO_DEBUGGER_BREAKPOINT_NO_LINE,
        out_id,
        "%s",
        function_name);

    woort_vector_deinit(&ctx.m_ips);

    return added;
}

/*
 * 枚举契约详见 _woort_WAIPO_BreakpointCollection_query_breakpoints：
 * 回调持读锁期间被调用，回调内不得再调用任何断点接口。
 */
WOORT_NODISCARD bool woort_WAIPO_Debugger_query_breakpoints(
    woort_WAIPO_Debugger* debugger,
    woort_WAIPO_Debugger_QueryBreakpointCallback callback,
    void* userdata)
{
    return _woort_WAIPO_BreakpointCollection_query_breakpoints(
        &debugger->m_breakpoint_collection, callback, userdata);
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_get_breakpoint_info(
    woort_WAIPO_Debugger* debugger,
    woort_WAIPO_Debugger_BreakpointId breakpoint_id,
    woort_WAIPO_Debugger_BreakpointInfo* out_breakinfo)
{
    return _woort_WAIPO_BreakpointCollection_find_user_breakpoint_and_get_info(
        &debugger->m_breakpoint_collection, breakpoint_id, out_breakinfo);
}

WOORT_NODISCARD bool woort_WAIPO_Debugger_delete_breakpoint(
    woort_WAIPO_Debugger* debugger,
    woort_WAIPO_Debugger_BreakpointId breakpoint_id)
{
    return _woort_WAIPO_BreakpointCollection_delete_user_breakpoint(
        &debugger->m_breakpoint_collection, breakpoint_id);
}

void woort_WAIPO_Debugger_clear_breakpoint(woort_WAIPO_Debugger* debugger)
{
    _woort_WAIPO_BreakpointCollection_clear_user_breakpoints(
        &debugger->m_breakpoint_collection);
}
