#pragma once

/*
woort_mem_os.h
OS-level virtual memory primitives.
*/

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t woort_mem_os_page_size(void);
/* OPTIONAL */ void* woort_mem_os_reserve_memory(size_t size);
int /* 0 means OK */ woort_mem_os_commit_memory(void* addr, size_t size);
int /* 0 means OK */ woort_mem_os_decommit_memory(void* addr, size_t size);
int /* 0 means OK */ woort_mem_os_release_memory(void* addr, size_t size);

#ifdef __cplusplus
}
#endif
