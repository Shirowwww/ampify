/* stdatomic.h for MSVC, used only by the Atomics code of quickjs.c (see CMakeLists.txt). MSVC has no C11 atomics
   in every version, so this header implements the functions that quickjs.c calls, for objects of 1, 2, 4 and 8
   bytes, with the Interlocked intrinsics. It is not a general implementation of the header. */
#ifndef AMK_MSVC_STDATOMIC_H
#define AMK_MSVC_STDATOMIC_H

#include <intrin.h>
#include <stddef.h>

#define _Atomic(type) type

static inline unsigned long long amk_atomic_load(void* object, size_t size) {
    switch (size) {
        case 1: return (unsigned char)_InterlockedOr8((volatile char*)object, 0);
        case 2: return (unsigned short)_InterlockedOr16((volatile short*)object, 0);
        case 4: return (unsigned long)_InterlockedOr((volatile long*)object, 0);
        default: return (unsigned long long)_InterlockedOr64((volatile __int64*)object, 0);
    }
}

static inline unsigned long long amk_atomic_exchange(void* object, unsigned long long value, size_t size) {
    switch (size) {
        case 1: return (unsigned char)_InterlockedExchange8((volatile char*)object, (char)value);
        case 2: return (unsigned short)_InterlockedExchange16((volatile short*)object, (short)value);
        case 4: return (unsigned long)_InterlockedExchange((volatile long*)object, (long)value);
        default: return (unsigned long long)_InterlockedExchange64((volatile __int64*)object, (__int64)value);
    }
}

static inline unsigned long long amk_atomic_fetch_add(void* object, unsigned long long operand, size_t size) {
    switch (size) {
        case 1: return (unsigned char)_InterlockedExchangeAdd8((volatile char*)object, (char)operand);
        case 2: return (unsigned short)_InterlockedExchangeAdd16((volatile short*)object, (short)operand);
        case 4: return (unsigned long)_InterlockedExchangeAdd((volatile long*)object, (long)operand);
        default: return (unsigned long long)_InterlockedExchangeAdd64((volatile __int64*)object, (__int64)operand);
    }
}

static inline unsigned long long amk_atomic_fetch_and(void* object, unsigned long long operand, size_t size) {
    switch (size) {
        case 1: return (unsigned char)_InterlockedAnd8((volatile char*)object, (char)operand);
        case 2: return (unsigned short)_InterlockedAnd16((volatile short*)object, (short)operand);
        case 4: return (unsigned long)_InterlockedAnd((volatile long*)object, (long)operand);
        default: return (unsigned long long)_InterlockedAnd64((volatile __int64*)object, (__int64)operand);
    }
}

static inline unsigned long long amk_atomic_fetch_or(void* object, unsigned long long operand, size_t size) {
    switch (size) {
        case 1: return (unsigned char)_InterlockedOr8((volatile char*)object, (char)operand);
        case 2: return (unsigned short)_InterlockedOr16((volatile short*)object, (short)operand);
        case 4: return (unsigned long)_InterlockedOr((volatile long*)object, (long)operand);
        default: return (unsigned long long)_InterlockedOr64((volatile __int64*)object, (__int64)operand);
    }
}

static inline unsigned long long amk_atomic_fetch_xor(void* object, unsigned long long operand, size_t size) {
    switch (size) {
        case 1: return (unsigned char)_InterlockedXor8((volatile char*)object, (char)operand);
        case 2: return (unsigned short)_InterlockedXor16((volatile short*)object, (short)operand);
        case 4: return (unsigned long)_InterlockedXor((volatile long*)object, (long)operand);
        default: return (unsigned long long)_InterlockedXor64((volatile __int64*)object, (__int64)operand);
    }
}

// Stores `desired` if the object still holds `*expected`. Otherwise copies the current value into `*expected`.
static inline int amk_atomic_compare_exchange(void* object, void* expected, unsigned long long desired, size_t size) {
    switch (size) {
        case 1: {
            char wanted = *(char*)expected;
            char seen = _InterlockedCompareExchange8((volatile char*)object, (char)desired, wanted);
            if (seen == wanted) return 1;
            *(char*)expected = seen;
            return 0;
        }
        case 2: {
            short wanted = *(short*)expected;
            short seen = _InterlockedCompareExchange16((volatile short*)object, (short)desired, wanted);
            if (seen == wanted) return 1;
            *(short*)expected = seen;
            return 0;
        }
        case 4: {
            long wanted = *(long*)expected;
            long seen = _InterlockedCompareExchange((volatile long*)object, (long)desired, wanted);
            if (seen == wanted) return 1;
            *(long*)expected = seen;
            return 0;
        }
        default: {
            __int64 wanted = *(__int64*)expected;
            __int64 seen = _InterlockedCompareExchange64((volatile __int64*)object, (__int64)desired, wanted);
            if (seen == wanted) return 1;
            *(__int64*)expected = seen;
            return 0;
        }
    }
}

#define atomic_load(object) amk_atomic_load((void*)(object), sizeof(*(object)))
#define atomic_exchange(object, desired) \
    amk_atomic_exchange((void*)(object), (unsigned long long)(desired), sizeof(*(object)))
#define atomic_store(object, desired) \
    ((void)amk_atomic_exchange((void*)(object), (unsigned long long)(desired), sizeof(*(object))))
#define atomic_fetch_add(object, operand) \
    amk_atomic_fetch_add((void*)(object), (unsigned long long)(operand), sizeof(*(object)))
#define atomic_fetch_sub(object, operand) \
    amk_atomic_fetch_add((void*)(object), 0ULL - (unsigned long long)(operand), sizeof(*(object)))
#define atomic_fetch_and(object, operand) \
    amk_atomic_fetch_and((void*)(object), (unsigned long long)(operand), sizeof(*(object)))
#define atomic_fetch_or(object, operand) \
    amk_atomic_fetch_or((void*)(object), (unsigned long long)(operand), sizeof(*(object)))
#define atomic_fetch_xor(object, operand) \
    amk_atomic_fetch_xor((void*)(object), (unsigned long long)(operand), sizeof(*(object)))
#define atomic_compare_exchange_strong(object, expected, desired) \
    amk_atomic_compare_exchange((void*)(object), (void*)(expected), (unsigned long long)(desired), sizeof(*(object)))

#endif  // AMK_MSVC_STDATOMIC_H
