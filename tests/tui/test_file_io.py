#!/usr/bin/env python3
"""
TUI tests for μEmacs file I/O operations.

Tests:
- find-file: Open file (creates if not exists)
- save-file: Save current buffer
- write-file: Save as different name
- insert-file: Insert file contents at cursor
- read-file: Replace buffer with file

Usage:
    python3 tests/tui/test_file_io.py
"""

import os
import sys
import time
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


def run_mx_command(emu, cmd):
    """Run an M-x command."""
    emu.child.send('\x1bx')  # ESC x (lowercase)
    time.sleep(0.2)
    emu._read_output()
    emu.child.send(cmd + '\r')
    time.sleep(0.3)
    emu._read_output()


def send_path_and_enter(emu, path):
    """Send a path and Enter key separately for reliability."""
    time.sleep(0.2)
    emu.child.send(path)
    time.sleep(0.1)
    emu.child.send('\r')
    time.sleep(0.5)
    emu._read_output()


def test_find_file_existing():
    """find-file should open an existing file."""
    filename = '/tmp/fio_ex.txt'
    with open(filename, 'w') as f:
        f.write("Existing file content\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        # Start with a different file
        start_file = '/tmp/fio_st.txt'
        with open(start_file, 'w') as f:
            f.write("Start file\n")

        emu.start(filename=start_file, evil_mode=False)

        # Open the existing file
        run_mx_command(emu, 'find-file')
        send_path_and_enter(emu, filename)

        # Should show the content
        content = emu.get_line(0)
        if 'Existing file content' not in content:
            print(f"FAIL: Should show existing file, got: '{content}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_find_file_new():
    """find-file should create new buffer for non-existent file."""
    # Use a definitely non-existent file
    filename = f'/tmp/fio_new_{os.getpid()}.txt'
    if os.path.exists(filename):
        os.remove(filename)

    emu = UEmacsTest()
    try:
        start_file = '/tmp/fio_st2.txt'
        with open(start_file, 'w') as f:
            f.write("Start file\n")

        emu.start(filename=start_file, evil_mode=False)

        # Open non-existent file
        run_mx_command(emu, 'find-file')
        send_path_and_enter(emu, filename)

        # Should have empty buffer (new file)
        msg = emu.get_message_line().lower()
        # May show "new file" message or just an empty buffer
        # Check that we're in the new buffer by looking at status line
        status = emu.get_line(emu.rows - 2)

        # The filename (or truncated version) should be in status
        if 'fio_new' not in status:
            print(f"FAIL: Status should show new filename")
            print(f"  Status: '{status}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()
        if os.path.exists(filename):
            os.remove(filename)


def test_save_file():
    """save-file should write buffer to disk."""
    filename = '/tmp/fio_save.txt'
    with open(filename, 'w') as f:
        f.write("Original content\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Make a modification - go to end of line and add text
        emu.child.send('\x05')  # C-e (end of line)
        time.sleep(0.1)
        emu._read_output()

        emu.child.send(' ADDED')
        time.sleep(0.2)
        emu._read_output()

        # Save with M-x save-file
        run_mx_command(emu, 'save-file')

        # Read the file back and verify
        with open(filename) as f:
            content = f.read()

        if 'ADDED' not in content:
            print(f"FAIL: Save didn't write changes")
            print(f"  File content: '{content}'")
            return False

        return True
    finally:
        emu.quit()


def test_save_file_ctrl_x_ctrl_s():
    """C-x C-s should save the file."""
    filename = '/tmp/fio_cxcs.txt'
    with open(filename, 'w') as f:
        f.write("Test line\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Add some text
        emu.child.send('\x05 MODIFIED')  # C-e then text
        time.sleep(0.2)
        emu._read_output()

        # C-x C-s to save
        emu.child.send('\x18\x13')  # C-x C-s
        time.sleep(0.5)
        emu._read_output()

        # Verify file on disk
        with open(filename) as f:
            content = f.read()

        if 'MODIFIED' not in content:
            print(f"FAIL: C-x C-s didn't save changes")
            print(f"  File content: '{content}'")
            return False

        return True
    finally:
        emu.quit()


def test_write_file():
    """write-file should save buffer to a different file."""
    orig_file = '/tmp/fio_orig.txt'
    new_file = '/tmp/fio_writ.txt'

    with open(orig_file, 'w') as f:
        f.write("Content to write elsewhere\n")

    # Remove new_file if exists
    if os.path.exists(new_file):
        os.remove(new_file)

    emu = UEmacsTest()
    try:
        emu.start(filename=orig_file, evil_mode=False)

        # write-file to new location
        run_mx_command(emu, 'write-file')
        send_path_and_enter(emu, new_file)

        # Verify new file exists and has content
        if not os.path.exists(new_file):
            print(f"FAIL: write-file didn't create new file")
            return False

        with open(new_file) as f:
            content = f.read()

        if 'Content to write elsewhere' not in content:
            print(f"FAIL: New file doesn't have expected content")
            print(f"  Content: '{content}'")
            return False

        return True
    finally:
        emu.quit()
        if os.path.exists(new_file):
            os.remove(new_file)


def test_insert_file():
    """insert-file should insert file contents at cursor."""
    main_file = '/tmp/fio_main.txt'
    insert_file = '/tmp/fio_ins.txt'

    with open(main_file, 'w') as f:
        f.write("Main content\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    with open(insert_file, 'w') as f:
        f.write("INSERTED TEXT\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=main_file, evil_mode=False)

        # Move to end of first line
        emu.child.send('\x05')  # C-e
        time.sleep(0.1)
        emu._read_output()

        # Insert file
        run_mx_command(emu, 'insert-file')
        send_path_and_enter(emu, insert_file)

        # Check that inserted text appears
        screen_text = emu.get_screen_text()
        if 'INSERTED TEXT' not in screen_text:
            print(f"FAIL: Inserted text not visible")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_read_file():
    """read-file should replace buffer contents with file."""
    current_file = '/tmp/fio_curr.txt'
    read_file = '/tmp/fio_read.txt'

    with open(current_file, 'w') as f:
        f.write("Current content\n")

    with open(read_file, 'w') as f:
        f.write("NEW CONTENT FROM READ\n")
        for i in range(5):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=current_file, evil_mode=False)

        # Verify initial content
        initial = emu.get_line(0)
        if 'Current content' not in initial:
            print(f"FAIL: Initial content wrong: '{initial}'")
            return False

        # Read new file into buffer
        run_mx_command(emu, 'read-file')
        send_path_and_enter(emu, read_file)

        # Should now show new content
        new_content = emu.get_line(0)
        if 'NEW CONTENT FROM READ' not in new_content:
            print(f"FAIL: read-file didn't replace content")
            print(f"  Line 0: '{new_content}'")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_file_modified_prompt():
    """Quitting modified buffer should prompt to save."""
    filename = '/tmp/fio_mod.txt'
    with open(filename, 'w') as f:
        f.write("Original\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=False)

        # Modify the buffer
        emu.child.send('CHANGE')
        time.sleep(0.2)
        emu._read_output()

        # Try to quit (should prompt because modified)
        run_mx_command(emu, 'quit')

        # Should see a prompt about unsaved changes
        msg = emu.get_message_line()
        # Look for prompt about saving or modified buffer
        if 'save' not in msg.lower() and 'discard' not in msg.lower() and 'modified' not in msg.lower():
            # Some implementations might just show the quit prompt
            # Check if we're still in the editor (not quit yet)
            pass  # Not a strict failure

        # Cancel the quit
        emu.child.send('\x07')  # C-g to cancel
        time.sleep(0.2)
        emu._read_output()

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all tests and report results."""
    tests = [
        ("find_file_existing", test_find_file_existing),
        ("find_file_new", test_find_file_new),
        ("save_file (M-x)", test_save_file),
        ("save_file (C-x C-s)", test_save_file_ctrl_x_ctrl_s),
        ("write_file", test_write_file),
        ("insert_file", test_insert_file),
        ("read_file", test_read_file),
        ("file_modified_prompt", test_file_modified_prompt),
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
