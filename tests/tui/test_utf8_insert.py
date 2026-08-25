#!/usr/bin/env python3
"""
Regression test: multi-byte UTF-8 codepoints typed directly (compose-key
input, terminal-sent Unicode) must be inserted as their full UTF-8 byte
sequence, not truncated to a single byte.

This test exists because a prior version of src/core/main.c called
linsert(n, c) with c potentially up to 0x10FFFF. linsert() truncates via
(char)c, writing a single byte per codepoint — so U+2019 (' E2 80 99)
got stored as 0x19, U+25CF (● E2 97 8F) as 0xCF, and so on. The fix in
main.c detects c > 0x7F and encodes via unicode_to_utf8 + linstr before
writing.

Saved file should contain the correct UTF-8 bytes. No bytes 0x80-0xFF
should appear in isolation — every one must be part of a valid UTF-8
sequence.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp


def assert_valid_utf8(data, label):
    """Every 0x80-0xFF byte must be part of a valid UTF-8 sequence."""
    try:
        data.decode('utf-8', errors='strict')
    except UnicodeDecodeError as e:
        raise AssertionError(
            f"{label}: invalid UTF-8 in saved file — {e}. "
            f"bytes: {' '.join(f'{b:02x}' for b in data)}"
        )


def hex_dump(data):
    return ' '.join(f'{b:02x}' for b in data)


def test_right_single_quote():
    """Typing U+2019 (curly apostrophe) directly must save as E2 80 99."""
    fname = test_tmp('muemacs_utf8_rsquote.txt')
    with open(fname, 'w') as f:
        f.write('')

    emu = UEmacsTest()
    emu.start(filename=fname)
    try:
        # Send raw UTF-8 bytes for "it" + U+2019 + "s" as a terminal would
        # (compose key input produces the 3-byte UTF-8 sequence directly).
        emu.child.send('it\u2019s')
        time.sleep(0.3)
        emu._read_output()

        # Save: Ctrl-X Ctrl-S
        emu.send('\x18\x13')
        time.sleep(0.5)
        emu._read_output()
    finally:
        emu.child.sendcontrol('c')
        try:
            emu.child.send('\x18\x03')  # Ctrl-X Ctrl-C (exit)
        except Exception:
            pass
        try:
            emu.child.close(force=True)
        except Exception:
            pass

    with open(fname, 'rb') as f:
        data = f.read()

    # Remove trailing newline
    content = data.rstrip(b'\n')

    assert content == b'it\xe2\x80\x99s', (
        f"FAIL: expected b'it\\xe2\\x80\\x99s', got bytes: {hex_dump(content)}"
    )
    assert_valid_utf8(content, 'right-single-quote test')
    print(f"  PASS right_single_quote: {hex_dump(content)}")


def test_bullet():
    """Typing U+25CF (●) directly must save as E2 97 8F, not truncated."""
    fname = test_tmp('muemacs_utf8_bullet.txt')
    with open(fname, 'w') as f:
        f.write('')

    emu = UEmacsTest()
    emu.start(filename=fname)
    try:
        # Terminal-sent bullet (3 bytes UTF-8)
        emu.child.send('\u25cf x')
        time.sleep(0.3)
        emu._read_output()

        emu.send('\x18\x13')  # Ctrl-X Ctrl-S
        time.sleep(0.5)
        emu._read_output()
    finally:
        try:
            emu.child.send('\x18\x03')
        except Exception:
            pass
        try:
            emu.child.close(force=True)
        except Exception:
            pass

    with open(fname, 'rb') as f:
        content = f.read().rstrip(b'\n')

    assert content == b'\xe2\x97\x8f x', (
        f"FAIL: expected b'\\xe2\\x97\\x8f x', got bytes: {hex_dump(content)}"
    )
    assert_valid_utf8(content, 'bullet test')
    print(f"  PASS bullet: {hex_dump(content)}")


def test_em_dash():
    """Typing U+2014 (em dash) directly must save as E2 80 94."""
    fname = test_tmp('muemacs_utf8_emdash.txt')
    with open(fname, 'w') as f:
        f.write('')

    emu = UEmacsTest()
    emu.start(filename=fname)
    try:
        emu.child.send('a\u2014b')
        time.sleep(0.3)
        emu._read_output()
        emu.send('\x18\x13')
        time.sleep(0.5)
        emu._read_output()
    finally:
        try:
            emu.child.send('\x18\x03')
        except Exception:
            pass
        try:
            emu.child.close(force=True)
        except Exception:
            pass

    with open(fname, 'rb') as f:
        content = f.read().rstrip(b'\n')

    assert content == b'a\xe2\x80\x94b', (
        f"FAIL: expected b'a\\xe2\\x80\\x94b', got bytes: {hex_dump(content)}"
    )
    assert_valid_utf8(content, 'em-dash test')
    print(f"  PASS em_dash: {hex_dump(content)}")


def run_tests():
    print("Running UTF-8 multi-byte insert regression tests...")
    test_right_single_quote()
    test_bullet()
    test_em_dash()
    print("All UTF-8 insert tests passed.")


if __name__ == '__main__':
    run_tests()
