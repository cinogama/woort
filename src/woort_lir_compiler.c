#include "woort_lir_compiler.h"
#include "woort_log.h"
#include "woort_opcode_formal.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

void _woort_LIRCompiler_free_update_static_storage_list(
    woort_LIRCompiler_UpdateStaticStorage* /* OPTIONAL */ update_list)
{
    woort_LIRCompiler_UpdateStaticStorage* current = update_list;
    while (current != NULL)
    {
        woort_LIRCompiler_UpdateStaticStorage* next = current->m_next;
        free(current);
        current = next;
    }
}

void _woort_LIRCompiler_free_static_storage_list(
    woort_LIRCompiler_StaticStorageData* /* OPTIONAL */ static_storage_list)
{
    woort_LIRCompiler_StaticStorageData* current = static_storage_list;
    while (current != NULL)
    {
        woort_LIRCompiler_StaticStorageData* next = current->m_next;
        
        // Free the update list.
        _woort_LIRCompiler_free_update_static_storage_list(
            current->m_code_update_list);

        free(current);
        current = next;
    }
}

void _woort_LIRCompiler_free_label_update_list(
    woort_LIRCompiler_UpdateJmpOffset* /* OPTIONAL */ update_list)
{
    woort_LIRCompiler_UpdateJmpOffset* current = update_list;
    while (current != NULL)
    {
        woort_LIRCompiler_UpdateJmpOffset* next = current->m_next;
        free(current);
        current = next;
    }
}

void _woort_LIRCompiler_free_label_list(
    woort_LIRCompiler_JmpLabelData* /* OPTIONAL */ label_list)
{
    woort_LIRCompiler_JmpLabelData* current = label_list;
    while (current != NULL)
    {
        woort_LIRCompiler_JmpLabelData* next = current->m_next;
        // Free the update list.
        _woort_LIRCompiler_free_label_update_list(
            current->m_code_update_list);
        free(current);
        current = next;
    }
}

////////////////////////////////////////////////////////////////////////

bool woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler)
{
    lir_compiler->m_code_holder = 
        malloc(8 * sizeof(woort_Bytecode));

    lir_compiler->m_constant_storage_holder =
        malloc(8 * sizeof(woort_Value));

    if (lir_compiler->m_code_holder == NULL
        || lir_compiler->m_constant_storage_holder == NULL)
    {
        WOORT_DEBUG("Allocation failed.");

        free(lir_compiler->m_code_holder);
        free(lir_compiler->m_constant_storage_holder);

        return false;
    }

    lir_compiler->m_code_capacity = 8;
    lir_compiler->m_code_size = 0;

    lir_compiler->m_constant_storage_capacity = 2;
    lir_compiler->m_constant_storage_size = 0;

    lir_compiler->m_static_storage_count = 0;
    lir_compiler->m_static_storage_list = NULL;

    lir_compiler->m_label_list = NULL;

    return true;
}

void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler)
{
    _woort_LIRCompiler_free_label_list(
        lir_compiler->m_label_list);

    _woort_LIRCompiler_free_static_storage_list(
        lir_compiler->m_static_storage_list);

    if (lir_compiler->m_code_holder != NULL)
        free(lir_compiler->m_code_holder);

    if (lir_compiler->m_constant_storage_holder != NULL)
        free(lir_compiler->m_constant_storage_holder);
}

bool _woort_LIRCompiler_emit(
    woort_LIRCompiler* lir_compiler, 
    woort_Bytecode bytecode)
{
    if (lir_compiler->m_code_size >= lir_compiler->m_code_capacity)
    {
        // Need to reallocate.
        size_t new_capacity = lir_compiler->m_code_capacity * 2;
        woort_Bytecode* new_holder = 
            realloc(
                lir_compiler->m_code_holder, 
                new_capacity * sizeof(woort_Bytecode));

        if (new_holder == NULL)
        {
            WOORT_DEBUG("Reallocation failed.");
            return false;
        }
        lir_compiler->m_code_holder = new_holder;
        lir_compiler->m_code_capacity = new_capacity;
    }
    lir_compiler->m_code_holder[lir_compiler->m_code_size++] = bytecode;
    return true;
}

bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIRCompiler_ConstantStorage* out_constant_address)
{
    if (lir_compiler->m_constant_storage_size >= lir_compiler->m_constant_storage_capacity)
    {
        // Need to reallocate.
        size_t new_capacity = lir_compiler->m_constant_storage_capacity * 2;
        woort_Value* new_holder = 
            realloc(
                lir_compiler->m_constant_storage_holder, 
                new_capacity * sizeof(woort_Value));
        if (new_holder == NULL)
        {
            WOORT_DEBUG("Reallocation failed.");
            return false;
        }
        lir_compiler->m_constant_storage_holder = new_holder;
        lir_compiler->m_constant_storage_capacity = new_capacity;
    }

    // Constant and global address will share the same space.
    *out_constant_address = 
        (woort_LIRCompiler_ConstantStorage)(lir_compiler->m_constant_storage_size++);

    return true;
}
bool woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_StaticStorage* out_static_storage_address)
{
    woort_LIRCompiler_StaticStorageData* new_static_storage =
        malloc(sizeof(woort_LIRCompiler_StaticStorageData));

    if (new_static_storage == NULL)
    {
        WOORT_DEBUG("Allocation failed.");
        return false;
    }

    new_static_storage->m_static_index = lir_compiler->m_static_storage_count++;
    new_static_storage->m_code_update_list = NULL;
    new_static_storage->m_next = lir_compiler->m_static_storage_list;

    // Update the list head.
    lir_compiler->m_static_storage_list = new_static_storage;

    *out_static_storage_address = new_static_storage;

    return true;
}
bool woort_LIRCompiler_allocate_label(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_JmpLabel* out_label_address)
{
    woort_LIRCompiler_JmpLabelData* new_label =
        malloc(sizeof(woort_LIRCompiler_JmpLabelData));
    
    if (new_label == NULL)
    {
        WOORT_DEBUG("Allocation failed.");
        return false;
    }

    new_label->m_binded_code_offset = SIZE_MAX; // Not binded yet.
    new_label->m_code_update_list = NULL;
    new_label->m_next = lir_compiler->m_label_list;
    // Update the list head.
    lir_compiler->m_label_list = new_label;
    *out_label_address = new_label;
    return true;
}

woort_Value* woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_ConstantStorage constant_address)
{
    assert(constant_address < lir_compiler->m_constant_storage_size);

    return &lir_compiler->m_constant_storage_holder[constant_address];
}
