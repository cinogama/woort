#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>

#include "woort_linklist.h"
#include "woort_log.h"

void woort_linklist_init(woort_LinkList* list, size_t storage_size)
{
    list->m_head = NULL;
    list->m_tail = NULL;

    list->m_element_size = storage_size;
}
void woort_linklist_deinit(woort_LinkList* list)
{
    woort_LinkList_Node* node = list->m_head;
    while (node)
    {
        woort_LinkList_Node* next = node->m_next;
        free(node);
        node = next;
    }
}

WOORT_NODISCARD bool woort_linklist_emplace_back(woort_LinkList* list, void** out_storage)
{
    woort_LinkList_Node* new_node =
        malloc(sizeof(woort_LinkList_Node) + list->m_element_size);

    if (NULL == new_node)
    {
        WOORT_DEBUG("Allocation failed.");
        return false;
    }

    new_node->m_next = NULL;

    if (NULL == list->m_tail)
    {
        // Is first node.
        list->m_head = new_node;
        list->m_tail = new_node;
    }
    else
    {
        list->m_tail->m_next = new_node;
        list->m_tail = new_node;
    }

    *out_storage = new_node->m_storage;
    return true;
}

WOORT_NODISCARD bool woort_linklist_push_back(woort_LinkList* list, const void* data)
{
    void* storage;
    if (!woort_linklist_emplace_back(list, &storage))
        return false;

    memcpy(storage, data, list->m_element_size);
    return true;
}
void woort_linklist_clear(woort_LinkList* list)
{
    woort_linklist_deinit(list);
    list->m_head = NULL;
    list->m_tail = NULL;
}
WOORT_NODISCARD bool woort_linklist_index(woort_LinkList* list, size_t index, void** out_storage)
{
    woort_LinkList_Node* current = list->m_head;
    while (index--)
    {
        if (current == NULL)
            // Out of range.
            break;

        current = current->m_next;
    }

    if (current == NULL)
    {
        // Out of range.
        return false;
    }

    *out_storage = current->m_storage;
    return true;
}

/* OPTIONAL */ void* woort_linklist_iter(woort_LinkList* list)
{
    if (list->m_head == NULL)
        return NULL;

    return list->m_head->m_storage;
}
/* OPTIONAL */ void* woort_linklist_next(void* iterator)
{
    woort_LinkList_Node* current_node =
        (woort_LinkList_Node*)(
            (char*)iterator - offsetof(woort_LinkList_Node, m_storage));

    if (current_node->m_next == NULL)
        return NULL;
    return current_node->m_next->m_storage;
}
