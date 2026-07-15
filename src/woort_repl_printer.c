#include "woort.h"

#include "woort_repl_printer.h"
#include "woort_vector.h"
#include "woort_log.h"
#include "woort_serialize.h"
#include "woort_util.h"
#include "woort_value_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct woort_REPLPrinter
{
    woort_Vector /* char */ m_print_buffer;
    /* OPTIONAL */ woort_REPLPrinter_ResultCallback m_print_callback;
};

WOORT_NODISCARD bool woort_REPLPrinter_create(
    /* OPTIONAL */woort_REPLPrinter_ResultCallback callback,
    woort_REPLPrinter** out_printer)
{
    woort_REPLPrinter* const instance =
        (woort_REPLPrinter*)malloc(sizeof(woort_REPLPrinter));

    if (instance == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }

    woort_vector_init(&instance->m_print_buffer, sizeof(char));
    instance->m_print_callback = callback;

    *out_printer = instance;

    return true;
}

void woort_REPLPrinter_destroy(woort_REPLPrinter* printer)
{
    woort_vector_deinit(&printer->m_print_buffer);
    free(printer);
}

WOORT_NODISCARD bool woort_REPLPrinter_print_mixed(
    woort_REPLPrinter* printer, woort_DynBox boxed)
{
    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    const size_t origin_buf_len = printer->m_print_buffer.m_size;
    if (!_woort_serialize_dynbox_to_buf(
        boxed,
        &printer->m_print_buffer,
        &visited_set,
        0,
        WOORT_SERIALIZE_FLAG_NONE))
    {
        woort_hashmap_deinit(&visited_set);
        (void)woort_vector_resize(&printer->m_print_buffer, origin_buf_len);
        return false;
    }

    woort_hashmap_deinit(&visited_set);
    return true;
}

WOORT_NODISCARD bool woort_REPLPrinter_print_debug(
    woort_REPLPrinter* printer, woort_DynBox boxed)
{
    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    const size_t origin_buf_len = printer->m_print_buffer.m_size;
    if (!_woort_serialize_dynbox_to_buf_for_debug(
        boxed,
        &printer->m_print_buffer,
        &visited_set,
        0,
        false))
    {
        woort_hashmap_deinit(&visited_set);
        (void)woort_vector_resize(&printer->m_print_buffer, origin_buf_len);
        return false;
    }

    woort_hashmap_deinit(&visited_set);
    return true;
}

WOORT_NODISCARD bool woort_REPLPrinter_print_string(
    woort_REPLPrinter* printer, const char* str)
{
    const size_t str_len = strlen(str);

    const size_t origin_buf_len = printer->m_print_buffer.m_size;
    if (!woort_vector_resize(&printer->m_print_buffer, origin_buf_len + str_len))
        return false;

    memcpy(
        (char*)printer->m_print_buffer.m_data + origin_buf_len,
        str,
        str_len);

    return true;
}

WOORT_NODISCARD WOORT_API woort_REPLPrinter_FlushResult woort_REPLPrinter_flush(
    woort_REPLPrinter* printer)
{
    if (printer->m_print_buffer.m_size == 0)
        return WOORT_REPL_PRINTER_FLUSH_NOTHING;

    const char zero = '\0';
    if (!woort_vector_push_back(&printer->m_print_buffer, 1, &zero))
        return WOORT_REPL_PRINTER_FLUSH_FAILED;

    if (printer->m_print_callback == NULL)
    {
        fputs((char*)printer->m_print_buffer.m_data, stdout);
        fflush(stdout);
    }
    else
    {
        printer->m_print_callback(
            (char*)printer->m_print_buffer.m_data,
            printer->m_print_buffer.m_size - 1);
    }

    woort_vector_clear(&printer->m_print_buffer);

    return WOORT_REPL_PRINTER_FLUSH_OK;
}