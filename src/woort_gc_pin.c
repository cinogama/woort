#include "woort.h"

#include "woort_gc_pin.h"
#include "woort_spin.h"
#include "woort_gc_units.h"
#include "woort_gc.h"
#include "woort_vm.h"
#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"

#include <assert.h>

static void _woort_GCPin_mark_callback(woort_GCUnit* p)
{
    woort_GCPin* const pin = (woort_GCPin*)p;

    for (size_t i = 0; i < pin->m_size; ++i)
    {
        woort_GC_mark_internal_value_manually(
            &pin->m_datas[i]);
    }
}

const woort_GCUnitProxy WOORT_GCPIN_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = &_woort_GCPin_mark_callback,
};

static woort_Spinlock _woort_gcpin_chain_mx;
static /* OPTIONAL */ woort_GCPin** _woort_gcpin_chain_head;

void woort_GCPin_bootup(void)
{
    _woort_gcpin_chain_head = NULL;
    woort_spinlock_init(&_woort_gcpin_chain_mx);
}
void woort_GCPin_shutdown(void)
{
    assert(_woort_gcpin_chain_head == NULL);
    woort_spinlock_deinit(&_woort_gcpin_chain_mx);
}

WOORT_NODISCARD woort_GCPin* woort_GCPin_create(size_t count)
{
    assert(_woort_GC_Debug_current_thread_in_scope());

    woort_GCPin* const p = woort_GCUnit_alloc_delay_init(
        sizeof(woort_GCPin) + count * sizeof(woort_Value));

    p->m_gc_unit.m_proxy = &WOORT_GCPIN_UNIT_PROXY;
    p->m_size = count;

    p->m_prev = NULL;
    woort_spinlock_lock(&_woort_gcpin_chain_mx);
    {
        if (_woort_gcpin_chain_head == NULL)
        {
            /* No chain yet. */
            _woort_gcpin_chain_head =
                woort_GCUnit_alloc_delay_init(sizeof(woort_GCPin*));

            *_woort_gcpin_chain_head = NULL;

            woomem_allocate_end_as_root(
                _woort_gcpin_chain_head,
                WOOMEM_ATTRIB_NEED_SWEEP | WOOMEM_ATTRIB_AUTO_MARK);

            p->m_next = NULL;
        }
        else
        {
            woort_GC_init_write_barrier_gcunit(
                (void**)&p->m_next, *_woort_gcpin_chain_head);

            woort_GC_init_write_barrier_gcunit(
                (void**)&p->m_next->m_prev, p);
        }
        woort_GC_mixed_write_barrier_gcunit(
            (void**)_woort_gcpin_chain_head, p);
    }
    woort_spinlock_unlock(&_woort_gcpin_chain_mx);

    woort_GCUnit_init_delay_alloc(AM, p);

    return p;
}
void woort_GCPin_destroy(woort_GCPin* pin)
{
    assert(_woort_GC_Debug_current_thread_in_scope());

    assert(pin != NULL);
    assert(pin->m_gc_unit.m_proxy == &WOORT_GCPIN_UNIT_PROXY);
    assert(_woort_gcpin_chain_head != NULL);

    woort_GC_delete_barrier_gcunit(pin);

    woort_spinlock_lock(&_woort_gcpin_chain_mx);
    {
        if (pin->m_prev != NULL)
        {
            woort_GC_mixed_write_barrier_gcunit(
                (void**)&pin->m_prev->m_next, pin->m_next);
        }
        else
        {
            assert(*_woort_gcpin_chain_head == pin);

            if (pin->m_next == NULL)
            {
                /* No pin in chain. */
                woomem_remove_from_root_set(_woort_gcpin_chain_head);
                _woort_gcpin_chain_head = NULL;
            }
            else
            {
                woort_GC_mixed_write_barrier_gcunit(
                    (void**)_woort_gcpin_chain_head, pin->m_next);
            }
        }

        if (pin->m_next != NULL)
        {
            woort_GC_mixed_write_barrier_gcunit(
                (void**)&pin->m_next->m_prev, pin->m_prev);
        }
    }
    woort_spinlock_unlock(&_woort_gcpin_chain_mx);
}
void woort_GCPin_set_internal(woort_GCPin* pin, size_t idx, const woort_Value* val)
{
    assert(_woort_GC_Debug_current_thread_in_scope());
    assert(pin != NULL);
    assert(pin->m_gc_unit.m_proxy == &WOORT_GCPIN_UNIT_PROXY);
    assert(idx < pin->m_size);
    assert(val != NULL);

    woort_GC_mixed_write_barrier_value(&pin->m_datas[idx], *val);
}
void woort_GCPin_get_internal(woort_Value* dst, woort_GCPin* pin, size_t idx)
{
    assert(_woort_GC_Debug_current_thread_in_scope());
    assert(pin != NULL);
    assert(pin->m_gc_unit.m_proxy == &WOORT_GCPIN_UNIT_PROXY);
    assert(idx < pin->m_size);
    assert(dst != NULL);

    woort_GC_mixed_write_barrier_value(dst, pin->m_datas[idx]);
}
void woort_GCPin_get_internal_without_barrier(woort_Value* dst, woort_GCPin* pin, size_t idx)
{
    assert(_woort_GC_Debug_current_thread_in_scope());
    assert(pin != NULL);
    assert(pin->m_gc_unit.m_proxy == &WOORT_GCPIN_UNIT_PROXY);
    assert(idx < pin->m_size);
    assert(dst != NULL);

    *dst = pin->m_datas[idx];
}
void woort_GCPin_set_dup_boxed_internal(
    woort_GCPin* pin, size_t idx, const woort_Value* val)
{
    assert(_woort_GC_Debug_current_thread_in_scope());
    assert(pin != NULL);
    assert(pin->m_gc_unit.m_proxy == &WOORT_GCPIN_UNIT_PROXY);
    assert(idx < pin->m_size);
    assert(val != NULL);

    woort_GC_mixed_write_barrier_dynbox(
        &pin->m_datas[idx].m_dynamic,
        _woort_DynBox_make_dup_boxed(val->m_dynamic));
}
