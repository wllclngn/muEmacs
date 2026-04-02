// test_wrap_segments.c - Tests for Pretext-inspired wrap segment cache
//
// Tests the prepare (segmentation), layout (row counting + cursor positioning),
// and cache lifecycle (invalidation, lazy rebuild) subsystems.

#include "../test_utils.h"
#include "line.h"
#include "wrap_segments.h"
#include "estruct.h"
#include "edef.h"
#include "memory.h"
#include "internal/text_storage.h"

// Helper: create a standalone line with given text content
static struct line *make_test_line(const char *text)
{
	int len = (int)strlen(text);
	struct line *lp = lalloc(len + 16);
	if (!lp) return nullptr;
	// Insert text via storage API so llength() returns correct value
	if (lp->storage)
		TS_INSERT(lp->storage, 0, text, (size_t)len);
	return lp;
}

static void free_test_line(struct line *lp)
{
	if (!lp) return;
	wrap_invalidate(lp);
	if (lp->storage)
		TS_DESTROY(lp->storage);
	safe_free((void **)&lp);
}

// PREPARE TESTS

static int test_wrap_prepare_empty(void)
{
	int ok = 1;
	PHASE_START("WRAP: PREPARE_EMPTY", "Empty line produces valid cache");

	struct line *lp = lalloc(0);
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache) {
		LOG_ERROR("[FAIL] wrap_prepare returned nullptr for empty line");
		ok = 0; goto cleanup;
	}
	if (cache->count != 0) {
		LOG_ERRORF("[FAIL] Expected 0 segments, got %d", cache->count);
		ok = 0;
	}
	if (cache->total_display_width != 0) {
		LOG_ERRORF("[FAIL] Expected total_width 0, got %d", cache->total_display_width);
		ok = 0;
	}
	if (!(cache->flags & WRAP_SIMPLE)) {
		LOG_ERROR("[FAIL] Empty line should have WRAP_SIMPLE flag");
		ok = 0;
	}

cleanup:
	wrap_invalidate(lp);
	if (lp->storage) TS_DESTROY(lp->storage);
	safe_free((void **)&lp);
done:
	PHASE_END("WRAP: PREPARE_EMPTY", ok);
	return ok;
}

static int test_wrap_prepare_simple_text(void)
{
	int ok = 1;
	PHASE_START("WRAP: PREPARE_SIMPLE", "\"hello world\" -> [TEXT, SPACE, TEXT]");

	struct line *lp = make_test_line("hello world");
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache) { ok = 0; goto cleanup; }

	if (cache->count != 3) {
		LOG_ERRORF("[FAIL] Expected 3 segments, got %d", cache->count);
		ok = 0; goto cleanup;
	}
	// Segment 0: TEXT "hello" w=5
	if (cache->segments[0].kind != SEG_TEXT || cache->segments[0].display_width != 5) {
		LOG_ERRORF("[FAIL] Seg0: kind=%d width=%d (expected TEXT/5)",
		           cache->segments[0].kind, cache->segments[0].display_width);
		ok = 0;
	}
	// Segment 1: SPACE " " w=1
	if (cache->segments[1].kind != SEG_SPACE || cache->segments[1].display_width != 1) {
		LOG_ERRORF("[FAIL] Seg1: kind=%d width=%d (expected SPACE/1)",
		           cache->segments[1].kind, cache->segments[1].display_width);
		ok = 0;
	}
	// Segment 2: TEXT "world" w=5
	if (cache->segments[2].kind != SEG_TEXT || cache->segments[2].display_width != 5) {
		LOG_ERRORF("[FAIL] Seg2: kind=%d width=%d (expected TEXT/5)",
		           cache->segments[2].kind, cache->segments[2].display_width);
		ok = 0;
	}
	if (cache->total_display_width != 11) {
		LOG_ERRORF("[FAIL] Total width %d, expected 11", cache->total_display_width);
		ok = 0;
	}
	if (!(cache->flags & WRAP_SIMPLE)) {
		LOG_ERROR("[FAIL] Simple ASCII should have WRAP_SIMPLE flag");
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: PREPARE_SIMPLE", ok);
	return ok;
}

static int test_wrap_prepare_tabs(void)
{
	int ok = 1;
	PHASE_START("WRAP: PREPARE_TABS", "\"a\\tb\" -> [TEXT, TAB, TEXT]");

	struct line *lp = make_test_line("a\tb");
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache) { ok = 0; goto cleanup; }

	if (cache->count != 3) {
		LOG_ERRORF("[FAIL] Expected 3 segments, got %d", cache->count);
		ok = 0; goto cleanup;
	}
	if (cache->segments[0].kind != SEG_TEXT || cache->segments[0].display_width != 1) {
		LOG_ERRORF("[FAIL] Seg0: expected TEXT/1, got %d/%d",
		           cache->segments[0].kind, cache->segments[0].display_width);
		ok = 0;
	}
	if (cache->segments[1].kind != SEG_TAB) {
		LOG_ERRORF("[FAIL] Seg1: expected TAB, got kind=%d", cache->segments[1].kind);
		ok = 0;
	}
	// Tab at column 1 with tab_width=8: width should be 7
	if (cache->segments[1].display_width != 7) {
		LOG_ERRORF("[FAIL] Tab width at col 1 should be 7, got %d",
		           cache->segments[1].display_width);
		ok = 0;
	}
	if (cache->segments[2].kind != SEG_TEXT || cache->segments[2].display_width != 1) {
		LOG_ERRORF("[FAIL] Seg2: expected TEXT/1, got %d/%d",
		           cache->segments[2].kind, cache->segments[2].display_width);
		ok = 0;
	}
	if (cache->flags & WRAP_SIMPLE) {
		LOG_ERROR("[FAIL] Line with tab should NOT have WRAP_SIMPLE");
		ok = 0;
	}
	if (!(cache->flags & WRAP_HAS_TAB)) {
		LOG_ERROR("[FAIL] Should have WRAP_HAS_TAB flag");
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: PREPARE_TABS", ok);
	return ok;
}

static int test_wrap_prepare_control(void)
{
	int ok = 1;
	PHASE_START("WRAP: PREPARE_CTRL", "Control char -> SEG_CTRL width=2");

	struct line *lp = make_test_line("\x01hello");
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache) { ok = 0; goto cleanup; }

	if (cache->count < 2) {
		LOG_ERRORF("[FAIL] Expected >=2 segments, got %d", cache->count);
		ok = 0; goto cleanup;
	}
	if (cache->segments[0].kind != SEG_CTRL || cache->segments[0].display_width != 2) {
		LOG_ERRORF("[FAIL] Seg0: expected CTRL/2, got %d/%d",
		           cache->segments[0].kind, cache->segments[0].display_width);
		ok = 0;
	}
	if (cache->segments[1].kind != SEG_TEXT) {
		LOG_ERRORF("[FAIL] Seg1: expected TEXT, got %d", cache->segments[1].kind);
		ok = 0;
	}
	if (cache->flags & WRAP_SIMPLE) {
		LOG_ERROR("[FAIL] Line with ctrl should NOT have WRAP_SIMPLE");
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: PREPARE_CTRL", ok);
	return ok;
}

static int test_wrap_prepare_multiple_spaces(void)
{
	int ok = 1;
	PHASE_START("WRAP: PREPARE_MULTI_SPACE", "\"a   b\" -> [TEXT, SPACE(3), TEXT]");

	struct line *lp = make_test_line("a   b");
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache) { ok = 0; goto cleanup; }

	if (cache->count != 3) {
		LOG_ERRORF("[FAIL] Expected 3 segments, got %d", cache->count);
		ok = 0; goto cleanup;
	}
	if (cache->segments[1].kind != SEG_SPACE || cache->segments[1].display_width != 3) {
		LOG_ERRORF("[FAIL] Space seg: kind=%d width=%d (expected SPACE/3)",
		           cache->segments[1].kind, cache->segments[1].display_width);
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: PREPARE_MULTI_SPACE", ok);
	return ok;
}

static int test_wrap_prepare_long_line(void)
{
	int ok = 1;
	PHASE_START("WRAP: PREPARE_LONG", "8192-byte line (regression test for 4KB bug)");

	// Build 8192-char line: "aaaa bbbb aaaa bbbb ..."
	char *text = safe_alloc(8193, "test long line", __FILE__, __LINE__);
	if (!text) { ok = 0; goto done; }

	for (int i = 0; i < 8192; i++) {
		text[i] = (i % 5 == 4) ? ' ' : 'a';
	}
	text[8192] = '\0';

	struct line *lp = make_test_line(text);
	safe_free((void **)&text);
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache) { ok = 0; goto cleanup; }

	// Total display width should equal line length (all ASCII)
	if (cache->total_display_width != 8192) {
		LOG_ERRORF("[FAIL] Total width %d, expected 8192 (4KB truncation bug?)",
		           cache->total_display_width);
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: PREPARE_LONG", ok);
	return ok;
}

// LAYOUT TESTS

static int test_wrap_rows_no_wrap(void)
{
	int ok = 1;
	PHASE_START("WRAP: ROWS_NO_WRAP", "Short line fits in one row");

	struct line *lp = make_test_line("hello");
	if (!lp) { ok = 0; goto done; }

	int rows = wrap_line_rows(lp, 80, 8);
	if (rows != 1) {
		LOG_ERRORF("[FAIL] Expected 1 row, got %d", rows);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: ROWS_NO_WRAP", ok);
	return ok;
}

static int test_wrap_rows_exact_fit(void)
{
	int ok = 1;
	PHASE_START("WRAP: ROWS_EXACT", "Line width == wrap_col -> 1 row");

	struct line *lp = make_test_line("12345");
	if (!lp) { ok = 0; goto done; }

	int rows = wrap_line_rows(lp, 5, 8);
	if (rows != 1) {
		LOG_ERRORF("[FAIL] Expected 1 row for exact fit, got %d", rows);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: ROWS_EXACT", ok);
	return ok;
}

static int test_wrap_rows_word_break(void)
{
	int ok = 1;
	PHASE_START("WRAP: ROWS_WORD_BREAK", "\"aaaa bbbb\" wrap_col=5 -> 2 rows");

	struct line *lp = make_test_line("aaaa bbbb");
	if (!lp) { ok = 0; goto done; }

	int rows = wrap_line_rows(lp, 5, 8);
	if (rows != 2) {
		LOG_ERRORF("[FAIL] Expected 2 rows, got %d", rows);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: ROWS_WORD_BREAK", ok);
	return ok;
}

static int test_wrap_rows_multi_word(void)
{
	int ok = 1;
	PHASE_START("WRAP: ROWS_MULTI_WORD", "\"aaa bbb ccc\" wrap_col=8 -> 2 rows");

	struct line *lp = make_test_line("aaa bbb ccc");
	if (!lp) { ok = 0; goto done; }

	// "aaa bbb" = 7 cols, fits. Adding " ccc" would be 11 > 8.
	// Break at space after "bbb". Row 1: "aaa bbb " (8 cols incl trailing space)
	// Row 2: "ccc"
	int rows = wrap_line_rows(lp, 8, 8);
	if (rows != 2) {
		LOG_ERRORF("[FAIL] Expected 2 rows, got %d", rows);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: ROWS_MULTI_WORD", ok);
	return ok;
}

static int test_wrap_rows_no_space(void)
{
	int ok = 1;
	PHASE_START("WRAP: ROWS_NO_SPACE", "\"aaaaabbbbb\" wrap_col=5 -> 2 rows");

	struct line *lp = make_test_line("aaaaabbbbb");
	if (!lp) { ok = 0; goto done; }

	int rows = wrap_line_rows(lp, 5, 8);
	if (rows != 2) {
		LOG_ERRORF("[FAIL] Expected 2 rows, got %d", rows);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: ROWS_NO_SPACE", ok);
	return ok;
}

static int test_wrap_rows_three_wraps(void)
{
	int ok = 1;
	PHASE_START("WRAP: ROWS_THREE_WRAPS", "Long line wrapping 3+ times");

	// 25 chars, wrap_col=10: should wrap at least twice
	struct line *lp = make_test_line("aaaa bbbbb cccc ddddd eee");
	if (!lp) { ok = 0; goto done; }

	int rows = wrap_line_rows(lp, 10, 8);
	if (rows < 3) {
		LOG_ERRORF("[FAIL] Expected >=3 rows, got %d", rows);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: ROWS_THREE_WRAPS", ok);
	return ok;
}

// CURSOR TESTS

static int test_wrap_cursor_first_row(void)
{
	int ok = 1;
	PHASE_START("WRAP: CURSOR_FIRST_ROW", "Cursor in first segment");

	struct line *lp = make_test_line("hello world foo");
	if (!lp) { ok = 0; goto done; }

	int col = 0;
	int extra = wrap_cursor_pos(lp, 3, 8, 8, &col);
	// Byte 3 = 'l' in "hello", should be row 0 col 3
	if (extra != 0) {
		LOG_ERRORF("[FAIL] Expected cursor_row=0, got %d", extra);
		ok = 0;
	}
	if (col != 3) {
		LOG_ERRORF("[FAIL] Expected cursor_col=3, got %d", col);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: CURSOR_FIRST_ROW", ok);
	return ok;
}

static int test_wrap_cursor_second_row(void)
{
	int ok = 1;
	PHASE_START("WRAP: CURSOR_SECOND_ROW", "Cursor past word-wrap point");

	// "aaaa bbbbb" wrap_col=5
	// Row 0: "aaaa " (5 cols), Row 1: "bbbbb"
	// Byte 5 = first 'b', should be row 1 col 0
	struct line *lp = make_test_line("aaaa bbbbb");
	if (!lp) { ok = 0; goto done; }

	int col = 0;
	int extra = wrap_cursor_pos(lp, 5, 5, 8, &col);
	if (extra != 1) {
		LOG_ERRORF("[FAIL] Expected cursor on row 1, got row %d", extra);
		ok = 0;
	}
	if (col != 0) {
		LOG_ERRORF("[FAIL] Expected cursor col 0 on row 1, got %d", col);
		ok = 0;
	}

	free_test_line(lp);
done:
	PHASE_END("WRAP: CURSOR_SECOND_ROW", ok);
	return ok;
}

// CACHE LIFECYCLE TESTS

static int test_wrap_invalidation(void)
{
	int ok = 1;
	PHASE_START("WRAP: INVALIDATION", "Cache freed on invalidation");

	struct line *lp = make_test_line("test text");
	if (!lp) { ok = 0; goto done; }

	wrap_cache_t *cache = wrap_prepare(lp, 8);
	if (!cache || !lp->l_wrap_cache) {
		LOG_ERROR("[FAIL] wrap_prepare should set l_wrap_cache");
		ok = 0; goto cleanup;
	}

	wrap_invalidate(lp);
	if (lp->l_wrap_cache != nullptr) {
		LOG_ERROR("[FAIL] l_wrap_cache should be nullptr after invalidation");
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: INVALIDATION", ok);
	return ok;
}

static int test_wrap_lazy_rebuild(void)
{
	int ok = 1;
	PHASE_START("WRAP: LAZY_REBUILD", "Cache rebuilt lazily on next access");

	struct line *lp = make_test_line("hello world");
	if (!lp) { ok = 0; goto done; }

	// Build cache
	wrap_prepare(lp, 8);
	if (!lp->l_wrap_cache) { ok = 0; goto cleanup; }

	// Invalidate
	wrap_invalidate(lp);
	if (lp->l_wrap_cache != nullptr) {
		LOG_ERROR("[FAIL] Should be nullptr after invalidation");
		ok = 0; goto cleanup;
	}

	// Access via wrap_line_rows should rebuild
	int rows = wrap_line_rows(lp, 80, 8);
	if (rows != 1) {
		LOG_ERRORF("[FAIL] Expected 1 row, got %d", rows);
		ok = 0;
	}
	if (!lp->l_wrap_cache) {
		LOG_ERROR("[FAIL] Cache should be rebuilt after wrap_line_rows");
		ok = 0;
	}

cleanup:
	free_test_line(lp);
done:
	PHASE_END("WRAP: LAZY_REBUILD", ok);
	return ok;
}

// Entry point
int test_wrap_segments(void)
{
	int all_passed = 1;

	// Prepare tests
	all_passed &= test_wrap_prepare_empty();
	all_passed &= test_wrap_prepare_simple_text();
	all_passed &= test_wrap_prepare_tabs();
	all_passed &= test_wrap_prepare_control();
	all_passed &= test_wrap_prepare_multiple_spaces();
	all_passed &= test_wrap_prepare_long_line();

	// Layout tests
	all_passed &= test_wrap_rows_no_wrap();
	all_passed &= test_wrap_rows_exact_fit();
	all_passed &= test_wrap_rows_word_break();
	all_passed &= test_wrap_rows_multi_word();
	all_passed &= test_wrap_rows_no_space();
	all_passed &= test_wrap_rows_three_wraps();

	// Cursor tests
	all_passed &= test_wrap_cursor_first_row();
	all_passed &= test_wrap_cursor_second_row();

	// Cache lifecycle tests
	all_passed &= test_wrap_invalidation();
	all_passed &= test_wrap_lazy_rebuild();

	return all_passed;
}
