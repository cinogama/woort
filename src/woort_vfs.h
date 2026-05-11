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

#include <stddef.h>

#define WOORT_VFS_SCHEME      "woovf://"
#define WOORT_VFS_SCHEME_LEN  8

typedef struct woort_VFSEntry
{
    /* OPTIONAL */ char*    m_filepath;      /* owned copy of the key     */
    bool                    m_enable_modify; /* can be overwritten        */
    size_t                  m_data_length;   /* length of m_data          */
    /* OPTIONAL */ char*    m_data;          /* owned copy of file content */
} woort_VFSEntry;

void _woort_vfs_bootup(void);
void _woort_vfs_shutdown(void);
