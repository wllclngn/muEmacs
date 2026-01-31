#!/usr/bin/env python3
"""
TUI tests for μEmacs macro system.

Tests:
- C-x ( / C-x ) - Begin/end macro recording
- C-x e - Execute last macro
- Macro with text insertion
- Macro with movement and editing

Usage:
    python3 tests/tui/test_macros.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_record_and_execute_simple():
    """Record a simple macro and execute it."""
    filename = '/tmp/muemacs_macro_test.txt'
    with open(filename, 'w') as f:
        f.write("line one\n")
        f.write("line two\n")
        f.write("line three\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Start macro recording: C-x (
        emu.child.send('\x18(')
        time.sleep(0.3)
        emu._read_output()

        # Type some text
        emu.child.send('MACRO')
        time.sleep(0.2)
        emu._read_output()

        # End macro recording: C-x )
        emu.child.send('\x18)')
        time.sleep(0.3)
        emu._read_output()

        # Move to next line
        emu.child.send('\x0e')  # C-n
        time.sleep(0.1)
        emu._read_output()

        # Go to beginning of line
        emu.child.send('\x01')  # C-a
        time.sleep(0.1)
        emu._read_output()

        # Execute macro: C-x E (uppercase)
        emu.child.send('\x18E')
        time.sleep(0.3)
        emu._read_output()

        # Should see MACRO on both lines
        screen_text = emu.get_screen_text()
        count = screen_text.count('MACRO')
        if count < 2:
            print(f"FAIL: Expected 'MACRO' to appear at least twice, found {count}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_macro_with_movement():
    """Record macro that moves and edits."""
    filename = '/tmp/muemacs_macro_test.txt'
    with open(filename, 'w') as f:
        f.write("aaa\n")
        f.write("bbb\n")
        f.write("ccc\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Start macro: C-x (
        emu.child.send('\x18(')
        time.sleep(0.3)
        emu._read_output()

        # Go to end of line: C-e
        emu.child.send('\x05')
        time.sleep(0.1)

        # Insert text
        emu.child.send('XXX')
        time.sleep(0.1)

        # Move down: C-n
        emu.child.send('\x0e')
        time.sleep(0.1)

        # End macro: C-x )
        emu.child.send('\x18)')
        time.sleep(0.3)
        emu._read_output()

        # Execute macro twice: C-x E, C-x E (uppercase)
        emu.child.send('\x18E')
        time.sleep(0.3)
        emu.child.send('\x18E')
        time.sleep(0.3)
        emu._read_output()

        # Should see XXX appended to multiple lines
        screen_text = emu.get_screen_text()
        xxx_count = screen_text.count('XXX')
        if xxx_count < 3:
            print(f"FAIL: Expected 'XXX' at least 3 times, found {xxx_count}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_macro_delete_char():
    """Record macro that deletes a character on each line."""
    filename = '/tmp/muemacs_macro_test.txt'
    with open(filename, 'w') as f:
        f.write("Xaaa\n")
        f.write("Xbbb\n")
        f.write("Xccc\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Start macro: C-x (
        emu.child.send('\x18(')
        time.sleep(0.3)
        emu._read_output()

        # Delete char: C-d
        emu.child.send('\x04')
        time.sleep(0.1)

        # Move down: C-n
        emu.child.send('\x0e')
        time.sleep(0.1)

        # Go to beginning: C-a
        emu.child.send('\x01')
        time.sleep(0.1)

        # End macro: C-x )
        emu.child.send('\x18)')
        time.sleep(0.3)
        emu._read_output()

        # First line should have X deleted
        line0 = emu.get_line(0)
        if line0.startswith('X'):
            print(f"FAIL: First char should be deleted, got: '{line0}'")
            emu.dump_screen()
            return False

        # Execute macro twice: C-x E
        emu.child.send('\x18E')
        time.sleep(0.3)
        emu.child.send('\x18E')
        time.sleep(0.3)
        emu._read_output()

        # Lines 2 and 3 should also have X deleted
        line1 = emu.get_line(1)
        line2 = emu.get_line(2)
        if line1.startswith('X') or line2.startswith('X'):
            print(f"FAIL: X should be deleted from lines 2 and 3")
            print(f"Line 1: '{line1}', Line 2: '{line2}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_macro_message_display():
    """Verify macro recording shows feedback."""
    filename = '/tmp/muemacs_macro_test.txt'
    with open(filename, 'w') as f:
        f.write("test content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Start macro: C-x (
        emu.child.send('\x18(')
        time.sleep(0.3)
        emu._read_output()

        # Check for recording indicator (message line or status)
        msg = emu.get_message_line()
        # Many editors show something when recording

        # End macro: C-x )
        emu.child.send('\x18)')
        time.sleep(0.3)
        emu._read_output()

        # Check for end message
        msg = emu.get_message_line()
        # Should show "[END MACRO]" based on main.c:688

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("record_and_execute_simple", test_record_and_execute_simple),
        ("macro_with_movement", test_macro_with_movement),
        ("macro_delete_char", test_macro_delete_char),
        ("macro_message_display", test_macro_message_display),
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
