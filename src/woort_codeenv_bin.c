#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "woort_codeenv.h"
#include "woort_dylib.h"
#include "woort_gc_string.h"
#include "woort_gc_closure.h"
#include "woort_gc_struct.h"
#include "woort_log.h"
#include "woort_util.h"
#include "woort_vfs.h"

/* ========================================================================
 * 二进制序列化 / 反序列化
 * ======================================================================== */

 /*
  * 二进制格式版本号与魔数。
  */
#define WOORT_CODEENV_BINARY_MAGIC   0x30314345u  /* "EC10" */
#define WOORT_CODEENV_BINARY_VERSION 6u

  /* ================================================================
   * 序列化期间的字符串池（本地使用）
   * ================================================================ */

typedef struct _CodeEnvBinStrPool
{
    woort_Vector m_strings;   /* char* 列表 */
    woort_Vector m_data;      /* 所有字符串数据拼接 */
} _CodeEnvBinStrPool;

static void _bin_strpool_init(_CodeEnvBinStrPool* sp)
{
    woort_vector_init(&sp->m_strings, sizeof(char*));
    woort_vector_init(&sp->m_data, sizeof(char));
}

static void _bin_strpool_deinit(_CodeEnvBinStrPool* sp)
{
    char** strs = (char**)sp->m_strings.m_data;
    for (size_t i = 0; i < sp->m_strings.m_size; ++i)
        free(strs[i]);
    woort_vector_deinit(&sp->m_strings);
    woort_vector_deinit(&sp->m_data);
}

/*
 * 插入字符串，返回其在 m_data 中的偏移量。
 * 如果字符串已存在，返回已有偏移量。
 *
 * 字符串以长度前缀格式存储：uint32_t len + 原始数据(len 字节)。
 * 这是二进制安全的——字符串可包含嵌入的空字符。
 */
static bool _bin_strpool_insert(
    _CodeEnvBinStrPool* sp,
    /* OPTIONAL */ const char* str,
    size_t len,
    /* OPTIONAL */ uint32_t* out_offset)
{
    if (str == NULL)
    {
        if (out_offset != NULL)
            *out_offset = UINT32_MAX;
        return true;
    }

    /* 查找是否已存在 */
    size_t existing_count = sp->m_strings.m_size;
    for (size_t i = 0; i < existing_count; ++i)
    {
        char* existing = ((char**)sp->m_strings.m_data)[i];
        uint32_t existing_len;
        memcpy(&existing_len, existing, sizeof(uint32_t));

        if (existing_len == (uint32_t)len
            && memcmp(existing + sizeof(uint32_t), str, len) == 0)
        {
            /* 找到：返回已有偏移量 */
            uint32_t off = 0;
            for (size_t j = 0; j < i; ++j)
            {
                char* entry = ((char**)sp->m_strings.m_data)[j];
                uint32_t entry_len;
                memcpy(&entry_len, entry, sizeof(uint32_t));
                off += sizeof(uint32_t) + entry_len;
            }

            if (out_offset != NULL)
                *out_offset = off;
            return true;
        }
    }

    /* 新字符串：以长度前缀格式复制 */
    size_t alloc_size = sizeof(uint32_t) + len;
    char* copy = (char*)malloc(alloc_size);
    if (copy == NULL)
        return false;

    {
        uint32_t len32 = (uint32_t)len;
        memcpy(copy, &len32, sizeof(uint32_t));
        memcpy(copy + sizeof(uint32_t), str, len);
    }

    uint32_t data_offset = (uint32_t)sp->m_data.m_size;

    if (!woort_vector_push_back(&sp->m_strings, 1u, &copy))
    {
        free(copy);
        return false;
    }

    if (!woort_vector_push_back(&sp->m_data, alloc_size, copy))
    {
        free(copy);
        sp->m_strings.m_size--;
        return false;
    }

    if (out_offset != NULL)
        *out_offset = data_offset;
    return true;
}

/*
 * 从字符串池读取字符串。
 * data: 池数据区起始地址。
 * offset: 字符串在池中的偏移。
 * out_len: 如果非空，写入字符串长度。
 * 返回指向池数据区的稳定指针（跳过长度前缀），或 NULL。
 */
static /* OPTIONAL */ const char* _bin_strpool_get(
    const char* data, uint32_t offset,
    /* OPTIONAL */ size_t* out_len)
{
    if (offset == UINT32_MAX)
    {
        if (out_len != NULL)
            *out_len = 0;
        return NULL;
    }
    {
        uint32_t len;
        memcpy(&len, data + offset, sizeof(uint32_t));
        if (out_len != NULL)
            *out_len = (size_t)len;
        return data + offset + sizeof(uint32_t);
    }
}

/* ================================================================
 * 序列化辅助：缓冲写入器
 * ================================================================ */

typedef struct _BinWriter
{
    woort_Vector m_buf;  /* unsigned char */
    size_t m_pos;
} _BinWriter;

static void _bin_writer_init(_BinWriter* w)
{
    woort_vector_init(&w->m_buf, sizeof(unsigned char));
    w->m_pos = 0;
}

static void _bin_writer_deinit(_BinWriter* w)
{
    woort_vector_deinit(&w->m_buf);
}

static bool _bin_write_raw(_BinWriter* w, const void* data, size_t len)
{
    if (len == 0)
        return true;
    return woort_vector_push_back(&w->m_buf, len, data);
}

static bool _bin_write_u8(_BinWriter* w, uint8_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_u32(_BinWriter* w, uint32_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_u64(_BinWriter* w, uint64_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_i64(_BinWriter* w, int64_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_f64(_BinWriter* w, double v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static void* _bin_writer_detach(_BinWriter* w, size_t* out_len)
{
    *out_len = w->m_buf.m_size;
    void* data = malloc(w->m_buf.m_size);
    if (data != NULL && w->m_buf.m_size > 0)
        memcpy(data, w->m_buf.m_data, w->m_buf.m_size);
    return data;
}

/* ================================================================
 * 反序列化辅助：缓冲读取器
 * ================================================================ */

typedef struct _BinReader
{
    /* OPTIONAL */ const unsigned char* m_data;   /* NULL for VFile mode */
    size_t            m_size;
    size_t            m_pos;
    /* OPTIONAL */ woort_VFile* m_file;            /* NULL for memory mode */
} _BinReader;

static bool _bin_reader_init_memory(
    _BinReader* r, const void* data, size_t size)
{
    r->m_data = (const unsigned char*)data;
    r->m_size = size;
    r->m_pos = 0;
    r->m_file = NULL;
    return true;
}

static bool _bin_read_raw(_BinReader* r, void* out, size_t len)
{
    if (r->m_pos + len > r->m_size)
        return false;
    if (r->m_file != NULL)
    {
        const size_t bytes_read = woort_vfile_read(r->m_file, out, len);
        if (bytes_read != len)
            return false;
    }
    else
    {
        memcpy(out, r->m_data + r->m_pos, len);
    }
    r->m_pos += len;
    return true;
}

static bool _bin_read_u8(_BinReader* r, uint8_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_u32(_BinReader* r, uint32_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_u64(_BinReader* r, uint64_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_i64(_BinReader* r, int64_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_f64(_BinReader* r, double* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

/*
 * Hashmap foreach 回调上下文和回调函数。
 * woort_HashMap 没有迭代器，必须使用 foreach 模式。
 */

 /* 用于字符串池构建阶段，收集 extern constants 的 key 字符串 */
struct _SaveStrPoolCtx {
    _CodeEnvBinStrPool* m_sp;
    bool* m_ok;
};

static bool _save_strpool_add_key(
    const void* key, void* value, void* user_data)
{
    (void)value;
    struct _SaveStrPoolCtx* ctx = (struct _SaveStrPoolCtx*)user_data;
    *ctx->m_ok = *ctx->m_ok
        && _bin_strpool_insert(ctx->m_sp, *(const char**)key,
            strlen(*(const char**)key), NULL);
    return true;
}

/* 用于写入 extern 常量映射表 */
struct _WriteExternConstCtx {
    _CodeEnvBinStrPool* m_sp;
    _BinWriter* m_w;
    bool* m_ok;
};

static bool _write_extern_const(
    const void* key, void* value, void* user_data)
{
    struct _WriteExternConstCtx* ctx =
        (struct _WriteExternConstCtx*)user_data;
    uint32_t name_off;
    uint32_t name_len = (uint32_t)strlen(*(const char**)key);
    *ctx->m_ok = *ctx->m_ok
        && _bin_strpool_insert(ctx->m_sp, *(const char**)key,
            name_len, &name_off);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, name_off);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, name_len);
    *ctx->m_ok = *ctx->m_ok
        && _bin_write_u32(ctx->m_w, *(woort_IRConstantIndex*)value);
    return *ctx->m_ok;
}

/* 用于写入 trap 记录表 */
struct _WriteTrapCtx {
    const woort_Bytecode* m_code_begin;
    _BinWriter* m_w;
    bool* m_ok;
};

static bool _write_trap(
    const void* key, void* value, void* user_data)
{
    struct _WriteTrapCtx* ctx = (struct _WriteTrapCtx*)user_data;
    const woort_Bytecode* addr = *(const woort_Bytecode**)key;
    woort_Bytecode orig = *(woort_Bytecode*)value;
    uint32_t off = (uint32_t)(addr - ctx->m_code_begin);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, off);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, orig);
    return *ctx->m_ok;
}

/* ================================================================
 * 序列化主函数
 * ================================================================ */

WOORT_NODISCARD bool woort_CodeEnv_save_binary(
    woort_CodeEnv* code_env, void** out_buffer, size_t* out_len)
{
    assert(code_env != NULL);
    assert(out_buffer != NULL);
    assert(out_len != NULL);

    *out_buffer = NULL;
    *out_len = 0;

    woort_CodeEnv_lock(code_env);

    _BinWriter w;
    _bin_writer_init(&w);

    _CodeEnvBinStrPool strpool;
    _bin_strpool_init(&strpool);

    bool ok = true;

    const size_t code_size = (size_t)(code_env->m_code_end - code_env->m_code_begin);
    const size_t data_count = code_env->m_data_count;
    const size_t const_count = code_env->m_constant_count;

    /*
     * 写入头部。
     */
    ok = ok && _bin_write_u32(&w, WOORT_CODEENV_BINARY_MAGIC);
    ok = ok && _bin_write_u32(&w, WOORT_CODEENV_BINARY_VERSION);
    ok = ok && _bin_write_u64(&w, (uint64_t)code_size);
    ok = ok && _bin_write_u64(&w, (uint64_t)data_count);
    ok = ok && _bin_write_u64(&w, (uint64_t)const_count);

    /*
     * 写入字节码。
     */
    if (code_size > 0)
        ok = ok && _bin_write_raw(&w, code_env->m_code_begin, code_size * sizeof(woort_Bytecode));

    /*
     * 串池阶段：遍历所有需要写入的字符串，建立字符串池。
     */

     /* 遍历常量数据，收集字符串/库名/函数名 */
    for (size_t i = 0; ok && i < data_count; ++i)
    {
        const woort_ConstRecord* rec = (const woort_ConstRecord*)woort_vector_at(
            &code_env->m_const_records, i);

        switch (rec->m_type)
        {
        case WOORT_CONST_TYPE_STRING:
        {
            const woort_GCString* gcs = code_env->m_data_begin[i].m_string;
            if (gcs != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    gcs->m_content, gcs->m_length, NULL);
            break;
        }
        case WOORT_CONST_TYPE_EXTERN_FUNC:
        case WOORT_CONST_TYPE_EXTERN_CLOSURE:
        {
            const char* lib_name = rec->m_lib_name;
            const char* func_name = rec->m_func_name;

            /* 如果 record 中没有登记库名/函数名，尝试从 dylib 运行时解析 */
            if (lib_name == NULL || func_name == NULL)
            {
                woort_NativeFunction nf;
                if (rec->m_type == WOORT_CONST_TYPE_EXTERN_CLOSURE)
                {
                    const woort_GCClosure* closure =
                        code_env->m_data_begin[i].m_closure;
                    if (closure == NULL || closure->m_script_function != NULL)
                    {
                        /* 闭包没有原生函数指针，无法解析 */
                        WOORT_DEBUG("CodeEnv save: cannot resolve extern closure names for const %zu.", i);
                        ok = false;
                        break;
                    }
                    nf = closure->m_native_function;
                }
                else
                {
                    nf = code_env->m_data_begin[i].m_native_function;
                    if (nf == NULL)
                    {
                        WOORT_DEBUG("CodeEnv save: null extern function for const %zu.", i);
                        ok = false;
                        break;
                    }
                }

                /* 尝试从 dylib 注册表查找库和函数名 */
                woort_Dylib* found_lib = NULL;
                if (!woort_Dylib_find_by_resolved_func((void*)nf, &found_lib)
                    || found_lib == NULL)
                {
                    WOORT_DEBUG("CodeEnv save: extern function not found in any loaded library for const %zu.", i);
                    ok = false;
                    break;
                }

                lib_name = found_lib->m_name;

                const char* resolved_name = NULL;
                if (!woort_Dylib_get_function_name(found_lib, (void*)nf, &resolved_name)
                    || resolved_name == NULL)
                {
                    WOORT_DEBUG("CodeEnv save: cannot resolve function name for const %zu.", i);
                    ok = false;
                    break;
                }
                func_name = resolved_name;
            }

            ok = ok && _bin_strpool_insert(&strpool,
                lib_name, strlen(lib_name), NULL);
            ok = ok && _bin_strpool_insert(&strpool,
                func_name, strlen(func_name), NULL);
            break;
        }
        default:
            break;
        }
    }

    /* 遍历 extern constants 名称 */
    if (ok)
    {
        struct _SaveStrPoolCtx ctx = { &strpool, &ok };
        (void)woort_hashmap_foreach(&code_env->m_extern_constants,
            &_save_strpool_add_key, &ctx);
    }

    /* 遍历 extern libs 名称、路径、脚本路径 */
    if (ok)
    {
        for (size_t i = 0; i < code_env->m_extern_libs.m_size; ++i)
        {
            woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(
                &code_env->m_extern_libs, i);
            ok = ok && _bin_strpool_insert(&strpool,
                lib->m_name, strlen(lib->m_name), NULL);
            if (lib->m_path != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    lib->m_path, strlen(lib->m_path), NULL);
            if (lib->m_script_path != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    lib->m_script_path, strlen(lib->m_script_path), NULL);
        }
    }

    /* 遍历函数边界名称 */
    if (ok)
    {
        for (size_t i = 0; i < code_env->m_pdb.m_function_boundaries.m_size; ++i)
        {
            const woort_FunctionBoundary* fb =
                (const woort_FunctionBoundary*)woort_vector_at(
                    &code_env->m_pdb.m_function_boundaries, i);
            if (fb->m_name != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    fb->m_name, strlen(fb->m_name), NULL);
        }
    }

    /* 遍历源码映射文件路径 */
    if (ok)
    {
        for (uint32_t i = 0; i < code_env->m_pdb.m_source_map.m_entry_count; ++i)
        {
            const woort_SourceMap_Entry* entry = &code_env->m_pdb.m_source_map.m_entries[i];
            if (entry->m_location.m_filepath != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    entry->m_location.m_filepath,
                    strlen(entry->m_location.m_filepath), NULL);
        }
    }

    /* 遍历外部库名称、路径 */
    if (ok)
    {
        for (size_t i = 0; ok && i < code_env->m_extern_libs.m_size; ++i)
        {
            woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(
                &code_env->m_extern_libs, i);

            ok = ok && _bin_strpool_insert(&strpool,
                lib->m_name, strlen(lib->m_name), NULL);

            ok = ok && _bin_strpool_insert(&strpool,
                lib->m_path, strlen(lib->m_path), NULL);

            if (lib->m_script_path != NULL)
            {
                ok = ok && _bin_strpool_insert(&strpool,
                    lib->m_script_path, strlen(lib->m_script_path), NULL);
            }
        }
    }

    /* 遍历局部变量调试信息名称 */
    if (ok)
    {
        for (size_t i = 0; i < code_env->m_pdb.m_local_var_debug_info.m_size; ++i)
        {
            const woort_LocalVarDebugInfo* info =
                (const woort_LocalVarDebugInfo*)woort_vector_at(
                    &code_env->m_pdb.m_local_var_debug_info, i);
            if (info->m_name != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    info->m_name, strlen(info->m_name), NULL);
        }
    }

    /* 遍历静态变量调试信息名称 */
    if (ok)
    {
        for (size_t i = 0; i < code_env->m_pdb.m_static_var_debug_info.m_size; ++i)
        {
            const woort_StaticVarDebugInfo* info =
                (const woort_StaticVarDebugInfo*)woort_vector_at(
                    &code_env->m_pdb.m_static_var_debug_info, i);
            if (info->m_name != NULL)
                ok = ok && _bin_strpool_insert(&strpool,
                    info->m_name, strlen(info->m_name), NULL);
        }
    }

    /*
     * 写入字符串池。
     */
    ok = ok && _bin_write_u64(&w, (uint64_t)strpool.m_data.m_size);
    if (ok && strpool.m_data.m_size > 0)
        ok = ok && _bin_write_raw(&w, strpool.m_data.m_data, strpool.m_data.m_size);

    /*
     * 写入外部库列表。
     */
    if (ok)
    {
        uint64_t lib_count = (uint64_t)code_env->m_extern_libs.m_size;
        ok = ok && _bin_write_u64(&w, lib_count);
        for (size_t i = 0; ok && i < code_env->m_extern_libs.m_size; ++i)
        {
            woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(
                &code_env->m_extern_libs, i);
            uint32_t name_off, name_len;
            name_len = (uint32_t)strlen(lib->m_name);
            ok = ok && _bin_strpool_insert(&strpool,
                lib->m_name, name_len, &name_off);
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);

            uint32_t path_off, path_len;
            path_len = (uint32_t)strlen(lib->m_path);
            ok = ok && _bin_strpool_insert(&strpool,
                lib->m_path, path_len, &path_off);
            ok = ok && _bin_write_u32(&w, path_off);
            ok = ok && _bin_write_u32(&w, path_len);

            if (lib->m_script_path != NULL)
            {
                uint32_t script_path_off, script_path_len;
                script_path_len = (uint32_t)strlen(lib->m_script_path);
                ok = ok && _bin_strpool_insert(&strpool,
                    lib->m_script_path, script_path_len, &script_path_off);
                ok = ok && _bin_write_u32(&w, script_path_off);
                ok = ok && _bin_write_u32(&w, script_path_len);
            }
            else
            {
                ok = ok && _bin_write_u32(&w, UINT32_MAX);
                ok = ok && _bin_write_u32(&w, 0);
            }
        }
    }

    /*
     * 写入常量数据。
     */
    if (ok)
    {
        for (size_t i = 0; ok && i < const_count; ++i)
        {
            const woort_ConstRecord* rec = (const woort_ConstRecord*)woort_vector_at(
                &code_env->m_const_records, i);
            const woort_Value* val = &code_env->m_data_begin[i];

            ok = ok && _bin_write_u8(&w, (uint8_t)rec->m_type);

            switch (rec->m_type)
            {
            case WOORT_CONST_TYPE_NIL:
                break;

            case WOORT_CONST_TYPE_INT:
                ok = ok && _bin_write_i64(&w, val->m_integer);
                break;

            case WOORT_CONST_TYPE_REAL:
                ok = ok && _bin_write_f64(&w, val->m_real);
                break;

            case WOORT_CONST_TYPE_STRING:
            {
                uint32_t off;
                const woort_GCString* gcs = val->m_string;
                if (gcs != NULL)
                {
                    ok = ok && _bin_strpool_insert(&strpool,
                        gcs->m_content, gcs->m_length, &off);
                    ok = ok && _bin_write_u32(&w, off);
                    ok = ok && _bin_write_u32(&w, (uint32_t)gcs->m_length);
                }
                else
                {
                    ok = ok && _bin_write_u32(&w, UINT32_MAX);
                    ok = ok && _bin_write_u32(&w, 0);
                }
                break;
            }

            case WOORT_CONST_TYPE_SCRIPT_FUNC:
            {
                /* 保存为相对于 m_code_begin 的偏移量 */
                uint32_t off = (val->m_script_function != NULL)
                    ? (uint32_t)(val->m_script_function - code_env->m_code_begin)
                    : UINT32_MAX;
                ok = ok && _bin_write_u32(&w, off);
                break;
            }

            case WOORT_CONST_TYPE_EXTERN_FUNC:
            case WOORT_CONST_TYPE_EXTERN_CLOSURE:
            {
                /* 使用 record 中的名字，或运行时解析（与字符串池收集阶段相同逻辑）*/
                const char* lib_name = rec->m_lib_name;
                const char* func_name = rec->m_func_name;

                if (lib_name == NULL || func_name == NULL)
                {
                    woort_NativeFunction nf;
                    if (rec->m_type == WOORT_CONST_TYPE_EXTERN_CLOSURE)
                    {
                        const woort_GCClosure* closure = val->m_closure;
                        if (closure != NULL && closure->m_script_function == NULL)
                            nf = closure->m_native_function;
                        else
                            nf = NULL;
                    }
                    else
                    {
                        nf = val->m_native_function;
                    }

                    if (nf != NULL)
                    {
                        woort_Dylib* found_lib = NULL;
                        if (!woort_Dylib_find_by_resolved_func((void*)nf, &found_lib)
                            || found_lib == NULL)
                        {
                            ok = false;
                            break;
                        }
                        lib_name = found_lib->m_name;
                        if (!woort_Dylib_get_function_name(found_lib, (void*)nf, &func_name))
                        {
                            ok = false;
                            break;
                        }
                    }
                    else
                    {
                        ok = false;
                        break;
                    }
                }

                uint32_t lib_off, func_off;
                uint32_t lib_len = (uint32_t)strlen(lib_name);
                uint32_t func_len = (uint32_t)strlen(func_name);
                ok = ok && _bin_strpool_insert(&strpool,
                    lib_name, lib_len, &lib_off);
                ok = ok && _bin_strpool_insert(&strpool,
                    func_name, func_len, &func_off);
                ok = ok && _bin_write_u32(&w, lib_off);
                ok = ok && _bin_write_u32(&w, lib_len);
                ok = ok && _bin_write_u32(&w, func_off);
                ok = ok && _bin_write_u32(&w, func_len);
                break;
            }

            case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
            {
                /* 闭包：记录脚本函数偏移 */
                const woort_GCClosure* closure = val->m_closure;
                uint32_t off = UINT32_MAX;
                if (closure != NULL && closure->m_script_function != NULL)
                    off = (uint32_t)(closure->m_script_function - code_env->m_code_begin);
                ok = ok && _bin_write_u32(&w, off);
                break;
            }

            case WOORT_CONST_TYPE_BOX_INT:
                ok = ok && _bin_write_i64(&w,
                    _woort_unbox_int64((woort_BoxedInt62)val->m_dynamic.m_boxed));
                break;

            case WOORT_CONST_TYPE_BOX_REAL:
                ok = ok && _bin_write_f64(&w,
                    _woort_unbox_float64((woort_BoxedFloat63)val->m_dynamic.m_boxed));
                break;

            case WOORT_CONST_TYPE_BOX_BOOL:
                ok = ok && _bin_write_u8(&w,
                    _woort_unbox_bool(val->m_dynamic.m_boxed) ? 1 : 0);
                break;

            case WOORT_CONST_TYPE_STRUCT:
            {
                woort_GCStruct* s = val->m_struct;
                uint32_t member_count = (s != NULL) ? (uint32_t)s->m_size : 0;
                ok = ok && _bin_write_u32(&w, member_count);
                for (uint32_t mi = 0; ok && mi < member_count; ++mi)
                {
                    /*
                     * 在常量池中查找与 s->m_datas[mi] 值相等的常量索引。
                     * struct 的成员值是 m_data_begin[original_index] 的副本，
                     * 通过值与类型匹配可找到原始常量索引。
                     */
                    const woort_Value* mv = &s->m_datas[mi];
                    woort_IRConstantIndex found_idx = (woort_IRConstantIndex)mi; /* fallback */
                    bool found = false;

                    for (size_t j = 0; j < data_count; ++j)
                    {
                        const woort_ConstRecord* rec_j =
                            (const woort_ConstRecord*)woort_vector_at(
                                &code_env->m_const_records, j);
                        const woort_Value* cv = &code_env->m_data_begin[j];

                        switch (rec_j->m_type)
                        {
                        case WOORT_CONST_TYPE_NIL:
                            if (mv->m_gcinstance == cv->m_gcinstance)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_INT:
                            if (mv->m_integer == cv->m_integer)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_REAL:
                            if (mv->m_real == cv->m_real)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_STRING:
                            if (mv->m_gcinstance == cv->m_gcinstance)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_SCRIPT_FUNC:
                            if (mv->m_script_function == cv->m_script_function)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_EXTERN_FUNC:
                            if (mv->m_native_function == cv->m_native_function)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
                        case WOORT_CONST_TYPE_EXTERN_CLOSURE:
                            if (mv->m_gcinstance == cv->m_gcinstance)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_BOX_INT:
                        case WOORT_CONST_TYPE_BOX_REAL:
                        case WOORT_CONST_TYPE_BOX_BOOL:
                            if (mv->m_dynamic.m_boxed == cv->m_dynamic.m_boxed)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        case WOORT_CONST_TYPE_STRUCT:
                            if (mv->m_gcinstance == cv->m_gcinstance)
                            {
                                found_idx = (woort_IRConstantIndex)j; found = true;
                            }
                            break;
                        default:
                            break;
                        }
                        if (found)
                            break;
                    }
                    ok = ok && _bin_write_u32(&w, (uint32_t)found_idx);
                }
                break;
            }

            default:
                WOORT_DEBUG("CodeEnv save: unknown const type %d at %zu.", (int)rec->m_type, i);
                ok = false;
                break;
            }
        }
    }

    /*
     * 写入 extern 常量映射表。
     */
    if (ok)
    {
        /* 先计数 */
        uint64_t extern_count = (uint64_t)code_env->m_extern_constants.m_size;
        ok = ok && _bin_write_u64(&w, extern_count);

        if (ok && extern_count > 0)
        {
            struct _WriteExternConstCtx ctx = { &strpool, &w, &ok };
            (void)woort_hashmap_foreach(&code_env->m_extern_constants,
                &_write_extern_const, &ctx);
        }
    }

    /*
     * 写入函数边界表。
     */
    if (ok)
    {
        uint64_t fb_count = (uint64_t)code_env->m_pdb.m_function_boundaries.m_size;
        ok = ok && _bin_write_u64(&w, fb_count);
        for (size_t i = 0; ok && i < code_env->m_pdb.m_function_boundaries.m_size; ++i)
        {
            const woort_FunctionBoundary* fb =
                (const woort_FunctionBoundary*)woort_vector_at(
                    &code_env->m_pdb.m_function_boundaries, i);
            ok = ok && _bin_write_u32(&w, fb->m_offset_begin);
            ok = ok && _bin_write_u32(&w, fb->m_code_length);

            uint32_t name_off, name_len;
            name_len = (uint32_t)strlen(fb->m_name);
            ok = ok && _bin_strpool_insert(&strpool,
                fb->m_name, name_len, &name_off);
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);
        }
    }

    /*
     * 写入源码映射表。
     */
    if (ok)
    {
        uint64_t sm_count = (uint64_t)code_env->m_pdb.m_source_map.m_entry_count;
        ok = ok && _bin_write_u64(&w, sm_count);
        for (uint32_t i = 0; ok && i < code_env->m_pdb.m_source_map.m_entry_count; ++i)
        {
            const woort_SourceMap_Entry* entry = &code_env->m_pdb.m_source_map.m_entries[i];
            ok = ok && _bin_write_u32(&w, entry->m_bytecode_offset);

            uint32_t fp_off, fp_len;
            fp_len = (uint32_t)strlen(entry->m_location.m_filepath);
            ok = ok && _bin_strpool_insert(&strpool,
                entry->m_location.m_filepath,
                fp_len, &fp_off);
            ok = ok && _bin_write_u32(&w, fp_off);
            ok = ok && _bin_write_u32(&w, fp_len);

            ok = ok && _bin_write_u32(&w, entry->m_location.m_begin_line);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_begin_column);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_end_line);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_end_column);
        }
    }

    /*
     * 写入 trap 记录表。
     */
    if (ok)
    {
        uint64_t trap_count = (uint64_t)code_env->m_trap_records.m_size;
        ok = ok && _bin_write_u64(&w, trap_count);

        if (ok && trap_count > 0)
        {
            struct _WriteTrapCtx ctx = { code_env->m_code_begin, &w, &ok };
            (void)woort_hashmap_foreach(&code_env->m_trap_records,
                &_write_trap, &ctx);
        }
    }

    /*
     * 写入局部变量调试信息。
     */
    if (ok)
    {
        uint64_t lv_count = (uint64_t)code_env->m_pdb.m_local_var_debug_info.m_size;
        ok = ok && _bin_write_u64(&w, lv_count);
        for (size_t i = 0; ok && i < code_env->m_pdb.m_local_var_debug_info.m_size; ++i)
        {
            const woort_LocalVarDebugInfo* info =
                (const woort_LocalVarDebugInfo*)woort_vector_at(
                    &code_env->m_pdb.m_local_var_debug_info, i);

            uint32_t name_off, name_len;
            if (info->m_name != NULL)
            {
                name_len = (uint32_t)strlen(info->m_name);
                ok = ok && _bin_strpool_insert(&strpool,
                    info->m_name, name_len, &name_off);
            }
            else
            {
                name_off = UINT32_MAX;
                name_len = 0;
            }
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);
            ok = ok && _bin_write_u32(&w, info->m_function_offset);
            ok = ok && _bin_write_u32(&w, (uint32_t)info->m_stack_offset);
        }
    }

    /*
     * 写入静态变量调试信息。
     */
    if (ok)
    {
        uint64_t sv_count = (uint64_t)code_env->m_pdb.m_static_var_debug_info.m_size;
        ok = ok && _bin_write_u64(&w, sv_count);
        for (size_t i = 0; ok && i < code_env->m_pdb.m_static_var_debug_info.m_size; ++i)
        {
            const woort_StaticVarDebugInfo* info =
                (const woort_StaticVarDebugInfo*)woort_vector_at(
                    &code_env->m_pdb.m_static_var_debug_info, i);

            uint32_t name_off, name_len;
            if (info->m_name != NULL)
            {
                name_len = (uint32_t)strlen(info->m_name);
                ok = ok && _bin_strpool_insert(&strpool,
                    info->m_name, name_len, &name_off);
            }
            else
            {
                name_off = UINT32_MAX;
                name_len = 0;
            }
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);
            ok = ok && _bin_write_u32(&w, info->m_static_idx);
        }
    }

    if (ok)
    {
        *out_buffer = _bin_writer_detach(&w, out_len);
        if (*out_buffer == NULL)
            ok = false;
    }

    _bin_strpool_deinit(&strpool);
    _bin_writer_deinit(&w);
    woort_CodeEnv_unlock(code_env);

    return ok;
}

/* ================================================================
 * 反序列化辅助：字符串追踪器
 *
 * 反序列化时，从二进制引用读出 (offset, length) 对后，需要创建
 * 以空字符结尾的 C 字符串副本。本追踪器收集所有副本，统一释放。
 * ================================================================ */

typedef struct _RestoreStrTracker
{
    woort_Vector m_copies;  /* char* 列表 */
} _RestoreStrTracker;

static void _rst_strtracker_init(_RestoreStrTracker* t)
{
    woort_vector_init(&t->m_copies, sizeof(char*));
}

static void _rst_strtracker_deinit(_RestoreStrTracker* t)
{
    char** copies = (char**)t->m_copies.m_data;
    for (size_t i = 0; i < t->m_copies.m_size; ++i)
        free(copies[i]);
    woort_vector_deinit(&t->m_copies);
}

/*
 * 从字符串池中取数据，创建以空字符结尾的副本并登记到追踪器。
 * off 和 len 来自二进制引用（序列化时写入）。
 * 如果 off == UINT32_MAX，返回 NULL。
 */
static /* OPTIONAL */ const char* _rst_make_cstr(
    _RestoreStrTracker* t,
    /* OPTIONAL */ const char* strpool_data,
    uint32_t off, uint32_t len)
{
    if (off == UINT32_MAX || strpool_data == NULL)
        return NULL;

    {
        const char* data = _bin_strpool_get(strpool_data, off, NULL);
        if (data == NULL)
            return NULL;

        char* copy = (char*)malloc(len + 1);
        if (copy == NULL)
            return NULL;
        memcpy(copy, data, len);
        copy[len] = '\0';

        if (!woort_vector_push_back(&t->m_copies, 1, &copy))
        {
            free(copy);
            return NULL;
        }
        return copy;
    }
}

/* ================================================================
 * 反序列化主函数
 * ================================================================ */

WOORT_NODISCARD woort_CodeEnv_RestoreResult woort_CodeEnv_restore_binary(
    woort_VFile* f, woort_CodeEnv** out_code_env)
{
    woort_CodeEnv_RestoreResult result = WOORT_CODEENV_RESTORE_OK;

    assert(f != NULL);
    assert(out_code_env != NULL);

    *out_code_env = NULL;

    /* 获取文件总大小，用于增量读取校验 */
    int64_t fsize_val = woort_vfile_size(f);
    if (fsize_val < 0)
        return WOORT_CODEENV_RESTORE_FAIL_READ;
    size_t total_size = (size_t)fsize_val;

    /* 定位到文件开头 */
    if (!woort_vfile_seek(f, 0, SEEK_SET))
        return WOORT_CODEENV_RESTORE_FAIL_READ;

    /* 读取头部 32 字节到栈上缓冲区 */
    unsigned char header_buf[32];
    {
        const size_t bytes_read =
            woort_vfile_read(f, header_buf, sizeof(header_buf));
        if (bytes_read != sizeof(header_buf))
            /* File header doesn't match. */
            return WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH;
    }

    /* 解析头部 */
    uint32_t magic, version;
    uint64_t code_size, data_count, constant_count;

    do
    {
        _BinReader rh;
        _bin_reader_init_memory(&rh, header_buf, sizeof(header_buf));

        if (!_bin_read_u32(&rh, &magic) || magic != WOORT_CODEENV_BINARY_MAGIC)
        {
            /* Bad magic */
            return WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH;
        }

        if (!_bin_read_u32(&rh, &version)
            || !_bin_read_u64(&rh, &code_size)
            || !_bin_read_u64(&rh, &data_count)
            || !_bin_read_u64(&rh, &constant_count))
        {
            WOORT_DEBUG("CodeEnv restore: truncated header.");
            return WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
        }

    } while (0);

    if (version != WOORT_CODEENV_BINARY_VERSION)
    {
        WOORT_DEBUG("CodeEnv restore: version mismatch ver=%u.", version);
        return WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH;
    }

    /* 校验 code_size */
    size_t code_bytes = (size_t)code_size * sizeof(woort_Bytecode);
    if (code_bytes > total_size - sizeof(header_buf))
    {
        WOORT_DEBUG("CodeEnv restore: invalid code size %llu.", (unsigned long long)code_size);
        return WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE;
    }

    /* 从 VFile 增量读取字节码到临时缓冲区 */
    woort_Bytecode* codes_from_bin = NULL;
    if (code_bytes > 0)
    {
        codes_from_bin = (woort_Bytecode*)malloc(code_bytes);
        if (codes_from_bin == NULL)
            return WOORT_CODEENV_RESTORE_FAIL_ALLOC;
        {
            const size_t bytes_read = woort_vfile_read(f, codes_from_bin, code_bytes);
            if (bytes_read != code_bytes)
            {
                free(codes_from_bin);
                return WOORT_CODEENV_RESTORE_FAIL_READ;
            }
        }
    }

    /* 创建 CodeEnv (内部会复制字节码，故临时缓冲区可随后释放) */
    woort_CodeEnv* cenv = NULL;
    if (!woort_CodeEnv_create(
        codes_from_bin,
        (size_t)code_size,
        (size_t)constant_count,
        (size_t)(data_count - constant_count),
        &cenv)
        || cenv == NULL)
    {
        free(codes_from_bin);
        return WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV;
    }

    free(codes_from_bin);
    codes_from_bin = NULL;

    _RestoreStrTracker str_tracker;
    _rst_strtracker_init(&str_tracker);

    woort_HashMap /* const char* -> woort_Dylib* */ lib_map;
    woort_hashmap_init(
        &lib_map,
        sizeof(const char*),
        sizeof(woort_Dylib*),
        woort_util_cstr_hash,
        woort_util_cstr_equal);

    woort_CodeEnv_lock(cenv);

    /* 增量读取字符串池 */
    void* strpool_buf = NULL;
    {
        uint64_t strpool_size;
        {
            const size_t bytes_read = woort_vfile_read(f, &strpool_size, sizeof(strpool_size));
            if (bytes_read != sizeof(strpool_size))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
        }

        const char* strpool_data = NULL;
        if (strpool_size > 0)
        {
            strpool_buf = malloc((size_t)strpool_size);
            if (strpool_buf == NULL)
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail_after_create;
            }
            {
                const size_t bytes_read = woort_vfile_read(f, strpool_buf, (size_t)strpool_size);
                if (bytes_read != (size_t)strpool_size)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_READ;
                    goto _restore_fail_after_create;
                }
            }
            strpool_data = (const char*)strpool_buf;
        }

        /* 初始化 VFile 模式读取器，从 VFile 当前位置流式解析剩余数据 */
        _BinReader r;
        r.m_data = NULL;
        r.m_file = f;
        r.m_pos = 0;
        /* 剩余大小 = 文件总大小 - 已读部分 */
        {
            size_t read_so_far = sizeof(header_buf) + code_bytes
                + sizeof(strpool_size) + (size_t)strpool_size;
            r.m_size = (total_size > read_so_far) ? total_size - read_so_far : 0;
        }

        /* 辅助宏：从二进制引用 (offset, length) 创建以空字符结尾的 C 字符串 */
#define _RESTORE_CSTR(off, len) \
        _rst_make_cstr(&str_tracker, strpool_data, (off), (len))

        /*
         * 读取外部库列表，形成 HashMap 以便后续查询。
         */
        {
            uint64_t lib_count;
            if (!_bin_read_u64(&r, &lib_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t li = 0; li < lib_count; ++li)
            {
                uint32_t name_off, name_len;
                uint32_t path_off, path_len;
                uint32_t script_path_off, script_path_len;
                if (!_bin_read_u32(&r, &name_off)
                    || !_bin_read_u32(&r, &name_len)
                    || !_bin_read_u32(&r, &path_off)
                    || !_bin_read_u32(&r, &path_len)
                    || !_bin_read_u32(&r, &script_path_off)
                    || !_bin_read_u32(&r, &script_path_len))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                const char* lib_name = _RESTORE_CSTR(name_off, name_len);
                const char* lib_path = _RESTORE_CSTR(path_off, path_len);
                const char* lib_script_path = _RESTORE_CSTR(script_path_off, script_path_len);
                if (lib_name != NULL)
                {
                    woort_Dylib* const lib = woort_dylib_load(
                        lib_name,
                        lib_path,
                        lib_script_path, false);

                    if (lib != NULL)
                    {
                        (void)woort_CodeEnv_add_extern_lib(cenv, lib);
                        (void)woort_hashmap_insert(&lib_map, &lib_name, &lib);

                        woort_dylib_unload(lib, WOORT_DYLIB_UNREF);
                    }
                }
            }
        }

        /*
         * 读取常量数据。
         */
        {
            for (size_t i = 0; i < constant_count; ++i)
            {
                uint8_t type;
                if (!_bin_read_u8(&r, &type))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                switch (type)
                {
                case WOORT_CONST_TYPE_NIL:
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_NIL, NULL, NULL);
                    break;

                case WOORT_CONST_TYPE_INT:
                {
                    int64_t v;
                    if (!_bin_read_i64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_int(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_INT, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_REAL:
                {
                    double v;
                    if (!_bin_read_f64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_real(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_REAL, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_STRING:
                {
                    uint32_t off;
                    uint32_t slen;
                    if (!_bin_read_u32(&r, &off)
                        || !_bin_read_u32(&r, &slen))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    const char* s = (off == UINT32_MAX)
                        ? NULL
                        : _bin_strpool_get(strpool_data, off, NULL);
                    woort_CodeEnv_set_const_buffer(cenv, (woort_IRConstantIndex)i,
                        s != NULL ? (const void*)s : "", s != NULL ? (size_t)slen : 0);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_STRING, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_SCRIPT_FUNC:
                {
                    uint32_t off;
                    if (!_bin_read_u32(&r, &off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    const woort_Bytecode* addr = (off != UINT32_MAX && cenv->m_code_end > cenv->m_code_begin)
                        ? cenv->m_code_begin + off : NULL;
                    woort_CodeEnv_set_const_script_function(cenv, (woort_IRConstantIndex)i, addr);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_SCRIPT_FUNC, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_EXTERN_FUNC:
                {
                    uint32_t lib_off, lib_len, func_off, func_len;
                    if (!_bin_read_u32(&r, &lib_off)
                        || !_bin_read_u32(&r, &lib_len)
                        || !_bin_read_u32(&r, &func_off)
                        || !_bin_read_u32(&r, &func_len))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    const char* lib_name = _RESTORE_CSTR(lib_off, lib_len);
                    const char* func_name = _RESTORE_CSTR(func_off, func_len);

                    if (lib_name == NULL || func_name == NULL)
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET;
                        goto _restore_fail_after_create;
                    }

                    woort_Dylib** lib = NULL;
                    if (!woort_hashmap_find(&lib_map, &lib_name, (void**)&lib))
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot find lib '%s' in lib_map.", lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_NativeFunction nf = (woort_NativeFunction)woort_dylib_load_func(*lib, func_name);
                    if (nf == NULL)
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot find func '%s' in lib '%s'.", func_name, lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_CodeEnv_set_const_extern_function(cenv, (woort_IRConstantIndex)i, nf);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_EXTERN_FUNC, lib_name, func_name);
                    break;
                }

                case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
                {
                    uint32_t off;
                    if (!_bin_read_u32(&r, &off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    const woort_Bytecode* addr = (off != UINT32_MAX && cenv->m_code_end > cenv->m_code_begin)
                        ? cenv->m_code_begin + off : NULL;
                    woort_CodeEnv_set_const_script_closure(cenv, (woort_IRConstantIndex)i, addr);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_SCRIPT_CLOSURE, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_EXTERN_CLOSURE:
                {
                    uint32_t lib_off, lib_len, func_off, func_len;
                    if (!_bin_read_u32(&r, &lib_off)
                        || !_bin_read_u32(&r, &lib_len)
                        || !_bin_read_u32(&r, &func_off)
                        || !_bin_read_u32(&r, &func_len))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    const char* lib_name = _RESTORE_CSTR(lib_off, lib_len);
                    const char* func_name = _RESTORE_CSTR(func_off, func_len);
                    if (lib_name == NULL || func_name == NULL)
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET;
                        goto _restore_fail_after_create;
                    }

                    woort_Dylib** lib = NULL;
                    if (!woort_hashmap_find(&lib_map, &lib_name, (void**)&lib))
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot find lib '%s' in lib_map.", lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_NativeFunction nf = (woort_NativeFunction)woort_dylib_load_func(*lib, func_name);
                    if (nf == NULL)
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot find func '%s' in lib '%s'.", func_name, lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_CodeEnv_set_const_extern_closure(cenv, (woort_IRConstantIndex)i, nf);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_EXTERN_CLOSURE, lib_name, func_name);
                    break;
                }

                case WOORT_CONST_TYPE_BOX_INT:
                {
                    int64_t v;
                    if (!_bin_read_i64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_box_int(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_BOX_INT, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_BOX_REAL:
                {
                    double v;
                    if (!_bin_read_f64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_box_real(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_BOX_REAL, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_BOX_BOOL:
                {
                    uint8_t v;
                    if (!_bin_read_u8(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_box_bool(cenv, (woort_IRConstantIndex)i, v != 0);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_BOX_BOOL, NULL, NULL);
                    break;
                }

                case WOORT_CONST_TYPE_STRUCT:
                {
                    uint32_t member_count;
                    if (!_bin_read_u32(&r, &member_count))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    /* struct 成员索引表在常数池内按序读取 */
                    woort_IRConstantIndex* members = NULL;
                    if (member_count > 0)
                    {
                        members = (woort_IRConstantIndex*)malloc(
                            member_count * sizeof(woort_IRConstantIndex));
                        if (members == NULL)
                        {
                            result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                            goto _restore_fail_after_create;
                        }
                        for (uint32_t mi = 0; mi < member_count; ++mi)
                        {
                            uint32_t midx;
                            if (!_bin_read_u32(&r, &midx))
                            {
                                free(members);
                                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                                goto _restore_fail_after_create;
                            }
                            members[mi] = (woort_IRConstantIndex)midx;
                        }
                    }

                    woort_CodeEnv_set_const_struct(cenv, (woort_IRConstantIndex)i,
                        members, member_count);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_STRUCT, NULL, NULL);

                    free(members);
                    break;
                }

                default:
                    WOORT_DEBUG("CodeEnv restore: unknown const type %d at %zu.", (int)type, i);
                    result = WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE;
                    goto _restore_fail_after_create;
                }
            }
        }

        /*
         * 读取 extern 常量映射表。
         */
        {
            uint64_t extern_count;
            if (!_bin_read_u64(&r, &extern_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t ei = 0; ei < extern_count; ++ei)
            {
                uint32_t name_off, name_len;
                uint32_t cidx;
                if (!_bin_read_u32(&r, &name_off)
                    || !_bin_read_u32(&r, &name_len)
                    || !_bin_read_u32(&r, &cidx))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                const char* name = _RESTORE_CSTR(name_off, name_len);
                if (name != NULL)
                    (void)woort_CodeEnv_register_extern_constant(cenv, name, cidx);
            }
        }

        /*
         * 读取函数边界表。
         */
        {
            uint64_t fb_count;
            if (!_bin_read_u64(&r, &fb_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t fi = 0; fi < fb_count; ++fi)
            {
                woort_FunctionBoundary fb;
                uint32_t name_off, name_len;
                if (!_bin_read_u32(&r, &fb.m_offset_begin)
                    || !_bin_read_u32(&r, &fb.m_code_length)
                    || !_bin_read_u32(&r, &name_off)
                    || !_bin_read_u32(&r, &name_len))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                const char* name = _RESTORE_CSTR(name_off, name_len);
                /* intern into CodeEnv string pool for lifetime management */
                if (name != NULL)
                    name = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, name);
                fb.m_name = name;

                if (!woort_vector_push_back(
                    &cenv->m_pdb.m_function_boundaries, 1, &fb))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail_after_create;
                }
            }
        }

        /*
         * 读取源码映射表。
         */
        {
            uint64_t sm_count;
            if (!_bin_read_u64(&r, &sm_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            if (sm_count > 0)
            {
                woort_SourceMap_Entry* entries = (woort_SourceMap_Entry*)malloc(
                    sizeof(woort_SourceMap_Entry) * (size_t)sm_count);
                if (entries == NULL)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail_after_create;
                }

                for (uint64_t si = 0; si < sm_count; ++si)
                {
                    uint32_t fp_off, fp_len;
                    if (!_bin_read_u32(&r, &entries[si].m_bytecode_offset)
                        || !_bin_read_u32(&r, &fp_off)
                        || !_bin_read_u32(&r, &fp_len)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_begin_line)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_begin_column)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_end_line)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_end_column))
                    {
                        free(entries);
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    const char* fp = _RESTORE_CSTR(fp_off, fp_len);
                    if (fp != NULL)
                        fp = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, fp);
                    entries[si].m_location.m_filepath = fp;
                }

                free(cenv->m_pdb.m_source_map.m_entries);
                cenv->m_pdb.m_source_map.m_entries = entries;
                cenv->m_pdb.m_source_map.m_entry_count = (uint32_t)sm_count;
            }
        }

        /*
         * 读取 trap 记录表。
         */
        {
            uint64_t trap_count;
            if (!_bin_read_u64(&r, &trap_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t ti = 0; ti < trap_count; ++ti)
            {
                uint32_t off, orig;
                if (!_bin_read_u32(&r, &off)
                    || !_bin_read_u32(&r, &orig))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                if (cenv->m_code_end > cenv->m_code_begin
                    && (size_t)off < (size_t)(cenv->m_code_end - cenv->m_code_begin))
                {
                    /* 注意：trap 记录在恢复时仅仅存下来，不激活（因为可能字节码已变） */
                    (void)woort_hashmap_insert(
                        &cenv->m_trap_records,
                        &cenv->m_code_begin[off],
                        &orig);
                }
            }
        }

        /*
         * 读取局部变量调试信息。
         */
        {
            uint64_t lv_count;
            if (!_bin_read_u64(&r, &lv_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t li = 0; li < lv_count; ++li)
            {
                uint32_t name_off, name_len;
                uint32_t function_offset;
                uint32_t stack_offset;
                if (!_bin_read_u32(&r, &name_off)
                    || !_bin_read_u32(&r, &name_len)
                    || !_bin_read_u32(&r, &function_offset)
                    || !_bin_read_u32(&r, &stack_offset))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                woort_LocalVarDebugInfo info;
                const char* name = _RESTORE_CSTR(name_off, name_len);
                if (name != NULL)
                    name = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, name);
                info.m_name = name;
                info.m_function_offset = function_offset;
                info.m_stack_offset = (int32_t)stack_offset;

                if (!woort_vector_push_back(
                    &cenv->m_pdb.m_local_var_debug_info, 1, &info))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail_after_create;
                }
            }
        }

        /*
         * 读取静态变量调试信息。
         */
        {
            uint64_t sv_count;
            if (!_bin_read_u64(&r, &sv_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t si = 0; si < sv_count; ++si)
            {
                uint32_t name_off, name_len;
                uint32_t static_idx;
                if (!_bin_read_u32(&r, &name_off)
                    || !_bin_read_u32(&r, &name_len)
                    || !_bin_read_u32(&r, &static_idx))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                woort_StaticVarDebugInfo info;
                const char* name = _RESTORE_CSTR(name_off, name_len);
                if (name != NULL)
                    name = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, name);
                info.m_name = name;
                info.m_static_idx = (woort_IRStaticIndex)static_idx;

                if (!woort_vector_push_back(
                    &cenv->m_pdb.m_static_var_debug_info, 1, &info))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail_after_create;
                }
            }
        }
    }

    _rst_strtracker_deinit(&str_tracker);
    free(strpool_buf);
    woort_hashmap_deinit(&lib_map);
    woort_CodeEnv_unlock(cenv);

    *out_code_env = cenv;
    return WOORT_CODEENV_RESTORE_OK;

_restore_fail_after_create:
    _rst_strtracker_deinit(&str_tracker);
    free(strpool_buf);
    woort_hashmap_deinit(&lib_map);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_drop(cenv);
    return result;
}

WOORT_NODISCARD const char* woort_CodeEnv_restore_failed_desc(
    woort_CodeEnv_RestoreResult rt)
{
    switch (rt)
    {
    case WOORT_CODEENV_RESTORE_OK:
        return "OK";
    case WOORT_CODEENV_RESTORE_FAIL_READ:
        return "Failed to restore binary: read error.";
    case WOORT_CODEENV_RESTORE_FAIL_ALLOC:
        return "Failed to restore binary: out of memory.";
    case WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH:
        return "Failed to restore binary: bad magic number.";
    case WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH:
        return "Failed to restore binary: unsupported binary version.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE:
        return "Failed to restore binary: invalid code size.";
    case WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV:
        return "Failed to restore binary: internal creation failure.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_STRPOOL:
        return "Failed to restore binary: invalid string pool.";
    case WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA:
        return "Failed to restore binary: truncated data.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE:
        return "Failed to restore binary: unknown constant type.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET:
        return "Failed to restore binary: invalid pool offset.";
    case WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE:
        return "Failed to restore binary: cannot resolve external symbol.";
    default:
        return "Failed to restore binary: unknown error.";
    }
}

#undef _RESTORE_CSTR
