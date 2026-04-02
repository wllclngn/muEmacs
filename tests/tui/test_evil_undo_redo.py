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
from base import UEmacsTest, test_tmp


def test_undo_single_change():
    """'u' should undo the last change."""
    filename = test_tmp('muemacs_evil_test.txt')
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
    """Redo command should restore undone change.

    Note: Uses 'r' for redo per user's custom settings.
    Standard vim uses Ctrl-R, but settings.toml has r=redo.
    """
    filename = test_tmp('muemacs_evil_test.txt')
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

        # Redo with 'r' (per user's settings.toml: r=redo)
        emu.send('r')
        time.sleep(0.1)
        emu._read_output()

        line = emu.get_line(0)
        if not line.startswith("ello"):
            print(f"FAIL: After redo, expected 'ello', got: '{line}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_multiple_undo():
    """Multiple 'u' should undo multiple changes."""
    filename = test_tmp('muemacs_evil_test.txt')
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
    filename = test_tmp('muemacs_evil_test.txt')
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
    filename = test_tmp('muemacs_evil_test.txt')
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


def has_delta(emu):
    """Check if the delta (modified) indicator is shown in the modeline."""
    for i in range(emu.rows - 1, -1, -1):
        line = emu.get_line(i)
        if 'Δ' in line:
            return True
    return False


def test_undo_to_original_clears_delta():
    """Undo to original state should clear the delta (modified) indicator."""
    import tempfile
    # Use short temp filename so delta fits in modeline
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write("original\n")
        filename = f.name

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Initially no delta
        if has_delta(emu):
            print("FAIL: Delta should not be shown initially")
            return False

        # Insert 'X' at start (with proper timing for each keystroke)
        emu.send('i')
        time.sleep(0.15)
        emu.send('X')
        time.sleep(0.15)
        emu.send('\x1b')  # ESC
        time.sleep(0.2)
        emu._read_output(timeout=0.1)  # Longer timeout to catch modeline update

        # Should have delta now
        if not has_delta(emu):
            print("FAIL: Delta should be shown after edit")
            emu.dump_screen()
            return False

        # Undo
        emu.send('u')
        time.sleep(0.2)
        emu._read_output(timeout=0.1)

        # Delta should be cleared (back to original state)
        if has_delta(emu):
            print("FAIL: Delta should be cleared after undo to original state")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()
        os.unlink(filename)


def test_redo_restores_delta():
    """Redo should restore the delta indicator.

    Note: Uses 'r' for redo per user's settings.toml.
    """
    import tempfile
    # Use short temp filename so delta fits in modeline
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write("original\n")
        filename = f.name

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Insert 'X' at start
        emu.send('i')
        time.sleep(0.15)
        emu.send('X')
        time.sleep(0.15)
        emu.send('\x1b')  # ESC
        time.sleep(0.2)
        emu._read_output(timeout=0.1)

        line = emu.get_line(0)
        if not line.startswith("Xoriginal"):
            print(f"FAIL: After insert, expected 'Xoriginal', got: '{line}'")
            emu.dump_screen()
            return False

        # Undo
        emu.send('u')
        time.sleep(0.2)
        emu._read_output(timeout=0.1)

        # Delta should be cleared
        if has_delta(emu):
            print("FAIL: Delta should be cleared after undo")
            return False

        # Redo with 'r' (per user's settings.toml: r=redo)
        emu.send('r')
        time.sleep(0.2)
        emu._read_output(timeout=0.1)

        line = emu.get_line(0)
        if not line.startswith("Xoriginal"):
            print(f"FAIL: After redo, expected 'Xoriginal', got: '{line}'")
            emu.dump_screen()
            return False

        # Delta should be restored
        if not has_delta(emu):
            print("FAIL: Delta should be restored after redo")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()
        os.unlink(filename)


def test_undo_to_saved_state_clears_delta():
    """Undo to saved state (after save) should clear the delta indicator.

    Note: Tests must complete quickly (within 3s of evil-mode enable) because
    after 3s the "EVIL" splash expires and the modeline format changes, which
    can affect delta indicator visibility due to display formatting issues.
    """
    import tempfile
    # Use short temp filename so delta fits in modeline
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write("start\n")
        filename = f.name

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Insert 'X' at start
        emu.send('i')
        time.sleep(0.1)
        emu.send('X')
        time.sleep(0.1)
        emu.send('\x1b')
        time.sleep(0.15)
        emu._read_output(timeout=0.1)

        line = emu.get_line(0)
        if not line.startswith("Xstart"):
            print(f"FAIL: After insert, expected 'Xstart', got: '{line}'")
            emu.dump_screen()
            return False

        # Save with Ctrl-X Ctrl-S
        emu.send('\x18\x13')
        time.sleep(0.25)
        emu._read_output(timeout=0.1)

        # Delta should be cleared after save
        if has_delta(emu):
            print("FAIL: Delta should be cleared after save")
            emu.dump_screen()
            return False

        # Insert 'Y' quickly
        emu.send('iY')
        time.sleep(0.1)
        emu.send('\x1b')
        time.sleep(0.15)
        emu._read_output(timeout=0.1)

        line = emu.get_line(0)
        if not line.startswith("XYstart"):
            print(f"FAIL: After second insert, expected 'XYstart', got: '{line}'")
            emu.dump_screen()
            return False

        # Should have delta now
        if not has_delta(emu):
            print("FAIL: Delta should be shown after edit")
            emu.dump_screen()
            return False

        # Undo - should go back to saved state "Xstart" and clear delta
        emu.send('u')
        time.sleep(0.15)
        emu._read_output(timeout=0.1)

        line = emu.get_line(0)
        if not line.startswith("Xstart"):
            print(f"FAIL: After undo, expected 'Xstart', got: '{line}'")
            emu.dump_screen()
            return False

        # Delta should be cleared (back to saved state)
        if has_delta(emu):
            print("FAIL: Delta should be cleared when undo reaches saved state")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()
        os.unlink(filename)


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("undo_single_change (u)", test_undo_single_change),
        ("redo_after_undo (Ctrl-R)", test_redo_after_undo),
        ("multiple_undo (u u u)", test_multiple_undo),
        ("undo_insert_mode_change (u)", test_undo_insert_mode_change),
        ("undo_with_count (3u)", test_undo_with_count),
        ("undo_to_original_clears_delta", test_undo_to_original_clears_delta),
        ("redo_restores_delta", test_redo_restores_delta),
        ("undo_to_saved_state_clears_delta", test_undo_to_saved_state_clears_delta),
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
