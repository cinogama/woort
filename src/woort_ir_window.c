#include "woort.h"

#include "woort_ir_window.h"
#include "woort_ir_function.h"
#include "woort_linklist.h"
#include "woort_vector.h"

#include <stdlib.h>
#include <stddef.h>

WOORT_NODISCARD /* OPTIONAL */ woort_IRWindow* _woort_IRWindow_create(
    woort_IRFunction* f, uint32_t count)
{
    if (count > UINT32_MAX / sizeof(woort_IRValue*))
        return NULL;

    woort_IRWindow* const w = (woort_IRWindow*)calloc(
        1, sizeof(woort_IRWindow) + (size_t)count * sizeof(woort_IRValue*));
    if (w == NULL)
        return NULL;

    w->m_function = f;
    w->m_id = (uint32_t)f->m_ir_windows.m_size;
    w->m_count = count;
    w->m_create_index = (uint32_t)f->m_instructions.m_size;
    w->m_consumer_index = UINT32_MAX;

    for (uint32_t i = 0; i < count; ++i)
    {
        woort_IRValue* v;
        if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&v))
            goto failure;

        woort_IRValue_init_slot(v, f->m_next_vreg_id, w, i);
        f->m_next_vreg_id++;
        w->m_slots[i] = v;
    }

    if (!woort_vector_push_back(&f->m_ir_windows, 1, &w))
        goto failure;

    return w;

failure:
    /* 已创建的槽位退化为无定义的 vreg，不再指向即将释放的窗口 */
    for (uint32_t i = 0; i < count; ++i)
    {
        if (w->m_slots[i] == NULL)
            break;
        w->m_slots[i]->m_source = WOORT_IRVALUE_SOURCE_VREG;
        w->m_slots[i]->m_window = NULL;
    }
    free(w);
    return NULL;
}

void _woort_IRWindow_destroy(woort_IRWindow* w)
{
    free(w);
}

WOORT_NODISCARD bool _woort_IRWindow_consume(
    woort_IRFunction* f, woort_IRWindow* w)
{
    if (w == NULL || w->m_function != f || w->m_consumed)
        return false;

    w->m_consumed = true;
    w->m_consumer_index = (uint32_t)f->m_instructions.m_size;
    return true;
}

/* ===================================================================
 * 公共 API
 * =================================================================== */

WOORT_NODISCARD /* OPTIONAL */ woort_IRWindow* woort_IRFunction_new_window(
    woort_IRFunction* f,
    uint32_t count)
{
    if (f == NULL)
        return NULL;

    return _woort_IRWindow_create(f, count);
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRWindow_slot(
    woort_IRWindow* w,
    uint32_t index)
{
    if (w == NULL || index >= w->m_count)
        return NULL;

    return w->m_slots[index];
}
