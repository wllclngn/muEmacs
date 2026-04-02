// Pretext-inspired wrap segment cache for muEmacs
// Two-phase architecture: prepare (segment + measure) once per edit,
// layout (walk cached segments) on every resize/redisplay.

#include <stdlib.h>
#include <string.h>

#include "estruct.h"
#include "line.h"
#include "wrap_segments.h"
#include "utf8.h"
#include "../util/display_width.h"
#include "memory.h"

#define INITIAL_SEG_CAPACITY 16

// Ensure capacity for one more segment
static bool ensure_capacity(wrap_cache_t *cache)
{
	if (cache->count < cache->capacity)
		return true;

	uint16_t new_cap = (cache->capacity == 0) ? INITIAL_SEG_CAPACITY
	                                          : (uint16_t)(cache->capacity * 2);
	wrap_segment_t *new_segs = safe_alloc(
		(size_t)new_cap * sizeof(wrap_segment_t),
		"wrap segments", __FILE__, __LINE__);
	if (!new_segs)
		return false;

	if (cache->segments && cache->count > 0)
		memcpy(new_segs, cache->segments,
		       (size_t)cache->count * sizeof(wrap_segment_t));

	if (cache->segments)
		safe_free((void **)&cache->segments);

	cache->segments = new_segs;
	cache->capacity = new_cap;
	return true;
}

// Push a new segment
static void push_segment(wrap_cache_t *cache, uint16_t byte_off,
                          uint16_t byte_len, uint16_t disp_width,
                          uint16_t char_count, wrap_seg_kind_t kind)
{
	if (!ensure_capacity(cache))
		return;

	wrap_segment_t *seg = &cache->segments[cache->count++];
	seg->byte_offset = byte_off;
	seg->byte_len = byte_len;
	seg->display_width = disp_width;
	seg->char_count = char_count;
	seg->kind = kind;
	cache->total_display_width += disp_width;
}

// Effective display width of a tab at a given column
static int tab_width_at(int col, int tab_width)
{
	return ((col + tab_width) / tab_width) * tab_width - col;
}

wrap_cache_t *wrap_prepare(struct line *lp, int tab_width)
{
	if (!lp)
		return nullptr;

	// If cache already exists, return it
	if (lp->l_wrap_cache)
		return lp->l_wrap_cache;

	wrap_cache_t *cache = safe_alloc(sizeof(wrap_cache_t),
	                                 "wrap cache", __FILE__, __LINE__);
	if (!cache)
		return nullptr;

	cache->segments = nullptr;
	cache->count = 0;
	cache->capacity = 0;
	cache->total_display_width = 0;
	cache->flags = WRAP_SIMPLE; // assume simple until proven otherwise

	int len = llength(lp);
	if (len == 0) {
		lp->l_wrap_cache = cache;
		return cache;
	}

	int i = 0;
	int running_col = 0; // for tab width computation

	// Current segment being built
	int seg_start = 0;
	int seg_bytes = 0;
	int seg_width = 0;
	int seg_chars = 0;
	wrap_seg_kind_t seg_kind = SEG_TEXT;
	bool in_segment = false;

	// Flush current segment
	#define FLUSH_SEG() do { \
		if (in_segment && seg_bytes > 0) { \
			push_segment(cache, (uint16_t)seg_start, (uint16_t)seg_bytes, \
			             (uint16_t)seg_width, (uint16_t)seg_chars, seg_kind); \
		} \
		in_segment = false; \
		seg_bytes = 0; \
		seg_width = 0; \
		seg_chars = 0; \
	} while (0)

	while (i < len) {
		// Decode UTF-8 character via lgetc
		char utf8_buf[6];
		int remaining = len - i;
		int extract = (remaining > 6) ? 6 : remaining;
		for (int j = 0; j < extract; j++)
			utf8_buf[j] = (char)lgetc(lp, i + j);

		unicode_t c;
		int bytes = (int)utf8_to_unicode(utf8_buf, 0, (unsigned)extract, &c);
		if (bytes <= 0) bytes = 1;

		// Classify this character
		wrap_seg_kind_t char_kind;
		int char_width;

		if (c == '\t') {
			char_kind = SEG_TAB;
			char_width = tab_width_at(running_col, tab_width);
			cache->flags &= (uint8_t)~WRAP_SIMPLE;
			cache->flags |= WRAP_HAS_TAB;
		} else if (c == ' ') {
			char_kind = SEG_SPACE;
			char_width = 1;
		} else if (c < 32 || c == 127) {
			char_kind = SEG_CTRL;
			char_width = 2; // ^X display
			cache->flags &= (uint8_t)~WRAP_SIMPLE;
			cache->flags |= WRAP_HAS_CTRL;
		} else {
			char_kind = SEG_TEXT;
			int w = unicode_display_width(c);
			char_width = (w > 0) ? w : 1;
			if (w == 2) {
				cache->flags &= (uint8_t)~WRAP_SIMPLE;
				cache->flags |= WRAP_HAS_WIDE;
			}
		}

		// Tabs are always their own segment (width depends on column)
		if (char_kind == SEG_TAB) {
			FLUSH_SEG();
			push_segment(cache, (uint16_t)i, (uint16_t)bytes,
			             (uint16_t)char_width, 1, SEG_TAB);
			running_col += char_width;
			i += bytes;
			continue;
		}

		// If kind changed, flush previous segment and start new
		if (in_segment && char_kind != seg_kind) {
			FLUSH_SEG();
		}

		if (!in_segment) {
			seg_start = i;
			seg_kind = char_kind;
			in_segment = true;
		}

		seg_bytes += bytes;
		seg_width += char_width;
		seg_chars++;
		running_col += char_width;
		i += bytes;
	}

	FLUSH_SEG();
	#undef FLUSH_SEG

	lp->l_wrap_cache = cache;
	return cache;
}

void wrap_invalidate(struct line *lp)
{
	if (!lp || !lp->l_wrap_cache)
		return;

	wrap_cache_t *cache = lp->l_wrap_cache;
	if (cache->segments)
		safe_free((void **)&cache->segments);
	safe_free((void **)&lp->l_wrap_cache);
}

// Compute effective width of a segment at a given running column.
// Only differs from seg->display_width for tabs.
static int seg_effective_width(const wrap_segment_t *seg, int running_col,
                               int tab_width)
{
	if (seg->kind == SEG_TAB)
		return tab_width_at(running_col, tab_width);
	return seg->display_width;
}

// Core layout engine: walks segments with pending-break pattern.
// Computes row count and optionally cursor position.
void wrap_layout(struct line *lp, int wrap_col, int tab_width,
                 int cursor_byte, wrap_result_t *result)
{
	result->row_count = 1;
	result->cursor_row = 0;
	result->cursor_col = 0;

	if (!lp || wrap_col <= 0)
		return;

	int len = llength(lp);
	if (len == 0)
		return;

	wrap_cache_t *cache = wrap_prepare(lp, tab_width);
	if (!cache || cache->count == 0)
		return;

	// Fast path: line fits in one row
	if (cache->total_display_width <= wrap_col && !(cache->flags & WRAP_HAS_TAB)) {
		// No wrapping needed. Find cursor if requested.
		if (cursor_byte >= 0) {
			// Walk segments to find cursor column
			int col = 0;
			for (uint16_t s = 0; s < cache->count; s++) {
				const wrap_segment_t *seg = &cache->segments[s];
				if (cursor_byte >= seg->byte_offset &&
				    cursor_byte < seg->byte_offset + seg->byte_len) {
					// Cursor is within this segment
					if (cache->flags & WRAP_SIMPLE) {
						result->cursor_col = col + (cursor_byte - seg->byte_offset);
					} else {
						// Walk characters within segment
						int bi = seg->byte_offset;
						int sc = col;
						while (bi < seg->byte_offset + seg->byte_len && bi < cursor_byte) {
							char buf[6];
							int rem = len - bi;
							int ext = (rem > 6) ? 6 : rem;
							for (int j = 0; j < ext; j++)
								buf[j] = (char)lgetc(lp, bi + j);
							unicode_t ch;
							int nb = (int)utf8_to_unicode(buf, 0, (unsigned)ext, &ch);
							if (nb <= 0) nb = 1;
							if (ch == '\t')
								sc += tab_width_at(sc, tab_width);
							else if (ch < 32 || ch == 127)
								sc += 2;
							else {
								int w = unicode_display_width(ch);
								sc += (w > 0) ? w : 1;
							}
							bi += nb;
						}
						result->cursor_col = sc;
					}
					return;
				}
				col += seg->display_width;
			}
			// Cursor at or past end of line
			result->cursor_col = cache->total_display_width;
		}
		return;
	}

	// Full layout walk with pending-break pattern
	int running_col = 0;
	int rows = 0;
	bool has_content = false;
	int pending_break_seg = -1;
	bool cursor_found = false;

	int seg_idx = 0;
	while (seg_idx < cache->count) {
		const wrap_segment_t *seg = &cache->segments[seg_idx];
		int seg_w = seg_effective_width(seg, running_col, tab_width);

		// Check cursor before potential wrap (cursor might be at wrap boundary)
		if (!cursor_found && cursor_byte >= 0 &&
		    cursor_byte >= seg->byte_offset &&
		    cursor_byte < seg->byte_offset + seg->byte_len) {
			// Cursor is within this segment - compute exact column
			result->cursor_row = rows;
			if (cache->flags & WRAP_SIMPLE) {
				result->cursor_col = running_col + (cursor_byte - seg->byte_offset);
			} else {
				int bi = seg->byte_offset;
				int sc = running_col;
				while (bi < seg->byte_offset + seg->byte_len && bi < cursor_byte) {
					char buf[6];
					int rem = len - bi;
					int ext = (rem > 6) ? 6 : rem;
					for (int j = 0; j < ext; j++)
						buf[j] = (char)lgetc(lp, bi + j);
					unicode_t ch;
					int nb = (int)utf8_to_unicode(buf, 0, (unsigned)ext, &ch);
					if (nb <= 0) nb = 1;
					if (ch == '\t')
						sc += tab_width_at(sc, tab_width);
					else if (ch < 32 || ch == 127)
						sc += 2;
					else {
						int w = unicode_display_width(ch);
						sc += (w > 0) ? w : 1;
					}
					bi += nb;
				}
				result->cursor_col = sc;
			}
			cursor_found = true;
		}

		// Would adding this segment overflow?
		if (has_content && running_col + seg_w > wrap_col) {
			// Breakable segment (space/tab): trailing whitespace, emit line
			if (seg->kind == SEG_SPACE || seg->kind == SEG_TAB) {
				// Check cursor at the space itself (before wrap)
				if (!cursor_found && cursor_byte >= 0 &&
				    cursor_byte >= seg->byte_offset &&
				    cursor_byte < seg->byte_offset + seg->byte_len) {
					result->cursor_row = rows;
					result->cursor_col = running_col;
					cursor_found = true;
				}
				rows++;
				running_col = 0;
				has_content = false;
				pending_break_seg = -1;
				seg_idx++;
				continue;
			}

			// Pending break available: break there
			if (pending_break_seg >= 0) {
				// If cursor was found on this row but AFTER the break point,
				// it needs to move to the next row. Re-find it.
				if (cursor_found && result->cursor_row == rows) {
					int break_end = cache->segments[pending_break_seg].byte_offset +
					                cache->segments[pending_break_seg].byte_len;
					if (cursor_byte >= break_end) {
						// Cursor is past the break - will be re-found
						cursor_found = false;
					}
				}
				rows++;
				seg_idx = pending_break_seg + 1;
				running_col = 0;
				has_content = false;
				pending_break_seg = -1;
				continue; // re-process segments after break
			}

			// No break point: hard break before this segment
			if (cursor_found && result->cursor_row == rows) {
				// cursor found in a segment that will be pushed to next row
				// (segments already added are fine, they stay on this row)
			}
			rows++;
			running_col = 0;
			has_content = false;
			pending_break_seg = -1;
			continue; // re-process this segment on new row
		}

		// Handle oversized segment at start of row (wider than wrap_col)
		if (!has_content && seg_w > wrap_col && seg->kind == SEG_TEXT) {
			if (cache->flags & WRAP_SIMPLE) {
				// All chars width 1: pure arithmetic
				int chars_in_seg = seg->char_count;
				int full_rows = (seg_w - 1) / wrap_col;

				// Track cursor within oversized segment
				if (!cursor_found && cursor_byte >= 0 &&
				    cursor_byte >= seg->byte_offset &&
				    cursor_byte < seg->byte_offset + seg->byte_len) {
					int cursor_off = cursor_byte - seg->byte_offset;
					int cursor_row_in_seg = cursor_off / wrap_col;
					result->cursor_row = rows + cursor_row_in_seg;
					result->cursor_col = cursor_off % wrap_col;
					cursor_found = true;
				}

				rows += full_rows;
				running_col = seg_w - full_rows * wrap_col;
				has_content = (running_col > 0);
				(void)chars_in_seg;
			} else {
				// Walk characters to find break points
				int bi = seg->byte_offset;
				int seg_end = seg->byte_offset + seg->byte_len;
				int col = 0;
				has_content = true;

				while (bi < seg_end) {
					char buf[6];
					int rem = len - bi;
					int ext = (rem > 6) ? 6 : rem;
					for (int j = 0; j < ext; j++)
						buf[j] = (char)lgetc(lp, bi + j);
					unicode_t ch;
					int nb = (int)utf8_to_unicode(buf, 0, (unsigned)ext, &ch);
					if (nb <= 0) nb = 1;

					int cw;
					if (ch < 32 || ch == 127)
						cw = 2;
					else {
						int dw = unicode_display_width(ch);
						cw = (dw > 0) ? dw : 1;
					}

					if (col >= wrap_col) {
						rows++;
						col = 0;
					}

					if (!cursor_found && cursor_byte >= 0 &&
					    cursor_byte >= bi && cursor_byte < bi + nb) {
						result->cursor_row = rows;
						result->cursor_col = col;
						cursor_found = true;
					}

					col += cw;
					bi += nb;
				}
				running_col = col;
			}
			seg_idx++;
			continue;
		}

		// Normal add
		running_col += seg_w;
		has_content = true;

		// Track break points at spaces and tabs
		if (seg->kind == SEG_SPACE || seg->kind == SEG_TAB)
			pending_break_seg = seg_idx;

		seg_idx++;
	}

	// Handle cursor at or past end of line
	if (!cursor_found && cursor_byte >= 0) {
		result->cursor_row = rows;
		result->cursor_col = running_col;

		// Match old behavior: if cursor is at wrap boundary and there's
		// more text, it wraps to next row
		if (running_col >= wrap_col && cursor_byte < len) {
			result->cursor_row = rows + 1;
			result->cursor_col = 0;
		}
	}

	result->row_count = rows + 1;
}

int wrap_line_rows(struct line *lp, int wrap_col, int tab_width)
{
	if (!lp || wrap_col <= 0)
		return 1;
	if (llength(lp) == 0)
		return 1;

	wrap_result_t result;
	wrap_layout(lp, wrap_col, tab_width, -1, &result);
	return result.row_count;
}

int wrap_cursor_pos(struct line *lp, int byte_offset, int wrap_col,
                    int tab_width, int *out_col)
{
	if (!lp || byte_offset <= 0 || wrap_col <= 0) {
		*out_col = calculate_display_column_cached(lp, byte_offset, tab_width);
		return 0;
	}

	wrap_result_t result;
	wrap_layout(lp, wrap_col, tab_width, byte_offset, &result);
	*out_col = result.cursor_col;
	return result.cursor_row;
}
