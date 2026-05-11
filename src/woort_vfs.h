#pragma once

/*
woort_vfs.h - Virtual File System internal header

Manages a global registry of virtual files that can be embedded
at runtime.  The public API is declared in woort.h; the bootup /
shutdown hooks are called by woort.c.
*/

#include "woort.h"
#include "woort_spin.h"
#include "woort_vector.h"

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
    WOORT_VFILE_TYPE_VIRTUAL = 1
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
    };
};

void _woort_vfs_bootup(void);
void _woort_vfs_shutdown(void);
