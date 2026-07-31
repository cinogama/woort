#include "woort.h"

#include "woort_codeenv.h"
#include "woort_dylib.h"
#include "woort_gc_string.h"
#include "woort_gc_closure.h"
#include "woort_gc_struct.h"
#include "woort_log.h"
#include "woort_util.h"
#include "woort_vfs.h"

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * 二进制序列化 / 反序列化
 * ========================================================================
 *
 *  整体字节布局（version 7；与 v6 同字段、同顺序、同编码，仅版本号变更，
 *  并修正 struct 成员索引回退 bug——对状态正常的 CodeEnv 产生相同字节）：
 *
 *    header (32 字节)
 *      u32 magic           0x54524f57 ("WORT")
 *      u32 version         7
 *      u64 code_size       字节码字数（woort_Bytecode 个数）
 *      u64 data_count      m_data_begin 总槽位
 *      u64 const_count     常量池槽位数（前 const_count 个）
 *
 *    bytecode[code_size]   原始 woort_Bytecode 数组
 *
 *    u64 strpool_size      字符串池字节数
 *    char strpool[strpool_size]
 *                          每条目：u32 len + len 字节（二进制安全）
 *
 *    u64 lib_count         外部库条目数
 *    per lib:
 *      u32 name_off / u32 name_len
 *      u32 path_off / u32 path_len
 *      u32 script_path_off / u32 script_path_len   (off == STRREF_NULL 表示无)
 *
 *    per const (const_count 个，无计数前缀；计数由 header 给出):
 *      u8 type             woort_ConstRecordType
 *      type 特定负载（见 save / restore 的 switch）
 *
 *    u64 extern_count      外部常量映射条目数
 *    per entry:
 *      u32 name_off / u32 name_len / u32 cidx
 *
 *    u64 fb_count          函数边界条目数
 *    per entry:
 *      u32 offset_begin / u32 code_length
 *      u32 name_off / u32 name_len
 *
 *    u64 sm_count          源码映射条目数
 *    per entry:
 *      u32 bytecode_offset
 *      u32 filepath_off / u32 filepath_len
 *      u32 begin_line / u32 begin_column / u32 end_line / u32 end_column
 *
 *    u64 trap_count        trap 记录条目数
 *    per entry:
 *      u32 bytecode_offset / u32 original_opcode
 *
 *    u64 lv_count          局部变量调试信息条目数
 *    per entry:
 *      u32 name_off / u32 name_len
 *      u32 function_offset / u32 stack_offset
 *
 *    u64 sv_count          静态变量调试信息条目数
 *    per entry:
 *      u32 name_off / u32 name_len / u32 static_idx
 *
 *  字符串引用统一编码为 (uint32_t offset, uint32_t length) 对。
 *  offset == WOORT_STRREF_NULL 表示该字符串不存在（NULL）。
 * ======================================================================== */

 /*
  * 二进制格式版本号与魔数。
  */
#define WOORT_CODEENV_BINARY_MAGIC   0x54524f57u  /* "WORT" */
#define WOORT_CODEENV_BINARY_VERSION 7u

  /* 字符串引用的 NULL 哨兵。序列化时写入此 offset 表示"无字符串"。 */
#define WOORT_STRREF_NULL UINT32_MAX

/* ========================================================================
 * 第一部分：字符串池（去重收集）
 *
 *  字符串池用于序列化期间对所有可变长字符串做内容去重。
 *  - m_data：拼接后的 length-prefix 字节流（即最终写入文件的池数据）。
 *  - m_keys：独立堆分配的 length-prefix 缓冲区列表（char*），每个唯一字符串各一份。
 *            这些缓冲区地址稳定（不会被 realloc 移动），作为 hashmap key 使用。
 *  - m_map：内容 -> offset 的 hashmap，O(1) 去重。
 *
 *  设计要点：m_data 的 push_back 会触发 realloc，导致其中数据地址变化，因此
 *  hashmap key 不能指向 m_data 内部。改为给每个唯一字符串单独 malloc 一个
 *  length-prefix 缓冲区（登记在 m_keys 中统一释放），用其稳定地址作 key。
 *
 *  hashmap key 类型为 const char*，指向 m_keys 中某缓冲区起始；
 *  自定义 hash/equal 按 (len, bytes) 比较（二进制安全）。
 * ======================================================================== */

typedef struct _CodeEnvBinStrPool
{
    woort_Vector m_data;      /* length-prefix 拼接字节流（最终写入的池数据） */
    woort_Vector m_keys;      /* char*：每个唯一字符串的独立 length-prefix 缓冲区 */
    woort_HashMap m_map;      /* char*（指向 m_keys 中缓冲区起始）-> uint32_t offset */
} _CodeEnvBinStrPool;

/* 自定义 hashmap 回调：key 指向某 length-prefix 缓冲区起始。 */
static size_t _strpool_hash_cb(const void* key)
{
    const char* p = *(const char* const*)key;
    uint32_t len;
    memcpy(&len, p, sizeof(uint32_t));
    return woort_hash_string(p + sizeof(uint32_t), (size_t)len);
}

static bool _strpool_equal_cb(const void* key1, const void* key2)
{
    const char* p1 = *(const char* const*)key1;
    const char* p2 = *(const char* const*)key2;
    uint32_t len1, len2;
    memcpy(&len1, p1, sizeof(uint32_t));
    memcpy(&len2, p2, sizeof(uint32_t));
    if (len1 != len2)
        return false;
    return memcmp(p1 + sizeof(uint32_t), p2 + sizeof(uint32_t), len1) == 0;
}

static void _bin_strpool_init(_CodeEnvBinStrPool* sp)
{
    woort_vector_init(&sp->m_data, sizeof(char));
    woort_vector_init(&sp->m_keys, sizeof(char*));
    woort_hashmap_init(&sp->m_map, sizeof(const char*), sizeof(uint32_t),
        &_strpool_hash_cb, &_strpool_equal_cb);
}

static void _bin_strpool_deinit(_CodeEnvBinStrPool* sp)
{
    woort_hashmap_deinit(&sp->m_map);
    {
        char** keys = (char**)sp->m_keys.m_data;
        for (size_t i = 0; i < sp->m_keys.m_size; ++i)
            free(keys[i]);
    }
    woort_vector_deinit(&sp->m_keys);
    woort_vector_deinit(&sp->m_data);
}

/*
 * 插入字符串，返回其在 m_data 中的偏移量。
 * 若 str == NULL，记为 WOORT_STRREF_NULL（NULL 哨兵），不插入任何数据。
 * 若字符串已存在，返回已有偏移量（O(1) hashmap 命中）。
 * 字符串以长度前缀格式存储（二进制安全）。
 * 返回 false 仅在 OOM。
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
            *out_offset = WOORT_STRREF_NULL;
        return true;
    }

    /*
     * 先查 hashmap：用调用方 str 的内容构造一个临时 length-prefix 用于比较。
     * 这里直接在栈上拼一个 length-prefix 视图（uint32 len + str 内容），无需额外分配。
     * 注意：woort_hash_string 与 equal_cb 都只读取 (len, bytes)，不要求内存所有权。
     */
    const uint32_t len32 = (uint32_t)len;
    void* found_off_addr = NULL;

    /*
     * hashmap_find 的 key 参数指向一个 const char*，该 char* 必须指向
     * length-prefix 数据。我们在栈上构造不可行（指针需指向连续的 len+data），
     * 故用一个短生命周期的堆缓冲区做查找 key。
     */
    const size_t probe_alloc = sizeof(uint32_t) + len;
    char* probe = (char*)malloc(probe_alloc);
    if (probe == NULL)
        return false;
    memcpy(probe, &len32, sizeof(uint32_t));
    if (len > 0)
        memcpy(probe + sizeof(uint32_t), str, len);

    const char* probe_key = probe;
    const bool existed = woort_hashmap_find(&sp->m_map, &probe_key, &found_off_addr);
    free(probe);

    if (existed)
    {
        /* 已存在：返回已有 offset，不改动 m_data / m_map。 */
        if (out_offset != NULL)
            *out_offset = *(const uint32_t*)found_off_addr;
        return true;
    }

    /* 新条目：
     *   1) 追加 length-prefix 到 m_data（计算最终 offset）；
     *   2) 单独 malloc 一份 length-prefix 作稳定 hashmap key，登记到 m_keys；
     *   3) 插入 hashmap。
     */
    const uint32_t data_offset = (uint32_t)sp->m_data.m_size;

    if (!woort_vector_push_back(&sp->m_data, sizeof(uint32_t), &len32))
        return false;
    if (len > 0 && !woort_vector_push_back(&sp->m_data, len, str))
    {
        sp->m_data.m_size = (size_t)data_offset;  /* 回滚 len 前缀 */
        return false;
    }

    /* 稳定 key 缓冲区（独立拥有，不随 m_data realloc 移动）。 */
    char* key_buf = (char*)malloc(probe_alloc);
    if (key_buf == NULL)
    {
        sp->m_data.m_size = (size_t)data_offset;
        return false;
    }
    memcpy(key_buf, &len32, sizeof(uint32_t));
    if (len > 0)
        memcpy(key_buf + sizeof(uint32_t), str, len);

    if (!woort_vector_push_back(&sp->m_keys, 1, &key_buf))
    {
        free(key_buf);
        sp->m_data.m_size = (size_t)data_offset;
        return false;
    }

    /*
     * 此时 key_buf 地址稳定（仅 m_keys 存其指针；m_data 后续 realloc 不影响它）。
     * 插入 hashmap：key = &key_buf（hashmap 复制该 const char* 指针值）。
     */
    const woort_hashmap_Result ir = woort_hashmap_insert(&sp->m_map, &key_buf, &data_offset);
    if (ir != WOORT_HASHMAP_RESULT_OK)
    {
        /* 回滚 m_keys 与 m_data。ALREADY_EXIST 不应发生（前面已查过），按 OOM 处理。 */
        sp->m_keys.m_size -= 1;
        free(key_buf);
        sp->m_data.m_size = (size_t)data_offset;
        return false;
    }

    if (out_offset != NULL)
        *out_offset = data_offset;
    return true;
}

/*
 * 从池数据区读取字符串（反序列化侧使用）。
 * 返回指向池数据区中跳过长度前缀后的指针（稳定），或 NULL（off == STRREF_NULL）。
 */
static /* OPTIONAL */ const char* _bin_strpool_get(
    const char* data, uint32_t offset,
    /* OPTIONAL */ size_t* out_len)
{
    if (offset == WOORT_STRREF_NULL)
    {
        if (out_len != NULL)
            *out_len = 0;
        return NULL;
    }
    uint32_t len;
    memcpy(&len, data + offset, sizeof(uint32_t));
    if (out_len != NULL)
        *out_len = (size_t)len;
    return data + offset + sizeof(uint32_t);
}

/* ================================================================
 * 第二部分：缓冲写入器
 * ================================================================ */

typedef struct _BinWriter
{
    woort_Vector m_buf;  /* unsigned char */
} _BinWriter;

static void _bin_writer_init(_BinWriter* w)
{
    woort_vector_init(&w->m_buf, sizeof(unsigned char));
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

/*
 * 分离出最终 buffer。malloc(0) 行为未定义，对空 buffer 显式返回非 NULL 的零长度分配。
 * 调用方负责 free()。
 */
static /* OPTIONAL */ void* _bin_writer_detach(_BinWriter* w, size_t* out_len)
{
    const size_t sz = w->m_buf.m_size;
    *out_len = sz;
    void* data = malloc(sz == 0 ? 1 : sz);
    if (data != NULL && sz > 0)
        memcpy(data, w->m_buf.m_data, sz);
    return data;
}

/* ================================================================
 * 第三部分：缓冲读取器（支持内存缓冲与 VFile 流式两种模式）
 * ================================================================ */

typedef struct _BinReader
{
    /* OPTIONAL */ const unsigned char* m_data;   /* 内存模式 */
    size_t            m_size;
    size_t            m_pos;
    /* OPTIONAL */ woort_VFile* m_file;            /* VFile 流式模式（m_data 为 NULL） */
} _BinReader;

static void _bin_reader_init_memory(
    _BinReader* r, const void* data, size_t size)
{
    r->m_data = (const unsigned char*)data;
    r->m_size = size;
    r->m_pos = 0;
    r->m_file = NULL;
}

static void _bin_reader_init_vfile(
    _BinReader* r, woort_VFile* f, size_t remaining)
{
    r->m_data = NULL;
    r->m_size = remaining;
    r->m_pos = 0;
    r->m_file = f;
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

/* ================================================================
 * 第四部分：extern 函数名解析（save 侧共用）
 *
 *  当 ConstRecord 未登记 lib/func 名时，从 dylib 运行时表反查。
 *  收集阶段与写常量阶段共用，消除重复逻辑。
 *  返回 false 表示无法解析（视为状态异常，应令 save 失败）。
 * ================================================================ */

static bool _save_resolve_extern_names(
    woort_ConstRecordType type,
    const woort_Value* val,
    /* OUT */ const char** out_lib_name,
    /* OUT */ const char** out_func_name)
{
    woort_NativeFunction nf;
    if (type == WOORT_CONST_TYPE_EXTERN_CLOSURE)
    {
        const woort_GCClosure* closure = val->m_closure;
        if (closure == NULL || closure->m_script_function != NULL)
            return false;  /* 闭包无原生函数指针，无法解析 */
        nf = closure->m_native_function;
    }
    else
    {
        nf = val->m_native_function;
        if (nf == NULL)
            return false;
    }

    woort_Dylib* found_lib = NULL;
    if (!woort_Dylib_find_by_resolved_func((void*)nf, &found_lib) || found_lib == NULL)
        return false;

    const char* resolved_name = NULL;
    if (!woort_Dylib_get_function_name(found_lib, (void*)nf, &resolved_name) || resolved_name == NULL)
        return false;

    *out_lib_name = found_lib->m_name;
    *out_func_name = resolved_name;
    return true;
}

/*
 * 取得 extern 函数/闭包的 lib/func 名（优先用 record 已登记的，否则运行时解析）。
 * 成功返回 true 并输出两个非 NULL 指针（指向 record 或 dylib 内的稳定内存）。
 */
static bool _save_get_extern_names(
    const woort_ConstRecord* rec,
    const woort_Value* val,
    /* OUT */ const char** out_lib_name,
    /* OUT */ const char** out_func_name)
{
    if (rec->m_lib_name != NULL && rec->m_func_name != NULL)
    {
        *out_lib_name = rec->m_lib_name;
        *out_func_name = rec->m_func_name;
        return true;
    }
    return _save_resolve_extern_names(rec->m_type, val, out_lib_name, out_func_name);
}

/* ================================================================
 * 第五部分：struct 成员常量索引查找（save 侧）
 *
 *  在常量池中查找与某个 woort_Value 值/类型匹配的常量索引。
 *  struct 的成员值必须是常量池中已存在条目的副本。
 *  找不到返回 false（CodeEnv 状态异常）——不再使用成员序号作回退，
 *  因为那会写入错误的常量索引。
 * ================================================================ */

static bool _const_value_matches(
    const woort_ConstRecord* rec, const woort_Value* cv, const woort_Value* mv)
{
    switch (rec->m_type)
    {
    case WOORT_CONST_TYPE_NIL:
        return mv->m_gcinstance == cv->m_gcinstance;
    case WOORT_CONST_TYPE_INT:
        return mv->m_integer == cv->m_integer;
    case WOORT_CONST_TYPE_REAL:
        return mv->m_real == cv->m_real;
    case WOORT_CONST_TYPE_STRING:
        return mv->m_gcinstance == cv->m_gcinstance;
    case WOORT_CONST_TYPE_SCRIPT_FUNC:
        return mv->m_script_function == cv->m_script_function;
    case WOORT_CONST_TYPE_EXTERN_FUNC:
        return mv->m_native_function == cv->m_native_function;
    case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
    case WOORT_CONST_TYPE_EXTERN_CLOSURE:
        return mv->m_gcinstance == cv->m_gcinstance;
    case WOORT_CONST_TYPE_BOX_INT:
    case WOORT_CONST_TYPE_BOX_REAL:
    case WOORT_CONST_TYPE_BOX_BOOL:
        return mv->m_dynamic.m_boxed == cv->m_dynamic.m_boxed;
    case WOORT_CONST_TYPE_STRUCT:
        return mv->m_gcinstance == cv->m_gcinstance;
    default:
        return false;
    }
}

static bool _find_const_index_for_value(
    const woort_CodeEnv* code_env,
    const woort_Value* mv,
    size_t const_count,
    /* OUT */ woort_IRConstantIndex* out_idx)
{
    for (size_t j = 0; j < const_count; ++j)
    {
        const woort_ConstRecord* rec_j = (const woort_ConstRecord*)woort_vector_at(
            (woort_Vector*)&code_env->m_const_records, j);
        const woort_Value* cv = &code_env->m_data_begin[j];
        if (_const_value_matches(rec_j, cv, mv))
        {
            *out_idx = (woort_IRConstantIndex)j;
            return true;
        }
    }
    return false;
}

/* ================================================================
 * 第六部分：HashMap foreach 回调上下文与回调（save 侧）
 *
 *  woort_HashMap 没有迭代器，必须用 foreach 模式。这里为三处使用
 *  （extern 常量名收集、extern 常量写入、trap 写入）定义统一的 ctx。
 * ================================================================ */

 /* 收集 extern 常量名到字符串池（collect 阶段）。 */
struct _StrpoolCollectCtx {
    _CodeEnvBinStrPool* m_sp;
    bool* m_ok;
};

static bool _strpool_collect_key_cb(const void* key, void* value, void* user_data)
{
    (void)value;
    struct _StrpoolCollectCtx* ctx = (struct _StrpoolCollectCtx*)user_data;
    const char* name = *(const char**)key;
    *ctx->m_ok = *ctx->m_ok
        && _bin_strpool_insert(ctx->m_sp, name, strlen(name), NULL);
    return true;  /* 继续遍历；失败由 m_ok 在调用方检查 */
}

/* 写入 extern 常量条目（emit 阶段）。 */
struct _WriteExternConstCtx {
    _CodeEnvBinStrPool* m_sp;
    _BinWriter* m_w;
    bool* m_ok;
};

static bool _write_extern_const_cb(const void* key, void* value, void* user_data)
{
    struct _WriteExternConstCtx* ctx = (struct _WriteExternConstCtx*)user_data;
    const char* name = *(const char**)key;
    uint32_t name_len = (uint32_t)strlen(name);
    uint32_t name_off = WOORT_STRREF_NULL;
    *ctx->m_ok = *ctx->m_ok
        && _bin_strpool_insert(ctx->m_sp, name, name_len, &name_off)
        && _bin_write_u32(ctx->m_w, name_off)
        && _bin_write_u32(ctx->m_w, name_len)
        && _bin_write_u32(ctx->m_w, *(woort_IRConstantIndex*)value);
    return *ctx->m_ok;  /* 失败即终止遍历 */
}

/* 写入 trap 记录条目（emit 阶段）。 */
struct _WriteTrapCtx {
    const woort_Bytecode* m_code_begin;
    _BinWriter* m_w;
    bool* m_ok;
};

static bool _write_trap_cb(const void* key, void* value, void* user_data)
{
    struct _WriteTrapCtx* ctx = (struct _WriteTrapCtx*)user_data;
    const woort_Bytecode* addr = *(const woort_Bytecode**)key;
    woort_Bytecode orig = *(woort_Bytecode*)value;
    uint32_t off = (uint32_t)(addr - ctx->m_code_begin);
    *ctx->m_ok = *ctx->m_ok
        && _bin_write_u32(ctx->m_w, off)
        && _bin_write_u32(ctx->m_w, orig);
    return *ctx->m_ok;  /* 失败即终止遍历 */
}

/* ================================================================
 * 第七部分：序列化主函数
 *
 *  结构：
 *    1. 写头部
 *    2. 写字节码
 *    3. 第一趟 collect：把所有需要的字符串插入池（hashmap 去重）
 *    4. 写字符串池（size + bytes）
 *    5. 第二趟 emit：写外部库 / 常量 / extern 常量 / 函数边界 /
 *       源码映射 / trap / 局部变量调试 / 静态变量调试
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
    const size_t const_count = code_env->m_const_records.m_size;

    /*
     * 写入头部。
     */
    ok = ok && _bin_write_u32(&w, WOORT_CODEENV_BINARY_MAGIC);
    ok = ok && _bin_write_u32(&w, WOORT_CODEENV_BINARY_VERSION);
    ok = ok && _bin_write_u64(&w, (uint64_t)code_size);
    ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_data_count);
    ok = ok && _bin_write_u64(&w, (uint64_t)const_count);

    /*
     * 写入字节码。
     */
    if (ok && code_size > 0)
        ok = ok && _bin_write_raw(&w, code_env->m_code_begin, code_size * sizeof(woort_Bytecode));

    /*
     * ---------- 第一趟：collect ----------
     * 把所有需要序列化的字符串插入池。之后池大小固定，写入各引用段时再 insert 全部命中。
     */

     /* 常量数据中的字符串/库名/函数名 */
    for (size_t i = 0; ok && i < const_count; ++i)
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
            const char* lib_name = NULL;
            const char* func_name = NULL;
            if (!_save_get_extern_names(rec, &code_env->m_data_begin[i], &lib_name, &func_name))
            {
                WOORT_DEBUG("CodeEnv save: cannot resolve extern names for const %zu.", i);
                ok = false;
                break;
            }
            ok = ok && _bin_strpool_insert(&strpool, lib_name, strlen(lib_name), NULL);
            ok = ok && _bin_strpool_insert(&strpool, func_name, strlen(func_name), NULL);
            break;
        }
        default:
            break;
        }
    }

    /* extern 常量映射表名称 */
    if (ok)
    {
        struct _StrpoolCollectCtx ctx = { &strpool, &ok };
        (void)woort_hashmap_foreach(&code_env->m_extern_constants,
            &_strpool_collect_key_cb, &ctx);
    }

    /* 外部库：name / path / script_path */
    for (size_t i = 0; ok && i < code_env->m_extern_libs.m_size; ++i)
    {
        woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(&code_env->m_extern_libs, i);
        ok = ok && _bin_strpool_insert(&strpool, lib->m_name, strlen(lib->m_name), NULL);
        ok = ok && _bin_strpool_insert(&strpool, lib->m_path, strlen(lib->m_path), NULL);
        if (lib->m_script_path != NULL)
            ok = ok && _bin_strpool_insert(&strpool, lib->m_script_path, strlen(lib->m_script_path), NULL);
    }

    /* 函数边界名称 */
    for (size_t i = 0; ok && i < code_env->m_function_boundaries.m_size; ++i)
    {
        const woort_FunctionBoundary* fb = (const woort_FunctionBoundary*)woort_vector_at(
            &code_env->m_function_boundaries, i);
        if (fb->m_name != NULL)
            ok = ok && _bin_strpool_insert(&strpool, fb->m_name, strlen(fb->m_name), NULL);
    }

    /* 源码映射文件路径 */
    for (uint32_t i = 0; ok && i < code_env->m_pdb.m_source_map.m_entry_count; ++i)
    {
        const woort_SourceMap_Entry* entry = &code_env->m_pdb.m_source_map.m_entries[i];
        if (entry->m_location.m_filepath != NULL)
            ok = ok && _bin_strpool_insert(&strpool,
                entry->m_location.m_filepath, strlen(entry->m_location.m_filepath), NULL);
    }

    /* 局部变量调试信息名称 */
    for (size_t i = 0; ok && i < code_env->m_pdb.m_local_var_debug_info.m_size; ++i)
    {
        const woort_LocalVarDebugInfo* info = (const woort_LocalVarDebugInfo*)woort_vector_at(
            &code_env->m_pdb.m_local_var_debug_info, i);
        if (info->m_name != NULL)
            ok = ok && _bin_strpool_insert(&strpool, info->m_name, strlen(info->m_name), NULL);
    }

    /* 静态变量调试信息名称 */
    for (size_t i = 0; ok && i < code_env->m_pdb.m_static_var_debug_info.m_size; ++i)
    {
        const woort_StaticVarDebugInfo* info = (const woort_StaticVarDebugInfo*)woort_vector_at(
            &code_env->m_pdb.m_static_var_debug_info, i);
        if (info->m_name != NULL)
            ok = ok && _bin_strpool_insert(&strpool, info->m_name, strlen(info->m_name), NULL);
    }

    /*
     * 写入字符串池（size + bytes）。
     */
    ok = ok && _bin_write_u64(&w, (uint64_t)strpool.m_data.m_size);
    if (ok && strpool.m_data.m_size > 0)
        ok = ok && _bin_write_raw(&w, strpool.m_data.m_data, strpool.m_data.m_size);

    /*
     * ---------- 第二趟：emit ----------
     * 写各引用段。此时 insert 全部命中已有条目（O(1)），仅返回 offset。
     */

     /* 写入外部库列表。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_extern_libs.m_size);
        for (size_t i = 0; ok && i < code_env->m_extern_libs.m_size; ++i)
        {
            woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(&code_env->m_extern_libs, i);

            uint32_t name_off = WOORT_STRREF_NULL;
            uint32_t name_len = (uint32_t)strlen(lib->m_name);
            ok = ok && _bin_strpool_insert(&strpool, lib->m_name, name_len, &name_off);
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);

            uint32_t path_off = WOORT_STRREF_NULL;
            uint32_t path_len = (uint32_t)strlen(lib->m_path);
            ok = ok && _bin_strpool_insert(&strpool, lib->m_path, path_len, &path_off);
            ok = ok && _bin_write_u32(&w, path_off);
            ok = ok && _bin_write_u32(&w, path_len);

            uint32_t script_off = WOORT_STRREF_NULL;
            uint32_t script_len = 0;
            if (lib->m_script_path != NULL)
            {
                script_len = (uint32_t)strlen(lib->m_script_path);
                ok = ok && _bin_strpool_insert(&strpool, lib->m_script_path, script_len, &script_off);
            }
            ok = ok && _bin_write_u32(&w, script_off);
            ok = ok && _bin_write_u32(&w, script_len);
        }
    }

    /* 写入常量数据。 */
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
                const woort_GCString* gcs = val->m_string;
                if (gcs != NULL)
                {
                    uint32_t off = WOORT_STRREF_NULL;
                    ok = ok && _bin_strpool_insert(&strpool,
                        gcs->m_content, gcs->m_length, &off);
                    ok = ok && _bin_write_u32(&w, off);
                    ok = ok && _bin_write_u32(&w, (uint32_t)gcs->m_length);
                }
                else
                {
                    ok = ok && _bin_write_u32(&w, WOORT_STRREF_NULL);
                    ok = ok && _bin_write_u32(&w, 0);
                }
                break;
            }

            case WOORT_CONST_TYPE_SCRIPT_FUNC:
            {
                uint32_t off = (val->m_script_function != NULL)
                    ? (uint32_t)(val->m_script_function - code_env->m_code_begin)
                    : WOORT_STRREF_NULL;
                ok = ok && _bin_write_u32(&w, off);
                break;
            }

            case WOORT_CONST_TYPE_EXTERN_FUNC:
            case WOORT_CONST_TYPE_EXTERN_CLOSURE:
            {
                const char* lib_name = NULL;
                const char* func_name = NULL;
                if (!_save_get_extern_names(rec, val, &lib_name, &func_name))
                {
                    WOORT_DEBUG("CodeEnv save: cannot resolve extern names for const %zu.", i);
                    ok = false;
                    break;
                }
                uint32_t lib_len = (uint32_t)strlen(lib_name);
                uint32_t func_len = (uint32_t)strlen(func_name);
                uint32_t lib_off = WOORT_STRREF_NULL, func_off = WOORT_STRREF_NULL;
                ok = ok && _bin_strpool_insert(&strpool, lib_name, lib_len, &lib_off);
                ok = ok && _bin_strpool_insert(&strpool, func_name, func_len, &func_off);
                ok = ok && _bin_write_u32(&w, lib_off);
                ok = ok && _bin_write_u32(&w, lib_len);
                ok = ok && _bin_write_u32(&w, func_off);
                ok = ok && _bin_write_u32(&w, func_len);
                break;
            }

            case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
            {
                const woort_GCClosure* closure = val->m_closure;
                uint32_t off = WOORT_STRREF_NULL;
                if (closure != NULL && closure->m_script_function != NULL)
                    off = (uint32_t)(closure->m_script_function - code_env->m_code_begin);
                ok = ok && _bin_write_u32(&w, off);
                break;
            }

            case WOORT_CONST_TYPE_BOX_INT:
            {
                woort_Int unboxed_int_val;
                if (val->m_dynamic.m_boxed & 0b0111)
                    unboxed_int_val = _woort_unbox_int64(
                        (woort_BoxedInt62)val->m_dynamic.m_boxed);
                else
                    unboxed_int_val = _woort_boxed_to_exvalue(val->m_dynamic.m_boxed)->m_int;

                ok = ok && _bin_write_i64(&w, unboxed_int_val);
                break;
            }
            case WOORT_CONST_TYPE_BOX_REAL:
            {
                woort_Real unboxed_real_val;
                if (val->m_dynamic.m_boxed & 0b0111)
                    unboxed_real_val = _woort_unbox_float64(
                        (woort_BoxedFloat63)val->m_dynamic.m_boxed);
                else
                    unboxed_real_val = _woort_boxed_to_exvalue(val->m_dynamic.m_boxed)->m_real;

                ok = ok && _bin_write_f64(&w, unboxed_real_val);
                break;
            }
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
                    const woort_Value* mv = &s->m_datas[mi];
                    woort_IRConstantIndex found_idx;
                    if (!_find_const_index_for_value(code_env, mv, const_count, &found_idx))
                    {
                        WOORT_DEBUG("CodeEnv save: struct const %zu member %u not found in const pool.", i, mi);
                        ok = false;
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

    /* 写入 extern 常量映射表。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_extern_constants.m_size);
        if (ok && code_env->m_extern_constants.m_size > 0)
        {
            struct _WriteExternConstCtx ctx = { &strpool, &w, &ok };
            (void)woort_hashmap_foreach(&code_env->m_extern_constants,
                &_write_extern_const_cb, &ctx);
        }
    }

    /* 写入函数边界表。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_function_boundaries.m_size);
        for (size_t i = 0; ok && i < code_env->m_function_boundaries.m_size; ++i)
        {
            const woort_FunctionBoundary* fb = (const woort_FunctionBoundary*)woort_vector_at(
                &code_env->m_function_boundaries, i);
            ok = ok && _bin_write_u32(&w, fb->m_offset_begin);
            ok = ok && _bin_write_u32(&w, fb->m_code_length);

            uint32_t name_off = WOORT_STRREF_NULL, name_len = 0;
            if (fb->m_name != NULL)
            {
                name_len = (uint32_t)strlen(fb->m_name);
                ok = ok && _bin_strpool_insert(&strpool, fb->m_name, name_len, &name_off);
            }
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);
        }
    }

    /* 写入源码映射表。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_pdb.m_source_map.m_entry_count);
        for (uint32_t i = 0; ok && i < code_env->m_pdb.m_source_map.m_entry_count; ++i)
        {
            const woort_SourceMap_Entry* entry = &code_env->m_pdb.m_source_map.m_entries[i];
            ok = ok && _bin_write_u32(&w, entry->m_bytecode_offset);

            uint32_t fp_off = WOORT_STRREF_NULL, fp_len = 0;
            if (entry->m_location.m_filepath != NULL)
            {
                fp_len = (uint32_t)strlen(entry->m_location.m_filepath);
                ok = ok && _bin_strpool_insert(&strpool,
                    entry->m_location.m_filepath, fp_len, &fp_off);
            }
            ok = ok && _bin_write_u32(&w, fp_off);
            ok = ok && _bin_write_u32(&w, fp_len);

            ok = ok && _bin_write_u32(&w, entry->m_location.m_begin_line);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_begin_column);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_end_line);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_end_column);
        }
    }

    /* 写入 trap 记录表。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_trap_records.m_size);
        if (ok && code_env->m_trap_records.m_size > 0)
        {
            struct _WriteTrapCtx ctx = { code_env->m_code_begin, &w, &ok };
            (void)woort_hashmap_foreach(&code_env->m_trap_records,
                &_write_trap_cb, &ctx);
        }
    }

    /* 写入局部变量调试信息。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_pdb.m_local_var_debug_info.m_size);
        for (size_t i = 0; ok && i < code_env->m_pdb.m_local_var_debug_info.m_size; ++i)
        {
            const woort_LocalVarDebugInfo* info = (const woort_LocalVarDebugInfo*)woort_vector_at(
                &code_env->m_pdb.m_local_var_debug_info, i);

            uint32_t name_off = WOORT_STRREF_NULL, name_len = 0;
            if (info->m_name != NULL)
            {
                name_len = (uint32_t)strlen(info->m_name);
                ok = ok && _bin_strpool_insert(&strpool, info->m_name, name_len, &name_off);
            }
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);
            ok = ok && _bin_write_u32(&w, info->m_function_offset);
            ok = ok && _bin_write_u32(&w, (uint32_t)info->m_stack_offset);
        }
    }

    /* 写入静态变量调试信息。 */
    if (ok)
    {
        ok = ok && _bin_write_u64(&w, (uint64_t)code_env->m_pdb.m_static_var_debug_info.m_size);
        for (size_t i = 0; ok && i < code_env->m_pdb.m_static_var_debug_info.m_size; ++i)
        {
            const woort_StaticVarDebugInfo* info = (const woort_StaticVarDebugInfo*)woort_vector_at(
                &code_env->m_pdb.m_static_var_debug_info, i);

            uint32_t name_off = WOORT_STRREF_NULL, name_len = 0;
            if (info->m_name != NULL)
            {
                name_len = (uint32_t)strlen(info->m_name);
                ok = ok && _bin_strpool_insert(&strpool, info->m_name, name_len, &name_off);
            }
            ok = ok && _bin_write_u32(&w, name_off);
            ok = ok && _bin_write_u32(&w, name_len);
            ok = ok && _bin_write_u32(&w, (uint32_t)info->m_static_idx);
        }
    }

    /*
     * 分离出最终 buffer。
     */
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
 * 第八部分：反序列化辅助：字符串追踪器
 *
 *  反序列化时，从二进制引用读出 (offset, length) 后，需要创建以空字符
 *  结尾的 C 字符串副本（用于 dylib 名、extern 常量名等需要 C 字符串的场合）。
 *  本追踪器收集所有副本，统一释放，并跟踪 OOM。
 * ================================================================ */

typedef struct _RestoreStrTracker
{
    woort_Vector m_copies;  /* char* 列表 */
    bool m_oom;
} _RestoreStrTracker;

static void _rst_strtracker_init(_RestoreStrTracker* t)
{
    woort_vector_init(&t->m_copies, sizeof(char*));
    t->m_oom = false;
}

static void _rst_strtracker_deinit(_RestoreStrTracker* t)
{
    char** copies = (char**)t->m_copies.m_data;
    for (size_t i = 0; i < t->m_copies.m_size; ++i)
        free(copies[i]);
    woort_vector_deinit(&t->m_copies);
}

/*
 * 从字符串池取数据，创建以空字符结尾的副本并登记到追踪器。
 * off/len 来自二进制引用。off == STRREF_NULL 返回 NULL（非 OOM）。
 * 真正 OOM 时置 t->m_oom=true 并返回 NULL。
 */
static /* OPTIONAL */ const char* _rst_make_cstr(
    _RestoreStrTracker* t,
    /* OPTIONAL */ const char* strpool_data,
    uint32_t off, uint32_t len)
{
    if (off == WOORT_STRREF_NULL || strpool_data == NULL)
        return NULL;

    const char* data = _bin_strpool_get(strpool_data, off, NULL);
    if (data == NULL)
        return NULL;

    char* copy = (char*)malloc(len + 1);
    if (copy == NULL)
    {
        t->m_oom = true;
        return NULL;
    }
    memcpy(copy, data, len);
    copy[len] = '\0';

    if (!woort_vector_push_back(&t->m_copies, 1, &copy))
    {
        free(copy);
        t->m_oom = true;
        return NULL;
    }
    return copy;
}

/*
 * 从读取器读一个字符串引用 (u32 off, u32 len)，并造 C 字符串副本。
 * - 读截断：返回 false，*oom=false。
 * - OOM（make_cstr 分配失败）：返回 false，*oom=true。
 * - 成功：返回 true；off==NULL 哨兵时 *out_cstr=NULL（合法的"无字符串"）。
 *
 * 用这个统一封装消除各调用点反复遗漏 OOM 检查的问题。
 */
static bool _restore_read_str_ref(
    _BinReader* r,
    _RestoreStrTracker* t,
    /* OPTIONAL */ const char* strpool_data,
    /* OPTIONAL */ const char** out_cstr,
    bool* oom)
{
    uint32_t off, len;
    if (!_bin_read_u32(r, &off) || !_bin_read_u32(r, &len))
    {
        *oom = false;
        return false;
    }
    const char* s = _rst_make_cstr(t, strpool_data, off, len);
    if (t->m_oom)
    {
        *oom = true;
        return false;
    }
    if (out_cstr != NULL)
        *out_cstr = s;
    *oom = false;
    return true;
}

/* ================================================================
 * 第九部分：反序列化主函数
 * ================================================================ */

WOORT_NODISCARD woort_CodeEnv_RestoreResult woort_CodeEnv_restore_binary(
    woort_VFile* f, woort_CodeEnv** out_code_env)
{
    assert(f != NULL);
    assert(out_code_env != NULL);

    *out_code_env = NULL;

    /* 获取文件总大小，用于边界校验。 */
    int64_t fsize_val = woort_vfile_size(f);
    if (fsize_val < 0)
        return WOORT_CODEENV_RESTORE_FAIL_READ;
    const size_t total_size = (size_t)fsize_val;

    if (!woort_vfile_seek(f, 0, SEEK_SET))
        return WOORT_CODEENV_RESTORE_FAIL_READ;

    /* 读取头部 32 字节到栈缓冲区。 */
    unsigned char header_buf[32];
    {
        const size_t bytes_read = woort_vfile_read(f, header_buf, sizeof(header_buf));
        if (bytes_read != sizeof(header_buf))
            return WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH;
    }

    /* 解析头部。 */
    uint32_t magic, version;
    uint64_t code_size, data_count, constant_count;
    {
        _BinReader rh;
        _bin_reader_init_memory(&rh, header_buf, sizeof(header_buf));

        if (!_bin_read_u32(&rh, &magic) || magic != WOORT_CODEENV_BINARY_MAGIC)
            return WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH;

        if (!_bin_read_u32(&rh, &version)
            || !_bin_read_u64(&rh, &code_size)
            || !_bin_read_u64(&rh, &data_count)
            || !_bin_read_u64(&rh, &constant_count))
        {
            WOORT_DEBUG("CodeEnv restore: truncated header.");
            return WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
        }
    }

    if (version != WOORT_CODEENV_BINARY_VERSION)
    {
        WOORT_DEBUG("CodeEnv restore: version mismatch ver=%u.", version);
        return WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH;
    }

    /* 校验 code_size 不超过文件剩余（显式避免下溢）。 */
    const size_t header_consumed = sizeof(header_buf);
    const size_t code_bytes = (size_t)code_size * sizeof(woort_Bytecode);
    if (code_bytes > total_size || code_bytes > total_size - header_consumed)
    {
        WOORT_DEBUG("CodeEnv restore: invalid code size %llu.", (unsigned long long)code_size);
        return WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE;
    }

    /* 从 VFile 增量读取字节码到临时缓冲区。 */
    woort_Bytecode* codes_from_bin = NULL;
    if (code_bytes > 0)
    {
        codes_from_bin = (woort_Bytecode*)malloc(code_bytes);
        if (codes_from_bin == NULL)
            return WOORT_CODEENV_RESTORE_FAIL_ALLOC;
        const size_t bytes_read = woort_vfile_read(f, codes_from_bin, code_bytes);
        if (bytes_read != code_bytes)
        {
            free(codes_from_bin);
            return WOORT_CODEENV_RESTORE_FAIL_READ;
        }
    }

    /* 创建 CodeEnv（内部会复制字节码，故临时缓冲区随后可释放）。 */
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

    woort_CodeEnv_RestoreResult result = WOORT_CODEENV_RESTORE_OK;

    _RestoreStrTracker str_tracker;
    _rst_strtracker_init(&str_tracker);

    woort_HashMap /* const char* -> woort_Dylib* */ lib_map;
    woort_hashmap_init(&lib_map,
        sizeof(const char*), sizeof(woort_Dylib*),
        woort_util_cstr_hash, woort_util_cstr_equal);

    void* strpool_buf = NULL;
    const char* strpool_data = NULL;

    _BinReader r;
    _bin_reader_init_vfile(&r, f, 0); /* m_size 在读入字符串池后设置 */

    woort_CodeEnv_lock(cenv);

    /* 增量读取字符串池。 */
    {
        uint64_t strpool_size;
        if (woort_vfile_read(f, &strpool_size, sizeof(strpool_size)) != sizeof(strpool_size))
        {
            result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
            goto _restore_fail;
        }

        if (strpool_size > 0)
        {
            strpool_buf = malloc((size_t)strpool_size);
            if (strpool_buf == NULL)
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            if (woort_vfile_read(f, strpool_buf, (size_t)strpool_size) != (size_t)strpool_size)
            {
                result = WOORT_CODEENV_RESTORE_FAIL_READ;
                goto _restore_fail;
            }
            strpool_data = (const char*)strpool_buf;
        }

        /* 设置剩余可读字节数。 */
        {
            const size_t read_so_far = header_consumed + code_bytes
                + sizeof(strpool_size) + (size_t)strpool_size;
            r.m_size = (total_size > read_so_far) ? total_size - read_so_far : 0;
        }
    }

    /*
     * 读取外部库列表，形成 HashMap 以便后续 extern 符号解析查询。
     * 硬失败策略：库加载失败/加入 CodeEnv 失败/map 插入失败/OOM 均中止恢复。
     */
    {
        uint64_t lib_count;
        if (!_bin_read_u64(&r, &lib_count))
        {
            result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
            goto _restore_fail;
        }
        for (uint64_t li = 0; li < lib_count; ++li)
        {
            bool oom = false;
            const char* lib_name = NULL;
            const char* lib_path = NULL;
            const char* lib_script_path = NULL;

            if (!_restore_read_str_ref(&r, &str_tracker, strpool_data, &lib_name, &oom)
                || !_restore_read_str_ref(&r, &str_tracker, strpool_data, &lib_path, &oom)
                || !_restore_read_str_ref(&r, &str_tracker, strpool_data, &lib_script_path, &oom))
            {
                result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                    : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }

            /* name 必须存在（库名不可缺）；path 可空，script_path 可空。 */
            if (lib_name == NULL)
            {
                WOORT_DEBUG("CodeEnv restore: extern lib %llu has no name.", (unsigned long long)li);
                result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                goto _restore_fail;
            }

            woort_Dylib* const lib = woort_dylib_load(
                lib_name, lib_path, lib_script_path, false);
            if (lib == NULL)
            {
                WOORT_DEBUG("CodeEnv restore: failed to load extern lib '%s'.", lib_name);
                result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                goto _restore_fail;
            }

            if (!woort_CodeEnv_add_extern_lib(cenv, lib))
            {
                woort_dylib_unload(lib, WOORT_DYLIB_UNREF); /* 释放本次 load 的引用 */
                WOORT_DEBUG("CodeEnv restore: failed to associate extern lib '%s' (OOM).", lib_name);
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }

            /*
             * 登记到 lib_map：name -> lib。
             * 用 get_or_emplace 处理潜在重复名（同名映射到最新解析结果）。
             */
            {
                void* slot = NULL;
                const woort_hashmap_Result mr =
                    woort_hashmap_get_or_emplace(&lib_map, &lib_name, &slot);
                if (mr == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail;
                }
                /* OK 或 ALREADY_EXIST：写入 lib 指针。 */
                if (slot != NULL)
                    *(woort_Dylib**)slot = lib;
            }

            /* 释放本次 woort_dylib_load 的引用；CodeEnv 已 keep 一份。 */
            woort_dylib_unload(lib, WOORT_DYLIB_UNREF);
        }
    }

    /*
     * 读取常量数据。
     */
    for (size_t i = 0; i < constant_count; ++i)
    {
        uint8_t type;
        if (!_bin_read_u8(&r, &type))
        {
            result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
            goto _restore_fail;
        }

        switch (type)
        {
        case WOORT_CONST_TYPE_NIL:
            if (!woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                WOORT_CONST_TYPE_NIL, NULL, NULL))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            break;

        case WOORT_CONST_TYPE_INT:
        {
            int64_t v;
            if (!_bin_read_i64(&r, &v))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            woort_CodeEnv_set_const_int(cenv, (woort_IRConstantIndex)i, v);
            break;
        }

        case WOORT_CONST_TYPE_REAL:
        {
            double v;
            if (!_bin_read_f64(&r, &v))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            woort_CodeEnv_set_const_real(cenv, (woort_IRConstantIndex)i, v);
            break;
        }

        case WOORT_CONST_TYPE_STRING:
        {
            uint32_t off, slen;
            if (!_bin_read_u32(&r, &off) || !_bin_read_u32(&r, &slen))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            /*
             * 字符串内容直接引用池内存（GCString 会复制其内容），无需 C 字符串副本。
             * 空字符串用空指针也以空缓冲传给 set_const_buffer。
             */
            const char* s = (off == WOORT_STRREF_NULL)
                ? NULL
                : _bin_strpool_get(strpool_data, off, NULL);
            woort_CodeEnv_set_const_buffer(cenv, (woort_IRConstantIndex)i,
                s != NULL ? (const void*)s : "", s != NULL ? (size_t)slen : 0);
            if (!woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                WOORT_CONST_TYPE_STRING, NULL, NULL))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            break;
        }

        case WOORT_CONST_TYPE_SCRIPT_FUNC:
        {
            uint32_t off;
            if (!_bin_read_u32(&r, &off))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            const woort_Bytecode* addr = (off != WOORT_STRREF_NULL && cenv->m_code_end > cenv->m_code_begin)
                ? cenv->m_code_begin + off : NULL;
            woort_CodeEnv_set_const_script_function(cenv, (woort_IRConstantIndex)i, addr);
            if (!woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                WOORT_CONST_TYPE_SCRIPT_FUNC, NULL, NULL))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            break;
        }

        case WOORT_CONST_TYPE_EXTERN_FUNC:
        case WOORT_CONST_TYPE_EXTERN_CLOSURE:
        {
            bool oom = false;
            const char* lib_name = NULL;
            const char* func_name = NULL;
            if (!_restore_read_str_ref(&r, &str_tracker, strpool_data, &lib_name, &oom)
                || !_restore_read_str_ref(&r, &str_tracker, strpool_data, &func_name, &oom))
            {
                result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                    : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            if (lib_name == NULL || func_name == NULL)
            {
                WOORT_DEBUG("CodeEnv restore: extern const %zu missing lib/func name.", i);
                result = WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET;
                goto _restore_fail;
            }

            woort_Dylib** lib_slot = NULL;
            if (!woort_hashmap_find(&lib_map, &lib_name, (void**)&lib_slot) || lib_slot == NULL)
            {
                WOORT_DEBUG("CodeEnv restore: cannot find lib '%s' for const %zu.", lib_name, i);
                result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                goto _restore_fail;
            }

            woort_NativeFunction nf = (woort_NativeFunction)woort_dylib_load_func(*lib_slot, func_name);
            if (nf == NULL)
            {
                WOORT_DEBUG("CodeEnv restore: cannot find func '%s' in lib '%s'.", func_name, lib_name);
                result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                goto _restore_fail;
            }

            if (type == WOORT_CONST_TYPE_EXTERN_FUNC)
                woort_CodeEnv_set_const_extern_function(cenv, (woort_IRConstantIndex)i, nf);
            else
                woort_CodeEnv_set_const_extern_closure(cenv, (woort_IRConstantIndex)i, nf);

            if (!woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                (woort_ConstRecordType)type, lib_name, func_name))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            break;
        }

        case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
        {
            uint32_t off;
            if (!_bin_read_u32(&r, &off))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            const woort_Bytecode* addr = (off != WOORT_STRREF_NULL && cenv->m_code_end > cenv->m_code_begin)
                ? cenv->m_code_begin + off : NULL;
            woort_CodeEnv_set_const_script_closure(cenv, (woort_IRConstantIndex)i, addr);
            if (!woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                WOORT_CONST_TYPE_SCRIPT_CLOSURE, NULL, NULL))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            break;
        }

        case WOORT_CONST_TYPE_BOX_INT:
        {
            int64_t v;
            if (!_bin_read_i64(&r, &v))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            woort_CodeEnv_set_const_box_int(cenv, (woort_IRConstantIndex)i, v);
            break;
        }

        case WOORT_CONST_TYPE_BOX_REAL:
        {
            double v;
            if (!_bin_read_f64(&r, &v))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            woort_CodeEnv_set_const_box_real(cenv, (woort_IRConstantIndex)i, v);
            break;
        }

        case WOORT_CONST_TYPE_BOX_BOOL:
        {
            uint8_t v;
            if (!_bin_read_u8(&r, &v))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            woort_CodeEnv_set_const_box_bool(cenv, (woort_IRConstantIndex)i, v != 0);
            break;
        }

        case WOORT_CONST_TYPE_STRUCT:
        {
            uint32_t member_count;
            if (!_bin_read_u32(&r, &member_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }

            woort_IRConstantIndex* members = NULL;
            if (member_count > 0)
            {
                members = (woort_IRConstantIndex*)malloc(
                    (size_t)member_count * sizeof(woort_IRConstantIndex));
                if (members == NULL)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail;
                }
                for (uint32_t mi = 0; mi < member_count; ++mi)
                {
                    uint32_t midx;
                    if (!_bin_read_u32(&r, &midx))
                    {
                        free(members);
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail;
                    }
                    members[mi] = (woort_IRConstantIndex)midx;
                }
            }

            woort_CodeEnv_set_const_struct(cenv, (woort_IRConstantIndex)i,
                members, member_count);
            if (!woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                WOORT_CONST_TYPE_STRUCT, NULL, NULL))
            {
                free(members);
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
            free(members);
            break;
        }

        default:
            WOORT_DEBUG("CodeEnv restore: unknown const type %d at %zu.", (int)type, i);
            result = WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE;
            goto _restore_fail;
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
            goto _restore_fail;
        }
        for (uint64_t ei = 0; ei < extern_count; ++ei)
        {
            bool oom = false;
            const char* name = NULL;
            if (!_restore_read_str_ref(&r, &str_tracker, strpool_data, &name, &oom))
            {
                result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                    : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            uint32_t cidx;
            if (!_bin_read_u32(&r, &cidx))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            if (name == NULL)
            {
                result = WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET;
                goto _restore_fail;
            }
            if (!woort_CodeEnv_register_extern_constant(cenv, name, (woort_IRConstantIndex)cidx))
            {
                /* 注册失败：重复名（不应出现在序列化数据中）或 OOM，均按失败处理。 */
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
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
            goto _restore_fail;
        }
        for (uint64_t fi = 0; fi < fb_count; ++fi)
        {
            woort_FunctionBoundary fb;
            bool oom = false;
            if (!_bin_read_u32(&r, &fb.m_offset_begin)
                || !_bin_read_u32(&r, &fb.m_code_length)
                || !_restore_read_str_ref(&r, &str_tracker, strpool_data, &fb.m_name, &oom))
            {
                result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                    : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            /* intern 到 CodeEnv 字符串池做生命周期管理（fb.m_name 可能是 NULL）。 */
            if (fb.m_name != NULL)
            {
                fb.m_name = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, fb.m_name);
                if (fb.m_name == NULL)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail;
                }
            }
            if (!woort_vector_push_back(&cenv->m_function_boundaries, 1, &fb))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
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
            goto _restore_fail;
        }
        if (sm_count > 0)
        {
            woort_SourceMap_Entry* entries = (woort_SourceMap_Entry*)malloc(
                sizeof(woort_SourceMap_Entry) * (size_t)sm_count);
            if (entries == NULL)
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }

            for (uint64_t si = 0; si < sm_count; ++si)
            {
                bool oom = false;
                const char* fp = NULL;
                if (!_bin_read_u32(&r, &entries[si].m_bytecode_offset)
                    || !_restore_read_str_ref(&r, &str_tracker, strpool_data, &fp, &oom)
                    || !_bin_read_u32(&r, &entries[si].m_location.m_begin_line)
                    || !_bin_read_u32(&r, &entries[si].m_location.m_begin_column)
                    || !_bin_read_u32(&r, &entries[si].m_location.m_end_line)
                    || !_bin_read_u32(&r, &entries[si].m_location.m_end_column))
                {
                    result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                        : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    free(entries);
                    goto _restore_fail;
                }
                if (fp != NULL)
                {
                    fp = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, fp);
                    if (fp == NULL)
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                        free(entries);
                        goto _restore_fail;
                    }
                }
                entries[si].m_location.m_filepath = fp;
            }

            free(cenv->m_pdb.m_source_map.m_entries);
            cenv->m_pdb.m_source_map.m_entries = entries;
            cenv->m_pdb.m_source_map.m_entry_count = (uint32_t)sm_count;
        }
    }

    /*
     * 读取 trap 记录表。
     * 恢复时仅记录，不激活（字节码可能已变；原值供后续按需重设）。
     */
    {
        uint64_t trap_count;
        if (!_bin_read_u64(&r, &trap_count))
        {
            result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
            goto _restore_fail;
        }
        for (uint64_t ti = 0; ti < trap_count; ++ti)
        {
            uint32_t off, orig;
            if (!_bin_read_u32(&r, &off) || !_bin_read_u32(&r, &orig))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            if (cenv->m_code_end > cenv->m_code_begin
                && (size_t)off < (size_t)(cenv->m_code_end - cenv->m_code_begin))
            {
                if (woort_hashmap_insert(
                    &cenv->m_trap_records,
                    &cenv->m_code_begin[off],
                    &orig) != WOORT_HASHMAP_RESULT_OK)
                {
                    /* 重复 offset（不应出现）或 OOM。 */
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail;
                }
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
            goto _restore_fail;
        }
        for (uint64_t li = 0; li < lv_count; ++li)
        {
            woort_LocalVarDebugInfo info;
            bool oom = false;
            if (!_restore_read_str_ref(&r, &str_tracker, strpool_data, &info.m_name, &oom)
                || !_bin_read_u32(&r, &info.m_function_offset)
                || !_bin_read_u32(&r, (uint32_t*)&info.m_stack_offset))
            {
                result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                    : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            if (info.m_name != NULL)
            {
                info.m_name = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, info.m_name);
                if (info.m_name == NULL)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail;
                }
            }
            if (!woort_vector_push_back(&cenv->m_pdb.m_local_var_debug_info, 1, &info))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
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
            goto _restore_fail;
        }
        for (uint64_t si = 0; si < sv_count; ++si)
        {
            woort_StaticVarDebugInfo info;
            bool oom = false;
            if (!_restore_read_str_ref(&r, &str_tracker, strpool_data, &info.m_name, &oom)
                || !_bin_read_u32(&r, (uint32_t*)&info.m_static_idx))
            {
                result = oom ? WOORT_CODEENV_RESTORE_FAIL_ALLOC
                    : WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail;
            }
            if (info.m_name != NULL)
            {
                info.m_name = woort_StringPool_intern(&cenv->m_pdb.m_srcloc_string_pool, info.m_name);
                if (info.m_name == NULL)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail;
                }
            }
            if (!woort_vector_push_back(&cenv->m_pdb.m_static_var_debug_info, 1, &info))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                goto _restore_fail;
            }
        }
    }

    /* 成功。 */
    _rst_strtracker_deinit(&str_tracker);
    free(strpool_buf);
    woort_hashmap_deinit(&lib_map);
    woort_CodeEnv_unlock(cenv);

    *out_code_env = cenv;
    return WOORT_CODEENV_RESTORE_OK;

_restore_fail:
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
