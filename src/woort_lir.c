#include "woort_lir.h"

void woort_LIR_update_static_storage(
    woort_LIR* lir, size_t constant_count)
{
    switch (lir->m_opnum_formal)
    {
    case WOORT_LIR_OPNUMFORMAL_CS:
        if (!lir->m_opnums.m_cs.m_cs.m_is_constant)
            lir->m_opnums.m_cs.m_cs.m_static += constant_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_CS_R:
        if (!lir->m_opnums.m_cs_r.m_cs.m_is_constant)
            lir->m_opnums.m_cs_r.m_cs.m_static += constant_count;
        break;
    case WOORT_LIR_OPNUMFORMAL_S_R:
        lir->m_opnums.m_s_r.m_s += constant_count;
        break;
    default:
        // No static storage to update.
        break;
    }
}
size_t woort_LIR_ir_length_exclude_jmp(const woort_LIR* lir)
{
    const size_t UINT18_MAX = (1 << 18) - 1;
    switch (lir->m_opcode)
    {
    case WOORT_LIR_OPCODE_LOAD:
        if (lir->m_opnums.m_LOAD.m_cs.m_is_constant)
        {
            if (lir->m_opnums.m_LOAD.m_cs.m_constant > UINT18_MAX)
                return 2;
        }
        else if (lir->m_opnums.m_LOAD.m_cs.m_static > UINT18_MAX)
            return 2;
        break;
    case WOORT_LIR_OPCODE_STORE:
        if (lir->m_opnums.m_STORE.m_s > UINT18_MAX)
            return 2;
        break;
    default:
        break;
    }
    return 1;
}