#!/usr/bin/env python3
"""
TUI tests for Evil mode Replace mode (R).

Replace mode should overwrite characters instead of inserting.

Usage:
    python3 tests/tui/test_evil_replace_mode.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def test_replace_mode_entry():
    """'R' should enter replace mode."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter replace mode
        emu.send('R')
        time.sleep(0.1)
        emu._read_output()

        # Check message shows REPLACE
        msg = emu.get_message_line()
        status = emu.get_line(emu.rows - 2)

        if 'REPLACE' not in msg and 'REPLACE' not in status:
            print(f"FAIL: Expected REPLACE indicator")
            print(f"Message: '{msg}'")
            print(f"Status: '{status}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_replace_mode_overwrites():
    """In replace mode, typing should overwrite existing characters."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter replace mode
        emu.send('R')
        time.sleep(0.1)

        # Type replacement text
        emu.send('XXXXX')
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        # "hello" should be replaced with "XXXXX", leaving " world"
        if not line.startswith("XXXXX world"):
            print(f"FAIL: Expected 'XXXXX world', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_replace_mode_preserves_length():
    """Replace mode should not change line length when replacing same amount."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("abcdefghij\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        original_line = emu.get_line(0)
        original_len = len(original_line.rstrip())

        # Enter replace mode and replace 3 chars
        emu.send('R')
        time.sleep(0.1)
        emu.send('123')
        time.sleep(0.1)
        emu.send('\x1b')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        new_len = len(line.rstrip())

        if new_len != original_len:
            print(f"FAIL: Line length changed from {original_len} to {new_len}")
            emu.dump_screen()
            return False

        if not line.startswith("123defghij"):
            print(f"FAIL: Expected '123defghij', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_replace_mode_at_eol_inserts():
    """At end of line, replace mode should insert (like vim)."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("abc\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Go to end of line
        emu.send('$')
        time.sleep(0.1)

        # Enter replace mode and type
        emu.send('R')
        time.sleep(0.1)
        emu.send('XYZ')
        time.sleep(0.1)
        emu.send('\x1b')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        # Should be "abcXYZ" (replaced 'c' with 'X', then inserted 'Y' and 'Z')
        # or "abXYZ" if cursor was on 'c' when R was pressed
        if 'XYZ' not in line:
            print(f"FAIL: Expected 'XYZ' in line, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_replace_mode_exit_esc():
    """ESC should exit replace mode."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter replace mode
        emu.send('R')
        time.sleep(0.1)
        emu.send('X')
        time.sleep(0.1)

        # Exit with ESC
        emu.send('\x1b')
        time.sleep(0.1)
        emu._read_output()

        # Now in normal mode - 'l' should move, not replace
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        # If still in replace mode, 'l' would overwrite
        # 'X' replaced 'h', then 'l' should just move cursor
        if line.startswith("Xlllo"):  # If 'l' was typed as replacement
            print(f"FAIL: Still in replace mode, 'l' was typed as replacement")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("replace_mode_entry (R)", test_replace_mode_entry),
        ("replace_mode_overwrites", test_replace_mode_overwrites),
        ("replace_mode_preserves_length", test_replace_mode_preserves_length),
        ("replace_mode_at_eol_inserts", test_replace_mode_at_eol_inserts),
        ("replace_mode_exit_esc", test_replace_mode_exit_esc),
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
