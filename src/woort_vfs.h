#pragma once

/*
woort_vfs.h - Virtual File System internal header

Manages a global registry of virtual files that can be embedded
at runtime.  The public API is declared in woort.h; the bootup /
shutdown hooks are called by woort.c.
*/

#include "woort.h"
#include "woort_atomic.h"
#include "woort_spin.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define WOORT_VFS_SCHEME      "woovf://"
#define WOORT_VFS_SCHEME_LEN  8

typedef struct woort_VFSEntry
{
    /* OPTIONAL */ char*        m_filepath;       /* owned copy of the key      */
    bool                        m_enable_modify;  /* can be overwritten         */
    size_t                      m_data_length;    /* length of m_data           */
    /* OPTIONAL */ char*        m_data;           /* owned copy of file content */
    woort_AtomicUInt64          m_refcount;       /* reference count            */
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
        FILE* m_real_file;
        struct
        {
            woort_VFSEntry* m_entry;        /* borrowed VFS entry, released on close */
            size_t      m_pos;              /* current read position                 */
        } m_virtual;
        struct
        {
            const void* m_data;             /* external buffer, not owned */
            size_t                  m_size; /* total size in bytes         */
            size_t                  m_pos;  /* current read position       */
        } m_reader;
    };
};

void _woort_vfs_bootup(void);
void _woort_vfs_shutdown(void);

/**
 * @brief Look up a virtual file entry by path and acquire a reference.
 *
 * The path may be supplied with or without the "woovf://" prefix.
 * On success @p out_vfsentry receives a pointer to the entry whose lifetime
 * is extended until woort_vfs_close() is called.  The caller must not free
 * the entry directly.
 *
 * @param filepath     The virtual file path.
 * @param out_vfsentry Receives the entry pointer.
 * @return true if the file was found.
 */
WOORT_NODISCARD bool woort_vfs_open(
    const char* filepath,
    woort_VFSEntry** out_vfsentry);

/**
 * @brief Release a reference acquired with woort_vfs_open().
 *
 * When the last reference is released (and the entry has been removed from
 * the registry), the entry is freed.
 *
 * @param entry  The entry pointer returned by woort_vfs_open().
 */
void woort_vfs_close(woort_VFSEntry* entry);
