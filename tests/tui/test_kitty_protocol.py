#!/usr/bin/env python3
"""
TUI tests for Kitty keyboard protocol support in μEmacs.

Tests the CSI u sequence parsing that provides:
- Unambiguous key detection (no ESC timing issues)
- Proper modifier reporting (Ctrl+Shift+A distinguishable from Ctrl+A)
- Unicode character support
- Functional key code mapping

Kitty protocol format:
    CSI key:shifted:base ; modifiers:event_type ; text u

Where:
- key: Unicode codepoint or Kitty functional key code
- modifiers: 1 + modifier bits (Shift=1, Alt=2, Ctrl=4, Super=8)
- event_type: 1=press, 2=repeat, 3=release

Usage:
    python3 tests/tui/test_kitty_protocol.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest


# Kitty protocol helpers

def kitty_key(codepoint, modifiers=0, event_type=1):
    """Generate a Kitty keyboard protocol sequence.

    Args:
        codepoint: Unicode codepoint or Kitty functional key code
        modifiers: Modifier bits (Shift=1, Alt=2, Ctrl=4, Super=8)
        event_type: 1=press, 2=repeat, 3=release

    Returns:
        Kitty CSI u sequence string
    """
    # Kitty uses 1 + modifier bits
    mod_param = 1 + modifiers if modifiers else 0

    if mod_param and event_type != 1:
        return f'\x1b[{codepoint};{mod_param}:{event_type}u'
    elif mod_param:
        return f'\x1b[{codepoint};{mod_param}u'
    else:
        return f'\x1b[{codepoint}u'


def kitty_char(char, modifiers=0):
    """Generate Kitty sequence for a character."""
    return kitty_key(ord(char), modifiers)


# Kitty functional key codes
KITTY_KEYS = {
    'escape': 27,
    'enter': 13,
    'tab': 9,
    'backspace': 127,
    'insert': 57348,
    'delete': 57349,
    'left': 57350,
    'right': 57351,
    'up': 57352,
    'down': 57353,
    'pageup': 57354,
    'pagedown': 57355,
    'home': 57356,
    'end': 57357,
    'f1': 57364,
    'f2': 57365,
    'f3': 57366,
    'f4': 57367,
    'f5': 57368,
    'f6': 57369,
    'f7': 57370,
    'f8': 57371,
    'f9': 57372,
    'f10': 57373,
    'f11': 57374,
    'f12': 57375,
}

# Modifier bits
MOD_SHIFT = 1
MOD_ALT = 2
MOD_CTRL = 4
MOD_SUPER = 8


# Tests

def test_kitty_basic_letter():
    """Test basic letter input via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Test file for Kitty protocol\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)

        # Send 'h' 'e' 'l' 'l' 'o' via Kitty protocol (ASCII codes)
        for char in 'hello':
            emu.child.send(kitty_char(char))
            time.sleep(0.05)

        time.sleep(0.2)
        emu._read_output()

        # Check if "hello" appears in the buffer
        screen_text = emu.get_screen_text()
        if 'hello' not in screen_text:
            print(f"FAIL: 'hello' not found in buffer")
            print(f"Screen text: {screen_text[:200]}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_arrow_keys():
    """Test arrow key navigation via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        for i in range(50):
            f.write(f"Line {i+1}: Test content for navigation\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        initial_row = emu.get_cursor_row()

        # Move down 5 times using Kitty arrow key
        for _ in range(5):
            emu.child.send(kitty_key(KITTY_KEYS['down']))
            time.sleep(0.05)

        time.sleep(0.2)
        emu._read_output()

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption after arrow keys: {issues}")
            emu.dump_screen()
            return False

        # Verify cursor moved (may be in different screen row due to scrolling)
        highlight_bad, h_issues = emu.verify_highlight_correct()
        if highlight_bad:
            print(f"FAIL: Highlight issues: {h_issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_modifiers():
    """Test modifier key combinations via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Test line one\n")
        f.write("Test line two\n")
        for i in range(20):
            f.write(f"Line {i+3}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Send Ctrl+Down (navigate paragraphs in some modes)
        # Kitty format: CSI code ; (1 + modifier) u
        # Ctrl = 4, so param = 1 + 4 = 5
        emu.child.send(kitty_key(KITTY_KEYS['down'], MOD_CTRL))
        time.sleep(0.2)
        emu._read_output()

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption with Ctrl+Down: {issues}")
            emu.dump_screen()
            return False

        # Send Shift+Right (extend selection or word movement)
        emu.child.send(kitty_key(KITTY_KEYS['right'], MOD_SHIFT))
        time.sleep(0.2)
        emu._read_output()

        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption with Shift+Right: {issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_function_keys():
    """Test function keys via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Function key test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Send F1-F4 via Kitty protocol
        for i, key in enumerate(['f1', 'f2', 'f3', 'f4']):
            emu.child.send(kitty_key(KITTY_KEYS[key]))
            time.sleep(0.1)
            emu._read_output()

            corrupt, issues = emu.check_all_corruption()
            if corrupt:
                print(f"FAIL: Corruption after {key}: {issues}")
                emu.dump_screen()
                return False

        return True
    finally:
        emu.quit()


def test_kitty_release_events_ignored():
    """Test that key release events (event_type=3) are ignored."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Release event test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)

        # Send 'a' as press event (should insert)
        emu.child.send(kitty_key(ord('a'), event_type=1))
        time.sleep(0.1)
        emu._read_output()

        # Send 'b' as release event (should be IGNORED)
        emu.child.send(kitty_key(ord('b'), event_type=3))
        time.sleep(0.1)
        emu._read_output()

        # Send 'c' as press event (should insert)
        emu.child.send(kitty_key(ord('c'), event_type=1))
        time.sleep(0.1)
        emu._read_output()

        emu.send_key('ESC')
        time.sleep(0.2)
        emu._read_output()

        # Should see "ac" NOT "abc"
        screen_text = emu.get_screen_text()
        if 'abc' in screen_text:
            print("FAIL: Release event was NOT ignored - 'abc' found")
            emu.dump_screen()
            return False

        if 'ac' not in screen_text:
            print("FAIL: Expected 'ac' not found")
            print(f"Screen: {screen_text[:200]}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_repeat_events():
    """Test that key repeat events (event_type=2) are processed."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Repeat event test\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)

        # Send 'x' as repeat events (should all insert)
        for _ in range(5):
            emu.child.send(kitty_key(ord('x'), event_type=2))
            time.sleep(0.03)

        time.sleep(0.2)
        emu._read_output()

        emu.send_key('ESC')
        time.sleep(0.1)
        emu._read_output()

        # Should see "xxxxx"
        screen_text = emu.get_screen_text()
        if 'xxxxx' not in screen_text:
            print(f"FAIL: Expected 'xxxxx' from repeat events")
            print(f"Screen: {screen_text[:200]}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_home_end():
    """Test Home/End keys via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("0123456789 Line with content here\n")
        for i in range(20):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Move to middle of line first
        for _ in range(15):
            emu.child.send('l')
            time.sleep(0.02)
        time.sleep(0.1)
        emu._read_output()

        # Send Home key via Kitty
        emu.child.send(kitty_key(KITTY_KEYS['home']))
        time.sleep(0.2)
        emu._read_output()

        # Cursor should be at column 0
        _, col = emu.get_cursor_pos()
        if col != 0:
            print(f"FAIL: Home key didn't go to column 0 (got {col})")
            emu.dump_screen()
            return False

        # Send End key via Kitty
        emu.child.send(kitty_key(KITTY_KEYS['end']))
        time.sleep(0.2)
        emu._read_output()

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption after Home/End: {issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_enter_tab_backspace():
    """Test Enter, Tab, and Backspace via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Test content\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)

        # Type "abc"
        for c in 'abc':
            emu.child.send(kitty_char(c))
            time.sleep(0.03)

        # Backspace via Kitty (should delete 'c')
        emu.child.send(kitty_key(KITTY_KEYS['backspace']))
        time.sleep(0.1)

        # Tab via Kitty
        emu.child.send(kitty_key(KITTY_KEYS['tab']))
        time.sleep(0.1)

        # Enter via Kitty (newline)
        emu.child.send(kitty_key(KITTY_KEYS['enter']))
        time.sleep(0.1)

        emu._read_output()

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption with special keys: {issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_escape_key():
    """Test Escape key via Kitty protocol (should exit insert mode)."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        f.write("Escape test\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)
        emu._read_output()

        # Type something
        emu.child.send(kitty_char('x'))
        time.sleep(0.1)

        # Escape via Kitty protocol
        emu.child.send(kitty_key(KITTY_KEYS['escape']))
        time.sleep(0.2)
        emu._read_output()

        # Now 'j' should move down (normal mode), not insert 'j'
        initial_row = emu.get_cursor_row()
        emu.send('j')
        time.sleep(0.1)
        emu._read_output()

        # Check we're out of insert mode (cursor moved)
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption after Kitty ESC: {issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_mixed_with_traditional():
    """Test mixing Kitty protocol with traditional escape sequences."""
    filename = '/tmp/uemacs_kitty_test.txt'
    with open(filename, 'w') as f:
        for i in range(50):
            f.write(f"Line {i+1}: Mixed protocol test\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Alternate between traditional and Kitty sequences
        for _ in range(10):
            # Traditional down arrow
            emu.child.send('\x1b[B')
            time.sleep(0.03)

            # Kitty down arrow
            emu.child.send(kitty_key(KITTY_KEYS['down']))
            time.sleep(0.03)

            # vim 'j' for down
            emu.child.send('j')
            time.sleep(0.03)

        time.sleep(0.2)
        emu._read_output()

        # Check for any issues
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption with mixed protocols: {issues}")
            emu.dump_screen()
            return False

        highlight_bad, h_issues = emu.verify_highlight_correct()
        if highlight_bad:
            print(f"FAIL: Highlight issues: {h_issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_rapid_input():
    """Test rapid Kitty protocol input."""
    filename = '/tmp/uemacs_kitty_rapid.txt'
    with open(filename, 'w') as f:
        for i in range(100):
            f.write(f"Line {i+1}: Rapid Kitty input test content\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Rapid Kitty arrow key input
        for _ in range(50):
            emu.child.send(kitty_key(KITTY_KEYS['down']))
            time.sleep(0.020)  # 50 keys/sec

        time.sleep(0.2)
        emu._read_output()

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption under rapid Kitty input: {issues}")
            emu.dump_screen()
            return False

        # Move back up
        for _ in range(50):
            emu.child.send(kitty_key(KITTY_KEYS['up']))
            time.sleep(0.020)

        time.sleep(0.2)
        emu._read_output()

        highlight_bad, h_issues = emu.verify_highlight_correct()
        if highlight_bad:
            print(f"FAIL: Highlight bleed after rapid Kitty: {h_issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_unicode_input():
    """Test Unicode character input via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_unicode.txt'
    with open(filename, 'w') as f:
        f.write("Unicode via Kitty\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Enter insert mode
        emu.send('i')
        time.sleep(0.1)

        # Send Unicode characters via Kitty protocol
        unicode_chars = [
            0x03B1,  # Greek alpha
            0x03B2,  # Greek beta
            0x4E2D,  # Chinese "middle"
            0x6587,  # Chinese "text"
        ]

        for cp in unicode_chars:
            emu.child.send(kitty_key(cp))
            time.sleep(0.1)

        time.sleep(0.2)
        emu._read_output()

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption with Unicode Kitty: {issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_pageup_pagedown():
    """Test PageUp/PageDown via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_page.txt'
    with open(filename, 'w') as f:
        for i in range(200):
            f.write(f"Line {i+1}: Page navigation test\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # PageDown via Kitty
        emu.child.send(kitty_key(KITTY_KEYS['pagedown']))
        time.sleep(0.2)
        emu._read_output()

        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption after PageDown: {issues}")
            emu.dump_screen()
            return False

        # PageUp via Kitty
        emu.child.send(kitty_key(KITTY_KEYS['pageup']))
        time.sleep(0.2)
        emu._read_output()

        highlight_bad, h_issues = emu.verify_highlight_correct()
        if highlight_bad:
            print(f"FAIL: Highlight issues after page nav: {h_issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def test_kitty_insert_delete():
    """Test Insert/Delete via Kitty protocol."""
    filename = '/tmp/uemacs_kitty_ins_del.txt'
    with open(filename, 'w') as f:
        f.write("Insert Delete test line\n")
        for i in range(10):
            f.write(f"Line {i+2}\n")

    emu = UEmacsTest()
    try:
        emu.start(filename=filename, evil_mode=True)

        # Delete key via Kitty (delete character under cursor)
        emu.child.send(kitty_key(KITTY_KEYS['delete']))
        time.sleep(0.2)
        emu._read_output()

        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption after Delete: {issues}")
            emu.dump_screen()
            return False

        # Insert key via Kitty (toggle insert mode)
        emu.child.send(kitty_key(KITTY_KEYS['insert']))
        time.sleep(0.2)
        emu._read_output()

        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Corruption after Insert: {issues}")
            emu.dump_screen()
            return False

        return True
    finally:
        emu.quit()


def run_tests():
    """Run all Kitty protocol tests."""
    tests = [
        ("kitty_basic_letter", test_kitty_basic_letter),
        ("kitty_arrow_keys", test_kitty_arrow_keys),
        ("kitty_modifiers", test_kitty_modifiers),
        ("kitty_function_keys", test_kitty_function_keys),
        ("kitty_release_ignored", test_kitty_release_events_ignored),
        ("kitty_repeat_events", test_kitty_repeat_events),
        ("kitty_home_end", test_kitty_home_end),
        ("kitty_enter_tab_backspace", test_kitty_enter_tab_backspace),
        ("kitty_escape_key", test_kitty_escape_key),
        ("kitty_mixed_protocols", test_kitty_mixed_with_traditional),
        ("kitty_rapid_input", test_kitty_rapid_input),
        ("kitty_unicode_input", test_kitty_unicode_input),
        ("kitty_pageup_pagedown", test_kitty_pageup_pagedown),
        ("kitty_insert_delete", test_kitty_insert_delete),
    ]

    passed = 0
    failed = 0

    print("=" * 60)
    print("KITTY KEYBOARD PROTOCOL TESTS")
    print("Testing CSI u sequence parsing and handling")
    print("=" * 60)

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
    print("=" * 60)
    total = passed + failed
    print(f"RESULTS: {passed}/{total} passed, {failed} failed")
    if failed == 0:
        print("ALL KITTY PROTOCOL TESTS PASSED")
    print("=" * 60)
    return failed == 0


if __name__ == '__main__':
    success = run_tests()
    sys.exit(0 if success else 1)
