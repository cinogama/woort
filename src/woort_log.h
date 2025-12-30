#pragma once

/*
woort_log.h
*/

void woort_log(const char* format, ...);

#ifdef NDEBUG
#   define WOORT_DEBUG(format, ...) do {} while(0)
#else
#   define WOORT_DEBUG(format, ...) \
        woort_log("WOORT(%s:%d): " format, __FILE__, __LINE__,##__VA_ARGS__)
#endif
