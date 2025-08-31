/*
 * utf8_optimized.h - High-performance UTF-8 display operations with atomic caching
 * Consolidates repeated UTF-8 patterns into optimized inline functions
 */

#ifndef UTF8_OPTIMIZED_H
#define UTF8_OPTIMIZED_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

/* UTF-8 sequence length lookup table - O(1) byte classification */
static const uint8_t utf8_sequence_length[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x00-0x0F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x10-0x1F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x20-0x2F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x30-0x3F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x40-0x4F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x50-0x5F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x60-0x6F */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  /* 0x70-0x7F */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0x80-0x8F continuation */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0x90-0x9F continuation */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xA0-0xAF continuation */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xB0-0xBF continuation */
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  /* 0xC0-0xCF 2-byte start */
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  /* 0xD0-0xDF 2-byte start */
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,  /* 0xE0-0xEF 3-byte start */
    4,4,4,4,4,4,4,4,5,5,5,5,6,6,0,0   /* 0xF0-0xFF 4+ byte start */
};

/* Display width lookup for common characters - O(1) width calculation */
static const uint8_t ascii_display_width[128] = {
    /* Control characters (0x00-0x1F) display as ^X (width 2) */
    2,2,2,2,2,2,2,2,2,4,2,2,2,2,2,2,  /* Tab shows as 4 spaces worst case */
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    /* Printable ASCII (0x20-0x7E) all width 1 */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2   /* DEL shows as ^? (width 2) */
};

/* Atomic UTF-8 character cache for display width calculations */
struct utf8_char_cache_entry {
    uint32_t codepoint;      /* Unicode codepoint */
    uint8_t display_width;   /* Cached display width */
    uint8_t byte_length;     /* Cached byte length */
    _Atomic bool valid;      /* Cache entry validity */
};

#define UTF8_CACHE_SIZE 256
static struct utf8_char_cache_entry utf8_display_cache[UTF8_CACHE_SIZE];
static _Atomic uint32_t cache_generation = 0;

/* Fast UTF-8 byte length - O(1) table lookup */
static inline int utf8_byte_length_fast(unsigned char first_byte)
{
    return utf8_sequence_length[first_byte];
}

/* Fast ASCII display width - O(1) table lookup */
static inline int ascii_display_width_fast(unsigned char c)
{
    return (c < 128) ? ascii_display_width[c] : 1;
}

/* Fast UTF-8 display width with atomic caching */
static inline int utf8_display_width_cached(const unsigned char *utf8_str, int byte_len)
{
    /* Fast path for ASCII */
    if (byte_len == 1 && utf8_str[0] < 128) {
        return ascii_display_width_fast(utf8_str[0]);
    }
    
    /* Generate cache key from first few bytes */
    uint32_t cache_key = 0;
    for (int i = 0; i < byte_len && i < 4; i++) {
        cache_key = (cache_key << 8) | utf8_str[i];
    }
    
    uint32_t cache_index = cache_key & (UTF8_CACHE_SIZE - 1);
    struct utf8_char_cache_entry *entry = &utf8_display_cache[cache_index];
    
    /* Check cache hit */
    if (atomic_load_explicit(&entry->valid, memory_order_acquire) &&
        entry->codepoint == cache_key && entry->byte_length == byte_len) {
        return entry->display_width;
    }
    
    /* Cache miss - calculate display width */
    int width;
    if (byte_len <= 0) {
        width = 0;
    } else if (utf8_str[0] < 0x20 || utf8_str[0] == 0x7F) {
        width = 2; /* Control character */
    } else if (utf8_str[0] < 0x80) {
        width = 1; /* ASCII */
    } else {
        /* Unicode character - simplified width calculation */
        /* Most CJK characters are width 2, others are width 1 */
        width = ((utf8_str[0] >= 0xE4 && utf8_str[0] <= 0xE9) || 
                (utf8_str[0] == 0xEF && byte_len == 3)) ? 2 : 1;
    }
    
    /* Update cache atomically */
    entry->codepoint = cache_key;
    entry->display_width = width;
    entry->byte_length = byte_len;
    atomic_store_explicit(&entry->valid, true, memory_order_release);
    
    return width;
}

/* Fast UTF-8 character iteration with caching */
static inline const unsigned char *utf8_next_char_fast(const unsigned char *str, 
                                                      const unsigned char *end,
                                                      int *char_width,
                                                      int *byte_len)
{
    if (str >= end) {
        *char_width = 0;
        *byte_len = 0;
        return str;
    }
    
    *byte_len = utf8_byte_length_fast(*str);
    if (*byte_len == 0 || str + *byte_len > end) {
        /* Invalid UTF-8 - treat as single byte */
        *byte_len = 1;
        *char_width = 1;
        return str + 1;
    }
    
    *char_width = utf8_display_width_cached(str, *byte_len);
    return str + *byte_len;
}

/* Optimized UTF-8 string display width calculation */
static inline int utf8_string_display_width(const unsigned char *str, int byte_count)
{
    const unsigned char *ptr = str;
    const unsigned char *end = str + byte_count;
    int total_width = 0;
    
    while (ptr < end) {
        int char_width, byte_len;
        ptr = utf8_next_char_fast(ptr, end, &char_width, &byte_len);
        total_width += char_width;
    }
    
    return total_width;
}

/* Reset UTF-8 display cache */
static inline void utf8_cache_reset(void)
{
    for (int i = 0; i < UTF8_CACHE_SIZE; i++) {
        atomic_store_explicit(&utf8_display_cache[i].valid, false, memory_order_release);
    }
    atomic_fetch_add_explicit(&cache_generation, 1, memory_order_release);
}

/* Get cache statistics */
static inline uint32_t utf8_cache_generation(void)
{
    return atomic_load_explicit(&cache_generation, memory_order_acquire);
}

#endif /* UTF8_OPTIMIZED_H */