#include "utf8.h"

/*
 * utf8_to_unicode()
 *
 * Convert a UTF-8 sequence to its unicode value, and return the length of
 * the sequence in bytes.
 *
 * NOTE! Invalid UTF-8 will be converted to a one-byte sequence, so you can
 * either use it as-is (ie as Latin1) or you can check for invalid UTF-8
 * by checking for a length of 1 and a result > 127.
 *
 * NOTE 2! This does *not* verify things like minimality. So overlong forms
 * are happily accepted and decoded, as are the various "invalid values".
 */
unsigned utf8_to_unicode(const char *line, unsigned index, unsigned len, unicode_t *res)
{
	unsigned value;
	unsigned char c = line[index];
	unsigned bytes, mask, i;

	*res = c;
	line += index;
	len -= index;

	/*
	 * 0xxxxxxx is valid utf8
	 * 10xxxxxx is invalid UTF-8, we assume it is Latin1
	 */
	if (c < 0xc0)
		return 1;

	/* Ok, it's 11xxxxxx, do a stupid decode */
	mask = 0x20;
	bytes = 2;
	while (c & mask) {
		bytes++;
		mask >>= 1;
	}

	/* Invalid? Do it as a single byte Latin1 */
	if (bytes > 6)
		return 1;
	if (bytes > len)
		return 1;

	value = c & (mask-1);

    /* Ok, do the bytes */
    for (i = 1; i < bytes; i++) {
        c = line[i];
        if ((c & 0xc0) != 0x80)
            return 1;
        value = (value << 6) | (c & 0x3f);
    }
    /* Minimality and range checks to reject overlongs and invalid scalars */
    if ((bytes == 2 && value < 0x80) ||
        (bytes == 3 && value < 0x800) ||
        (bytes == 4 && (value < 0x10000 || value > 0x10FFFF)) ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        return 1;
    }
    *res = value;
    return bytes;
}

static void reverse_string(char *begin, char *end)
{
	if (!begin || !end || end <= begin) return;
	
	end--; // Point to last valid character, not past-end
	while (begin < end) {
		char temp = *begin;
		*begin = *end;
		*end = temp;
		begin++;
		end--;
	}
}

/*
 * unicode_to_utf8()
 *
 * Convert a unicode value to its canonical UTF-8 sequence.
 * Proper C23-compliant UTF-8 encoding implementation.
 */
unsigned unicode_to_utf8(unsigned int c, char *utf8)
{
	if (c <= 0x7F) {
		// 1-byte sequence: 0xxxxxxx
		utf8[0] = (char)c;
		return 1;
	} else if (c <= 0x7FF) {
		// 2-byte sequence: 110xxxxx 10xxxxxx
		utf8[0] = (char)(0xC0 | (c >> 6));
		utf8[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	} else if (c <= 0xFFFF) {
		// 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
		utf8[0] = (char)(0xE0 | (c >> 12));
		utf8[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		utf8[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	} else if (c <= 0x10FFFF) {
		// 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		utf8[0] = (char)(0xF0 | (c >> 18));
		utf8[1] = (char)(0x80 | ((c >> 12) & 0x3F));
		utf8[2] = (char)(0x80 | ((c >> 6) & 0x3F));
		utf8[3] = (char)(0x80 | (c & 0x3F));
		return 4;
	} else {
		// Invalid Unicode code point - use replacement character U+FFFD
		utf8[0] = (char)0xEF;
		utf8[1] = (char)0xBF;
		utf8[2] = (char)0xBD;
		return 3;
	}
}
