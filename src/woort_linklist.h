#pragma once

/*
woort_linklist.h
*/

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct woort_LinkList_Node
{
    /* OPTIONAL */ struct woort_LinkList_Node* m_prev;
    /* OPTIONAL */ struct woort_LinkList_Node* m_next;

    _Alignas(8)
        char m_storage[];

} woort_LinkList_Node;

typedef struct woort_LinkList
{
    /* OPTIONAL */ woort_LinkList_Node* m_head;
    /* OPTIONAL */ woort_LinkList_Node* m_tail;

    size_t m_element_size;

} woort_LinkList;

void woort_linklist_init(woort_LinkList* list, size_t storage_size);
void woort_linklist_deinit(woort_LinkList* list);

WOORT_NODISCARD bool woort_linklist_emplace_back(woort_LinkList* list, void** out_storage);
WOORT_NODISCARD bool woort_linklist_push_back(woort_LinkList* list, const void* data);

WOORT_NODISCARD bool woort_linklist_front(woort_LinkList* list, void** out_storage);
WOORT_NODISCARD bool woort_linklist_back(woort_LinkList* list, void** out_storage);

WOORT_NODISCARD bool woort_linklist_pop_front(woort_LinkList* list);
WOORT_NODISCARD bool woort_linklist_pop_back(woort_LinkList* list);

void woort_linklist_clear(woort_LinkList* list);

WOORT_NODISCARD bool woort_linklist_index(woort_LinkList* list, size_t index, void** out_storage);
void woort_linklist_erase(woort_LinkList* list, void* storage);

WOORT_NODISCARD /* OPTIONAL */ void* woort_linklist_iter(woort_LinkList* list);
WOORT_NODISCARD /* OPTIONAL */ void* woort_linklist_next(void* iterator);
