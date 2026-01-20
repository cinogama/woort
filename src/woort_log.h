#pragma once

/*
woort_log.h
*/
#include <stdarg.h>

void woort_vlog(const char* format, va_list va);
void woort_log(const char* format, ...);

#ifdef NDEBUG
#   define WOORT_DEBUG(format, ...) do {} while(0)
#else
#   define WOORT_DEBUG(format, ...)                 \
        woort_log("WOORT(%s:%d) %s: " format "\n",  \
            __FILE__, __LINE__, __FUNCTION__,##__VA_ARGS__)
#endif
