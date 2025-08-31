/*
 * c23_compat.h - C23 Compatibility Layer for μEmacs
 * 
 * Provides C23 features with fallbacks for older compilers
 */

#ifndef C23_COMPAT_H
#define C23_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* C23 feature detection */
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 202311L
#define HAVE_C23 1
#else
#define HAVE_C23 0
#endif
#else
#define HAVE_C23 0
#endif

/* typeof support */
#if HAVE_C23
#define TYPEOF(x) typeof(x)
#elif defined(__GNUC__) || defined(__clang__)
#define TYPEOF(x) __typeof__(x)
#else
#define TYPEOF(x) void*  /* Fallback - loses type safety */
#endif

/* Type-safe min/max functions using _Generic */
static inline int safe_min_int(int a, int b) { return a < b ? a : b; }
static inline long safe_min_long(long a, long b) { return a < b ? a : b; }
static inline size_t safe_min_size_t(size_t a, size_t b) { return a < b ? a : b; }
#define SAFE_MIN(a, b) _Generic((a), \
    int: safe_min_int, \
    long: safe_min_long, \
    size_t: safe_min_size_t \
)(a, b)

static inline int safe_max_int(int a, int b) { return a > b ? a : b; }
static inline long safe_max_long(long a, long b) { return a > b ? a : b; }
static inline size_t safe_max_size_t(size_t a, size_t b) { return a > b ? a : b; }
#define SAFE_MAX(a, b) _Generic((a), \
    int: safe_max_int, \
    long: safe_max_long, \
    size_t: safe_max_size_t \
)(a, b)

/* Type-safe swap macro */
#define SAFE_SWAP(a, b) do { \
    TYPEOF(a) _temp = (a); \
    (a) = (b); \
    (b) = _temp; \
} while(0)

/* _BitInt support for efficient bit manipulation */
#if HAVE_C23 && defined(__BITINT_MAXWIDTH__)
#define HAVE_BITINT 1
typedef _BitInt(8) bitint8_t;
typedef _BitInt(16) bitint16_t; 
typedef _BitInt(32) bitint32_t;
#else
#define HAVE_BITINT 0
typedef uint8_t bitint8_t;
typedef uint16_t bitint16_t;
typedef uint32_t bitint32_t;
#endif

/* Bit manipulation helpers */
#define BIT_SET(val, bit)    ((val) |= (1U << (bit)))
#define BIT_CLEAR(val, bit)  ((val) &= ~(1U << (bit)))
#define BIT_TOGGLE(val, bit) ((val) ^= (1U << (bit)))
#define BIT_TEST(val, bit)   (((val) >> (bit)) & 1U)

/* Population count with fallback */
#if HAVE_C23 || defined(__GNUC__)
#define POPCOUNT(x) __builtin_popcount(x)
#else
static inline int popcount_fallback(unsigned int x) {
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}
#define POPCOUNT(x) popcount_fallback(x)
#endif

/* Alignment macros */
#if HAVE_C23
#define ALIGN_AS(type) _Alignas(type)
#define ALIGN_TO(n) _Alignas(n)
#elif defined(__GNUC__) || defined(__clang__)
#define ALIGN_AS(type) __attribute__((aligned(__alignof__(type))))
#define ALIGN_TO(n) __attribute__((aligned(n)))
#else
#define ALIGN_AS(type)  /* No alignment control */
#define ALIGN_TO(n)     /* No alignment control */
#endif

/* Safe character case conversion with bounds checking */
static inline int safe_chcase(int c) {
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? (c ^ 0x20) : c;
}
#define SAFE_CHCASE(c) safe_chcase(c)

/* C23 typed enum fallback - compiler compatibility issues detected */
// Standard enum with typedef for compatibility
#define TYPED_ENUM(name, type, ...) \
    enum name { __VA_ARGS__ }; \
    typedef enum name name##_t;

#define ENUM_HAS_FLAG(value, flag) ((value) & (flag))
#define ENUM_SET_FLAG(value, flag) ((value) |= (flag))
#define ENUM_CLEAR_FLAG(value, flag) ((value) &= ~(flag))

#endif /* C23_COMPAT_H */