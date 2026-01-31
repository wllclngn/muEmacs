#!/usr/bin/env python3
"""
TUI tests for Evil mode text objects (diw, da", ci(, etc.).

Usage:
    python3 tests/tui/test_evil_text_objects.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def test_delete_inner_word():
    """diw should delete the word under cursor."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("hello world test\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to middle of "world" (column 8)
        emu.send('8')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # Delete inner word with diw
        emu.send('d')
        time.sleep(0.05)
        emu.send('i')
        time.sleep(0.05)
        emu.send('w')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        # "hello world test" -> "hello  test" (word deleted, surrounding spaces remain)
        # Actually depends on implementation - might be "hello test" or "hello  test"
        if "world" in line:
            print(f"FAIL: 'world' should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_around_word():
    """daw should delete word plus surrounding whitespace."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("hello world test\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to "world"
        emu.send('6')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # Delete around word with daw
        emu.send('d')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.05)
        emu.send('w')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        if "world" in line:
            print(f"FAIL: 'world' should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_inner_quote():
    """di\" should delete text inside quotes (not quotes themselves)."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write('say "hello world" now\n')
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move into the quoted string
        emu.send('8')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # Delete inner quote with di"
        emu.send('d')
        time.sleep(0.05)
        emu.send('i')
        time.sleep(0.05)
        emu.send('"')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        # Should delete "hello world" leaving 'say "" now'
        if 'hello world' in line:
            print(f"FAIL: quoted text should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        # Quotes should remain
        if '""' not in line:
            print(f"FAIL: quotes should remain, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_around_quote():
    """da\" should delete quotes and their contents."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write('say "hello world" now\n')
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move into the quoted string
        emu.send('8')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # Delete around quote with da"
        emu.send('d')
        time.sleep(0.05)
        emu.send('a')
        time.sleep(0.05)
        emu.send('"')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        # Should delete '"hello world"' leaving 'say  now'
        if '"' in line:
            print(f"FAIL: quotes should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_inner_paren():
    """di( should delete text inside parentheses."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write('func(arg1, arg2) end\n')
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move into the parentheses
        emu.send('7')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # Delete inner paren with di(
        emu.send('d')
        time.sleep(0.05)
        emu.send('i')
        time.sleep(0.05)
        emu.send('(')
        time.sleep(0.2)
        emu._read_output()

        line = emu.get_line(0)
        # Should delete 'arg1, arg2' leaving 'func() end'
        if 'arg1' in line or 'arg2' in line:
            print(f"FAIL: parens content should be deleted, got: '{line}'")
            emu.dump_screen()
            return False

        # Parens should remain
        if '()' not in line:
            print(f"FAIL: parens should remain, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_change_inner_word():
    """ciw should delete word and enter insert mode."""
    filename = '/tmp/muemacs_evil_test.txt'
    with open(filename, 'w') as f:
        f.write("hello world test\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to "world"
        emu.send('6')
        emu.send('l')
        time.sleep(0.1)
        emu._read_output()

        # Change inner word with ciw
        emu.send('c')
        time.sleep(0.05)
        emu.send('i')
        time.sleep(0.05)
        emu.send('w')
        time.sleep(0.2)
        emu._read_output()

        # Should be in insert mode
        status = emu.get_line(emu.rows - 2)
        # Check if INSERT mode or similar indicator
        # (The status line should show INSERT or something similar)

        # Type replacement text
        emu.send('universe')
        time.sleep(0.1)
        emu._read_output()

        # Exit insert mode
        emu.send_key('ESC')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if 'universe' not in line:
            print(f"FAIL: 'universe' should be in line, got: '{line}'")
            emu.dump_screen()
            return False

        if 'world' in line:
            print(f"FAIL: 'world' should be replaced, got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("delete_inner_word (diw)", test_delete_inner_word),
        ("delete_around_word (daw)", test_delete_around_word),
        ("delete_inner_quote (di\")", test_delete_inner_quote),
        ("delete_around_quote (da\")", test_delete_around_quote),
        ("delete_inner_paren (di()", test_delete_inner_paren),
        ("change_inner_word (ciw)", test_change_inner_word),
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
