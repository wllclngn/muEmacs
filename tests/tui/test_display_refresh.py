#!/usr/bin/env python3
"""
TUI tests for display refresh bugs in μEmacs.

Tests specifically for:
1. Soft-wrap display during continuous typing (WriteEdit mode)
2. Display lag during rapid character insertion
3. Multiple Enter presses display refresh

These tests verify the fixes for:
- lchange() upgrading WFEDIT to WFHARD for soft-wrap
- typahead threshold raised from 5 to 30

Usage:
    python3 tests/tui/test_display_refresh.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def enable_writeedit_mode(emu):
    """Enable WriteEdit mode (soft-wrap at 80 columns)."""
    emu.child.send('\x1bX')  # Meta-X
    time.sleep(0.2)
    emu._read_output()
    emu.child.send('write-edit\r')
    time.sleep(0.3)
    emu._read_output()


def enter_insert_mode(emu):
    """Enter vim insert mode."""
    emu.child.send('i')
    time.sleep(0.1)
    emu._read_output()


def exit_insert_mode(emu):
    """Exit vim insert mode."""
    emu.child.send('\x1b')  # ESC
    time.sleep(0.1)
    emu._read_output()


def get_content_rows(emu, max_rows=10):
    """Get content from first N rows."""
    lines = []
    for i in range(max_rows):
        lines.append(emu.get_line(i))
    return lines


def count_non_empty_content_rows(emu):
    """Count rows with actual content (not ~ or empty)."""
    count = 0
    for i in range(emu.rows - 2):  # Skip status/message lines
        line = emu.get_line(i).strip()
        if line and not line.startswith('~'):
            count += 1
    return count


# =============================================================================
# SOFT-WRAP DISPLAY TESTS
# =============================================================================

def test_softwrap_continuous_typing():
    """Typing 100+ chars should soft-wrap at column 80 in WriteEdit mode.

    This tests the WFEDIT -> WFHARD upgrade in lchange() for soft-wrap.
    Previously, typing would overflow with $ instead of wrapping.

    With 80-col terminal and 80-col wrap, long lines wrap to next screen row
    instead of showing $ overflow at column 79.
    """
    filename = '/tmp/uemacs_softwrap_test.txt'
    with open(filename, 'w') as f:
        f.write("\n")  # Empty first line

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enable WriteEdit mode
        enable_writeedit_mode(emu)

        # Verify WE mode is active
        status = emu.get_line(emu.rows - 2)
        if ':WE' not in status and 'WE' not in status:
            print(f"FAIL: WriteEdit mode not enabled. Status: {status}")
            emu.dump_screen()
            return False

        # Enter insert mode on first line
        emu.send('gg')
        time.sleep(0.1)
        enter_insert_mode(emu)

        # Type 100 characters continuously (should wrap at 80)
        # CRITICAL: Must read output after EACH char to properly build pyte screen
        test_text = "A" * 100
        for char in test_text:
            emu.child.send(char)
            time.sleep(0.02)  # 50 chars/sec
            emu._read_output()  # Incremental read is essential

        # Exit insert mode and wait for display to settle
        emu.child.send('\x1b')  # ESC
        time.sleep(0.3)
        emu._read_output()

        # Check that content wrapped to multiple rows
        row0 = emu.get_line(0)
        row1 = emu.get_line(1)

        # With soft-wrap at 80:
        # - Row 0 should have 80 A's (or close to it)
        # - Row 1 should have remaining A's (~20)
        # - NO $ overflow indicator (which appears when NO wrap happens)

        row0_As = row0.count('A')
        row1_As = row1.count('A')
        total_As = row0_As + row1_As

        # Key check: row 1 should have SOME A's (soft-wrap happened)
        # If row1 has 0 A's, wrap didn't happen
        if row1_As == 0:
            # Check if there's $ overflow instead
            if '$' in row0:
                print("FAIL: Line shows $ overflow - soft-wrap not working")
            else:
                print(f"FAIL: All {row0_As} A's on row 0, none on row 1 - no wrap")
            print(f"  Row 0: {row0}")
            print(f"  Row 1: {row1}")
            emu.dump_screen()
            return False

        # Verify total visible characters is reasonable
        if total_As < 90:  # Should see ~100, allow margin for display lag
            print(f"FAIL: Only {total_As} A's visible ({row0_As} + {row1_As}), expected ~100")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_softwrap_display_updates_every_keystroke():
    """Each keystroke should trigger display refresh in WriteEdit mode.

    Tests that display doesn't lag behind buffer during typing.
    """
    filename = '/tmp/uemacs_softwrap_keystroke.txt'
    with open(filename, 'w') as f:
        f.write("\n")

    emu = UEmacsTest(rows=24, cols=120)  # Wider terminal to see wrap
    try:
        emu.start(filename=filename, evil_mode=True)
        enable_writeedit_mode(emu)

        emu.send('gg')
        enter_insert_mode(emu)

        # Type and verify display after EACH character
        # Use alphanumeric only to avoid trailing space issues
        test_chars = "Thequickbrownfox"
        displayed_so_far = ""

        for i, char in enumerate(test_chars):
            emu.child.send(char)
            time.sleep(0.08)  # Give time for display update
            emu._read_output()

            displayed_so_far += char
            row0 = emu.get_line(0)

            # The typed text should be visible (strip to handle trailing spaces)
            if displayed_so_far not in row0:
                print(f"FAIL: After typing '{char}' (char {i+1}), display lagged")
                print(f"  Expected to see: '{displayed_so_far}'")
                print(f"  Actual row 0: '{row0}'")
                emu.dump_screen()
                return False

        return True
    finally:
        emu.quit()


def test_softwrap_rapid_typing_no_lag():
    """Rapid typing (50+ chars/sec) should still display correctly.

    Tests the typeahead threshold fix (raised from 5 to 30).
    """
    filename = '/tmp/uemacs_softwrap_rapid.txt'
    with open(filename, 'w') as f:
        f.write("\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)
        enable_writeedit_mode(emu)

        emu.send('gg')
        enter_insert_mode(emu)

        # Type 50 characters rapidly
        test_text = "X" * 50
        for char in test_text:
            emu.child.send(char)
            time.sleep(0.015)  # ~67 chars/sec - very fast

        # Wait for display to catch up
        time.sleep(0.3)
        emu._read_output()

        row0 = emu.get_line(0)

        # Should see all 50 X's (or close to it)
        x_count = row0.count('X')
        if x_count < 45:  # Allow small margin
            print(f"FAIL: Only {x_count} X's visible after rapid typing, expected ~50")
            print(f"  Row 0: {row0}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


# =============================================================================
# MULTIPLE ENTER PRESS TESTS
# =============================================================================

def test_multiple_enter_display_refresh():
    """Multiple rapid Enter presses should all be reflected in display.

    Tests that each newline triggers proper display update.
    """
    filename = '/tmp/uemacs_enter_test.txt'
    with open(filename, 'w') as f:
        f.write("Line 1\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)

        # Go to end of line 1 and enter insert mode
        emu.send('A')  # Append at end of line
        time.sleep(0.1)
        emu._read_output()

        # Press Enter 5 times rapidly
        for i in range(5):
            emu.child.send('\r')
            time.sleep(0.03)  # ~33 Enter/sec

        time.sleep(0.2)
        emu._read_output()

        # Count content rows - should have increased by 5
        # Original: "Line 1" = 1 row
        # After: "Line 1" + 5 empty lines = 6 content areas
        non_tilde_rows = 0
        for i in range(10):
            line = emu.get_line(i).strip()
            if not line.startswith('~'):
                non_tilde_rows += 1

        # We should see at least 5 rows (cursor may be on line 6)
        if non_tilde_rows < 5:
            print(f"FAIL: Only {non_tilde_rows} content rows after 5 Enter presses")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_enter_creates_visible_lines():
    """Each Enter should create a visually distinct new line."""
    filename = '/tmp/uemacs_enter_visible.txt'
    with open(filename, 'w') as f:
        f.write("Start\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode at end
        emu.send('GA')  # Go to last line, append
        time.sleep(0.1)
        emu._read_output()

        # Type text, Enter, type text - verify each is visible
        emu.child.send('AAA')
        time.sleep(0.1)
        emu._read_output()

        row0 = emu.get_line(0)
        if 'AAA' not in row0 and 'AAA' not in emu.get_line(1):
            print("FAIL: First text 'AAA' not visible")
            emu.dump_screen()
            return False

        emu.child.send('\r')  # Enter
        emu.child.send('BBB')
        time.sleep(0.1)
        emu._read_output()

        # BBB should be on a different row than AAA
        screen_text = '\n'.join([emu.get_line(i) for i in range(5)])
        if 'AAA' not in screen_text:
            print("FAIL: 'AAA' disappeared after Enter")
            emu.dump_screen()
            return False
        if 'BBB' not in screen_text:
            print("FAIL: 'BBB' not visible after Enter")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


# =============================================================================
# INSERT MODE DISPLAY REFRESH TESTS
# =============================================================================

def test_insert_mode_continuous_typing():
    """Continuous typing in INSERT mode should display all characters.

    This is the core bug: display lagging during normal typing.
    """
    filename = '/tmp/uemacs_insert_typing.txt'
    with open(filename, 'w') as f:
        f.write("Test file\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)
        emu._read_output()

        # Type a sentence character by character
        sentence = "Hello World Test"
        for char in sentence:
            emu.child.send(char)
            time.sleep(0.03)  # ~33 chars/sec

        time.sleep(0.2)
        emu._read_output()

        # The full sentence should be visible
        row0 = emu.get_line(0)
        if sentence not in row0:
            print(f"FAIL: Full sentence not visible")
            print(f"  Expected: '{sentence}'")
            print(f"  Row 0: '{row0}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_backspace_triggers_refresh():
    """Backspace should immediately update display."""
    filename = '/tmp/uemacs_backspace.txt'
    with open(filename, 'w') as f:
        f.write("DELETE_ME extra text\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)

        # Go to position after DELETE_ME
        emu.send('fE')  # Find 'E' in DELETE_ME
        time.sleep(0.1)
        emu.send('a')   # Append after it (enters insert mode)
        time.sleep(0.1)
        emu._read_output()

        initial_row = emu.get_line(0)

        # Press backspace 3 times
        for _ in range(3):
            emu.child.send('\x7f')  # Backspace
            time.sleep(0.05)

        time.sleep(0.1)
        emu._read_output()

        # Display should show characters removed
        after_row = emu.get_line(0)
        if after_row == initial_row:
            print("FAIL: Backspace didn't update display")
            print(f"  Initial: '{initial_row}'")
            print(f"  After:   '{after_row}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_no_typeahead_deferral_at_normal_speed():
    """Normal typing speed should NOT trigger typeahead deferral.

    With threshold at 30, typing at 20 chars/sec should always update.
    """
    filename = '/tmp/uemacs_typeahead.txt'
    with open(filename, 'w') as f:
        f.write("\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)

        emu.send('i')
        time.sleep(0.1)
        emu._read_output()

        # Type at 20 chars/sec (well under threshold)
        test_text = "Normal typing speed test"
        deferred_count = 0

        for i, char in enumerate(test_text):
            emu.child.send(char)
            time.sleep(0.05)  # 20 chars/sec
            emu._read_output()

            # Check if this character is visible
            row0 = emu.get_line(0)
            expected = test_text[:i+1]
            if expected not in row0:
                deferred_count += 1

        # At 20 chars/sec, we should have very few deferred updates
        # Allow up to 5 due to timing variance in test environment
        if deferred_count > 5:
            print(f"FAIL: {deferred_count} characters were deferred at normal speed")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


# =============================================================================
# COMBINED STRESS TEST
# =============================================================================

def test_stress_insert_mode_60_seconds():
    """60 seconds of continuous insert mode typing with verification.

    The ultimate test for display refresh during editing.
    """
    filename = '/tmp/uemacs_insert_stress.txt'
    with open(filename, 'w') as f:
        f.write("\n")

    emu = UEmacsTest(rows=24, cols=80)
    try:
        emu.start(filename=filename, evil_mode=True)
        enable_writeedit_mode(emu)

        emu.send('i')
        time.sleep(0.1)
        emu._read_output()

        start = time.time()
        total_chars = 0
        lag_count = 0

        print("\n  [60s INSERT mode stress test]")

        while time.time() - start < 60:
            # Type a burst of 20 characters
            burst = "".join([chr(65 + (total_chars + i) % 26) for i in range(20)])

            for char in burst:
                emu.child.send(char)
                time.sleep(0.025)  # 40 chars/sec
                total_chars += 1

            emu._read_output()

            # Periodically check for corruption
            corrupt, issues = emu.check_all_corruption()
            if corrupt:
                print(f"\n  FAIL: Corruption at {total_chars} chars")
                for issue in issues[:3]:
                    print(f"    - {issue}")
                emu.dump_screen()
                return False

            # Occasionally press Enter to create new lines
            if total_chars % 200 == 0:
                emu.child.send('\r')
                time.sleep(0.05)
                total_chars += 1

            # Progress
            elapsed = time.time() - start
            if total_chars % 500 == 0:
                print(f"    {elapsed:.0f}s: {total_chars} chars typed", end='\r')

        elapsed = time.time() - start
        print(f"\n  Completed: {total_chars} chars in {elapsed:.1f}s")

        # Final integrity check
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print("FAIL: Final state corrupted")
            for issue in issues[:5]:
                print(f"  - {issue}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all display refresh tests."""
    tests = [
        # Soft-wrap tests
        ("softwrap_continuous_typing", test_softwrap_continuous_typing),
        ("softwrap_display_updates_every_keystroke", test_softwrap_display_updates_every_keystroke),
        ("softwrap_rapid_typing_no_lag", test_softwrap_rapid_typing_no_lag),

        # Enter key tests
        ("multiple_enter_display_refresh", test_multiple_enter_display_refresh),
        ("enter_creates_visible_lines", test_enter_creates_visible_lines),

        # Insert mode tests
        ("insert_mode_continuous_typing", test_insert_mode_continuous_typing),
        ("backspace_triggers_refresh", test_backspace_triggers_refresh),
        ("no_typeahead_deferral_at_normal_speed", test_no_typeahead_deferral_at_normal_speed),

        # Stress test
        ("stress_insert_mode_60_seconds", test_stress_insert_mode_60_seconds),
    ]

    passed = 0
    failed = 0

    print("=" * 60)
    print("DISPLAY REFRESH TESTS")
    print("=" * 60)

    for name, test_fn in tests:
        print(f"Running: {name}...", end=" ", flush=True)
        try:
            if test_fn():
                print("PASS")
                passed += 1
            else:
                print("FAIL")
                failed += 1
        except Exception as e:
            print(f"ERROR: {e}")
            import traceback
            traceback.print_exc()
            failed += 1

    print()
    print("=" * 60)
    total = passed + failed
    print(f"RESULTS: {passed}/{total} passed, {failed} failed")
    if failed == 0:
        print("ALL DISPLAY REFRESH TESTS PASSED")
    print("=" * 60)

    return failed == 0


if __name__ == '__main__':
    success = run_tests()
    sys.exit(0 if success else 1)
