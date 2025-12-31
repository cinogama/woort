#include "woort_lir_compiler.h"
#include "woort_log.h"
#include "woort_opcode_formal.h"
#include "woort_linklist.h"
#include "woort_vector.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

void _woort_LIRCompiler_free_static_storage_list(
    woort_LinkList /* woort_LIRCompiler_StaticStorageData */* static_storage_list)
{
    woort_LIRCompiler_StaticStorageData* current = woort_linklist_iter(static_storage_list);
    while (current != NULL)
    {
        // Free the update list.
        woort_linklist_deinit(&current->m_code_update_list);
        current = woort_linklist_next(current);
    }
    woort_linklist_deinit(static_storage_list);
}

void _woort_LIRCompiler_free_label_list(
    woort_LinkList /* woort_LIRCompiler_JmpLabelData */* label_list)
{
    woort_LIRCompiler_JmpLabelData* current = woort_linklist_iter(label_list);
    while (current != NULL)
    {
        // Free the update list.
        woort_linklist_deinit(&current->m_code_update_list);
        current = woort_linklist_next(current);
    }
    woort_linklist_deinit(label_list);
}

////////////////////////////////////////////////////////////////////////

void woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler)
{
    woort_vector_init(
        &lir_compiler->m_code_holder,
        sizeof(woort_Bytecode));

    woort_vector_init(
        &lir_compiler->m_constant_storage_holder,
        sizeof(woort_Value));

    lir_compiler->m_static_storage_count = 0;
    woort_linklist_init(
        &lir_compiler->m_static_storage_list,
        sizeof(woort_LIRCompiler_StaticStorageData));

    woort_linklist_init(
        &lir_compiler->m_label_list,
        sizeof(woort_LIRCompiler_JmpLabelData));
}

void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler)
{
    _woort_LIRCompiler_free_label_list(
        &lir_compiler->m_label_list);
    _woort_LIRCompiler_free_static_storage_list(
        &lir_compiler->m_static_storage_list);

    woort_vector_deinit(&lir_compiler->m_code_holder);
    woort_vector_deinit(&lir_compiler->m_constant_storage_holder);
}

bool _woort_LIRCompiler_emit(
    woort_LIRCompiler* lir_compiler,
    woort_Bytecode bytecode)
{
    woort_vector_push_back(
        &lir_compiler->m_code_holder,
        1,
        &bytecode);
    return true;
}

bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_ConstantStorage* out_constant_address)
{
    void* _useless_storage;
    if (!woort_vector_emplace_back(
        &lir_compiler->m_constant_storage_holder,
        1,
        &lir_compiler))
    {
        // Allocation failed.
        return false;
    }

    (void)_useless_storage;
    *out_constant_address =
        (woort_LIRCompiler_ConstantStorage)
        lir_compiler->m_constant_storage_holder.m_size;

    return true;
}
bool woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_StaticStorage* out_static_storage_address)
{
    woort_LIRCompiler_StaticStorageData* new_static_storage;
    if (!woort_linklist_emplace_back(
        &lir_compiler->m_static_storage_list,
        &new_static_storage))
    {
        // Allocation failed.
        return false;
    }

    // Initialize the new static storage data.
    new_static_storage->m_static_index = lir_compiler->m_static_storage_count++;
    woort_linklist_init(
        &new_static_storage->m_code_update_list,
        sizeof(woort_LIRCompiler_UpdateStaticStorage));

    *out_static_storage_address = new_static_storage;
    return true;
}
bool woort_LIRCompiler_allocate_label(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_JmpLabel* out_label_address)
{
    woort_LIRCompiler_JmpLabelData* new_label;
    if (!woort_linklist_emplace_back(
        &lir_compiler->m_label_list,
        &new_label))
    {
        // Allocation failed.
        return false;
    }

    new_label->m_binded_code_offset = SIZE_MAX; // Not binded yet.
    woort_linklist_init(
        &new_label->m_code_update_list,
        sizeof(woort_LIRCompiler_UpdateJmpOffset));

    *out_label_address = new_label;
    return true;
}

bool woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_ConstantStorage constant_address,
    woort_Value** out_constant_storage)
{
    woort_Value* constant_storage;
    if (!woort_vector_index(
        &lir_compiler->m_constant_storage_holder,
        (size_t)constant_address,
        (void**)&constant_storage))
    {
        // Invalid constant address.
        WOORT_DEBUG("Invalid constant address.");
        return false;
    }
    *out_constant_storage = constant_storage;
    return true;
}

//bool woort_LIRCompiler_bind(
//    woort_LIRCompiler* lir_compiler,
//    woort_LIRCompiler_JmpLabel* label)
//{
//    if (label->m_binded_code_offset != SIZE_MAX)
//    {
//        WOORT_DEBUG("Label already binded.");
//        return false;
//    }
//    
//    return false;
//}
