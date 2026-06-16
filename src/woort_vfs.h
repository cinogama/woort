#pragma once

/*
woort_vfs.h - Virtual File System internal header

Manages a global registry of virtual files that can be embedded
at runtime.  The public API is declared in woort.h; the bootup /
shutdown hooks are called by woort.c.
*/

#include "woort.h"
#include "woort_spin.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define WOORT_VFS_SCHEME      "woovf://"
#define WOORT_VFS_SCHEME_LEN  8

typedef struct woort_VFSEntry
{
    /* OPTIONAL */ char*    m_filepath;      /* owned copy of the key     */
    bool                    m_enable_modify; /* can be overwritten        */
    size_t                  m_data_length;   /* length of m_data          */
    /* OPTIONAL */ char*    m_data;          /* owned copy of file content */
} woort_VFSEntry;

typedef enum woort_VFileType
{
    WOORT_VFILE_TYPE_REAL    = 0,
    WOORT_VFILE_TYPE_VIRTUAL = 1,
    WOORT_VFILE_TYPE_READER  = 2   /* external buffer, not owned */
} woort_VFileType;

struct woort_VFile
{
    woort_VFileType m_type;
    union
    {
        /* OPTIONAL */ FILE* m_real_file;
        struct
        {
            /* OPTIONAL */ char* m_data;      /* owned copy of VFS data */
            size_t      m_size;               /* total size in bytes     */
            size_t      m_pos;                /* current read position   */
        } m_virtual;
        struct
        {
            /* OPTIONAL */ const void* m_data; /* external buffer, not owned */
            size_t                  m_size;   /* total size in bytes         */
            size_t                  m_pos;    /* current read position       */
        } m_reader;
    };
};

void _woort_vfs_bootup(void);
void _woort_vfs_shutdown(void);

/**
 * @brief Read the content of a virtual file.
 *
 * The path may be supplied with or without the "woovf://" prefix.
 *
 * If @p out_data is NULL, only the length is queried: @p inout_len is set to
 * the actual content length of the file.
 * If @p out_data is not NULL, it must point to a buffer whose capacity is given
 * by @p inout_len on input; at most that many bytes are copied into it. After
 * the call, @p inout_len is set to the actual content length of the file (a
 * value larger than the capacity passed in indicates the buffer was too small
 * and the content was truncated).
 *
 * @param filepath   The virtual file path.
 * @param out_data   Receives the content (may be NULL to query length only).
 * @param inout_len  On input the capacity of @p out_data (ignored when NULL);
 *                   on output the actual content length. Must not be NULL.
 * @return true if the file was found.
 */
WOORT_NODISCARD bool woort_vfs_read(
    const char* filepath,
    /* OPTIONAL */ void* out_data,
    size_t* inout_len);