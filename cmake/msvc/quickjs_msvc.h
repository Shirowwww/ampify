/* Force-included in the QuickJS sources when they are built with MSVC (see CMakeLists.txt). QuickJS uses GCC
   extensions that MSVC does not have: attributes, __builtin_expect, and the bit scans behind clz and ctz. They are
   mapped here, so the vendored files in vendor/quickjs stay unchanged. The bit scans need an x64 target. */
#ifndef AMK_QUICKJS_MSVC_H
#define AMK_QUICKJS_MSVC_H

#include <intrin.h>

#define __attribute__(attributes)
#define __builtin_expect(value, expected) (value)
#define __builtin_frame_address(level) _AddressOfReturnAddress()
#define __builtin_clz(value) amk_msvc_clz32(value)
#define __builtin_clzll(value) amk_msvc_clz64(value)
#define __builtin_ctz(value) amk_msvc_ctz32(value)
#define __builtin_ctzll(value) amk_msvc_ctz64(value)

static inline int amk_msvc_clz32(unsigned int a) {
    unsigned long index;
    return _BitScanReverse(&index, a) ? 31 - (int)index : 32;
}

static inline int amk_msvc_clz64(unsigned long long a) {
    unsigned long index;
    return _BitScanReverse64(&index, a) ? 63 - (int)index : 64;
}

static inline int amk_msvc_ctz32(unsigned int a) {
    unsigned long index;
    return _BitScanForward(&index, a) ? (int)index : 32;
}

static inline int amk_msvc_ctz64(unsigned long long a) {
    unsigned long index;
    return _BitScanForward64(&index, a) ? (int)index : 64;
}

#endif  // AMK_QUICKJS_MSVC_H
