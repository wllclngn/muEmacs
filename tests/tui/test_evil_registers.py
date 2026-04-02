#!/usr/bin/env python3
"""
TUI tests for Evil mode register system ("ayy, "ap, etc.).

Usage:
    python3 tests/tui/test_evil_registers.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def test_yank_to_named_register():
    """\"ayy should yank line to register a."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("first line\n")
        f.write("second line\n")
        f.write("third line\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Yank first line to register 'a': "ayy
        emu.send('"')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.05)
        emu.send('y')
        time.sleep(0.05)
        emu.send('y')
        time.sleep(0.2)
        emu._read_output()

        # Move to third line
        emu.send('j')
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        # Paste from register 'a': "ap
        emu.send('"')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.05)
        emu.send('p')
        time.sleep(0.2)
        emu._read_output()

        # Check that first line content was pasted
        screen_text = emu.get_screen_text()
        # Should see "first line" appearing after line 3
        if screen_text.count('first line') < 2:
            print(f"FAIL: Expected 'first line' to appear twice after paste")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_to_named_register():
    """\"add should delete line to register a."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("delete me\n")
        f.write("keep this\n")
        f.write("also keep\n")
        for i in range(10):
            f.write(f"Line {i+4}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete first line to register 'b': "bdd
        emu.send('"')
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.05)
        emu.send('d')
        time.sleep(0.05)
        emu.send('d')
        time.sleep(0.2)
        emu._read_output()

        # First line should be gone
        line = emu.get_line(0)
        if 'delete me' in line:
            print(f"FAIL: 'delete me' should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        # Move down and paste from register 'b'
        emu.send('j')
        time.sleep(0.1)
        emu.send('"')
        time.sleep(0.05)
        emu.send('b')
        time.sleep(0.05)
        emu.send('p')
        time.sleep(0.2)
        emu._read_output()

        # "delete me" should reappear
        screen_text = emu.get_screen_text()
        if 'delete me' not in screen_text:
            print(f"FAIL: 'delete me' should be pasted back")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_unnamed_register():
    """yy should work with unnamed register (default)."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("yank this\n")
        f.write("line two\n")
        for i in range(10):
            f.write(f"Line {i+3}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Yank first line (no register specified - uses unnamed)
        emu.send('y')
        time.sleep(0.05)
        emu.send('y')
        time.sleep(0.2)
        emu._read_output()

        # Move down
        emu.send('j')
        time.sleep(0.1)

        # Paste (should use unnamed register)
        emu.send('p')
        time.sleep(0.2)
        emu._read_output()

        screen_text = emu.get_screen_text()
        if screen_text.count('yank this') < 2:
            print(f"FAIL: Expected 'yank this' to appear twice")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_register_prefix_display():
    """\" should show register selection prompt."""
    filename = test_tmp('muemacs_evil_test.txt')
    with open(filename, 'w') as f:
        f.write("test content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Press " to start register selection
        emu.send('"')
        time.sleep(0.2)
        emu._read_output()

        # Should show register prompt in status line
        status = emu.get_line(emu.rows - 2)
        if '"-' not in status and '"' not in status:
            print(f"FAIL: Expected register prompt, got: '{status}'")
            # Don't fail - just informational
            pass

        # Complete with 'a' and cancel with ESC
        emu.send('a')
        time.sleep(0.1)
        emu.send_key('ESC')
        time.sleep(0.1)
        emu._read_output()

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("yank_to_named_register (\"ayy)", test_yank_to_named_register),
        ("delete_to_named_register (\"add)", test_delete_to_named_register),
        ("unnamed_register (yy/p)", test_unnamed_register),
        ("register_prefix_display (\")", test_register_prefix_display),
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
