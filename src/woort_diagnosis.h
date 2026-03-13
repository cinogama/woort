#pragma once

/*
woort_diagnosis.h
*/

#if defined(_MSC_VER)
    /* MSVC 支持 */
    #define WOORT_NODISCARD _Check_return_
    #define WOORT_DEPRECATED __declspec(deprecated)
#elif defined(__clang__) || defined(__GNUC__)
    /* Clang 和 GCC 支持 */
    #define WOORT_NODISCARD __attribute__((warn_unused_result))
    #define WOORT_DEPRECATED __attribute__((deprecated))
#else
    /* 其他编译器，不产生警告 */
    #define WOORT_NODISCARD
    #define WOORT_DEPRECATED
#endif

typedef enum woort_PanicReason
{
    WOORT_PANIC_BAD_BYTE_CODE = 0xD001,
    WOORT_PANIC_STACK_OVERFLOW = 0xD002,
    WOORT_PANIC_CODE_ENV_NOT_FOUND = 0xD003,
    WOORT_PANIC_BAD_CALLSTACK = 0xD004,
    WOORT_PANIC_BAD_TYPE = 0xD005,
    WOORT_PANIC_REENTRY_GC_SCOPE = 0xD006,
    WOORT_PANIC_BAD_VM_REQUEST = 0xD007,
    WOORT_PANIC_ABORTED = 0xD008,
    WOORT_PANIC_INDEX_OUT_OF_RANGE = 0xD009,
} woort_PanicReason;

void woort_panic(
    woort_PanicReason reason,
    const char* msgfmt, 
    ...);
