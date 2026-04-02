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

/*
 * C23 [[reproducible]] and [[unsequenced]] function attributes
 * Use GCC/Clang builtins — [[reproducible]]/[[unsequenced]] are C23 but
 * GCC/Clang don't support them yet (as of GCC 14, Clang 18).
 */
#if defined(__GNUC__) || defined(__clang__)
#define REPRODUCIBLE __attribute__((pure))
#define UNSEQUENCED __attribute__((const))
#else
#define REPRODUCIBLE
#define UNSEQUENCED
#endif

/*
 * C23 constexpr support - compile-time evaluation
 * Like OUROBOROS/montauk C++23 constexpr usage
 */
#if HAVE_C23
#define CONSTEXPR constexpr
#else
#define CONSTEXPR static const
#endif

/*
 * C23 [[nodiscard]] attribute - warn if return value ignored
 * Critical for functions returning error codes or allocated memory
 */
#if HAVE_C23 || (defined(__GNUC__) && __GNUC__ >= 4)
#define NODISCARD [[nodiscard]]
#define NODISCARD_MSG(msg) [[nodiscard(msg)]]
#elif defined(__clang__)
#define NODISCARD [[nodiscard]]
#define NODISCARD_MSG(msg) [[nodiscard]]
#else
#define NODISCARD
#define NODISCARD_MSG(msg)
#endif

/*
 * C23 [[maybe_unused]] - suppress unused warnings
 */
#if HAVE_C23 || defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED [[maybe_unused]]
#else
#define MAYBE_UNUSED
#endif

/*
 * C23 [[deprecated]] attribute
 */
#if HAVE_C23 || defined(__GNUC__) || defined(__clang__)
#define DEPRECATED [[deprecated]]
#define DEPRECATED_MSG(msg) [[deprecated(msg)]]
#else
#define DEPRECATED
#define DEPRECATED_MSG(msg)
#endif

/*
 * C23 nullptr constant - already a keyword in C23
 * Fallback for pre-C23
 */
#if !HAVE_C23 && !defined(__cplusplus)
#ifndef nullptr
#define nullptr ((void*)0)
#endif
#endif

/*
 * C23 bool/true/false are keywords - no stdbool.h needed
 * But keep including for compatibility
 */
#include <stdbool.h>

/*
 * C23 unreachable() - optimization hint for impossible code paths
 */
#if HAVE_C23
#include <stddef.h>
/* unreachable() is in <stddef.h> in C23 */
#elif defined(__GNUC__) || defined(__clang__)
#define unreachable() __builtin_unreachable()
#else
#define unreachable() do { } while(0)
#endif

/*
 * Static assertion - C11/C23 compatible
 */
#if HAVE_C23
/* static_assert is a keyword in C23 */
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* C11 has _Static_assert */
#define static_assert _Static_assert
#else
/* Fallback for older compilers */
#define static_assert(cond, msg) typedef char static_assertion_##__LINE__[(cond)?1:-1]
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
UNSEQUENCED static inline int safe_min_int(int a, int b) { return a < b ? a : b; }
UNSEQUENCED static inline long safe_min_long(long a, long b) { return a < b ? a : b; }
UNSEQUENCED static inline size_t safe_min_size_t(size_t a, size_t b) { return a < b ? a : b; }
#define SAFE_MIN(a, b) _Generic((a), \
    int: safe_min_int, \
    long: safe_min_long, \
    size_t: safe_min_size_t \
)(a, b)

UNSEQUENCED static inline int safe_max_int(int a, int b) { return a > b ? a : b; }
UNSEQUENCED static inline long safe_max_long(long a, long b) { return a > b ? a : b; }
UNSEQUENCED static inline size_t safe_max_size_t(size_t a, size_t b) { return a > b ? a : b; }
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

/*
 * =============================================================================
 * C23 <stdckdint.h> - Checked Integer Arithmetic
 * =============================================================================
 * Provides overflow-safe arithmetic: ckd_add, ckd_sub, ckd_mul
 * Returns true on overflow, false on success. Result stored in first arg.
 *
 * Source: https://gustedt.wordpress.com/2022/12/18/checked-integer-arithmetic-in-the-prospect-of-c23/
 */
#if HAVE_C23 && __has_include(<stdckdint.h>)
#include <stdckdint.h>
#define HAVE_STDCKDINT 1
#else
#define HAVE_STDCKDINT 0
/* Fallback using GCC/Clang builtins (available since GCC 5, Clang 3.8) */
#if defined(__GNUC__) && (__GNUC__ >= 5 || (defined(__clang__) && __clang_major__ >= 4))
#define ckd_add(result, a, b) __builtin_add_overflow((a), (b), (result))
#define ckd_sub(result, a, b) __builtin_sub_overflow((a), (b), (result))
#define ckd_mul(result, a, b) __builtin_mul_overflow((a), (b), (result))
#else
/* Manual fallback for non-GCC/Clang compilers */
static inline bool ckd_add_size(size_t *result, size_t a, size_t b) {
    if (a > SIZE_MAX - b) return true;
    *result = a + b;
    return false;
}
static inline bool ckd_mul_size(size_t *result, size_t a, size_t b) {
    if (a != 0 && b > SIZE_MAX / a) return true;
    *result = a * b;
    return false;
}
#define ckd_add(result, a, b) ckd_add_size((result), (a), (b))
#define ckd_sub(result, a, b) ((*(result) = (a) - (b)), ((a) < (b)))
#define ckd_mul(result, a, b) ckd_mul_size((result), (a), (b))
#endif
#endif

/*
 * =============================================================================
 * C23 <stdbit.h> - Bit Manipulation Functions
 * =============================================================================
 * Standardized bit operations: count_ones, leading_zeros, trailing_zeros, etc.
 *
 * Source: https://en.cppreference.com/w/c/header/stdbit.html
 */
#if HAVE_C23 && __has_include(<stdbit.h>)
#include <stdbit.h>
#define HAVE_STDBIT 1
#else
#define HAVE_STDBIT 0
/* Fallback using GCC/Clang builtins */
#if defined(__GNUC__) || defined(__clang__)
/* stdc_count_ones - population count */
#define stdc_count_ones_ui(x) ((unsigned int)__builtin_popcount(x))
#define stdc_count_ones_ul(x) ((unsigned int)__builtin_popcountl(x))
#define stdc_count_ones_ull(x) ((unsigned int)__builtin_popcountll(x))

/* stdc_leading_zeros - count leading zeros */
#define stdc_leading_zeros_ui(x) ((x) ? (unsigned int)__builtin_clz(x) : 32u)
#define stdc_leading_zeros_ul(x) ((x) ? (unsigned int)__builtin_clzl(x) : (unsigned int)(sizeof(unsigned long) * 8))
#define stdc_leading_zeros_ull(x) ((x) ? (unsigned int)__builtin_clzll(x) : 64u)

/* stdc_trailing_zeros - count trailing zeros */
#define stdc_trailing_zeros_ui(x) ((x) ? (unsigned int)__builtin_ctz(x) : 32u)
#define stdc_trailing_zeros_ul(x) ((x) ? (unsigned int)__builtin_ctzl(x) : (unsigned int)(sizeof(unsigned long) * 8))
#define stdc_trailing_zeros_ull(x) ((x) ? (unsigned int)__builtin_ctzll(x) : 64u)

/* stdc_has_single_bit - check if power of 2 */
#define stdc_has_single_bit_ui(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define stdc_has_single_bit_ul(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)

/* stdc_bit_width - minimum bits needed to represent value */
#define stdc_bit_width_ui(x) ((x) ? (32u - stdc_leading_zeros_ui(x)) : 0u)
#define stdc_bit_width_ul(x) ((x) ? ((unsigned int)(sizeof(unsigned long) * 8) - stdc_leading_zeros_ul(x)) : 0u)

/* stdc_bit_floor - largest power of 2 not greater than x */
#define stdc_bit_floor_ui(x) ((x) ? (1u << (stdc_bit_width_ui(x) - 1)) : 0u)

/* stdc_bit_ceil - smallest power of 2 not less than x */
static inline unsigned int stdc_bit_ceil_ui_impl(unsigned int x) {
    if (x <= 1) return 1;
    unsigned int w = stdc_bit_width_ui(x - 1);
    return 1u << w;
}
#define stdc_bit_ceil_ui(x) stdc_bit_ceil_ui_impl(x)

#else
/* Pure C fallbacks for non-GCC/Clang compilers */
static inline unsigned int stdc_count_ones_ui_fallback(unsigned int x) {
    unsigned int count = 0;
    while (x) { count += x & 1; x >>= 1; }
    return count;
}
#define stdc_count_ones_ui(x) stdc_count_ones_ui_fallback(x)

static inline unsigned int stdc_leading_zeros_ui_fallback(unsigned int x) {
    if (x == 0) return 32;
    unsigned int n = 0;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8;  x <<= 8;  }
    if (x <= 0x0FFFFFFF) { n += 4;  x <<= 4;  }
    if (x <= 0x3FFFFFFF) { n += 2;  x <<= 2;  }
    if (x <= 0x7FFFFFFF) { n += 1; }
    return n;
}
#define stdc_leading_zeros_ui(x) stdc_leading_zeros_ui_fallback(x)

static inline unsigned int stdc_trailing_zeros_ui_fallback(unsigned int x) {
    if (x == 0) return 32;
    unsigned int n = 0;
    if ((x & 0x0000FFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0x000000FF) == 0) { n += 8;  x >>= 8;  }
    if ((x & 0x0000000F) == 0) { n += 4;  x >>= 4;  }
    if ((x & 0x00000003) == 0) { n += 2;  x >>= 2;  }
    if ((x & 0x00000001) == 0) { n += 1; }
    return n;
}
#define stdc_trailing_zeros_ui(x) stdc_trailing_zeros_ui_fallback(x)

#define stdc_has_single_bit_ui(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#endif
#endif

/*
 * =============================================================================
 * C23 [[noreturn]] - Replaces deprecated _Noreturn
 * =============================================================================
 * C23 deprecates _Noreturn and <stdnoreturn.h> in favor of [[noreturn]].
 *
 * Source: https://en.cppreference.com/w/c/language/_Noreturn
 */
#if HAVE_C23
#define NORETURN [[noreturn]]
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* C11: Use _Noreturn keyword */
#define NORETURN _Noreturn
#elif defined(__GNUC__) || defined(__clang__)
/* GCC/Clang extension */
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

#endif /* C23_COMPAT_H */