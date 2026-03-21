/*
 * woort_ir_compiler.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_compiler.h"
#include "woort_codeenv.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#define WOORT_IR_INITIAL_FUNCTION_CAPACITY 16

WOORT_NODISCARD bool woort_IRCompiler_init(woort_IRCompiler** out_compiler)
{
    assert(out_compiler != NULL);

    woort_IRCompiler* compiler = (woort_IRCompiler*)malloc(sizeof(woort_IRCompiler));
    if (compiler == NULL)
    {
        return false;
    }

    compiler->m_functions = (woort_IRFunction**)malloc(sizeof(woort_IRFunction*) * WOORT_IR_INITIAL_FUNCTION_CAPACITY);
    if (compiler->m_functions == NULL)
    {
        free(compiler);
        return false;
    }

    compiler->m_function_count = 0;
    compiler->m_function_capacity = WOORT_IR_INITIAL_FUNCTION_CAPACITY;
    compiler->m_global_count = 0;
    compiler->m_error_buffer[0] = '\0';
    compiler->m_has_error = false;

    *out_compiler = compiler;
    return true;
}

void woort_IRCompiler_drop(woort_IRCompiler* compiler)
{
    if (compiler == NULL)
    {
        return;
    }

    if (compiler->m_functions != NULL)
    {
        for (uint32_t i = 0; i < compiler->m_function_count; ++i)
        {
            woort_IRFunction* func = compiler->m_functions[i];
            if (func != NULL)
            {
                _woort_ir_function_drop(func);
            }
        }
        free(compiler->m_functions);
    }

    free(compiler);
}

WOORT_NODISCARD woort_IRGlobalIndex woort_IRCompiler_alloc_global(woort_IRCompiler* compiler)
{
    assert(compiler != NULL);
    return compiler->m_global_count++;
}

WOORT_NODISCARD bool _woort_ir_compiler_ensure_function_capacity(woort_IRCompiler* compiler)
{
    if (compiler->m_function_count < compiler->m_function_capacity)
    {
        return true;
    }

    uint32_t new_capacity = compiler->m_function_capacity * 2;
    woort_IRFunction** new_functions = (woort_IRFunction**)realloc(
        compiler->m_functions,
        sizeof(woort_IRFunction*) * new_capacity);

    if (new_functions == NULL)
    {
        return false;
    }

    compiler->m_functions = new_functions;
    compiler->m_function_capacity = new_capacity;
    return true;
}

WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* compiler,
    uint32_t param_count,
    woort_IRFunction** out_func)
{
    assert(compiler != NULL);
    assert(out_func != NULL);

    if (!_woort_ir_compiler_ensure_function_capacity(compiler))
    {
        _woort_ir_compiler_set_error(compiler, "Failed to allocate function storage");
        return false;
    }

    woort_IRFunction* func;
    if (!_woort_ir_function_init(&func, compiler, param_count, compiler->m_function_count))
    {
        _woort_ir_compiler_set_error(compiler, "Failed to initialize function");
        return false;
    }

    compiler->m_functions[compiler->m_function_count] = func;
    compiler->m_function_count++;

    *out_func = func;
    return true;
}

WOORT_NODISCARD bool woort_IRCompiler_finish(
    woort_IRCompiler* compiler,
    woort_CodeEnv** out_codeenv)
{
    assert(compiler != NULL);
    assert(out_codeenv != NULL);

    if (compiler->m_function_count == 0)
    {
        _woort_ir_compiler_set_error(compiler, "No functions defined");
        return false;
    }

    if (!_woort_ir_validate(compiler))
    {
        return false;
    }

    if (!_woort_ir_codegen(compiler, out_codeenv))
    {
        return false;
    }

    return true;
}

WOORT_NODISCARD const char* woort_IRCompiler_get_error(woort_IRCompiler* compiler)
{
    assert(compiler != NULL);

    if (compiler->m_has_error)
    {
        return compiler->m_error_buffer;
    }
    return "";
}

WOORT_NODISCARD bool _woort_ir_compiler_set_error(woort_IRCompiler* compiler, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(compiler->m_error_buffer, sizeof(compiler->m_error_buffer), fmt, args);
    va_end(args);

    compiler->m_has_error = true;
    return false;
}