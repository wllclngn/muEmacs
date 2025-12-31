#!/usr/bin/env python3
"""
TUI tests for Evil mode dot repeat (.).

Usage:
    python3 tests/tui/test_evil_dot_repeat.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_repeat_dd():
    """dd then . should delete another line."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("line one\n")
        f.write("line two\n")
        f.write("line three\n")
        f.write("line four\n")
        for i in range(10):
            f.write(f"Line {i+5}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete first line with dd
        emu.send('d')
        time.sleep(0.05)
        emu.send('d')
        time.sleep(0.2)
        emu._read_output()

        # First line should be gone
        line0 = emu.get_line(0)
        if 'line one' in line0:
            print(f"FAIL: 'line one' should be deleted after dd")
            emu.dump_screen()
            return False

        # Press . to repeat
        emu.send('.')
        time.sleep(0.2)
        emu._read_output()

        # Now second line (which was "line two") should also be gone
        line0 = emu.get_line(0)
        if 'line two' in line0:
            print(f"FAIL: 'line two' should be deleted after .")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_repeat_x():
    """x then . should delete another character."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("abcdefgh\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete first char with x
        emu.send('x')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        if line.startswith('a'):
            print(f"FAIL: 'a' should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        # Press . to repeat
        emu.send('.')
        time.sleep(0.2)
        emu._read_output()

        # Now 'b' should also be gone
        line = emu.get_line(0)
        if line.startswith('b'):
            print(f"FAIL: 'b' should be deleted after ., got: '{line}'")
            emu.dump_screen()
            return False

        # Should start with 'c' now
        if not line.startswith('c'):
            print(f"FAIL: Expected line to start with 'c', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_repeat_diw():
    """diw then . should delete another word."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("apple banana cherry date\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete inner word (diw)
        emu.send('d')
        time.sleep(0.05)
        emu.send('i')
        time.sleep(0.05)
        emu.send('w')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        if 'apple' in line:
            print(f"FAIL: 'apple' should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        # Move to next word (banana)
        emu.send('w')
        time.sleep(0.1)
        emu._read_output()

        # Press . to repeat diw
        emu.send('.')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        if 'banana' in line:
            print(f"FAIL: 'banana' should be deleted after ., got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_repeat_with_count():
    """2. should repeat with new count."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        for i in range(15):
            f.write(f"Line {i+1}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete one line with dd
        emu.send('d')
        time.sleep(0.05)
        emu.send('d')
        time.sleep(0.2)
        emu._read_output()

        # Line 1 should be gone
        line = emu.get_line(0)
        if 'Line 1' in line:
            print(f"FAIL: 'Line 1' should be deleted")
            emu.dump_screen()
            return False

        # Repeat with 2. (should delete 2 lines)
        emu.send('2')
        time.sleep(0.05)
        emu.send('.')
        time.sleep(0.2)
        emu._read_output()

        # Lines 2 and 3 should be gone (now at Line 4)
        line = emu.get_line(0)
        if 'Line 2' in line or 'Line 3' in line:
            print(f"FAIL: Lines 2 and 3 should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("repeat_dd (dd.)", test_repeat_dd),
        ("repeat_x (x.)", test_repeat_x),
        ("repeat_diw (diw.)", test_repeat_diw),
        ("repeat_with_count (dd 2.)", test_repeat_with_count),
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
