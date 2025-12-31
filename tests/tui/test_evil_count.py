#!/usr/bin/env python3
"""
TUI tests for Evil mode count prefix system.
Tests: 5j, 3w, 2dd, 3x, etc.

Usage:
    python3 tests/tui/test_evil_count.py
    # Or with venv:
    .venv/bin/python tests/tui/test_evil_count.py
"""

import os
import sys
import time

# Add parent dir to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from base import UEmacsTest


def test_count_motion_down():
    """5j should move cursor down 5 lines."""
    emu = UEmacsTest()
    try:
        emu.start(evil_mode=True)

        initial_row = emu.get_cursor_row()

        # Type 5j
        emu.send('5')
        time.sleep(0.1)
        emu._read_output()
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        new_row = emu.get_cursor_row()
        expected_row = initial_row + 5

        if new_row != expected_row:
            print(f"FAIL: Expected cursor at row {expected_row}, got {new_row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_count_motion_up():
    """3k from line 5 should move cursor up 3 lines."""
    emu = UEmacsTest()
    try:
        emu.start(evil_mode=True)

        # First move down 5 lines
        emu.send('5')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        middle_row = emu.get_cursor_row()

        # Then 3k
        emu.send('3')
        emu.send('k')
        time.sleep(0.1)
        emu._read_output()

        new_row = emu.get_cursor_row()
        expected_row = middle_row - 3

        if new_row != expected_row:
            print(f"FAIL: Expected cursor at row {expected_row}, got {new_row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_count_delete_chars():
    """3x should delete 3 characters."""
    # Create test file with known content
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("ABCDEFGHIJ\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete 3 chars with 3x
        emu.send('3')
        emu.send('x')
        time.sleep(0.1)
        emu._read_output()

        # First line should now be "DEFGHIJ"
        line = emu.get_line(0)
        if not line.startswith("DEFGHIJ"):
            print(f"FAIL: Expected line to start with 'DEFGHIJ', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_count_delete_lines():
    """2dd should delete 2 lines."""
    # Create test file with known content
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("Line 1\n")
        f.write("Line 2\n")
        f.write("Line 3\n")
        f.write("Line 4\n")
        f.write("Line 5\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete 2 lines with 2dd
        emu.send('2')
        emu.send('d')
        emu.send('d')
        time.sleep(0.2)
        emu._read_output()

        # First line should now be "Line 3"
        line = emu.get_line(0)
        if not line.startswith("Line 3"):
            print(f"FAIL: Expected first line to be 'Line 3', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_count_with_operator():
    """d3w should delete 3 words."""
    # Create test file with known content
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("one two three four five six\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete 3 words with d3w
        emu.send('d')
        emu.send('3')
        emu.send('w')
        time.sleep(0.2)
        emu._read_output()

        # First line should now be "four five six"
        line = emu.get_line(0)
        if not line.startswith("four"):
            print(f"FAIL: Expected line to start with 'four', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_goto_line_with_count():
    """5G should go to line 5."""
    emu = UEmacsTest()
    try:
        emu.start(evil_mode=True)

        # Go to line 5 with 5G
        emu.send('5')
        emu.send('G')
        time.sleep(0.1)
        emu._read_output()

        row = emu.get_cursor_row()
        # Line 5 is row 4 (0-indexed)
        expected_row = 4

        if row != expected_row:
            print(f"FAIL: Expected cursor at row {expected_row} (line 5), got row {row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_gg_goes_to_top():
    """gg should go to first line."""
    emu = UEmacsTest()
    try:
        emu.start(evil_mode=True)

        # Move down first
        emu.send('5')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        # Then gg to top
        emu.send('g')
        emu.send('g')
        time.sleep(0.1)
        emu._read_output()

        row = emu.get_cursor_row()
        if row != 0:
            print(f"FAIL: Expected cursor at row 0, got {row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("count_motion_down (5j)", test_count_motion_down),
        ("count_motion_up (3k)", test_count_motion_up),
        ("count_delete_chars (3x)", test_count_delete_chars),
        ("count_delete_lines (2dd)", test_count_delete_lines),
        ("count_with_operator (d3w)", test_count_with_operator),
        ("goto_line_with_count (5G)", test_goto_line_with_count),
        ("gg_goes_to_top", test_gg_goes_to_top),
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
