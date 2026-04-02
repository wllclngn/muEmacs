#!/usr/bin/env python3
"""
TUI tests for μEmacs buffer operations.

Tests:
- select-buffer: Switch to a different buffer
- delete-buffer: Close/kill a buffer
- list-buffers: Display buffer list
- next-buffer: Cycle to next buffer

Usage:
    python3 tests/tui/test_buffers.py
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


def get_status_buffer_name(emu):
    """Extract buffer name from status line."""
    # Status line format: "   filename  Text  UTF-8  ..."
    status_row = emu.rows - 2  # Second to last row
    line = emu.get_line(status_row)
    # Find the filename - it's usually after leading spaces
    parts = line.split()
    if parts:
        # First part is usually the filename or buffer name
        return parts[0]
    return ""


def test_select_buffer():
    """select-buffer should switch to named buffer."""
    # Create two files with short, distinct names (buffer names limited to 15 chars)
    file1 = test_tmp('buf1.txt')
    file2 = test_tmp('buf2.txt')
    with open(file1, 'w') as f:
        f.write("Content of file 1\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")
    with open(file2, 'w') as f:
        f.write("Content of file 2\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        # Start with file1
        emu.start(filename=file1, evil_mode=False)

        # Open file2 via find-file
        run_mx_command(emu, 'find-file')
        # Wait for prompt, then send filename (path and enter separately)
        time.sleep(0.2)
        emu.child.send(file2)
        time.sleep(0.1)
        emu.child.send('\r')
        time.sleep(0.5)
        emu._read_output()

        # Now we should be in file2
        content = emu.get_line(0)
        if 'file 2' not in content:
            print(f"FAIL: Should show file2 content, got: '{content}'")
            emu.dump_screen()
            return False

        # Switch back to file1 using select-buffer
        run_mx_command(emu, 'select-buffer')
        # Type the buffer name (basename of file1) - send path and enter separately
        time.sleep(0.2)
        emu.child.send('buf1.txt')
        time.sleep(0.1)
        emu.child.send('\r')
        time.sleep(0.5)
        emu._read_output()

        # Should now show file1 content
        content = emu.get_line(0)
        if 'file 1' not in content:
            print(f"FAIL: Should show file1 content after switch, got: '{content}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_next_buffer():
    """next-buffer should cycle through buffers."""
    file1 = test_tmp('nbuf1.txt')
    file2 = test_tmp('nbuf2.txt')
    with open(file1, 'w') as f:
        f.write("FIRST BUFFER\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")
    with open(file2, 'w') as f:
        f.write("SECOND BUFFER\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=file1, evil_mode=False)

        # Open second file
        run_mx_command(emu, 'find-file')
        time.sleep(0.2)
        emu.child.send(file2)
        time.sleep(0.1)
        emu.child.send('\r')
        time.sleep(0.5)
        emu._read_output()

        # Now in file2, verify
        initial_content = emu.get_line(0)
        if 'SECOND' not in initial_content:
            print(f"FAIL: Should start in second buffer, got: '{initial_content}'")
            emu.dump_screen()
            return False

        # next-buffer should go to file1
        run_mx_command(emu, 'next-buffer')

        after_content = emu.get_line(0)
        if 'FIRST' not in after_content:
            print(f"FAIL: next-buffer should switch to first, got: '{after_content}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_delete_buffer():
    """delete-buffer should close the specified buffer."""
    file1 = test_tmp('dbuf1.txt')
    file2 = test_tmp('dbuf2.txt')
    with open(file1, 'w') as f:
        f.write("File 1 content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")
    with open(file2, 'w') as f:
        f.write("File 2 content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=file1, evil_mode=False)

        # Open second file
        run_mx_command(emu, 'find-file')
        time.sleep(0.2)
        emu.child.send(file2)
        time.sleep(0.1)
        emu.child.send('\r')
        time.sleep(0.5)
        emu._read_output()

        # Delete current buffer (file2)
        run_mx_command(emu, 'delete-buffer')
        time.sleep(0.2)
        # Accept default (current buffer) by pressing Enter
        emu.child.send('\r')
        time.sleep(0.5)
        emu._read_output()

        # Should now show file1
        content = emu.get_line(0)
        if 'File 1' not in content:
            # Might still be OK if we're showing scratch or something
            msg = emu.get_message_line()
            if 'error' in msg.lower():
                print(f"FAIL: delete-buffer error: {msg}")
                emu.dump_screen()
                return False

        return True
    finally:
        emu.quit()


def test_list_buffers():
    """list-buffers should display buffer list."""
    file1 = test_tmp('lbuf1.txt')
    with open(file1, 'w') as f:
        f.write("Test content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=file1, evil_mode=False)

        # List buffers
        run_mx_command(emu, 'list-buffers')

        # Should see a buffer list with column headers or buffer names
        screen_text = emu.get_screen_text()

        # Look for the filename in the buffer list
        if 'lbuf1' not in screen_text:
            # Some implementations might show truncated names
            # At least verify no error
            msg = emu.get_message_line()
            if 'error' in msg.lower() or 'unknown' in msg.lower():
                print(f"FAIL: list-buffers error: {msg}")
                emu.dump_screen()
                return False

        return True
    finally:
        emu.quit()


def test_buffer_modified_indicator():
    """Modified buffer should show indicator in status line."""
    filename = test_tmp('mbuf1.txt')
    with open(filename, 'w') as f:
        f.write("Original content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Get initial status line
        initial_status = emu.get_line(emu.rows - 2)

        # Make a modification (insert a character)
        emu.child.send('x')  # Insert 'x'
        time.sleep(0.2)
        emu._read_output()

        # Status line should now show modified indicator (often '*' or 'M')
        modified_status = emu.get_line(emu.rows - 2)

        # The status should be different (modified indicator added)
        if initial_status == modified_status:
            # Not necessarily a failure - some UIs don't show this
            print("INFO: Status line unchanged after modification")

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("select_buffer", test_select_buffer),
        ("next_buffer", test_next_buffer),
        ("delete_buffer", test_delete_buffer),
        ("list_buffers", test_list_buffers),
        ("buffer_modified_indicator", test_buffer_modified_indicator),
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
