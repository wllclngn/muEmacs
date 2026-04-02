#!/usr/bin/env python3
"""
TUI tests for Evil mode f/F/t/T character motions.

Usage:
    python3 tests/tui/test_evil_char_motion.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def test_find_char_forward():
    """fa should move cursor to next 'a'."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world apple\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)
        initial_col = emu.get_cursor_pos()[1]

        # Find 'a' with fa
        emu.send('f')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.1)
        emu._read_output()

        new_col = emu.get_cursor_pos()[1]
        # 'a' in "apple" is at column 12
        expected_col = 12

        if new_col != expected_col:
            print(f"FAIL: Expected cursor at col {expected_col}, got {new_col}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_find_char_backward():
    """Fa from end should find 'a' backward."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world apple\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Go to end of line
        emu.send('$')
        time.sleep(0.1)
        emu._read_output()

        # Find 'o' backward with Fo
        emu.send('F')
        time.sleep(0.05)
        emu.send('o')
        time.sleep(0.1)
        emu._read_output()

        new_col = emu.get_cursor_pos()[1]
        # Last 'o' before end is in "world" at column 7
        expected_col = 7

        if new_col != expected_col:
            print(f"FAIL: Expected cursor at col {expected_col}, got {new_col}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_till_char_forward():
    """ta should move cursor to just before next 'a'."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world apple\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Till 'a' with ta
        emu.send('t')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.1)
        emu._read_output()

        new_col = emu.get_cursor_pos()[1]
        # 'a' in "apple" is at column 12, so till is column 11
        expected_col = 11

        if new_col != expected_col:
            print(f"FAIL: Expected cursor at col {expected_col}, got {new_col}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_repeat_find():
    """; should repeat last f command."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("aaa bbb aaa ccc aaa\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Find first 'b' with fb
        emu.send('f')
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.1)
        emu._read_output()

        first_col = emu.get_cursor_pos()[1]
        # First 'b' is at column 4
        if first_col != 4:
            print(f"FAIL: First fb, expected col 4, got {first_col}")
            emu.dump_screen()
            return False

        # Repeat with ;
        emu.send(';')
        time.sleep(0.1)
        emu._read_output()

        second_col = emu.get_cursor_pos()[1]
        # Second 'b' is at column 5
        if second_col != 5:
            print(f"FAIL: After ;, expected col 5, got {second_col}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_count_find():
    """2fa should find second 'a'."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("aaa bbb aaa ccc aaa\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Find second 'a' with 2fa
        emu.send('2')
        emu.send('f')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.1)
        emu._read_output()

        col = emu.get_cursor_pos()[1]
        # Starting at 0, 2fa finds: 1st 'a' at col 1, 2nd 'a' at col 2
        expected_col = 2

        if col != expected_col:
            print(f"FAIL: 2fa expected col {expected_col}, got {col}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_till_char():
    """dta should delete from cursor to just before 'a'."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello apple\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete till 'a' with dta
        emu.send('d')
        emu.send('t')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        # Should delete "hello " leaving "apple"
        if not line.startswith("apple"):
            print(f"FAIL: Expected line to start with 'apple', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("find_char_forward (fa)", test_find_char_forward),
        ("find_char_backward (Fa)", test_find_char_backward),
        ("till_char_forward (ta)", test_till_char_forward),
        ("repeat_find (;)", test_repeat_find),
        ("count_find (2fa)", test_count_find),
        ("delete_till_char (dta)", test_delete_till_char),
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
