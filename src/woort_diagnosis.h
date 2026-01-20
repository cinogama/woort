#pragma once

/*
woort_diagnosis.h
*/

#if defined(_MSC_VER)
    /* MSVC 支持 */
    #define WOORT_NODISCARD _Check_return_
#elif defined(__clang__) || defined(__GNUC__)
    /* Clang 和 GCC 支持 */
    #define WOORT_NODISCARD __attribute__((warn_unused_result))
#else
    /* 其他编译器，不产生警告 */
    #define WOORT_NODISCARD
#endif
