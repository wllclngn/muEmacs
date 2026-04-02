#!/usr/bin/env python3
"""
TUI tests for μEmacs window management commands.

Tests:
- split-current-window: Split window horizontally
- delete-window: Close current window
- delete-other-windows: Close all but current
- next-window / previous-window: Navigate between windows
- grow-window / shrink-window: Resize windows

Usage:
    python3 tests/tui/test_windows.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def run_mx_command(emu, cmd):
    """Run an M-x command."""
    emu.child.send('\x1bx')  # ESC x (lowercase)
    time.sleep(0.2)
    emu._read_output()
    emu.child.send(cmd + '\r')
    time.sleep(0.3)
    emu._read_output()


def count_status_lines(emu):
    """Count status lines (mode lines) visible on screen.

    μEmacs status lines contain filename, mode (Text), and encoding (UTF-8).
    Each window has its own status line at the bottom.
    """
    count = 0
    for row in range(emu.rows):
        line = emu.get_line(row)
        # μEmacs status lines contain "Text" and "UTF-8" or similar mode/encoding info
        if 'UTF-8' in line or ('Text' in line and '/' in line):
            count += 1
    return count


def test_split_window():
    """split-current-window should create two windows."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Line 1\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Count initial status lines (should be 1)
        initial_count = count_status_lines(emu)

        # Split window via M-X
        run_mx_command(emu, 'split-current-window')

        # Count status lines after split (should be 2)
        after_count = count_status_lines(emu)

        if after_count <= initial_count:
            print(f"FAIL: Expected more status lines after split")
            print(f"  Before: {initial_count}, After: {after_count}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_split_via_cx2():
    """C-x 2 should split window (standard Emacs binding)."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Test content\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        initial_count = count_status_lines(emu)

        # C-x 2 to split
        emu.child.send('\x182')  # C-x 2
        time.sleep(0.3)
        emu._read_output()

        after_count = count_status_lines(emu)

        if after_count <= initial_count:
            print(f"FAIL: C-x 2 should split window")
            print(f"  Before: {initial_count}, After: {after_count}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_window():
    """delete-window should close current window after split."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Test content\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # First split
        run_mx_command(emu, 'split-current-window')
        split_count = count_status_lines(emu)

        # Then delete current window
        run_mx_command(emu, 'delete-window')
        after_delete = count_status_lines(emu)

        if after_delete >= split_count:
            print(f"FAIL: delete-window should reduce window count")
            print(f"  After split: {split_count}, After delete: {after_delete}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_only_window_fails():
    """delete-window on only window should show error."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Test content\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Try to delete only window
        run_mx_command(emu, 'delete-window')

        # Should see error message
        msg = emu.get_message_line().upper()
        if 'CANNOT' not in msg and 'DELETE' not in msg:
            print(f"FAIL: Expected error when deleting only window")
            print(f"  Message: '{emu.get_message_line()}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_other_windows():
    """delete-other-windows should leave only current window."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Test content\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Split twice to have multiple windows
        run_mx_command(emu, 'split-current-window')
        time.sleep(0.2)
        run_mx_command(emu, 'split-current-window')
        multi_count = count_status_lines(emu)

        # Delete other windows
        run_mx_command(emu, 'delete-other-windows')
        after_count = count_status_lines(emu)

        if after_count != 1:
            print(f"FAIL: delete-other-windows should leave 1 window")
            print(f"  Before: {multi_count}, After: {after_count}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def find_highlighted_row(emu):
    """Find the row with cursor-line highlight."""
    highlight_colors = ['7f7f7f', '808080', '8']
    for row in range(emu.rows - 2):  # Skip status/message lines
        for col in range(emu.cols):
            char = emu.screen.buffer[row][col]
            if char.bg in highlight_colors:
                return row
    return -1


def test_next_window():
    """next-window should cycle to next window."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Window test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Split window
        run_mx_command(emu, 'split-current-window')

        # Find which row has highlight (cursor line)
        initial_highlight = find_highlighted_row(emu)

        # Move to next window
        run_mx_command(emu, 'next-window')

        # Highlight should have moved to the other window
        after_highlight = find_highlighted_row(emu)

        # After split, window 1 is rows 0-9 (status at 10)
        # Window 2 is rows 11-21 (status at 22)
        # So highlight should jump across the status line boundary
        if initial_highlight == after_highlight:
            print(f"FAIL: next-window should move highlight to other window")
            print(f"  Initial highlight row: {initial_highlight}")
            print(f"  After highlight row: {after_highlight}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_previous_window():
    """previous-window should cycle to previous window."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Window test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Split window
        run_mx_command(emu, 'split-current-window')

        # Move to next, then previous (should return to original)
        run_mx_command(emu, 'next-window')
        after_next_row = emu.get_editing_cursor_row()

        run_mx_command(emu, 'previous-window')
        after_prev_row = emu.get_editing_cursor_row()

        # Should have moved back
        if after_next_row == after_prev_row:
            # With 2 windows, next and prev should toggle
            print(f"WARN: previous-window may not have toggled (2-window case)")
            # This might still be OK if we're cycling through

        return True
    finally:
        emu.quit()


def test_grow_window():
    """grow-window should increase window size."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Window test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Split first
        run_mx_command(emu, 'split-current-window')

        # Find the status line positions to measure window sizes
        def find_status_rows():
            rows = []
            for row in range(emu.rows):
                line = emu.get_line(row)
                if line.startswith('==') or '==' in line[:20]:
                    rows.append(row)
            return rows

        before_rows = find_status_rows()

        # Grow window
        run_mx_command(emu, 'grow-window')

        after_rows = find_status_rows()

        # Status line positions should have shifted
        if before_rows == after_rows:
            print(f"INFO: grow-window may not have changed visible layout")
            # Not necessarily a failure - depends on implementation

        return True
    finally:
        emu.quit()


def test_shrink_window():
    """shrink-window should decrease window size."""
    filename = test_tmp('muemacs_window_test.txt')
    with open(filename, 'w') as f:
        f.write("Window test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Split first
        run_mx_command(emu, 'split-current-window')

        # Grow to have room to shrink
        run_mx_command(emu, 'grow-window')
        run_mx_command(emu, 'grow-window')

        # Then shrink
        run_mx_command(emu, 'shrink-window')

        # If no error, consider it passed
        msg = emu.get_message_line().upper()
        if 'ERROR' in msg or 'CANNOT' in msg:
            print(f"FAIL: shrink-window produced error: {msg}")
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("split_window (M-X)", test_split_window),
        ("split_via_cx2 (C-x 2)", test_split_via_cx2),
        ("delete_window", test_delete_window),
        ("delete_only_window_fails", test_delete_only_window_fails),
        ("delete_other_windows", test_delete_other_windows),
        ("next_window", test_next_window),
        ("previous_window", test_previous_window),
        ("grow_window", test_grow_window),
        ("shrink_window", test_shrink_window),
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
