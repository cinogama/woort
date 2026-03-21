#include "woort_ir.h"
#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort.h"

static void dump_codeenv(const char* name, woort_CodeEnv* codeenv)
{
    printf("\n  === Bytecode dump: %s ===\n", name);
    printf("  Code count: %llu\n", (unsigned long long)(codeenv->m_code_end - codeenv->m_code_begin));
    printf("  Data count: (see woort_Value array)\n\n");

    const woort_Bytecode* pc = codeenv->m_code_begin;
    while (pc < codeenv->m_code_end)
    {
        printf("  [%04llu] ", (unsigned long long)(pc - codeenv->m_code_begin));
        pc = woort_Disassembly(pc);
    }
    printf("  === End dump ===\n");
}

void simple_test()
{
    woort_IRCompiler* c;
    (void)woort_IRCompiler_init(&c); /* 假定这类操作必然成功 */

    woort_IRFunction* func;
    (void)woort_IRCompiler_add_function(c, 0, &func);

    woort_IRGlobalIndex const_0 = woort_IRCompiler_alloc_global(c);
    woort_IRGlobalIndex const_1 = woort_IRCompiler_alloc_global(c);

    woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);

    const woort_IRValue* const_val_0 = woort_IRBlock_load_const(entry, const_0);
    const woort_IRValue* const_val_1 = woort_IRBlock_load_const(entry, const_1);

    const woort_IRValue* result = woort_IRBlock_ADD_I(entry, const_val_0, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);
    result = woort_IRBlock_ADD_I(entry, result, const_val_1);

    woort_IRBlock_ret(entry, result);

    woort_CodeEnv* codeenv;
    (void)woort_IRCompiler_finish(c, &codeenv);

    codeenv->m_data_begin[const_0].m_integer = 0;
    codeenv->m_data_begin[const_1].m_integer = 1;

    dump_codeenv("simple_test", codeenv);

    woort_CodeEnv_drop(codeenv);
    woort_IRCompiler_drop(c);
}

int main(void)
{
    woort_init();

    simple_test();

    woort_shutdown();
}