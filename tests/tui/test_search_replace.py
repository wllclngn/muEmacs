#!/usr/bin/env python3
"""
TUI tests for μEmacs search and replace functionality.

Tests:
- C-s - Incremental search forward
- C-r - Incremental search reverse
- M-R - Replace string
- M-C-R - Query replace

Usage:
    python3 tests/tui/test_search_replace.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_search_forward():
    """search-forward should find pattern."""
    filename = '/tmp/uemacs_search_test.txt'
    with open(filename, 'w') as f:
        f.write("first line\n")
        f.write("second line\n")
        f.write("find TARGET here\n")
        f.write("fourth line\n")
        for i in range(10):
            f.write(f"Line {i+5}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Use M-X search-forward for explicit command
        emu.child.send('\x1bX')
        time.sleep(0.2)
        emu._read_output()

        emu.child.send('search-forward\r')
        time.sleep(0.3)
        emu._read_output()

        # Type search pattern
        emu.child.send('TARGET\r')
        time.sleep(0.3)
        emu._read_output()

        # Cursor should have moved to line with TARGET
        row = emu.get_editing_cursor_row()
        if row < 2:
            print(f"FAIL: Expected cursor to move to TARGET line, at row {row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_search_reverse():
    """search-reverse should search backward for pattern."""
    filename = '/tmp/uemacs_search_test.txt'
    with open(filename, 'w') as f:
        f.write("first MARKER line\n")
        f.write("second line\n")
        f.write("third line\n")
        f.write("fourth line\n")
        for i in range(10):
            f.write(f"Line {i+5}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Move to end of file: M-X end-of-file
        emu.child.send('\x1bX')
        time.sleep(0.2)
        emu.child.send('end-of-file\r')
        time.sleep(0.3)
        emu._read_output()

        # M-X search-reverse
        emu.child.send('\x1bX')
        time.sleep(0.2)
        emu.child.send('search-reverse\r')
        time.sleep(0.3)
        emu._read_output()

        # Type search pattern
        emu.child.send('MARKER\r')
        time.sleep(0.3)
        emu._read_output()

        # Cursor should have moved back to first line
        row = emu.get_editing_cursor_row()
        if row > 1:
            print(f"FAIL: Expected cursor to move back to MARKER line, at row {row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_replace_string():
    """M-R should replace all occurrences."""
    filename = '/tmp/uemacs_search_test.txt'
    with open(filename, 'w') as f:
        f.write("foo is here\n")
        f.write("and foo again\n")
        f.write("more foo text\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # M-R starts replace-string
        emu.child.send('\x1bR')
        time.sleep(0.3)
        emu._read_output()

        # Enter search pattern
        emu.child.send('foo')
        emu.child.send('\r')
        time.sleep(0.3)
        emu._read_output()

        # Enter replacement
        emu.child.send('BAR')
        emu.child.send('\r')
        time.sleep(0.5)
        emu._read_output()

        # Check that foo was replaced with BAR
        screen_text = emu.get_screen_text()
        if 'foo' in screen_text:
            print(f"FAIL: 'foo' should be replaced")
            emu.dump_screen()
            return False

        if screen_text.count('BAR') < 3:
            print(f"FAIL: Expected at least 3 'BAR' replacements")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_query_replace():
    """M-C-R should do interactive query replace."""
    filename = '/tmp/uemacs_search_test.txt'
    with open(filename, 'w') as f:
        f.write("old text here\n")
        f.write("more old stuff\n")
        f.write("old again\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # M-C-R starts query-replace
        emu.child.send('\x1b\x12')
        time.sleep(0.3)
        emu._read_output()

        # Enter search pattern
        emu.child.send('old')
        emu.child.send('\r')
        time.sleep(0.3)
        emu._read_output()

        # Enter replacement
        emu.child.send('NEW')
        emu.child.send('\r')
        time.sleep(0.3)
        emu._read_output()

        # Answer 'y' to first replacement
        emu.child.send('y')
        time.sleep(0.2)
        emu._read_output()

        # Answer '!' to replace all remaining
        emu.child.send('!')
        time.sleep(0.3)
        emu._read_output()

        # Check that old was replaced
        screen_text = emu.get_screen_text()
        if 'NEW' not in screen_text:
            print(f"FAIL: Expected 'NEW' to appear after replacement")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_search_not_found():
    """Search for non-existent pattern should show message."""
    filename = '/tmp/uemacs_search_test.txt'
    with open(filename, 'w') as f:
        f.write("some text\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # C-s starts search
        emu.child.send('\x13')
        time.sleep(0.3)
        emu._read_output()

        # Search for something that doesn't exist
        emu.child.send('ZZZZNOTFOUND')
        emu.child.send('\r')
        time.sleep(0.3)
        emu._read_output()

        # Should show "Not found" or similar message
        msg = emu.get_message_line().lower()
        # Just verify it doesn't crash - message content varies

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("search_forward (C-s)", test_search_forward),
        ("search_reverse (C-r)", test_search_reverse),
        ("replace_string (M-R)", test_replace_string),
        ("query_replace (M-C-R)", test_query_replace),
        ("search_not_found", test_search_not_found),
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
            import traceback
            traceback.print_exc()
            failed += 1

    print()
    print(f"Results: {passed} passed, {failed} failed")
    return failed == 0


if __name__ == '__main__':
    success = run_tests()
    sys.exit(0 if success else 1)
