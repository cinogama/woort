#pragma once

/*
woort_atomic.h
*/

#include <stdint.h>
#include <stddef.h>

#ifdef __STDC_NO_ATOMICS__
#   define woort_Atomic volatile
#else
#   define woort_Atomic _Atomic
#endif

/* Supported Base Atomic type */
typedef woort_Atomic int8_t   woort_AtomicInt8;
typedef woort_Atomic uint8_t  woort_AtomicUInt8;
typedef woort_Atomic int16_t  woort_AtomicInt16;
typedef woort_Atomic uint16_t woort_AtomicUInt16;
typedef woort_Atomic int32_t  woort_AtomicInt32;
typedef woort_Atomic uint32_t woort_AtomicUInt32;
typedef woort_Atomic int64_t  woort_AtomicInt64;
typedef woort_Atomic uint64_t woort_AtomicUInt64;
typedef woort_Atomic intptr_t woort_AtomicIntPtr;
typedef woort_Atomic uintptr_t woort_AtomicUIntPtr;
typedef woort_Atomic size_t  woort_AtomicSize;
typedef woort_Atomic ptrdiff_t woort_AtomicPtrDiff;
typedef void* woort_Atomic woort_AtomicPtr;

#ifdef __STDC_NO_ATOMICS__

typedef enum woort_atomic_MemoryOrder
{
    WOORT_ATOMIC_MEMORY_ORDER_RELAXED,
    WOORT_ATOMIC_MEMORY_ORDER_CONSUME,
    WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE,
    WOORT_ATOMIC_MEMORY_ORDER_RELEASE,
    WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL,
    WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST,

} woort_atomic_MemoryOrder;

/* ============================================================================
 * Platform detection and intrinsics
 * ============================================================================ */

#if defined(_MSC_VER)
    /* Microsoft Visual C++ */
#   include <intrin.h>
#   if defined(_M_IX86) || defined(_M_X64)
#       pragma intrinsic(_mm_mfence)
#   endif
#   define WOORT_ATOMIC_MSVC 1
#elif defined(__GNUC__) || defined(__clang__)
    /* GCC or Clang */
#   define WOORT_ATOMIC_GCC 1
#else
#   error "Unsupported compiler for atomic operations fallback"
#endif

/* ============================================================================
 * Memory barrier
 * ============================================================================ */

static inline void woort_atomic_compiler_barrier(void)
{
#if defined(WOORT_ATOMIC_MSVC)
    _ReadWriteBarrier();
#elif defined(WOORT_ATOMIC_GCC)
    __asm__ __volatile__("" ::: "memory");
#endif
}

static inline void woort_atomic_memory_barrier(woort_atomic_MemoryOrder order)
{
#if defined(WOORT_ATOMIC_MSVC)
    (void)order;
    _ReadWriteBarrier();
#   if defined(_M_IX86) || defined(_M_X64)
    _mm_mfence();
#   elif defined(_M_ARM) || defined(_M_ARM64)
    __dmb(_ARM_BARRIER_ISH);
#   endif
#elif defined(WOORT_ATOMIC_GCC)
    switch (order)
    {
    case WOORT_ATOMIC_MEMORY_ORDER_RELAXED:
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_CONSUME:
    case WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE:
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_RELEASE:
        __atomic_thread_fence(__ATOMIC_RELEASE);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL:
    case WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST:
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        break;
    }
#endif
}

/* ============================================================================
 * woort_AtomicFlag and related operations
 * ============================================================================ */

typedef struct woort_AtomicFlag
{
    woort_Atomic int flag;
} woort_AtomicFlag;

#define WOORT_ATOMIC_FLAG_INIT { 0 }

#if defined(WOORT_ATOMIC_MSVC)

static inline int woort_atomic_flag_test_and_set(woort_AtomicFlag* flag)
{
    return _InterlockedExchange((volatile long*)&flag->flag, 1) != 0;
}

static inline int woort_atomic_flag_test_and_set_explicit(woort_AtomicFlag* flag, woort_atomic_MemoryOrder order)
{
    (void)order;
    return _InterlockedExchange((volatile long*)&flag->flag, 1) != 0;
}

static inline void woort_atomic_flag_clear(woort_AtomicFlag* flag)
{
    _InterlockedExchange((volatile long*)&flag->flag, 0);
}

static inline void woort_atomic_flag_clear_explicit(woort_AtomicFlag* flag, woort_atomic_MemoryOrder order)
{
    (void)order;
    _InterlockedExchange((volatile long*)&flag->flag, 0);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int woort_atomic_flag_test_and_set(woort_AtomicFlag* flag)
{
    return __atomic_test_and_set(&flag->flag, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_flag_test_and_set_explicit(woort_AtomicFlag* flag, woort_atomic_MemoryOrder order)
{
    int gcc_order = __ATOMIC_SEQ_CST;
    switch (order)
    {
    case WOORT_ATOMIC_MEMORY_ORDER_RELAXED: gcc_order = __ATOMIC_RELAXED; break;
    case WOORT_ATOMIC_MEMORY_ORDER_CONSUME: gcc_order = __ATOMIC_CONSUME; break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE: gcc_order = __ATOMIC_ACQUIRE; break;
    case WOORT_ATOMIC_MEMORY_ORDER_RELEASE: gcc_order = __ATOMIC_RELEASE; break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL: gcc_order = __ATOMIC_ACQ_REL; break;
    case WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST: gcc_order = __ATOMIC_SEQ_CST; break;
    }
    return __atomic_test_and_set(&flag->flag, gcc_order);
}

static inline void woort_atomic_flag_clear(woort_AtomicFlag* flag)
{
    __atomic_clear(&flag->flag, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_flag_clear_explicit(woort_AtomicFlag* flag, woort_atomic_MemoryOrder order)
{
    int gcc_order = __ATOMIC_SEQ_CST;
    switch (order)
    {
    case WOORT_ATOMIC_MEMORY_ORDER_RELAXED: gcc_order = __ATOMIC_RELAXED; break;
    case WOORT_ATOMIC_MEMORY_ORDER_CONSUME: gcc_order = __ATOMIC_CONSUME; break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE: gcc_order = __ATOMIC_ACQUIRE; break;
    case WOORT_ATOMIC_MEMORY_ORDER_RELEASE: gcc_order = __ATOMIC_RELEASE; break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL: gcc_order = __ATOMIC_ACQ_REL; break;
    case WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST: gcc_order = __ATOMIC_SEQ_CST; break;
    }
    __atomic_clear(&flag->flag, gcc_order);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* ============================================================================
 * woort_atomic_init - Initialize atomic variable (non-atomic operation)
 * ============================================================================ */

#define woort_atomic_init(obj, value) (*(obj) = (value))

/* ============================================================================
 * woort_atomic_store / woort_atomic_store_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline void woort_atomic_store_int8(woort_AtomicInt8* obj, int8_t value)
{
    _InterlockedExchange8((volatile char*)obj, (char)value);
}

static inline void woort_atomic_store_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    _InterlockedExchange8((volatile char*)obj, (char)value);
}

static inline void woort_atomic_store_int16(woort_AtomicInt16* obj, int16_t value)
{
    _InterlockedExchange16((volatile short*)obj, (short)value);
}

static inline void woort_atomic_store_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    _InterlockedExchange16((volatile short*)obj, (short)value);
}

static inline void woort_atomic_store_int32(woort_AtomicInt32* obj, int32_t value)
{
    _InterlockedExchange((volatile long*)obj, (long)value);
}

static inline void woort_atomic_store_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    _InterlockedExchange((volatile long*)obj, (long)value);
}

static inline void woort_atomic_store_int64(woort_AtomicInt64* obj, int64_t value)
{
    _InterlockedExchange64((volatile __int64*)obj, (__int64)value);
}

static inline void woort_atomic_store_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    _InterlockedExchange64((volatile __int64*)obj, (__int64)value);
}

static inline void woort_atomic_store_ptr(woort_AtomicPtr* obj, void* value)
{
    _InterlockedExchangePointer(obj, value);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline void woort_atomic_store_int8(woort_AtomicInt8* obj, int8_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_int16(woort_AtomicInt16* obj, int16_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_int32(woort_AtomicInt32* obj, int32_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_int64(woort_AtomicInt64* obj, int64_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

static inline void woort_atomic_store_ptr(woort_AtomicPtr* obj, void* value)
{
    __atomic_store_n(obj, value, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_store (without explicit memory order) */
#define woort_atomic_store(obj, value) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_store_int8, \
        woort_AtomicUInt8*:   woort_atomic_store_uint8, \
        woort_AtomicInt16*:   woort_atomic_store_int16, \
        woort_AtomicUInt16*:  woort_atomic_store_uint16, \
        woort_AtomicInt32*:   woort_atomic_store_int32, \
        woort_AtomicUInt32*:  woort_atomic_store_uint32, \
        woort_AtomicInt64*:   woort_atomic_store_int64, \
        woort_AtomicUInt64*:  woort_atomic_store_uint64, \
        woort_AtomicPtr*:     woort_atomic_store_ptr \
    )(obj, value)

/* woort_atomic_store_explicit with memory order (order is currently ignored for MSVC) */
#define woort_atomic_store_explicit(obj, value, order) \
    do { \
        woort_atomic_store(obj, value); \
        (void)(order); \
    } while(0)

/* ============================================================================
 * woort_atomic_load / woort_atomic_load_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int8_t woort_atomic_load_int8(woort_AtomicInt8* obj)
{
    return (int8_t)_InterlockedOr8((volatile char*)obj, 0);
}

static inline uint8_t woort_atomic_load_uint8(woort_AtomicUInt8* obj)
{
    return (uint8_t)_InterlockedOr8((volatile char*)obj, 0);
}

static inline int16_t woort_atomic_load_int16(woort_AtomicInt16* obj)
{
    return (int16_t)_InterlockedOr16((volatile short*)obj, 0);
}

static inline uint16_t woort_atomic_load_uint16(woort_AtomicUInt16* obj)
{
    return (uint16_t)_InterlockedOr16((volatile short*)obj, 0);
}

static inline int32_t woort_atomic_load_int32(woort_AtomicInt32* obj)
{
    return (int32_t)_InterlockedOr((volatile long*)obj, 0);
}

static inline uint32_t woort_atomic_load_uint32(woort_AtomicUInt32* obj)
{
    return (uint32_t)_InterlockedOr((volatile long*)obj, 0);
}

static inline int64_t woort_atomic_load_int64(woort_AtomicInt64* obj)
{
    return (int64_t)_InterlockedOr64((volatile __int64*)obj, 0);
}

static inline uint64_t woort_atomic_load_uint64(woort_AtomicUInt64* obj)
{
    return (uint64_t)_InterlockedOr64((volatile __int64*)obj, 0);
}

static inline void* woort_atomic_load_ptr(woort_AtomicPtr* obj)
{
    return (void*)_InterlockedCompareExchangePointer(obj, NULL, NULL);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int8_t woort_atomic_load_int8(woort_AtomicInt8* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline uint8_t woort_atomic_load_uint8(woort_AtomicUInt8* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline int16_t woort_atomic_load_int16(woort_AtomicInt16* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline uint16_t woort_atomic_load_uint16(woort_AtomicUInt16* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline int32_t woort_atomic_load_int32(woort_AtomicInt32* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline uint32_t woort_atomic_load_uint32(woort_AtomicUInt32* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline int64_t woort_atomic_load_int64(woort_AtomicInt64* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline uint64_t woort_atomic_load_uint64(woort_AtomicUInt64* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

static inline void* woort_atomic_load_ptr(woort_AtomicPtr* obj)
{
    return __atomic_load_n(obj, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_load (without explicit memory order) */
#define woort_atomic_load(obj) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_load_int8, \
        woort_AtomicUInt8*:   woort_atomic_load_uint8, \
        woort_AtomicInt16*:   woort_atomic_load_int16, \
        woort_AtomicUInt16*:  woort_atomic_load_uint16, \
        woort_AtomicInt32*:   woort_atomic_load_int32, \
        woort_AtomicUInt32*:  woort_atomic_load_uint32, \
        woort_AtomicInt64*:   woort_atomic_load_int64, \
        woort_AtomicUInt64*:  woort_atomic_load_uint64, \
        woort_AtomicPtr*:     woort_atomic_load_ptr \
    )(obj)

/* woort_atomic_load_explicit with memory order */
#define woort_atomic_load_explicit(obj, order) \
    (woort_atomic_load(obj) + 0 * (int)(order))

/* ============================================================================
 * woort_atomic_fetch_add / woort_atomic_fetch_add_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int8_t woort_atomic_fetch_add_int8(woort_AtomicInt8* obj, int8_t value)
{
    return (int8_t)_InterlockedExchangeAdd8((volatile char*)obj, (char)value);
}

static inline uint8_t woort_atomic_fetch_add_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return (uint8_t)_InterlockedExchangeAdd8((volatile char*)obj, (char)value);
}

static inline int16_t woort_atomic_fetch_add_int16(woort_AtomicInt16* obj, int16_t value)
{
    return (int16_t)_InterlockedExchangeAdd16((volatile short*)obj, (short)value);
}

static inline uint16_t woort_atomic_fetch_add_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return (uint16_t)_InterlockedExchangeAdd16((volatile short*)obj, (short)value);
}

static inline int32_t woort_atomic_fetch_add_int32(woort_AtomicInt32* obj, int32_t value)
{
    return (int32_t)_InterlockedExchangeAdd((volatile long*)obj, (long)value);
}

static inline uint32_t woort_atomic_fetch_add_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)obj, (long)value);
}

static inline int64_t woort_atomic_fetch_add_int64(woort_AtomicInt64* obj, int64_t value)
{
    return (int64_t)_InterlockedExchangeAdd64((volatile __int64*)obj, (__int64)value);
}

static inline uint64_t woort_atomic_fetch_add_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return (uint64_t)_InterlockedExchangeAdd64((volatile __int64*)obj, (__int64)value);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int8_t woort_atomic_fetch_add_int8(woort_AtomicInt8* obj, int8_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint8_t woort_atomic_fetch_add_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline int16_t woort_atomic_fetch_add_int16(woort_AtomicInt16* obj, int16_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint16_t woort_atomic_fetch_add_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline int32_t woort_atomic_fetch_add_int32(woort_AtomicInt32* obj, int32_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t woort_atomic_fetch_add_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline int64_t woort_atomic_fetch_add_int64(woort_AtomicInt64* obj, int64_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint64_t woort_atomic_fetch_add_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_fetch_add */
#define woort_atomic_fetch_add(obj, value) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_fetch_add_int8, \
        woort_AtomicUInt8*:   woort_atomic_fetch_add_uint8, \
        woort_AtomicInt16*:   woort_atomic_fetch_add_int16, \
        woort_AtomicUInt16*:  woort_atomic_fetch_add_uint16, \
        woort_AtomicInt32*:   woort_atomic_fetch_add_int32, \
        woort_AtomicUInt32*:  woort_atomic_fetch_add_uint32, \
        woort_AtomicInt64*:   woort_atomic_fetch_add_int64, \
        woort_AtomicUInt64*:  woort_atomic_fetch_add_uint64 \
    )(obj, value)

/* woort_atomic_fetch_add_explicit with memory order */
#define woort_atomic_fetch_add_explicit(obj, value, order) \
    (woort_atomic_fetch_add(obj, value) + 0 * (int)(order))

/* ============================================================================
 * woort_atomic_fetch_sub / woort_atomic_fetch_sub_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int8_t woort_atomic_fetch_sub_int8(woort_AtomicInt8* obj, int8_t value)
{
    return (int8_t)_InterlockedExchangeAdd8((volatile char*)obj, -(char)value);
}

static inline uint8_t woort_atomic_fetch_sub_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return (uint8_t)_InterlockedExchangeAdd8((volatile char*)obj, -(char)value);
}

static inline int16_t woort_atomic_fetch_sub_int16(woort_AtomicInt16* obj, int16_t value)
{
    return (int16_t)_InterlockedExchangeAdd16((volatile short*)obj, -(short)value);
}

static inline uint16_t woort_atomic_fetch_sub_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return (uint16_t)_InterlockedExchangeAdd16((volatile short*)obj, -(short)value);
}

static inline int32_t woort_atomic_fetch_sub_int32(woort_AtomicInt32* obj, int32_t value)
{
    return (int32_t)_InterlockedExchangeAdd((volatile long*)obj, -(long)value);
}

static inline uint32_t woort_atomic_fetch_sub_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)obj, -(long)value);
}

static inline int64_t woort_atomic_fetch_sub_int64(woort_AtomicInt64* obj, int64_t value)
{
    return (int64_t)_InterlockedExchangeAdd64((volatile __int64*)obj, -(__int64)value);
}

static inline uint64_t woort_atomic_fetch_sub_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return (uint64_t)_InterlockedExchangeAdd64((volatile __int64*)obj, -(__int64)value);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int8_t woort_atomic_fetch_sub_int8(woort_AtomicInt8* obj, int8_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint8_t woort_atomic_fetch_sub_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline int16_t woort_atomic_fetch_sub_int16(woort_AtomicInt16* obj, int16_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint16_t woort_atomic_fetch_sub_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline int32_t woort_atomic_fetch_sub_int32(woort_AtomicInt32* obj, int32_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t woort_atomic_fetch_sub_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline int64_t woort_atomic_fetch_sub_int64(woort_AtomicInt64* obj, int64_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint64_t woort_atomic_fetch_sub_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return __atomic_fetch_sub(obj, value, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_fetch_sub */
#define woort_atomic_fetch_sub(obj, value) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_fetch_sub_int8, \
        woort_AtomicUInt8*:   woort_atomic_fetch_sub_uint8, \
        woort_AtomicInt16*:   woort_atomic_fetch_sub_int16, \
        woort_AtomicUInt16*:  woort_atomic_fetch_sub_uint16, \
        woort_AtomicInt32*:   woort_atomic_fetch_sub_int32, \
        woort_AtomicUInt32*:  woort_atomic_fetch_sub_uint32, \
        woort_AtomicInt64*:   woort_atomic_fetch_sub_int64, \
        woort_AtomicUInt64*:  woort_atomic_fetch_sub_uint64 \
    )(obj, value)

/* woort_atomic_fetch_sub_explicit with memory order */
#define woort_atomic_fetch_sub_explicit(obj, value, order) \
    (woort_atomic_fetch_sub(obj, value) + 0 * (int)(order))

/* ============================================================================
 * woort_atomic_fetch_or / woort_atomic_fetch_or_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int8_t woort_atomic_fetch_or_int8(woort_AtomicInt8* obj, int8_t value)
{
    return (int8_t)_InterlockedOr8((volatile char*)obj, (char)value);
}

static inline uint8_t woort_atomic_fetch_or_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return (uint8_t)_InterlockedOr8((volatile char*)obj, (char)value);
}

static inline int16_t woort_atomic_fetch_or_int16(woort_AtomicInt16* obj, int16_t value)
{
    return (int16_t)_InterlockedOr16((volatile short*)obj, (short)value);
}

static inline uint16_t woort_atomic_fetch_or_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return (uint16_t)_InterlockedOr16((volatile short*)obj, (short)value);
}

static inline int32_t woort_atomic_fetch_or_int32(woort_AtomicInt32* obj, int32_t value)
{
    return (int32_t)_InterlockedOr((volatile long*)obj, (long)value);
}

static inline uint32_t woort_atomic_fetch_or_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return (uint32_t)_InterlockedOr((volatile long*)obj, (long)value);
}

static inline int64_t woort_atomic_fetch_or_int64(woort_AtomicInt64* obj, int64_t value)
{
    return (int64_t)_InterlockedOr64((volatile __int64*)obj, (__int64)value);
}

static inline uint64_t woort_atomic_fetch_or_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return (uint64_t)_InterlockedOr64((volatile __int64*)obj, (__int64)value);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int8_t woort_atomic_fetch_or_int8(woort_AtomicInt8* obj, int8_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint8_t woort_atomic_fetch_or_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline int16_t woort_atomic_fetch_or_int16(woort_AtomicInt16* obj, int16_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint16_t woort_atomic_fetch_or_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline int32_t woort_atomic_fetch_or_int32(woort_AtomicInt32* obj, int32_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t woort_atomic_fetch_or_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline int64_t woort_atomic_fetch_or_int64(woort_AtomicInt64* obj, int64_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint64_t woort_atomic_fetch_or_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return __atomic_fetch_or(obj, value, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_fetch_or */
#define woort_atomic_fetch_or(obj, value) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_fetch_or_int8, \
        woort_AtomicUInt8*:   woort_atomic_fetch_or_uint8, \
        woort_AtomicInt16*:   woort_atomic_fetch_or_int16, \
        woort_AtomicUInt16*:  woort_atomic_fetch_or_uint16, \
        woort_AtomicInt32*:   woort_atomic_fetch_or_int32, \
        woort_AtomicUInt32*:  woort_atomic_fetch_or_uint32, \
        woort_AtomicInt64*:   woort_atomic_fetch_or_int64, \
        woort_AtomicUInt64*:  woort_atomic_fetch_or_uint64 \
    )(obj, value)

/* woort_atomic_fetch_or_explicit with memory order */
#define woort_atomic_fetch_or_explicit(obj, value, order) \
    (woort_atomic_fetch_or(obj, value) + 0 * (int)(order))

/* ============================================================================
 * woort_atomic_fetch_xor / woort_atomic_fetch_xor_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int8_t woort_atomic_fetch_xor_int8(woort_AtomicInt8* obj, int8_t value)
{
    return (int8_t)_InterlockedXor8((volatile char*)obj, (char)value);
}

static inline uint8_t woort_atomic_fetch_xor_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return (uint8_t)_InterlockedXor8((volatile char*)obj, (char)value);
}

static inline int16_t woort_atomic_fetch_xor_int16(woort_AtomicInt16* obj, int16_t value)
{
    return (int16_t)_InterlockedXor16((volatile short*)obj, (short)value);
}

static inline uint16_t woort_atomic_fetch_xor_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return (uint16_t)_InterlockedXor16((volatile short*)obj, (short)value);
}

static inline int32_t woort_atomic_fetch_xor_int32(woort_AtomicInt32* obj, int32_t value)
{
    return (int32_t)_InterlockedXor((volatile long*)obj, (long)value);
}

static inline uint32_t woort_atomic_fetch_xor_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return (uint32_t)_InterlockedXor((volatile long*)obj, (long)value);
}

static inline int64_t woort_atomic_fetch_xor_int64(woort_AtomicInt64* obj, int64_t value)
{
    return (int64_t)_InterlockedXor64((volatile __int64*)obj, (__int64)value);
}

static inline uint64_t woort_atomic_fetch_xor_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return (uint64_t)_InterlockedXor64((volatile __int64*)obj, (__int64)value);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int8_t woort_atomic_fetch_xor_int8(woort_AtomicInt8* obj, int8_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint8_t woort_atomic_fetch_xor_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline int16_t woort_atomic_fetch_xor_int16(woort_AtomicInt16* obj, int16_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint16_t woort_atomic_fetch_xor_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline int32_t woort_atomic_fetch_xor_int32(woort_AtomicInt32* obj, int32_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t woort_atomic_fetch_xor_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline int64_t woort_atomic_fetch_xor_int64(woort_AtomicInt64* obj, int64_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint64_t woort_atomic_fetch_xor_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return __atomic_fetch_xor(obj, value, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_fetch_xor */
#define woort_atomic_fetch_xor(obj, value) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_fetch_xor_int8, \
        woort_AtomicUInt8*:   woort_atomic_fetch_xor_uint8, \
        woort_AtomicInt16*:   woort_atomic_fetch_xor_int16, \
        woort_AtomicUInt16*:  woort_atomic_fetch_xor_uint16, \
        woort_AtomicInt32*:   woort_atomic_fetch_xor_int32, \
        woort_AtomicUInt32*:  woort_atomic_fetch_xor_uint32, \
        woort_AtomicInt64*:   woort_atomic_fetch_xor_int64, \
        woort_AtomicUInt64*:  woort_atomic_fetch_xor_uint64 \
    )(obj, value)

/* woort_atomic_fetch_xor_explicit with memory order */
#define woort_atomic_fetch_xor_explicit(obj, value, order) \
    (woort_atomic_fetch_xor(obj, value) + 0 * (int)(order))

/* ============================================================================
 * woort_atomic_fetch_and / woort_atomic_fetch_and_explicit
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int8_t woort_atomic_fetch_and_int8(woort_AtomicInt8* obj, int8_t value)
{
    return (int8_t)_InterlockedAnd8((volatile char*)obj, (char)value);
}

static inline uint8_t woort_atomic_fetch_and_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return (uint8_t)_InterlockedAnd8((volatile char*)obj, (char)value);
}

static inline int16_t woort_atomic_fetch_and_int16(woort_AtomicInt16* obj, int16_t value)
{
    return (int16_t)_InterlockedAnd16((volatile short*)obj, (short)value);
}

static inline uint16_t woort_atomic_fetch_and_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return (uint16_t)_InterlockedAnd16((volatile short*)obj, (short)value);
}

static inline int32_t woort_atomic_fetch_and_int32(woort_AtomicInt32* obj, int32_t value)
{
    return (int32_t)_InterlockedAnd((volatile long*)obj, (long)value);
}

static inline uint32_t woort_atomic_fetch_and_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return (uint32_t)_InterlockedAnd((volatile long*)obj, (long)value);
}

static inline int64_t woort_atomic_fetch_and_int64(woort_AtomicInt64* obj, int64_t value)
{
    return (int64_t)_InterlockedAnd64((volatile __int64*)obj, (__int64)value);
}

static inline uint64_t woort_atomic_fetch_and_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return (uint64_t)_InterlockedAnd64((volatile __int64*)obj, (__int64)value);
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int8_t woort_atomic_fetch_and_int8(woort_AtomicInt8* obj, int8_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint8_t woort_atomic_fetch_and_uint8(woort_AtomicUInt8* obj, uint8_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline int16_t woort_atomic_fetch_and_int16(woort_AtomicInt16* obj, int16_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint16_t woort_atomic_fetch_and_uint16(woort_AtomicUInt16* obj, uint16_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline int32_t woort_atomic_fetch_and_int32(woort_AtomicInt32* obj, int32_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint32_t woort_atomic_fetch_and_uint32(woort_AtomicUInt32* obj, uint32_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline int64_t woort_atomic_fetch_and_int64(woort_AtomicInt64* obj, int64_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

static inline uint64_t woort_atomic_fetch_and_uint64(woort_AtomicUInt64* obj, uint64_t value)
{
    return __atomic_fetch_and(obj, value, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_fetch_and */
#define woort_atomic_fetch_and(obj, value) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_fetch_and_int8, \
        woort_AtomicUInt8*:   woort_atomic_fetch_and_uint8, \
        woort_AtomicInt16*:   woort_atomic_fetch_and_int16, \
        woort_AtomicUInt16*:  woort_atomic_fetch_and_uint16, \
        woort_AtomicInt32*:   woort_atomic_fetch_and_int32, \
        woort_AtomicUInt32*:  woort_atomic_fetch_and_uint32, \
        woort_AtomicInt64*:   woort_atomic_fetch_and_int64, \
        woort_AtomicUInt64*:  woort_atomic_fetch_and_uint64 \
    )(obj, value)

/* woort_atomic_fetch_and_explicit with memory order */
#define woort_atomic_fetch_and_explicit(obj, value, order) \
    (woort_atomic_fetch_and(obj, value) + 0 * (int)(order))

/* ============================================================================
 * woort_atomic_compare_exchange_strong / weak
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

static inline int woort_atomic_compare_exchange_strong_int8(woort_AtomicInt8* obj, int8_t* expected, int8_t desired)
{
    int8_t exp = *expected;
    int8_t old = (int8_t)_InterlockedCompareExchange8((volatile char*)obj, (char)desired, (char)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_uint8(woort_AtomicUInt8* obj, uint8_t* expected, uint8_t desired)
{
    uint8_t exp = *expected;
    uint8_t old = (uint8_t)_InterlockedCompareExchange8((volatile char*)obj, (char)desired, (char)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_int16(woort_AtomicInt16* obj, int16_t* expected, int16_t desired)
{
    int16_t exp = *expected;
    int16_t old = (int16_t)_InterlockedCompareExchange16((volatile short*)obj, (short)desired, (short)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_uint16(woort_AtomicUInt16* obj, uint16_t* expected, uint16_t desired)
{
    uint16_t exp = *expected;
    uint16_t old = (uint16_t)_InterlockedCompareExchange16((volatile short*)obj, (short)desired, (short)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_int32(woort_AtomicInt32* obj, int32_t* expected, int32_t desired)
{
    int32_t exp = *expected;
    int32_t old = (int32_t)_InterlockedCompareExchange((volatile long*)obj, (long)desired, (long)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_uint32(woort_AtomicUInt32* obj, uint32_t* expected, uint32_t desired)
{
    uint32_t exp = *expected;
    uint32_t old = (uint32_t)_InterlockedCompareExchange((volatile long*)obj, (long)desired, (long)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_int64(woort_AtomicInt64* obj, int64_t* expected, int64_t desired)
{
    int64_t exp = *expected;
    int64_t old = (int64_t)_InterlockedCompareExchange64((volatile __int64*)obj, (__int64)desired, (__int64)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_uint64(woort_AtomicUInt64* obj, uint64_t* expected, uint64_t desired)
{
    uint64_t exp = *expected;
    uint64_t old = (uint64_t)_InterlockedCompareExchange64((volatile __int64*)obj, (__int64)desired, (__int64)exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

static inline int woort_atomic_compare_exchange_strong_ptr(woort_AtomicPtr* obj, void** expected, void* desired)
{
    void* exp = *expected;
    void* old = _InterlockedCompareExchangePointer(obj, desired, exp);
    if (old == exp) {
        return 1;
    } else {
        *expected = old;
        return 0;
    }
}

#elif defined(WOORT_ATOMIC_GCC)

static inline int woort_atomic_compare_exchange_strong_int8(woort_AtomicInt8* obj, int8_t* expected, int8_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_uint8(woort_AtomicUInt8* obj, uint8_t* expected, uint8_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_int16(woort_AtomicInt16* obj, int16_t* expected, int16_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_uint16(woort_AtomicUInt16* obj, uint16_t* expected, uint16_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_int32(woort_AtomicInt32* obj, int32_t* expected, int32_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_uint32(woort_AtomicUInt32* obj, uint32_t* expected, uint32_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_int64(woort_AtomicInt64* obj, int64_t* expected, int64_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_uint64(woort_AtomicUInt64* obj, uint64_t* expected, uint64_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_strong_ptr(woort_AtomicPtr* obj, void** expected, void* desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_compare_exchange_strong */
#define woort_atomic_compare_exchange_strong(obj, expected, desired) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_compare_exchange_strong_int8, \
        woort_AtomicUInt8*:   woort_atomic_compare_exchange_strong_uint8, \
        woort_AtomicInt16*:   woort_atomic_compare_exchange_strong_int16, \
        woort_AtomicUInt16*:  woort_atomic_compare_exchange_strong_uint16, \
        woort_AtomicInt32*:   woort_atomic_compare_exchange_strong_int32, \
        woort_AtomicUInt32*:  woort_atomic_compare_exchange_strong_uint32, \
        woort_AtomicInt64*:   woort_atomic_compare_exchange_strong_int64, \
        woort_AtomicUInt64*:  woort_atomic_compare_exchange_strong_uint64, \
        woort_AtomicPtr*:     woort_atomic_compare_exchange_strong_ptr \
    )(obj, expected, desired)

/* woort_atomic_compare_exchange_strong_explicit with memory order */
#define woort_atomic_compare_exchange_strong_explicit(obj, expected, desired, success, failure) \
    (woort_atomic_compare_exchange_strong(obj, expected, desired) + 0 * ((int)(success) + (int)(failure)))

/* ============================================================================
 * woort_atomic_compare_exchange_weak
 * Note: On MSVC, weak is the same as strong. On GCC, weak allows spurious failures.
 * ============================================================================ */

#if defined(WOORT_ATOMIC_MSVC)

/* On MSVC, weak CAS is the same as strong CAS */
#define woort_atomic_compare_exchange_weak_int8    woort_atomic_compare_exchange_strong_int8
#define woort_atomic_compare_exchange_weak_uint8   woort_atomic_compare_exchange_strong_uint8
#define woort_atomic_compare_exchange_weak_int16   woort_atomic_compare_exchange_strong_int16
#define woort_atomic_compare_exchange_weak_uint16  woort_atomic_compare_exchange_strong_uint16
#define woort_atomic_compare_exchange_weak_int32   woort_atomic_compare_exchange_strong_int32
#define woort_atomic_compare_exchange_weak_uint32  woort_atomic_compare_exchange_strong_uint32
#define woort_atomic_compare_exchange_weak_int64   woort_atomic_compare_exchange_strong_int64
#define woort_atomic_compare_exchange_weak_uint64  woort_atomic_compare_exchange_strong_uint64
#define woort_atomic_compare_exchange_weak_ptr     woort_atomic_compare_exchange_strong_ptr

#elif defined(WOORT_ATOMIC_GCC)

static inline int woort_atomic_compare_exchange_weak_int8(woort_AtomicInt8* obj, int8_t* expected, int8_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_uint8(woort_AtomicUInt8* obj, uint8_t* expected, uint8_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_int16(woort_AtomicInt16* obj, int16_t* expected, int16_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_uint16(woort_AtomicUInt16* obj, uint16_t* expected, uint16_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_int32(woort_AtomicInt32* obj, int32_t* expected, int32_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_uint32(woort_AtomicUInt32* obj, uint32_t* expected, uint32_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_int64(woort_AtomicInt64* obj, int64_t* expected, int64_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_uint64(woort_AtomicUInt64* obj, uint64_t* expected, uint64_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int woort_atomic_compare_exchange_weak_ptr(woort_AtomicPtr* obj, void** expected, void* desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif /* WOORT_ATOMIC_MSVC / WOORT_ATOMIC_GCC */

/* Generic macro for woort_atomic_compare_exchange_weak */
#define woort_atomic_compare_exchange_weak(obj, expected, desired) \
    _Generic((obj), \
        woort_AtomicInt8*:    woort_atomic_compare_exchange_weak_int8, \
        woort_AtomicUInt8*:   woort_atomic_compare_exchange_weak_uint8, \
        woort_AtomicInt16*:   woort_atomic_compare_exchange_weak_int16, \
        woort_AtomicUInt16*:  woort_atomic_compare_exchange_weak_uint16, \
        woort_AtomicInt32*:   woort_atomic_compare_exchange_weak_int32, \
        woort_AtomicUInt32*:  woort_atomic_compare_exchange_weak_uint32, \
        woort_AtomicInt64*:   woort_atomic_compare_exchange_weak_int64, \
        woort_AtomicUInt64*:  woort_atomic_compare_exchange_weak_uint64, \
        woort_AtomicPtr*:     woort_atomic_compare_exchange_weak_ptr \
    )(obj, expected, desired)

/* woort_atomic_compare_exchange_weak_explicit with memory order */
#define woort_atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    (woort_atomic_compare_exchange_weak(obj, expected, desired) + 0 * ((int)(success) + (int)(failure)))

/* ============================================================================
 * woort_atomic_thread_fence / woort_atomic_signal_fence
 * ============================================================================ */

static inline void woort_atomic_thread_fence(woort_atomic_MemoryOrder order)
{
#if defined(WOORT_ATOMIC_MSVC)
    (void)order;
#   if defined(_M_IX86) || defined(_M_X64)
    _mm_mfence();
#   elif defined(_M_ARM) || defined(_M_ARM64)
    __dmb(_ARM_BARRIER_ISH);
#   endif
#elif defined(WOORT_ATOMIC_GCC)
    switch (order)
    {
    case WOORT_ATOMIC_MEMORY_ORDER_RELAXED:
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_CONSUME:
    case WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE:
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_RELEASE:
        __atomic_thread_fence(__ATOMIC_RELEASE);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL:
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST:
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        break;
    }
#endif
}

static inline void woort_atomic_signal_fence(woort_atomic_MemoryOrder order)
{
#if defined(WOORT_ATOMIC_MSVC)
    (void)order;
    _ReadWriteBarrier();
#elif defined(WOORT_ATOMIC_GCC)
    switch (order)
    {
    case WOORT_ATOMIC_MEMORY_ORDER_RELAXED:
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_CONSUME:
    case WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE:
        __atomic_signal_fence(__ATOMIC_ACQUIRE);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_RELEASE:
        __atomic_signal_fence(__ATOMIC_RELEASE);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL:
        __atomic_signal_fence(__ATOMIC_ACQ_REL);
        break;
    case WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST:
        __atomic_signal_fence(__ATOMIC_SEQ_CST);
        break;
    }
#endif
}

#else
#   include <stdatomic.h>

#   define woort_Atomic _Atomic

typedef atomic_flag woort_AtomicFlag;

#   define WOORT_ATOMIC_FLAG_INIT ATOMIC_FLAG_INIT

#   define woort_atomic_flag_test_and_set atomic_flag_test_and_set
#   define woort_atomic_flag_test_and_set_explicit atomic_flag_test_and_set_explicit
#   define woort_atomic_flag_clear atomic_flag_clear
#   define woort_atomic_flag_clear_explicit atomic_flag_clear_explicit

typedef memory_order woort_atomic_MemoryOrder;

#   define WOORT_ATOMIC_MEMORY_ORDER_RELAXED memory_order_relaxed
#   define WOORT_ATOMIC_MEMORY_ORDER_CONSUME memory_order_consume
#   define WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE memory_order_acquire
#   define WOORT_ATOMIC_MEMORY_ORDER_RELEASE memory_order_release
#   define WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL memory_order_acq_rel
#   define WOORT_ATOMIC_MEMORY_ORDER_SEQ_CST memory_order_seq_cst

#   define woort_atomic_init atomic_init
#   define woort_atomic_store atomic_store
#   define woort_atomic_store_explicit atomic_store_explicit
#   define woort_atomic_load atomic_load
#   define woort_atomic_load_explicit atomic_load_explicit
#   define woort_atomic_fetch_add atomic_fetch_add
#   define woort_atomic_fetch_add_explicit atomic_fetch_add_explicit
#   define woort_atomic_fetch_sub atomic_fetch_sub
#   define woort_atomic_fetch_sub_explicit atomic_fetch_sub_explicit
#   define woort_atomic_fetch_or atomic_fetch_or
#   define woort_atomic_fetch_or_explicit atomic_fetch_or_explicit
#   define woort_atomic_fetch_xor atomic_fetch_xor
#   define woort_atomic_fetch_xor_explicit atomic_fetch_xor_explicit
#   define woort_atomic_fetch_and atomic_fetch_and
#   define woort_atomic_fetch_and_explicit atomic_fetch_and_explicit
#   define woort_atomic_compare_exchange_strong atomic_compare_exchange_strong
#   define woort_atomic_compare_exchange_strong_explicit atomic_compare_exchange_strong_explicit
#   define woort_atomic_compare_exchange_weak atomic_compare_exchange_weak
#   define woort_atomic_compare_exchange_weak_explicit atomic_compare_exchange_weak_explicit

#   define woort_atomic_thread_fence atomic_thread_fence
#   define woort_atomic_signal_fence atomic_signal_fence
#endif

