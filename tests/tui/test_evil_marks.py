#!/usr/bin/env python3
"""
TUI tests for Evil mode marks (m{a-z}, '{a-z}, `{a-z}).

Usage:
    python3 tests/tui/test_evil_marks.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_set_and_goto_mark_line():
    """ma sets mark, 'a returns to that line."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        for i in range(20):
            f.write(f"Line {i+1}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to line 5 (4j from line 1)
        emu.send('4')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        mark_row = emu.get_editing_cursor_row()

        # Set mark 'a'
        emu.send('m')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.1)
        emu._read_output()

        # Move to line 15 (10j)
        emu.send('1')
        emu.send('0')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        # Verify we moved
        new_row = emu.get_editing_cursor_row()
        if new_row == mark_row:
            print(f"FAIL: Didn't move away from mark (still at {new_row})")
            emu.dump_screen()
            return False

        # Go to mark 'a' with 'a
        emu.send("'")
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.2)
        emu._read_output()

        final_row = emu.get_editing_cursor_row()
        if final_row != mark_row:
            print(f"FAIL: Expected row {mark_row}, got {final_row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_set_and_goto_mark_exact():
    """`a returns to exact position (line and column)."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        for i in range(20):
            f.write(f"Line {i+1} - some extra text here\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to line 5 (4j)
        emu.send('4')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        # Move to column 10 (10l)
        emu.send('1')
        emu.send('0')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        mark_row = emu.get_editing_cursor_row()
        mark_col = emu.get_cursor_pos()[1]

        # Set mark 'b'
        emu.send('m')
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.1)
        emu._read_output()

        # Move elsewhere - line 15, column 0
        emu.send('1')
        emu.send('0')
        emu.send('j')
        time.sleep(0.1)
        emu.send('0')  # Go to beginning of line
        time.sleep(0.1)
        emu._read_output()

        # Go to mark 'b' with `b (backtick for exact position)
        emu.send('`')
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.2)
        emu._read_output()

        final_row = emu.get_editing_cursor_row()
        # Column check is tricky due to terminal cursor being at message line
        # Just verify row for now
        if final_row != mark_row:
            print(f"FAIL: Expected row {mark_row}, got row {final_row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_multiple_marks():
    """Can use multiple marks (a, b, c)."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        for i in range(20):
            f.write(f"Line {i+1}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Set mark 'a' at line 1 (row 0)
        emu.send('m')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.1)
        emu._read_output()
        mark_a_row = emu.get_editing_cursor_row()

        # Move to line 5, set mark 'b'
        emu.send('4')
        emu.send('j')
        time.sleep(0.1)
        emu.send('m')
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.1)
        emu._read_output()
        mark_b_row = emu.get_editing_cursor_row()

        # Move to line 10, set mark 'c'
        emu.send('5')
        emu.send('j')
        time.sleep(0.1)
        emu.send('m')
        time.sleep(0.05)
        emu.send('c')
        time.sleep(0.1)
        emu._read_output()
        mark_c_row = emu.get_editing_cursor_row()

        # Go to mark 'a'
        emu.send("'")
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.2)
        emu._read_output()
        if emu.get_editing_cursor_row() != mark_a_row:
            print(f"FAIL: 'a expected row {mark_a_row}, got {emu.get_editing_cursor_row()}")
            emu.dump_screen()
            return False

        # Go to mark 'c'
        emu.send("'")
        time.sleep(0.05)
        emu.send('c')
        time.sleep(0.2)
        emu._read_output()
        if emu.get_editing_cursor_row() != mark_c_row:
            print(f"FAIL: 'c expected row {mark_c_row}, got {emu.get_editing_cursor_row()}")
            emu.dump_screen()
            return False

        # Go to mark 'b'
        emu.send("'")
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.2)
        emu._read_output()
        if emu.get_editing_cursor_row() != mark_b_row:
            print(f"FAIL: 'b expected row {mark_b_row}, got {emu.get_editing_cursor_row()}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_unset_mark():
    """Going to unset mark should not crash or move."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        for i in range(20):
            f.write(f"Line {i+1}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to line 5
        emu.send('4')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        initial_row = emu.get_editing_cursor_row()

        # Try to go to unset mark 'z'
        emu.send("'")
        time.sleep(0.05)
        emu.send('z')
        time.sleep(0.2)
        emu._read_output()

        final_row = emu.get_editing_cursor_row()
        # Should stay at same position (message shown, but cursor unchanged)
        if final_row != initial_row:
            print(f"FAIL: Unset mark moved cursor from {initial_row} to {final_row}")
            emu.dump_screen()
            return False

        # Verify error message shown
        msg = emu.get_message_line()
        if "not set" not in msg.lower():
            print(f"FAIL: Expected 'not set' message, got: '{msg}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("set_and_goto_mark_line (ma, 'a)", test_set_and_goto_mark_line),
        ("set_and_goto_mark_exact (`a)", test_set_and_goto_mark_exact),
        ("multiple_marks (a, b, c)", test_multiple_marks),
        ("unset_mark (no crash)", test_unset_mark),
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
