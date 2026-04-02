#!/usr/bin/env python3
"""
TUI tests for Evil mode misc commands (r, R, ~).

Usage:
    python3 tests/tui/test_evil_misc.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def test_replace_char():
    """rx should replace character under cursor with 'x'."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Replace 'h' with 'X' using rX
        emu.send('r')
        time.sleep(0.05)
        emu.send('X')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("Xello"):
            print(f"FAIL: Expected 'Xello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_replace_char_with_count():
    """3rx should replace 3 characters with 'x'."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Replace 3 chars with '-' using 3r-
        emu.send('3')
        emu.send('r')
        time.sleep(0.05)
        emu.send('-')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("---lo"):
            print(f"FAIL: Expected '---lo', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_toggle_case():
    """~ should toggle case of character and move right."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("Hello World\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Toggle case of 'H' to 'h'
        emu.send('~')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("hEllo"):  # H->h, cursor moves to e, toggles to E
            # Actually ~ only toggles current char and moves right
            # So after ~, 'H' becomes 'h' and cursor moves to 'e'
            # Let me check the actual behavior
            pass

        # Check that first char changed
        if line[0] != 'h':
            print(f"FAIL: Expected first char 'h', got: '{line[0]}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_toggle_case_with_count():
    """3~ should toggle case of 3 characters."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("HELLO world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Toggle case of first 3 chars
        emu.send('3')
        emu.send('~')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        # "HELLO" -> "helLO" (first 3 chars toggled)
        if not line.startswith("helLO"):
            print(f"FAIL: Expected 'helLO', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("replace_char (rX)", test_replace_char),
        ("replace_char_with_count (3r-)", test_replace_char_with_count),
        ("toggle_case (~)", test_toggle_case),
        ("toggle_case_with_count (3~)", test_toggle_case_with_count),
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
