#pragma once

/*
 * woort_ir_codegen.h
 */

#include "woort_ir_module.h"
#include "../woort_codeenv.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct woort_StackSlotInfo
{
    int32_t         m_offset;
    bool            m_is_allocated;

} woort_StackSlotInfo;

typedef struct woort_StackAllocator
{
    woort_StackSlotInfo*    m_value_slots;
    uint32_t                m_value_count;
    int32_t                 m_max_local_offset;
    int32_t                 m_max_sp_offset;

} woort_StackAllocator;

typedef struct woort_ConstantPool
{
    woort_Value*    m_entries;
    uint32_t        m_count;
    uint32_t        m_capacity;

} woort_ConstantPool;

typedef enum woort_PatchKind
{
    WOORT_PATCH_KIND_MABC26,
    WOORT_PATCH_KIND_BC16,

} woort_PatchKind;

typedef struct woort_CodeEmitter
{
    woort_Bytecode*     m_code;
    uint32_t            m_code_size;
    uint32_t            m_code_capacity;

    struct {
        uint32_t        m_inst_offset;
        uint32_t        m_target_block_id;
        woort_PatchKind m_kind;
    }*                  m_patches;
    uint32_t            m_patch_count;
    uint32_t            m_patch_capacity;

    uint32_t*           m_block_offsets;
    uint32_t            m_block_count;

} woort_CodeEmitter;

typedef struct woort_IRCodegenResult
{
    woort_CodeEnv*          m_codeenv;
    const woort_Bytecode**  m_function_entries;
    uint32_t                m_function_count;

} woort_IRCodegenResult;

typedef struct woort_FuncPatchInfo
{
    uint32_t        m_pushrchk_offset;
    uint32_t        m_func_entry_offset;

} woort_FuncPatchInfo;

WOORT_NODISCARD bool woort_IRModule_codegen(
    woort_IRModule* module,
    woort_IRCodegenResult* out_result);