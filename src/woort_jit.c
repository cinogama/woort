/*
woort_jit.c

WooRT JIT 代码生成框架——流程实现。
接口契约与编译流程见 woort_jit.h。

本文件只负责编排（地址反查、边界定位、缓存、线性遍历驱动、结果安装、
生命周期管理），不包含任何具体后端的机器码发射。后端通过
woort_JIT_install_backend() 注册。
*/

#include "woort_jit.h"

#include "woort_codeenv.h"
#include "woort_hashmap.h"
#include "woort_vector.h"
#include "woort_log.h"
#include "woort_util.h"
#include "woort_spin.h"
#include "woort_atomic.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>   /* malloc */

/* ======================================================================== */
/* 全局状态                                                                    */
/* ======================================================================== */

/*
 * 编译缓存条目。缓存使用 woort_Vector 线性表：
 *   - 规模为「每 CodeEnv 的函数数量级」，小；
 *   - compile_function 的去重查询仅在 compile_env 期间逐函数发生，
 *     每个 entry 至多注册一次，故线性扫描足够；
 *   - release_env 可就地扫描 + swap-pop，无需 foreach-then-remove 的复杂处理。
 */
typedef struct woort_JIT_CacheEntry
{
    const woort_Bytecode* m_entry;     /* 函数首指令（去重 key） */
    woort_CodeEnv*        m_env;       /* 所属 CodeEnv（release_env 过滤 + 安装定位） */
    woort_IRConstantIndex m_const_idx; /* 对应的 SCRIPT_FUNC 常量池槽（安装/卸载用） */
    woort_JitFunction     m_function;  /* 编译产物 */
} woort_JIT_CacheEntry;

/*
 * 全局 JIT 框架上下文（单例）。
 */
static struct _woort_JIT_GlobalCtx
{
    bool                     m_inited;
    const woort_JIT_Backend* m_backend;
    woort_Vector             m_cache;      /* woort_JIT_CacheEntry[] */
    woort_RWSpinlock         m_cache_lock; /* 保护 m_cache 与 m_backend */
} * _jit_global_ctx = NULL;

/* ======================================================================== */
/* 内部：缓存维护                                                              */
/* ======================================================================== */

/* 线性查找 entry 在缓存中的下标；调用方须持锁。返回 SIZE_MAX 表示未找到。 */
static WOORT_NODISCARD size_t _woort_JIT_cache_find_locked(const woort_Bytecode* entry)
{
    for (size_t i = 0; i < _jit_global_ctx->m_cache.m_size; ++i)
    {
        const woort_JIT_CacheEntry* const e =
            (const woort_JIT_CacheEntry*)woort_vector_at(
                (woort_Vector*)&_jit_global_ctx->m_cache, i);
        if (e->m_entry == entry)
            return i;
    }
    return SIZE_MAX;
}

/* 移除并释放缓存中所有条目（调后端 free_func）。调用方须持写锁。 */
static void _woort_JIT_cache_clear_locked(void)
{
    const woort_JIT_Backend* const backend = _jit_global_ctx->m_backend;

    for (size_t i = 0; i < _jit_global_ctx->m_cache.m_size; ++i)
    {
        woort_JIT_CacheEntry* const e =
            (woort_JIT_CacheEntry*)woort_vector_at(
                &_jit_global_ctx->m_cache, i);
        if (e->m_function != NULL && backend != NULL)
            backend->m_free_func(e->m_function);

        /* 顺带清空常量池槽，防止解释器/CALLNJIT 引用到已释放的入口。 */
        if (e->m_env != NULL)
        {
            woort_CodeEnv_lock(e->m_env);
            e->m_env->m_data_begin[e->m_const_idx].m_jit_function = NULL;
            woort_CodeEnv_unlock(e->m_env);
        }
    }

    woort_vector_clear(&_jit_global_ctx->m_cache);
}

/* ======================================================================== */
/* 内部：函数边界与常量池槽定位                                                  */
/* ======================================================================== */

/*
 * 在 env->m_function_boundaries 中二分定位 entry 所属函数。
 * 返回该 boundary 的指针（只读），或 NULL（未找到 / 表为空）。
 * 复用 woort_CodeEnv_find_function_name_by_offset 同款二分逻辑。
 */
static WOORT_NODISCARD const woort_FunctionBoundary* _woort_JIT_find_boundary(
    const woort_CodeEnv* env, const woort_Bytecode* entry)
{
    const woort_Vector* const vec = (const woort_Vector*)&env->m_function_boundaries;
    const uint32_t count = (uint32_t)vec->m_size;
    if (count == 0)
        return NULL;

    const uint32_t offset = (uint32_t)(entry - env->m_code_begin);

    /* 二分：找最后一个 m_offset_begin <= offset 的条目。 */
    uint32_t lo = 0, hi = count;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        const woort_FunctionBoundary* const m =
            (const woort_FunctionBoundary*)woort_vector_at(
                (woort_Vector*)vec, mid);
        if (m->m_offset_begin <= offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return NULL;

    const woort_FunctionBoundary* const found =
        (const woort_FunctionBoundary*)woort_vector_at(
            (woort_Vector*)vec, lo - 1);

    /* 确认 entry 确实落在该函数范围内（offset 在 [begin, begin+length)）。 */
    if (offset < found->m_offset_begin ||
        offset >= found->m_offset_begin + found->m_code_length)
        return NULL;

    return found;
}

/*
 * 定位 entry 指向的脚本函数在常量池中的槽索引。
 * 常量池中 SCRIPT_FUNC 条目存的是 m_script_function == entry。
 * 找不到则返回 SIZE_MAX（可能是匿名/入口未被常量化的函数，框架跳过编译）。
 */
static WOORT_NODISCARD size_t _woort_JIT_find_const_slot_for_script_func(
    const woort_CodeEnv* env, const woort_Bytecode* entry)
{
    for (size_t i = 0; i < env->m_const_records.m_size; ++i)
    {
        const woort_ConstRecord* const rec =
            (const woort_ConstRecord*)woort_vector_at(
                (woort_Vector*)&env->m_const_records, i);
        if (rec->m_type == WOORT_CONST_TYPE_SCRIPT_FUNC &&
            env->m_data_begin[i].m_script_function == entry)
            return i;
    }
    return SIZE_MAX;
}

/* ======================================================================== */
/* 内部：编译单个函数（核心流程）                                                */
/* ======================================================================== */

/*
 * 执行编译流程（不含缓存命中检查）。调用方已确认 entry 有效且有活跃后端。
 * 成功则把产物登记进缓存（持锁）。返回 true/false。
 */
static WOORT_NODISCARD bool _woort_JIT_compile_one(
    woort_CodeEnv* env,
    const woort_FunctionBoundary* boundary)
{
    const woort_JIT_Backend* const backend = _jit_global_ctx->m_backend;
    assert(backend != NULL);

    woort_JIT_CompileRequest req;
    req.m_env = env;
    req.m_name = boundary->m_name;
    req.m_entry = env->m_code_begin + boundary->m_offset_begin;
    req.m_code_end = req.m_entry + boundary->m_code_length;

    /* 定位常量池槽（写 m_jit_function 用）。无对应槽则不编译。 */
    const size_t const_idx = _woort_JIT_find_const_slot_for_script_func(env, req.m_entry);
    if (const_idx == SIZE_MAX)
    {
        WOORT_DEBUG("JIT: no SCRIPT_FUNC const slot for entry %p; skipped.",
            (const void*)req.m_entry);
        return false;
    }

    /* 1. 发射器生命周期开始（后端自管可执行内存）。 */
    woort_JIT_Emitter* emitter = backend->m_emitter_begin(&req);
    if (emitter == NULL)
    {
        WOORT_DEBUG("JIT: emitter_begin failed for '%s'.",
            boundary->m_name ? boundary->m_name : "?");
        return false;
    }

    woort_JitFunction produced = NULL;
    bool ok = false;

    /* 2. prologue。 */
    if (!backend->m_emit_prologue(emitter))
    {
        WOORT_DEBUG("JIT: emit_prologue failed for '%s'.",
            boundary->m_name ? boundary->m_name : "?");
        goto _cleanup_emitter;
    }

    /* 3. 线性遍历：逐指令解码并触发后端降级回调。 */
    {
        const woort_Bytecode* pc = req.m_entry;
        while (pc < req.m_code_end)
        {
            /*
             * woort_OpcodeDispatcher_decode 解码一条指令，调用后端对应回调，
             * 并返回下一条 PC（自动处理两字指令与 DEBUGTRAP 透明化）。
             * emitter 作为每个回调的 userctx 透传。
             */
            const woort_Bytecode* const next =
                woort_OpcodeDispatcher_decode(pc, backend->m_dispatchers, emitter);
            if (next <= pc)
            {
                /* 防御：decode 不应返回非前进的 PC（除非后端回调改变了流程，
                   但 decode 本身只前进）。出现则中止以避免死循环。 */
                WOORT_DEBUG("JIT: dispatch stall at %p in '%s'.",
                    (const void*)pc, boundary->m_name ? boundary->m_name : "?");
                goto _cleanup_emitter;
            }
            pc = next;
        }
    }

    /* 4. epilogue。 */
    if (!backend->m_emit_epilogue(emitter))
    {
        WOORT_DEBUG("JIT: emit_epilogue failed for '%s'.",
            boundary->m_name ? boundary->m_name : "?");
        goto _cleanup_emitter;
    }

    /* 5. finalize：重定位 + 翻转 RX，产出入口。 */
    if (!backend->m_emitter_finalize(emitter, &produced) || produced == NULL)
    {
        WOORT_DEBUG("JIT: emitter_finalize failed for '%s'.",
            boundary->m_name ? boundary->m_name : "?");
        /* finalize 失败：后端应在内部清理 emitter（emitter 句柄不再可用）。 */
        goto _cleanup_emitter;
    }

    ok = true;

_cleanup_emitter:
    (void)emitter; /* finalize 之后 emitter 由后端释放，框架不再触碰。 */

    if (!ok)
    {
        /* 失败回滚：若已产出入口则释放。 */
        if (produced != NULL)
            backend->m_free_func(produced);
        return false;
    }

    /* 6. 登记到缓存（持写锁）。 */
    {
        woort_rwspinlock_write_lock(&_jit_global_ctx->m_cache_lock);

        /* 再次确认未在并发下被编译（去重）。 */
        if (_woort_JIT_cache_find_locked(req.m_entry) != SIZE_MAX)
        {
            woort_rwspinlock_write_unlock(&_jit_global_ctx->m_cache_lock);
            /* 已有并发编译结果，丢弃本次产物。 */
            backend->m_free_func(produced);
            return true;
        }

        void* slot = NULL;
        if (!woort_vector_emplace_back(&_jit_global_ctx->m_cache, 1, &slot))
        {
            woort_rwspinlock_write_unlock(&_jit_global_ctx->m_cache_lock);
            WOORT_DEBUG("JIT: cache emplace OOM for '%s'.",
                boundary->m_name ? boundary->m_name : "?");
            backend->m_free_func(produced);
            return false;
        }
        woort_JIT_CacheEntry* const ce = (woort_JIT_CacheEntry*)slot;
        ce->m_entry = req.m_entry;
        ce->m_env = env;
        ce->m_const_idx = (woort_IRConstantIndex)const_idx;
        ce->m_function = produced;

        woort_rwspinlock_write_unlock(&_jit_global_ctx->m_cache_lock);
    }

    /* 7. 安装到常量池槽（持 CodeEnv 锁）。 */
    woort_CodeEnv_lock(env);
    env->m_data_begin[const_idx].m_jit_function = produced;
    woort_CodeEnv_unlock(env);

    return true;
}

/* ======================================================================== */
/* 框架初始化                                                                  */
/* ======================================================================== */

static WOORT_NODISCARD bool _woort_JIT_ensure_inited(void)
{
    if (_jit_global_ctx != NULL)
        return true;

    _jit_global_ctx =
        (struct _woort_JIT_GlobalCtx*)malloc(sizeof(*_jit_global_ctx));
    if (_jit_global_ctx == NULL)
    {
        WOORT_DEBUG("JIT: out of memory allocating global context.");
        return false;
    }

    _jit_global_ctx->m_inited = true;
    _jit_global_ctx->m_backend = NULL;
    woort_vector_init(&_jit_global_ctx->m_cache, sizeof(woort_JIT_CacheEntry));
    woort_rwspinlock_init(&_jit_global_ctx->m_cache_lock);

    return true;
}

/* ======================================================================== */
/* 公开 API                                                                    */
/* ======================================================================== */

WOORT_NODISCARD bool woort_JIT_install_backend(const woort_JIT_Backend* backend)
{
    if (backend == NULL || backend->m_query_support == NULL ||
        backend->m_emitter_begin == NULL || backend->m_emit_prologue == NULL ||
        backend->m_emit_epilogue == NULL || backend->m_emitter_finalize == NULL ||
        backend->m_dispatchers == NULL || backend->m_free_func == NULL)
    {
        assert(!"woort_JIT_install_backend: incomplete backend interface.");
        return false;
    }

    if (!_woort_JIT_ensure_inited())
        return false;

    if (!backend->m_query_support())
    {
        WOORT_DEBUG("JIT: backend '%s' not supported on this platform.",
            backend->m_name ? backend->m_name : "?");
        return false;
    }

    woort_rwspinlock_write_lock(&_jit_global_ctx->m_cache_lock);

    /* 若已有旧后端，先释放其名下所有已编译代码（避免换后端后入口悬空）。 */
    if (_jit_global_ctx->m_backend != NULL)
        _woort_JIT_cache_clear_locked();

    _jit_global_ctx->m_backend = backend;

    woort_rwspinlock_write_unlock(&_jit_global_ctx->m_cache_lock);

    WOORT_DEBUG("JIT: backend '%s' installed.",
        backend->m_name ? backend->m_name : "?");
    return true;
}

void woort_JIT_uninstall_backend(void)
{
    if (_jit_global_ctx == NULL)
        return;

    woort_rwspinlock_write_lock(&_jit_global_ctx->m_cache_lock);

    const bool had = (_jit_global_ctx->m_backend != NULL);
    if (had)
        _woort_JIT_cache_clear_locked();
    _jit_global_ctx->m_backend = NULL;

    woort_rwspinlock_write_unlock(&_jit_global_ctx->m_cache_lock);

    if (had)
        WOORT_DEBUG("JIT: backend uninstalled.");
}

WOORT_NODISCARD bool woort_JIT_is_enabled(void)
{
    return _jit_global_ctx != NULL && _jit_global_ctx->m_backend != NULL;
}

WOORT_NODISCARD bool woort_JIT_compile_function(const woort_Bytecode* function_entry)
{
    if (function_entry == NULL)
        return false;

    if (!woort_JIT_is_enabled())
        return false; /* 无活跃后端：空操作，保持解释执行。 */

    /* 1. 反查所属 CodeEnv。 */
    woort_CodeEnv* env = NULL;
    if (!woort_CodeEnv_find(function_entry, &env) || env == NULL)
    {
        WOORT_DEBUG("JIT: cannot locate CodeEnv for entry %p.",
            (const void*)function_entry);
        return false;
    }

    /* 2. 缓存命中检查。 */
    {
        woort_rwspinlock_read_lock(&_jit_global_ctx->m_cache_lock);
        const bool hit = (_woort_JIT_cache_find_locked(function_entry) != SIZE_MAX);
        woort_rwspinlock_read_unlock(&_jit_global_ctx->m_cache_lock);
        if (hit)
            return true;
    }

    /* 3. 定位函数边界。 */
    const woort_FunctionBoundary* const boundary = _woort_JIT_find_boundary(env, function_entry);
    if (boundary == NULL)
    {
        WOORT_DEBUG("JIT: entry %p not at any function boundary.",
            (const void*)function_entry);
        return false;
    }

    /* 4. 执行编译流程。 */
    return _woort_JIT_compile_one(env, boundary);
}

WOORT_NODISCARD bool woort_JIT_compile_env(woort_CodeEnv* env)
{
    if (env == NULL)
        return false;

    if (!woort_JIT_is_enabled())
        return false; /* 无后端：空操作。 */

    if (!_woort_JIT_ensure_inited())
        return false;

    const woort_Vector* const boundaries =
        (const woort_Vector*)&env->m_function_boundaries;
    const size_t count = boundaries->m_size;

    bool any_attempted = false;
    for (size_t i = 0; i < count; ++i)
    {
        const woort_FunctionBoundary* const b =
            (const woort_FunctionBoundary*)woort_vector_at(
                (woort_Vector*)boundaries, i);

        any_attempted = true;

        const woort_Bytecode* const entry =
            env->m_code_begin + b->m_offset_begin;

        if (!woort_JIT_compile_function(entry))
        {
            /* 单项失败仅记日志，不影响其余函数与启动。
               失败的函数保持解释执行（常量池槽 m_jit_function 仍为 NULL）。 */
            WOORT_DEBUG("JIT: compile failed for '%s' (falling back to interpreter).",
                b->m_name ? b->m_name : "<anonymous>");
        }
    }

    return any_attempted;
}

void woort_JIT_release_env(woort_CodeEnv* env)
{
    if (_jit_global_ctx == NULL || env == NULL)
        return;

    const woort_JIT_Backend* backend;
    woort_rwspinlock_write_lock(&_jit_global_ctx->m_cache_lock);
    backend = _jit_global_ctx->m_backend;

    /*
     * 扫描缓存，释放属于本 env 的条目（swap-pop 原地删除）。
     * 先释放可执行内存，再清空常量池槽，最后移除缓存项。
     */
    size_t i = 0;
    while (i < _jit_global_ctx->m_cache.m_size)
    {
        woort_JIT_CacheEntry* const e =
            (woort_JIT_CacheEntry*)woort_vector_at(
                &_jit_global_ctx->m_cache, i);

        if (e->m_env == env)
        {
            if (e->m_function != NULL && backend != NULL)
                backend->m_free_func(e->m_function);

            /* 清空常量池槽（持有锁之外的 CodeEnv 锁）：
               此处 env 正在被 GC 销毁，调用者保证无并发执行，
               故直接置 NULL 即可，无需 CodeEnv 锁（锁可能已销毁）。 */
            if (e->m_const_idx < env->m_data_count)
                env->m_data_begin[e->m_const_idx].m_jit_function = NULL;

            /* swap-pop：把末尾条目搬到 i 处，再缩短长度。 */
            const size_t last = _jit_global_ctx->m_cache.m_size - 1;
            if (i != last)
            {
                const woort_JIT_CacheEntry* const last_e =
                    (const woort_JIT_CacheEntry*)woort_vector_at(
                        &_jit_global_ctx->m_cache, last);
                *e = *last_e;
            }
            (void)woort_vector_erase_at(&_jit_global_ctx->m_cache, last);
            /* i 不递增：搬来的条目需重新检查。 */
        }
        else
        {
            ++i;
        }
    }

    woort_rwspinlock_write_unlock(&_jit_global_ctx->m_cache_lock);
}
