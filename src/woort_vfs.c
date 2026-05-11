#include "woort_vfs.h"

#include "woort_diagnosis.h"
#include "woort_util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 *  Global VFS State
 * ================================================================ */

static woort_Vector     g_vfs_entries;   /* vector of woort_VFSEntry*  */
static woort_RWSpinlock g_vfs_lock;      /* read-write spinlock        */
static bool             g_vfs_inited = false;

/* ================================================================
 *  Lifecycle
 * ================================================================ */

/*
Ensure the VFS is initialized.  Safe to call multiple times;
idempotent after the first successful call.
*/
static void _woort_vfs_ensure_inited(void)
{
    if (!g_vfs_inited)
    {
        woort_vector_init(&g_vfs_entries, sizeof(woort_VFSEntry*));
        woort_rwspinlock_init(&g_vfs_lock);
        g_vfs_inited = true;
    }
}

void _woort_vfs_bootup(void)
{
    _woort_vfs_ensure_inited();
}

void _woort_vfs_shutdown(void)
{
    if (!g_vfs_inited)
        return;

    woort_rwspinlock_write_lock(&g_vfs_lock);

    for (size_t i = 0; i < g_vfs_entries.m_size; ++i)
    {
        woort_VFSEntry** pentry = (woort_VFSEntry**)woort_vector_at(&g_vfs_entries, i);
        if (pentry != NULL && *pentry != NULL)
        {
            free((*pentry)->m_filepath);
            free((*pentry)->m_data);
            free(*pentry);
        }
    }

    woort_rwspinlock_write_unlock(&g_vfs_lock);

    woort_vector_deinit(&g_vfs_entries);
    woort_rwspinlock_deinit(&g_vfs_lock);
    g_vfs_inited = false;
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
    for (size_t i = 0; i < g_vfs_entries.m_size; ++i)
    {
        woort_VFSEntry** pentry = (woort_VFSEntry**)woort_vector_at(&g_vfs_entries, i);
        if (pentry != NULL && *pentry != NULL)
        {
            if (strcmp((*pentry)->m_filepath, filepath) == 0)
                return *pentry;
        }
    }
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
    if (filepath == NULL)
        return false;

    _woort_vfs_ensure_inited();

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

    if (!woort_vector_push_back(&g_vfs_entries, 1, &entry))
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
    if (filepath == NULL)
        return false;

    _woort_vfs_ensure_inited();

    woort_rwspinlock_write_lock(&g_vfs_lock);

    for (size_t i = 0; i < g_vfs_entries.m_size; ++i)
    {
        woort_VFSEntry** pentry = (woort_VFSEntry**)woort_vector_at(&g_vfs_entries, i);
        if (pentry != NULL && *pentry != NULL)
        {
            if (strcmp((*pentry)->m_filepath, filepath) == 0)
            {
                if (!(*pentry)->m_enable_modify)
                {
                    woort_rwspinlock_write_unlock(&g_vfs_lock);
                    return false;
                }

                free((*pentry)->m_filepath);
                free((*pentry)->m_data);
                free(*pentry);

                woort_vector_erase_at(&g_vfs_entries, i);

                woort_rwspinlock_write_unlock(&g_vfs_lock);
                return true;
            }
        }
    }

    woort_rwspinlock_write_unlock(&g_vfs_lock);
    return false;
}

WOORT_NODISCARD bool woort_vfs_is_virtual_uri(const char* uri)
{
    if (uri == NULL)
        return false;

    return strncmp(uri, WOORT_VFS_SCHEME, WOORT_VFS_SCHEME_LEN) == 0;
}

WOORT_NODISCARD bool woort_vfs_read(
    const char* filepath,
    /* OPTIONAL */ char** out_data,
    /* OPTIONAL */ size_t* out_length)
{
    if (filepath == NULL)
        return false;

    _woort_vfs_ensure_inited();

    const char* lookup_path = filepath;
    if (woort_vfs_is_virtual_uri(filepath))
        lookup_path = filepath + WOORT_VFS_SCHEME_LEN;

    woort_rwspinlock_read_lock(&g_vfs_lock);

    woort_VFSEntry* entry = _woort_vfs_find_entry(lookup_path);
    if (entry == NULL)
    {
        woort_rwspinlock_read_unlock(&g_vfs_lock);
        return false;
    }

    if (out_data != NULL)
    {
        if (entry->m_data_length > 0)
        {
            *out_data = (char*)malloc(entry->m_data_length);
            if (*out_data == NULL)
            {
                woort_rwspinlock_read_unlock(&g_vfs_lock);
                return false;
            }
            memcpy(*out_data, entry->m_data, entry->m_data_length);
        }
        else
        {
            *out_data = NULL;
        }
    }

    if (out_length != NULL)
        *out_length = entry->m_data_length;

    woort_rwspinlock_read_unlock(&g_vfs_lock);
    return true;
}

WOORT_NODISCARD bool woort_vfs_exists(const char* filepath)
{
    if (filepath == NULL)
        return false;

    _woort_vfs_ensure_inited();

    const char* lookup_path = filepath;
    if (woort_vfs_is_virtual_uri(filepath))
        lookup_path = filepath + WOORT_VFS_SCHEME_LEN;

    woort_rwspinlock_read_lock(&g_vfs_lock);

    woort_VFSEntry* entry = _woort_vfs_find_entry(lookup_path);

    woort_rwspinlock_read_unlock(&g_vfs_lock);

    return entry != NULL;
}

WOORT_NODISCARD size_t woort_vfs_get_all_paths(
    /* OPTIONAL */ char*** out_paths)
{
    _woort_vfs_ensure_inited();

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

    for (size_t i = 0; i < count; ++i)
    {
        woort_VFSEntry** pentry = (woort_VFSEntry**)woort_vector_at(&g_vfs_entries, i);
        if (pentry != NULL && *pentry != NULL && (*pentry)->m_filepath != NULL)
        {
            paths[i] = (char*)malloc(strlen((*pentry)->m_filepath) + 1);
            if (paths[i] != NULL)
                strcpy(paths[i], (*pentry)->m_filepath);
        }
        /* else paths[i] stays NULL */
    }

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
