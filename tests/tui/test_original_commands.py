#!/usr/bin/env python3
"""
TUI tests for original uEmacs commands to ensure they still work.

Usage:
    python3 tests/tui/test_original_commands.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_mx_command():
    """M-x should open command prompt."""
    filename = '/tmp/uemacs_test.txt'
    with open(filename, 'w') as f:
        f.write("test content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        # Start without evil mode to test emacs-style commands
        emu.start(filename=filename, evil_mode=False)

        # Send M-x (ESC x)
        emu.child.send('\x1bx')
        time.sleep(0.3)
        emu._read_output()

        # Should see command prompt
        msg = emu.get_message_line()
        # Cancel with C-g
        emu.send_key('CTRL_G')
        time.sleep(0.1)
        emu._read_output()

        return True
    finally:
        emu.quit()


def test_ctrl_k_kill_line():
    """C-k should kill to end of line."""
    filename = '/tmp/uemacs_test.txt'
    with open(filename, 'w') as f:
        f.write("hello world test\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # C-k kills to end of line
        emu.child.send('\x0b')  # C-k
        time.sleep(0.2)
        emu._read_output()

        # Line should be empty or just have minimal content
        line = emu.get_line(0)
        if 'world' in line or 'test' in line:
            print(f"FAIL: Line should be killed, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_ctrl_y_yank():
    """C-y should yank killed text."""
    filename = '/tmp/uemacs_test.txt'
    with open(filename, 'w') as f:
        f.write("kill this line\n")
        f.write("second line\n")
        for i in range(10):
            f.write(f"Line {i+3}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # C-k kills line
        emu.child.send('\x0b')  # C-k
        time.sleep(0.2)
        emu._read_output()

        # Move to next line
        emu.child.send('\x0e')  # C-n (forward line)
        time.sleep(0.1)
        emu._read_output()

        # C-y yanks
        emu.child.send('\x19')  # C-y
        time.sleep(0.2)
        emu._read_output()

        # Killed text should appear on this line
        screen_text = emu.get_screen_text()
        if screen_text.count('kill this line') < 1:
            print(f"FAIL: Killed text not yanked")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_ctrl_s_search():
    """C-s should start forward search."""
    filename = '/tmp/uemacs_test.txt'
    with open(filename, 'w') as f:
        f.write("first line\n")
        f.write("second line\n")
        f.write("target word here\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # C-s starts search
        emu.child.send('\x13')  # C-s
        time.sleep(0.2)
        emu._read_output()

        # Type search term and press enter
        emu.child.send('target')
        emu.send_key('ENTER')
        time.sleep(0.3)
        emu._read_output()

        # Cursor should be on or near the line with "target"
        # Check that we moved from first line
        row = emu.get_editing_cursor_row()
        if row < 1:
            print(f"FAIL: Search should move cursor, at row {row}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("mx_command (M-x)", test_mx_command),
        ("ctrl_k_kill_line (C-k)", test_ctrl_k_kill_line),
        ("ctrl_y_yank (C-y)", test_ctrl_y_yank),
        ("ctrl_s_search (C-s)", test_ctrl_s_search),
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
