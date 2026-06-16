#include "woort_vfs.h"

#include "woort_diagnosis.h"
#include "woort_hashmap.h"
#include "woort_util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* ================================================================
 *  Global VFS State
 * ================================================================ */

static woort_HashMap    g_vfs_entries;   /* hashmap of filepath -> woort_VFSEntry* */
static woort_RWSpinlock g_vfs_lock;      /* read-write spinlock        */

/* ================================================================
 *  Lifecycle
 * ================================================================ */

void _woort_vfs_bootup(void)
{
    woort_hashmap_init(
        &g_vfs_entries,
        sizeof(const char*),
        sizeof(woort_VFSEntry*),
        woort_util_cstr_hash,
        woort_util_cstr_equal);
    woort_rwspinlock_init(&g_vfs_lock);
}

static bool _woort_vfs_shutdown_foreach_callback(
    const void* key,
    void* value,
    /* OPTIONAL */ void* user_data)
{
    (void)key;
    (void)user_data;
    woort_VFSEntry* entry = *(woort_VFSEntry**)value;
    free(entry->m_filepath);
    free(entry->m_data);
    free(entry);
    return true;
}

void _woort_vfs_shutdown(void)
{
    woort_rwspinlock_write_lock(&g_vfs_lock);
    (void)woort_hashmap_foreach(&g_vfs_entries, _woort_vfs_shutdown_foreach_callback, NULL);
    woort_rwspinlock_write_unlock(&g_vfs_lock);

    woort_hashmap_deinit(&g_vfs_entries);
    woort_rwspinlock_deinit(&g_vfs_lock);
}

/* ================================================================
 *  Internal helpers
 * ================================================================ */

 /*
 Find a VFS entry by filepath.
 Must be called with at least a read lock held.
 Returns the entry pointer, or NULL if not found.
 */
static /* OPTIONAL */ woort_VFSEntry* _woort_vfs_find_entry(
    const char* filepath)
{
    void* value_addr;
    if (woort_hashmap_find(&g_vfs_entries, &filepath, &value_addr))
        return *(woort_VFSEntry**)value_addr;
    return NULL;
}

/* ================================================================
 *  Public API
 * ================================================================ */

WOORT_NODISCARD bool woort_vfs_create(
    const char* filepath,
    const void* data,
    size_t length,
    bool enable_modify)
{
    assert(filepath != NULL);

    woort_rwspinlock_write_lock(&g_vfs_lock);

    woort_VFSEntry* existing = _woort_vfs_find_entry(filepath);
    if (existing != NULL)
    {
        if (!existing->m_enable_modify)
        {
            woort_rwspinlock_write_unlock(&g_vfs_lock);
            return false;
        }

        /* Update existing entry */
        existing->m_enable_modify = enable_modify;
        free(existing->m_data);

        if (length > 0)
        {
            existing->m_data = (char*)malloc(length);
            if (existing->m_data == NULL)
            {
                existing->m_data_length = 0;
                woort_rwspinlock_write_unlock(&g_vfs_lock);
                return false;
            }
            memcpy(existing->m_data, data, length);
        }
        else
        {
            existing->m_data = NULL;
        }
        existing->m_data_length = length;

        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return true;
    }

    /* Create new entry */
    woort_VFSEntry* entry = (woort_VFSEntry*)calloc(1, sizeof(woort_VFSEntry));
    if (entry == NULL)
    {
        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return false;
    }

    entry->m_filepath = (char*)malloc(strlen(filepath) + 1);
    if (entry->m_filepath == NULL)
    {
        free(entry);
        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return false;
    }
    strcpy(entry->m_filepath, filepath);

    if (length > 0)
    {
        entry->m_data = (char*)malloc(length);
        if (entry->m_data == NULL)
        {
            free(entry->m_filepath);
            free(entry);
            woort_rwspinlock_write_unlock(&g_vfs_lock);
            return false;
        }
        if (data != NULL)
            memcpy(entry->m_data, data, length);
    }
    else
    {
        entry->m_data = NULL;
    }

    entry->m_data_length = length;
    entry->m_enable_modify = enable_modify;

    woort_hashmap_Result result = woort_hashmap_insert(
        &g_vfs_entries, &entry->m_filepath, &entry);
    if (result != WOORT_HASHMAP_RESULT_OK)
    {
        free(entry->m_data);
        free(entry->m_filepath);
        free(entry);
        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return false;
    }

    woort_rwspinlock_write_unlock(&g_vfs_lock);
    return true;
}

WOORT_NODISCARD bool woort_vfs_remove(const char* filepath)
{
    assert(filepath != NULL);

    woort_rwspinlock_write_lock(&g_vfs_lock);

    woort_VFSEntry* entry = _woort_vfs_find_entry(filepath);
    if (entry == NULL)
    {
        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return false;
    }

    if (!entry->m_enable_modify)
    {
        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return false;
    }

    if (!woort_hashmap_remove(&g_vfs_entries, &filepath))
    {
        woort_rwspinlock_write_unlock(&g_vfs_lock);
        return false;
    }

    free(entry->m_filepath);
    free(entry->m_data);
    free(entry);

    woort_rwspinlock_write_unlock(&g_vfs_lock);
    return true;
}

WOORT_NODISCARD bool woort_vfs_is_virtual_uri(const char* uri)
{
    if (uri == NULL)
        return false;

    return strncmp(uri, WOORT_VFS_SCHEME, WOORT_VFS_SCHEME_LEN) == 0;
}

WOORT_NODISCARD bool woort_vfs_read(
    const char* filepath,
    /* OPTIONAL */ void* out_data,
    size_t* inout_len)
{
    assert(filepath != NULL);
    assert(inout_len != NULL);

    const char* lookup_path = filepath;
    if (woort_vfs_is_virtual_uri(filepath))
        lookup_path = filepath + WOORT_VFS_SCHEME_LEN;

    woort_rwspinlock_read_lock(&g_vfs_lock);

    woort_VFSEntry* const entry = _woort_vfs_find_entry(lookup_path);
    if (entry == NULL)
    {
        woort_rwspinlock_read_unlock(&g_vfs_lock);
        return false;
    }

    size_t content_length = entry->m_data_length;

    if (out_data != NULL)
    {
        size_t capacity = *inout_len;
        size_t copy_len = (content_length < capacity) ? content_length : capacity;
        if (copy_len > 0)
            memcpy(out_data, entry->m_data, copy_len);
    }

    *inout_len = content_length;

    woort_rwspinlock_read_unlock(&g_vfs_lock);
    return true;
}

WOORT_NODISCARD bool woort_vfs_exists(const char* filepath)
{
    assert(filepath != NULL);

    const char* lookup_path = filepath;
    if (woort_vfs_is_virtual_uri(filepath))
        lookup_path = filepath + WOORT_VFS_SCHEME_LEN;

    woort_rwspinlock_read_lock(&g_vfs_lock);

    woort_VFSEntry* entry = _woort_vfs_find_entry(lookup_path);

    woort_rwspinlock_read_unlock(&g_vfs_lock);

    return entry != NULL;
}

static bool _woort_vfs_get_all_paths_foreach(
    const void* key,
    void* value,
    /* OPTIONAL */ void* user_data)
{
    (void)value;
    char** paths = (char**)user_data;
    const char* filepath = *(const char**)key;
    size_t index = 0;
    while (paths[index] != NULL)
        index++;
    paths[index] = (char*)malloc(strlen(filepath) + 1);
    if (paths[index] != NULL)
        strcpy(paths[index], filepath);
    return true;
}

WOORT_NODISCARD size_t woort_vfs_get_all_paths(
    /* OPTIONAL */ char*** out_paths)
{
    woort_rwspinlock_read_lock(&g_vfs_lock);

    size_t count = g_vfs_entries.m_size;

    if (out_paths == NULL)
    {
        woort_rwspinlock_read_unlock(&g_vfs_lock);
        return count;
    }

    char** paths = (char**)calloc(count, sizeof(char*));
    if (paths == NULL)
    {
        woort_rwspinlock_read_unlock(&g_vfs_lock);
        return 0;
    }

    (void)woort_hashmap_foreach(&g_vfs_entries, _woort_vfs_get_all_paths_foreach, paths);

    woort_rwspinlock_read_unlock(&g_vfs_lock);

    *out_paths = paths;
    return count;
}

WOORT_NODISCARD bool woort_fs_is_file_readable(const char* path)
{
    if (path == NULL || path[0] == '\0')
        return false;

    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return false;

    fclose(f);
    return true;
}

/* ================================================================
 *  Path resolution
 * ================================================================ */

 /*
 Walk through a list of search directories and try to resolve filepath.
 For each directory, try both virtual and real file lookups.
 */
static bool _woort_vfs_try_search_dir(
    const char* filepath,
    const char* search_dir,
    /* OPTIONAL */ char** out_resolved_path)
{
    if (search_dir == NULL || search_dir[0] == '\0')
        return false;

    /* Build: search_dir + "/" + filepath */
    size_t dir_len = strlen(search_dir);
    size_t fn_len = strlen(filepath);
    size_t total = dir_len + 1 + fn_len + 1;
    char* candidate = (char*)malloc(total);
    if (candidate == NULL)
        return false;

    memcpy(candidate, search_dir, dir_len);
    candidate[dir_len] = '/';
    memcpy(candidate + dir_len + 1, filepath, fn_len + 1);

    if (woort_vfs_is_virtual_uri(candidate))
    {
        if (woort_vfs_exists(candidate))
        {
            if (out_resolved_path != NULL)
                *out_resolved_path = candidate;
            else
                free(candidate);
            return true;
        }
    }
    else
    {
        if (woort_fs_is_file_readable(candidate))
        {
            if (out_resolved_path != NULL)
                *out_resolved_path = candidate;
            else
                free(candidate);
            return true;
        }
    }

    free(candidate);
    return false;
}

WOORT_NODISCARD bool woort_vfs_resolve_path(
    const char* filepath,
    /* OPTIONAL */ const char* const* search_dirs,
    size_t search_dir_count,
    /* OPTIONAL */ char** out_resolved_path)
{
    assert(filepath != NULL);

    /* 0) If the filepath itself is a virtual URI, return it as-is */
    if (woort_vfs_is_virtual_uri(filepath))
    {
        char* copy = (char*)malloc(strlen(filepath) + 1);
        if (copy == NULL)
            return false;
        strcpy(copy, filepath);
        woort_normalize_path(copy);

        if (out_resolved_path != NULL)
            *out_resolved_path = copy;
        else
            free(copy);
        return true;
    }

    /* 1) Search through caller-supplied search directories (import chain) */
    if (search_dirs != NULL && search_dir_count > 0)
    {
        for (size_t i = 0; i < search_dir_count; ++i)
        {
            if (_woort_vfs_try_search_dir(filepath, search_dirs[i], out_resolved_path))
            {
                if (out_resolved_path != NULL)
                    woort_normalize_path(*out_resolved_path);
                return true;
            }
        }
    }

    /* 2) Try: work_path + "/" + filepath */
    {
        size_t work_need = woort_work_path(NULL, 0) + 1;
        if (work_need > 1)
        {
            char* work = (char*)malloc(work_need);
            if (work != NULL)
            {
                woort_work_path(work, work_need);
                bool found = _woort_vfs_try_search_dir(filepath, work, out_resolved_path);
                free(work);
                if (found)
                {
                    if (out_resolved_path != NULL)
                        woort_normalize_path(*out_resolved_path);
                    return true;
                }
            }
        }
    }

    /* 3) Try: exe_path + "/" + filepath */
    {
        size_t exe_need = woort_exe_path(NULL, 0) + 1;
        if (exe_need > 1)
        {
            char* exe = (char*)malloc(exe_need);
            if (exe != NULL)
            {
                woort_exe_path(exe, exe_need);
                bool found = _woort_vfs_try_search_dir(filepath, exe, out_resolved_path);
                free(exe);
                if (found)
                {
                    if (out_resolved_path != NULL)
                        woort_normalize_path(*out_resolved_path);
                    return true;
                }
            }
        }
    }

    /* 4) Try: filepath as-is */
    if (woort_fs_is_file_readable(filepath))
    {
        char* copy = (char*)malloc(strlen(filepath) + 1);
        if (copy != NULL)
        {
            strcpy(copy, filepath);
            woort_normalize_path(copy);

            if (out_resolved_path != NULL)
                *out_resolved_path = copy;
            else
                free(copy);
        }
        return true;
    }

    /* 5) Try: WOORT_VFS_SCHEME + filepath (VFS lookup without scheme prefix) */
    {
        size_t scheme_len = WOORT_VFS_SCHEME_LEN;
        size_t fn_len = strlen(filepath);
        char* vfs_path = (char*)malloc(scheme_len + fn_len + 1);
        if (vfs_path != NULL)
        {
            memcpy(vfs_path, WOORT_VFS_SCHEME, scheme_len);
            memcpy(vfs_path + scheme_len, filepath, fn_len + 1);

            if (woort_vfs_exists(vfs_path))
            {
                woort_normalize_path(vfs_path);

                if (out_resolved_path != NULL)
                    *out_resolved_path = vfs_path;
                else
                    free(vfs_path);
                return true;
            }

            free(vfs_path);
        }
    }

    return false;
}

/* ================================================================
 *  Streaming virtual file handle
 * ================================================================ */

 /*
 Platform-specific 64-bit seek/tell wrappers so the vfile API
 can handle large files portably.
 */
#if defined(_MSC_VER)
#   define _WOORT_VFILE_FSEEK _fseeki64
#   define _WOORT_VFILE_FTELL _ftelli64
#else
#   define _WOORT_VFILE_FSEEK fseeko
#   define _WOORT_VFILE_FTELL ftello
#endif

WOORT_NODISCARD bool woort_vfile_open_reader(
    /* OPTIONAL */ const void* buf,
    size_t buflen,
    woort_VFile** out_file)
{
    assert(out_file != NULL);

    woort_VFile* file = (woort_VFile*)calloc(1, sizeof(woort_VFile));
    if (file == NULL)
        return false;

    file->m_type = WOORT_VFILE_TYPE_READER;
    file->m_reader.m_data = buf;
    file->m_reader.m_size = buflen;
    file->m_reader.m_pos = 0;

    *out_file = file;
    return true;
}

WOORT_NODISCARD bool woort_vfile_open(
    const char* filepath,
    woort_VFile** out_file)
{
    assert(filepath != NULL && out_file != NULL);

    woort_VFile* file = (woort_VFile*)calloc(1, sizeof(woort_VFile));
    if (file == NULL)
        return false;

    if (woort_vfs_is_virtual_uri(filepath))
    {
        file->m_type = WOORT_VFILE_TYPE_VIRTUAL;

        char* data = NULL;
        size_t length = 0;
        if (!woort_vfs_read(filepath, NULL, &length))
        {
            free(file);
            return false;
        }

        if (length > 0)
        {
            data = (char*)malloc(length);
            if (data == NULL)
            {
                free(file);
                return false;
            }

            if (!woort_vfs_read(filepath, data, &length))
            {
                free(data);
                free(file);
                return false;
            }
        }

        file->m_virtual.m_data = data;
        file->m_virtual.m_size = length;
        file->m_virtual.m_pos = 0;
    }
    else
    {
        file->m_type = WOORT_VFILE_TYPE_REAL;
        file->m_real_file = fopen(filepath, "rb");
        if (file->m_real_file == NULL)
        {
            free(file);
            return false;
        }
    }

    *out_file = file;
    return true;
}

WOORT_NODISCARD size_t woort_vfile_read(
    woort_VFile* file,
    /* OPTIONAL */ void* buffer,
    size_t size)
{
    size_t total_read = 0;
    assert(file != NULL);

    if (file->m_type == WOORT_VFILE_TYPE_VIRTUAL)
    {
        size_t available = file->m_virtual.m_size - file->m_virtual.m_pos;
        size_t to_read = (size < available) ? size : available;

        if (buffer != NULL && to_read > 0)
            memcpy(buffer, file->m_virtual.m_data + file->m_virtual.m_pos, to_read);

        file->m_virtual.m_pos += to_read;
        total_read = to_read;
    }
    else if (file->m_type == WOORT_VFILE_TYPE_READER)
    {
        size_t available = file->m_reader.m_size - file->m_reader.m_pos;
        size_t to_read = (size < available) ? size : available;

        if (buffer != NULL && to_read > 0)
            memcpy(buffer, (const char*)file->m_reader.m_data + file->m_reader.m_pos, to_read);

        file->m_reader.m_pos += to_read;
        total_read = to_read;
    }
    else
    {
        if (buffer != NULL && size > 0)
        {
            total_read = fread(buffer, 1, size, file->m_real_file);
        }
    }
    return total_read;
}

WOORT_NODISCARD bool woort_vfile_seek(
    woort_VFile* file,
    int64_t offset,
    int whence)
{
    assert(file != NULL);

    if (file->m_type == WOORT_VFILE_TYPE_VIRTUAL)
    {
        int64_t new_pos;

        switch (whence)
        {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int64_t)file->m_virtual.m_pos + offset;
            break;
        case SEEK_END:
            new_pos = (int64_t)file->m_virtual.m_size + offset;
            break;
        default:
            return false;
        }

        if (new_pos < 0)
            return false;

        file->m_virtual.m_pos = (new_pos > (int64_t)file->m_virtual.m_size)
            ? file->m_virtual.m_size
            : (size_t)new_pos;

        return true;
    }

    if (file->m_type == WOORT_VFILE_TYPE_READER)
    {
        int64_t new_pos;

        switch (whence)
        {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int64_t)file->m_reader.m_pos + offset;
            break;
        case SEEK_END:
            new_pos = (int64_t)file->m_reader.m_size + offset;
            break;
        default:
            return false;
        }

        if (new_pos < 0)
            return false;

        file->m_reader.m_pos = (new_pos > (int64_t)file->m_reader.m_size)
            ? file->m_reader.m_size
            : (size_t)new_pos;

        return true;
    }

    return _WOORT_VFILE_FSEEK(file->m_real_file, offset, whence) == 0;
}

WOORT_NODISCARD int64_t woort_vfile_tell(woort_VFile* file)
{
    assert(file != NULL);

    if (file->m_type == WOORT_VFILE_TYPE_VIRTUAL)
        return (int64_t)file->m_virtual.m_pos;

    if (file->m_type == WOORT_VFILE_TYPE_READER)
        return (int64_t)file->m_reader.m_pos;

    return (int64_t)_WOORT_VFILE_FTELL(file->m_real_file);
}

WOORT_NODISCARD int64_t woort_vfile_size(woort_VFile* file)
{
    assert(file != NULL);

    if (file->m_type == WOORT_VFILE_TYPE_VIRTUAL)
        return (int64_t)file->m_virtual.m_size;

    if (file->m_type == WOORT_VFILE_TYPE_READER)
        return (int64_t)file->m_reader.m_size;

    {
        int64_t saved = _WOORT_VFILE_FTELL(file->m_real_file);
        if (saved < 0)
            return -1;

        if (_WOORT_VFILE_FSEEK(file->m_real_file, 0, SEEK_END) != 0)
            return -1;

        int64_t size = _WOORT_VFILE_FTELL(file->m_real_file);

        _WOORT_VFILE_FSEEK(file->m_real_file, saved, SEEK_SET);

        return size;
    }
}

void woort_vfile_close(woort_VFile* file)
{
    assert(file != NULL);

    if (file->m_type == WOORT_VFILE_TYPE_VIRTUAL)
    {
        free(file->m_virtual.m_data);
    }
    else if (file->m_type == WOORT_VFILE_TYPE_REAL)
    {
        if (file->m_real_file != NULL)
            fclose(file->m_real_file);
    }
    /* WOORT_VFILE_TYPE_READER: external buffer, nothing to free */

    free(file);
}

#undef _WOORT_VFILE_FSEEK
#undef _WOORT_VFILE_FTELL
