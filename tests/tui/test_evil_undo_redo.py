#!/usr/bin/env python3
"""
TUI tests for Evil mode undo/redo (u, Ctrl-R).

These commands hook into the existing uEmacs undo system.

Usage:
    python3 tests/tui/test_evil_undo_redo.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_undo_single_change():
    """'u' should undo the last change."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete first character
        emu.send('x')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("ello"):
            print(f"FAIL: After x, expected 'ello', got: '{line}'")
            emu.dump_screen()
            return False

        # Undo
        emu.send('u')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("hello"):
            print(f"FAIL: After u, expected 'hello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_redo_after_undo():
    """Ctrl-R should redo after undo."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("hello world\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete first character
        emu.send('x')
        time.sleep(0.1)
        emu._read_output()

        # Undo
        emu.send('u')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("hello"):
            print(f"FAIL: After u, expected 'hello', got: '{line}'")
            return False

        # Redo with Ctrl-R (0x12)
        emu.send('\x12')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("ello"):
            print(f"FAIL: After Ctrl-R, expected 'ello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_multiple_undo():
    """Multiple 'u' should undo multiple changes."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("abcd\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete 'a'
        emu.send('x')
        time.sleep(0.1)

        # Delete 'b'
        emu.send('x')
        time.sleep(0.1)

        # Delete 'c'
        emu.send('x')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if line[0] != 'd':
            print(f"FAIL: After 3x, expected 'd', got: '{line}'")
            return False

        # Undo 3 times
        emu.send('u')
        time.sleep(0.1)
        emu.send('u')
        time.sleep(0.1)
        emu.send('u')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("abcd"):
            print(f"FAIL: After 3u, expected 'abcd', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_undo_insert_mode_change():
    """'u' should undo text inserted in insert mode."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("hello\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Insert 'X' at start
        emu.send('i')
        time.sleep(0.1)
        emu.send('X')
        time.sleep(0.1)
        emu.send('\x1b')  # ESC
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("Xhello"):
            print(f"FAIL: After insert, expected 'Xhello', got: '{line}'")
            return False

        # Undo
        emu.send('u')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("hello"):
            print(f"FAIL: After u, expected 'hello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_undo_with_count():
    """'3u' should undo 3 changes at once."""
    filename = '/tmp/uemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("abcdef\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Make 3 deletions
        emu.send('x')
        time.sleep(0.1)
        emu.send('x')
        time.sleep(0.1)
        emu.send('x')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("def"):
            print(f"FAIL: After 3x, expected 'def', got: '{line}'")
            return False

        # Undo with count
        emu.send('3u')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("abcdef"):
            print(f"FAIL: After 3u, expected 'abcdef', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("undo_single_change (u)", test_undo_single_change),
        ("redo_after_undo (Ctrl-R)", test_redo_after_undo),
        ("multiple_undo (u u u)", test_multiple_undo),
        ("undo_insert_mode_change (u)", test_undo_insert_mode_change),
        ("undo_with_count (3u)", test_undo_with_count),
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
