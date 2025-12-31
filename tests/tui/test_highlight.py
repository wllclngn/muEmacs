#!/usr/bin/env python3
"""
Automated TUI tests for μEmacs using pyte terminal emulator.
Tests cursor-line highlight, mode switching, and display rendering.

Usage:
    python3 tests/tui/test_highlight.py
    # Or with venv:
    .venv/bin/python tests/tui/test_highlight.py
"""

import sys
import time
from base import UEmacsTest


# =============================================================================
# Test Cases
# =============================================================================

def test_cursor_highlight_on_startup():
    """Cursor line should be highlighted on startup."""
    emu = UEmacsTest()
    try:
        emu.start()

        cursor_row = emu.get_cursor_row()
        has_hl = emu.has_highlight_at_row(cursor_row)

        if not has_hl:
            print("FAIL: No highlight at cursor row")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_highlight_follows_cursor():
    """Highlight should move when cursor moves down."""
    emu = UEmacsTest()
    try:
        emu.start()

        # Get initial state
        initial_row = emu.get_cursor_row()
        if not emu.has_highlight_at_row(initial_row):
            print(f"FAIL: No initial highlight at row {initial_row}")
            emu.dump_screen()
            return False

        # Move down with Ctrl-N (Emacs forward-line, works with evil_mode off)
        emu.send('\x0e')  # Ctrl-N
        new_row = emu.get_cursor_row()

        if new_row == initial_row:
            print("FAIL: Cursor didn't move (maybe only 1 line in file?)")
            emu.dump_screen()
            return False

        # Old row should NOT be highlighted
        if emu.has_highlight_at_row(initial_row):
            print(f"FAIL: Old row {initial_row} still highlighted")
            emu.dump_screen()
            return False

        # New row SHOULD be highlighted
        if not emu.has_highlight_at_row(new_row):
            print(f"FAIL: New row {new_row} not highlighted")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_highlight_in_insert_mode():
    """Highlight should persist when entering INSERT mode."""
    emu = UEmacsTest()
    try:
        emu.start()

        # Get initial editing position (should be row 0)
        initial_row = 0  # Cursor starts at top

        # Verify initial highlight
        if not emu.has_highlight_at_row(initial_row):
            print(f"FAIL: No initial highlight at row {initial_row}")
            emu.dump_screen()
            return False

        # Enter INSERT mode
        emu.send('i')
        time.sleep(0.2)
        emu._read_output()

        # In INSERT mode, the editing cursor stays on the same row
        # even though terminal cursor may move to message line
        # Check that the EDITING row (row 0) is still highlighted
        if not emu.has_highlight_at_row(initial_row):
            print(f"FAIL: Highlight missing in INSERT mode at row {initial_row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_highlight_after_enter():
    """Highlight should follow cursor after pressing Enter in INSERT mode."""
    emu = UEmacsTest()
    try:
        emu.start()

        initial_row = emu.get_cursor_row()

        # Enter INSERT mode and press Enter
        emu.send('i')
        emu.send_key('ENTER')

        new_row = emu.get_cursor_row()

        # Should have moved to next row
        if new_row <= initial_row:
            print(f"FAIL: Cursor didn't advance after Enter (was {initial_row}, now {new_row})")
            emu.dump_screen()
            return False

        # New row should be highlighted
        if not emu.has_highlight_at_row(new_row):
            print(f"FAIL: Highlight missing after Enter at row {new_row}")
            emu.dump_screen()
            return False

        # Old row should NOT be highlighted
        if emu.has_highlight_at_row(initial_row):
            print(f"FAIL: Old row {initial_row} still highlighted after Enter")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


# =============================================================================
# Main
# =============================================================================

def run_tests():
    """Run all tests and report results."""
    tests = [
        ("cursor_highlight_on_startup", test_cursor_highlight_on_startup),
        ("highlight_follows_cursor", test_highlight_follows_cursor),
        ("highlight_in_insert_mode", test_highlight_in_insert_mode),
        ("highlight_after_enter", test_highlight_after_enter),
    ]

    passed = 0
    failed = 0

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
            failed += 1

    print()
    print(f"Results: {passed} passed, {failed} failed")
    return failed == 0


if __name__ == '__main__':
    # Enable debug output with DEBUG=1
    success = run_tests()
    sys.exit(0 if success else 1)
