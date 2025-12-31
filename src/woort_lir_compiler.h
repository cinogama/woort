#pragma once

/*
woort_lir_compiler.h
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "woort_linklist.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"

// Static storage.
typedef enum woort_LIRCompiler_UpdateStaticStorage_Target
{
    WOORT_LIRCOMPILER_UPDATE_STATIC_STORAGE_TARGET_C18,
    WOORT_LIRCOMPILER_UPDATE_STATIC_STORAGE_TARGET_C18L_EXC26H,
    WOORT_LIRCOMPILER_UPDATE_STATIC_STORAGE_TARGET_C24,

}woort_LIRCompiler_UpdateStaticStorage_Target;

typedef struct woort_LIRCompiler_UpdateStaticStorage
{
    woort_LIRCompiler_UpdateStaticStorage_Target 
                m_target;
    size_t      m_code_offset;

}woort_LIRCompiler_UpdateStaticStorage;

typedef struct woort_LIRCompiler_StaticStorageData
{
    uint64_t    m_static_index;
    woort_LinkList /* woort_LIRCompiler_UpdateStaticStorage */ 
                m_code_update_list;

}woort_LIRCompiler_StaticStorageData, * woort_LIRCompiler_StaticStorage;

// Jump label.
typedef enum woort_LIRCompiler_UpdateJmpOffset_Target
{
    WOORT_LIRCOMPILER_UPDATE_JMP_OFFSET_TARGET_OFFSET26,
    WOORT_LIRCOMPILER_UPDATE_JMP_OFFSET_TARGET_OFFSET16,
    WOORT_LIRCOMPILER_UPDATE_JMP_OFFSET_TARGET_OFFSET8,

}woort_LIRCompiler_UpdateJmpOffset_Target;

typedef struct woort_LIRCompiler_UpdateJmpOffset
{
    woort_LIRCompiler_UpdateJmpOffset_Target 
                m_target;
    size_t      m_code_offset;

} woort_LIRCompiler_UpdateJmpOffset;

typedef struct woort_LIRCompiler_JmpLabelData
{
    /* NOTE: SIZE_MAX means bind less. */
    size_t      m_binded_code_offset;   
    /* NOTE: `m_code_update_list` will be apply and free if binded. */
    woort_LinkList /* woort_LIRCompiler_UpdateJmpOffset */
                m_code_update_list;

} woort_LIRCompiler_JmpLabelData, * woort_LIRCompiler_JmpLabel;

// Constant.
typedef uint64_t woort_LIRCompiler_ConstantStorage;

// LIRCompiler.

typedef struct woort_LIRCompiler
{
    // Code holder.
    woort_Vector /* woort_Bytecode */
                    m_code_holder;

    // Constant and Global holders.
    woort_Vector /* woort_Value */
                    m_constant_storage_holder;

    // Static storage data list.
    size_t          m_static_storage_count;
    woort_LinkList /* woort_LIRCompiler_StaticStorageData */
                    m_static_storage_list;

    // Label data list.
    woort_LinkList /* woort_LIRCompiler_JmpLabelData */
                    m_label_list;

} woort_LIRCompiler;

void woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler);
void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler);

bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIRCompiler_ConstantStorage* out_constant_address);
bool woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_StaticStorage* out_static_storage_address);
bool woort_LIRCompiler_allocate_label(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_JmpLabel* out_label_address);

/*
NOTE: The returned pointer is valid until the next call to
      woort_LIRCompiler_allocate_constant.
*/
bool woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIRCompiler_ConstantStorage constant_address,
    woort_Value** out_constant_storage);

// Code generator

bool woort_LIRCompiler_bind(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_JmpLabel* label);

