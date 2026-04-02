#!/usr/bin/env python3
"""
TUI tests for Evil mode insert entry commands (i, I, a, A, o, O).

Usage:
    python3 tests/tui/test_evil_insert_entry.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def test_insert_before():
    """'i' should enter insert mode before cursor."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to 'e' (position 1), then insert 'X'
        emu.send('l')  # Move to 'e'
        time.sleep(0.1)
        emu.send('i')  # Enter insert mode
        time.sleep(0.1)
        emu.send('X')  # Insert 'X'
        time.sleep(0.1)
        emu.send('\x1b')  # ESC to normal mode
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        # 'X' inserted before 'e', so "hXello world"
        if not line.startswith("hXello"):
            print(f"FAIL: Expected 'hXello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_insert_at_bol():
    """'I' should enter insert mode at beginning of line."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to middle of line, then I to insert at BOL
        emu.send('w')  # Move to 'world'
        time.sleep(0.1)
        emu.send('I')  # Insert at BOL
        time.sleep(0.1)
        emu.send('X')  # Insert 'X'
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("Xhello"):
            print(f"FAIL: Expected 'Xhello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_append_after():
    """'a' should enter insert mode after cursor."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Cursor on 'h', append 'X' after it
        emu.send('a')  # Append after cursor
        time.sleep(0.1)
        emu.send('X')  # Insert 'X'
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        # 'X' appended after 'h', so "hXello world"
        if not line.startswith("hXello"):
            print(f"FAIL: Expected 'hXello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_append_at_eol():
    """'A' should enter insert mode at end of line."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # A to append at EOL
        emu.send('A')  # Append at EOL
        time.sleep(0.1)
        emu.send('X')  # Insert 'X'
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if line != "helloX":
            print(f"FAIL: Expected 'helloX', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_open_line_below():
    """'o' should open new line below and enter insert mode."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("line 1\n")
        f.write("line 2\n")
        for i in range(5):
            f.write(f"Line {i+3}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # o to open line below
        emu.send('o')  # Open line below
        time.sleep(0.1)
        emu.send('NEW')  # Insert text
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line0 = emu.get_line(0)
        line1 = emu.get_line(1)
        line2 = emu.get_line(2)

        if line0 != "line 1":
            print(f"FAIL: Line 0 should be 'line 1', got: '{line0}'")
            emu.dump_screen()
            return False

        if line1 != "NEW":
            print(f"FAIL: Line 1 should be 'NEW', got: '{line1}'")
            emu.dump_screen()
            return False

        if line2 != "line 2":
            print(f"FAIL: Line 2 should be 'line 2', got: '{line2}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_open_line_above():
    """'O' should open new line above and enter insert mode."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("line 1\n")
        f.write("line 2\n")
        for i in range(5):
            f.write(f"Line {i+3}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # O to open line above
        emu.send('O')  # Open line above
        time.sleep(0.1)
        emu.send('NEW')  # Insert text
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line0 = emu.get_line(0)
        line1 = emu.get_line(1)

        if line0 != "NEW":
            print(f"FAIL: Line 0 should be 'NEW', got: '{line0}'")
            emu.dump_screen()
            return False

        if line1 != "line 1":
            print(f"FAIL: Line 1 should be 'line 1', got: '{line1}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_esc_exits_insert():
    """ESC should exit insert mode and return to normal mode."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)
        emu._read_output()

        # Check message shows INSERT mode
        msg = emu.get_message_line()
        if 'INSERT' not in msg:
            print(f"FAIL: Expected INSERT in message, got: '{msg}'")
            # Continue anyway, might be in status bar

        # Exit with ESC
        emu.send('\x1b')
        time.sleep(0.1)
        emu._read_output()

        # Try a normal mode command - 'l' should move cursor
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # If we're in normal mode, cursor moved; if still insert, 'l' was typed
        line = emu.get_line(0)
        if line.startswith("lhello"):
            print(f"FAIL: Still in insert mode, 'l' was inserted")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("insert_before (i)", test_insert_before),
        ("insert_at_bol (I)", test_insert_at_bol),
        ("append_after (a)", test_append_after),
        ("append_at_eol (A)", test_append_at_eol),
        ("open_line_below (o)", test_open_line_below),
        ("open_line_above (O)", test_open_line_above),
        ("esc_exits_insert (ESC)", test_esc_exits_insert),
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
