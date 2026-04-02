#!/usr/bin/env python3
"""
Realistic Human Typing Simulation Test for μEmacs.

Types the first 100 lines of Edgar Allan Poe's "The Gold-Bug" with:
- Human-like typing patterns (variable speed, pauses at punctuation)
- Random typos with backspace corrections
- Phase 1 (lines 1-50): Default μEmacs keybindings
- Phase 2: Switch to evil-mode
- Phase 3 (lines 51-100): Evil-mode vim keybindings

Exercises comprehensive keybindings in both modes to verify
the editor handles realistic usage patterns correctly.

Usage:
    python3 tests/tui/test_realistic_typing.py
"""

import os
import sys
import time
import random

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from base import UEmacsTest, test_tmp

# Control keys
CTRL_A = '\x01'  # Beginning of line
CTRL_B = '\x02'  # Backward char
CTRL_D = '\x04'  # Delete forward
CTRL_E = '\x05'  # End of line
CTRL_F = '\x06'  # Forward char
CTRL_K = '\x0b'  # Kill to end of line
CTRL_N = '\x0e'  # Next line
CTRL_O = '\x0f'  # Open line
CTRL_P = '\x10'  # Previous line
CTRL_R = '\x12'  # Redo (in evil)
CTRL_Y = '\x19'  # Yank
ESC = '\x1b'
DEL = '\x7f'     # Backspace
ENTER = '\r'

# Path to Poe text
POE_FILE = os.path.join(
    os.path.dirname(__file__), '..', 'data', 'poe-collected-works.txt'
)


def load_poe_lines(num_lines=100):
    """Load first N non-empty lines of Poe text, skipping line 1 (has test artifacts)."""
    with open(POE_FILE, 'r') as f:
        all_lines = f.readlines()

    # Skip line 0 (has test artifacts), collect non-empty lines
    clean_lines = []
    for line in all_lines[1:]:  # Skip first line
        text = line.rstrip('\n')
        # Remove leading indentation for cleaner test
        text = text.lstrip()
        if text:  # Only include non-empty lines
            clean_lines.append(text)
        if len(clean_lines) >= num_lines:
            break

    # If we didn't get enough, repeat the content
    if len(clean_lines) < num_lines:
        original = clean_lines.copy()
        while len(clean_lines) < num_lines:
            clean_lines.extend(original[:num_lines - len(clean_lines)])

    return clean_lines[:num_lines]


def simulate_human_char(emu, char, typo_rate=0.03):
    """Type a single character with human-like behavior.

    Returns True if a typo was made and corrected.
    Note: Does NOT call _read_output() - caller should batch reads.
    """
    made_typo = False

    # Random typo ~3% of the time for letters
    if char.isalpha() and random.random() < typo_rate:
        # Pick adjacent key on QWERTY
        adjacent = {
            'a': 'sq', 's': 'awd', 'd': 'sfe', 'f': 'dgr', 'g': 'fht',
            'h': 'gjy', 'j': 'hku', 'k': 'jli', 'l': 'ko',
            'q': 'wa', 'w': 'qes', 'e': 'wrd', 'r': 'etf', 't': 'ryg',
            'y': 'tuh', 'u': 'yij', 'i': 'uok', 'o': 'ipl', 'p': 'ol',
        }
        lower = char.lower()
        if lower in adjacent:
            wrong = random.choice(adjacent[lower])
            if char.isupper():
                wrong = wrong.upper()
            emu.child.send(wrong)
            time.sleep(random.uniform(0.01, 0.02))
            # Backspace to correct
            emu.child.send(DEL)
            time.sleep(random.uniform(0.02, 0.04))
            made_typo = True

    # Type the correct character
    emu.child.send(char)

    # Variable typing speed (faster for test but still human-like)
    if char in '.!?':
        # Pause at end of sentence
        time.sleep(random.uniform(0.02, 0.04))
    elif char in ',;:':
        # Brief pause at punctuation
        time.sleep(random.uniform(0.01, 0.02))
    elif char == ' ':
        # Slight pause between words
        time.sleep(random.uniform(0.005, 0.015))
    else:
        # Normal character
        time.sleep(random.uniform(0.003, 0.01))

    return made_typo


def type_line_uemacs(emu, text, line_num):
    """Type a line using μEmacs keybindings.

    Occasionally uses movement commands to exercise keybindings.
    """
    typos = 0

    # Every 5th line, demonstrate some μEmacs commands
    if line_num % 5 == 0 and len(text) > 20:
        # Type first half
        half = len(text) // 2
        for char in text[:half]:
            if simulate_human_char(emu, char):
                typos += 1

        # Use Ctrl-A to go to beginning, then Ctrl-E to go back to end
        emu.child.send(CTRL_A)
        time.sleep(0.05)
        emu._read_output()
        emu.child.send(CTRL_E)
        time.sleep(0.05)
        emu._read_output()

        # Type rest
        for char in text[half:]:
            if simulate_human_char(emu, char):
                typos += 1

    # Every 10th line, use Ctrl-K and Ctrl-Y
    elif line_num % 10 == 0 and len(text) > 10:
        # Type full line
        for char in text:
            if simulate_human_char(emu, char):
                typos += 1

        # Go to beginning, kill line, yank it back
        emu.child.send(CTRL_A)
        time.sleep(0.05)
        emu._read_output()
        emu.child.send(CTRL_K)
        time.sleep(0.05)
        emu._read_output()
        emu.child.send(CTRL_Y)
        time.sleep(0.05)
        emu._read_output()

    # Every 7th line, use word movement
    elif line_num % 7 == 0 and ' ' in text:
        # Type line
        for char in text:
            if simulate_human_char(emu, char):
                typos += 1

        # Move back one word (Meta-B = ESC b)
        emu.child.send(ESC + 'b')
        time.sleep(0.05)
        emu._read_output()
        # Move forward one word (Meta-F = ESC f)
        emu.child.send(ESC + 'f')
        time.sleep(0.05)
        emu._read_output()

    else:
        # Normal typing
        for char in text:
            if simulate_human_char(emu, char):
                typos += 1

    # Press Enter to finish line
    emu.child.send(ENTER)
    time.sleep(0.03)
    emu._read_output()

    return typos


def type_line_evil(emu, text, line_num, is_first=False):
    """Type a line using evil-mode keybindings.

    Uses various vim commands to exercise the keymap.
    """
    typos = 0

    if is_first:
        # First line in evil mode - use 'o' to open line below
        emu.child.send('o')
        time.sleep(0.05)
        emu._read_output()
    else:
        # For subsequent lines, we're already in insert mode from previous line
        # Just press Enter
        emu.child.send(ENTER)
        time.sleep(0.03)
        emu._read_output()

    # Every 5th line, use different insert commands
    if line_num % 5 == 0 and len(text) > 20:
        # Type first part
        half = len(text) // 2
        for char in text[:half]:
            if simulate_human_char(emu, char):
                typos += 1

        # Exit insert, use movements, re-enter
        emu.child.send(ESC)
        time.sleep(0.05)
        emu._read_output()

        # Move with h/l
        emu.child.send('hh')
        time.sleep(0.03)
        emu._read_output()
        emu.child.send('ll')
        time.sleep(0.03)
        emu._read_output()

        # Append to continue
        emu.child.send('a')
        time.sleep(0.05)
        emu._read_output()

        # Type rest
        for char in text[half:]:
            if simulate_human_char(emu, char):
                typos += 1

    # Every 8th line, use word movements in normal mode
    elif line_num % 8 == 0 and ' ' in text:
        # Type full line
        for char in text:
            if simulate_human_char(emu, char):
                typos += 1

        # Exit, word movements, back to insert
        emu.child.send(ESC)
        time.sleep(0.05)
        emu._read_output()

        emu.child.send('b')  # Back word
        time.sleep(0.03)
        emu._read_output()
        emu.child.send('w')  # Forward word
        time.sleep(0.03)
        emu._read_output()
        emu.child.send('$')  # End of line
        time.sleep(0.03)
        emu._read_output()

        # Re-enter insert for next line
        emu.child.send('a')
        time.sleep(0.05)
        emu._read_output()

    # Every 12th line, test undo
    elif line_num % 12 == 0 and len(text) > 5:
        # Type line
        for char in text:
            if simulate_human_char(emu, char):
                typos += 1

        # Exit, delete char with x, undo
        emu.child.send(ESC)
        time.sleep(0.05)
        emu._read_output()

        emu.child.send('x')  # Delete char
        time.sleep(0.03)
        emu._read_output()
        emu.child.send('u')  # Undo
        time.sleep(0.05)
        emu._read_output()

        # Back to insert at end
        emu.child.send('A')
        time.sleep(0.05)
        emu._read_output()

    else:
        # Normal typing
        for char in text:
            if simulate_human_char(emu, char):
                typos += 1

    return typos


def test_realistic_poe_typing():
    """Main test: Type 100 lines of Poe with human-like behavior."""
    print("=" * 60, flush=True)
    print("REALISTIC POE TYPING TEST", flush=True)
    print("=" * 60, flush=True)

    # Load Poe text
    try:
        poe_lines = load_poe_lines(100)
    except FileNotFoundError:
        print(f"FAIL: Could not load Poe text from {POE_FILE}")
        return False

    print(f"Loaded {len(poe_lines)} lines of Poe text", flush=True)

    # Create temp file for test
    test_file = test_tmp('muemacs_poe_test.txt')
    with open(test_file, 'w') as f:
        f.write('')  # Empty file

    print("Creating UEmacsTest...", flush=True)
    emu = UEmacsTest(rows=30, cols=100)  # Larger terminal for prose

    try:
        # Start WITHOUT evil mode (default μEmacs)
        print("Starting μEmacs...", flush=True)
        emu.start(filename=test_file, evil_mode=False)
        print("μEmacs started.", flush=True)

        # Check message line for actual evil mode state
        # Status line always shows "EVIL" label, message line shows actual state
        print("Getting message line...", flush=True)
        message = emu.get_message_line()
        print(f"Message: {message!r}", flush=True)
        if 'EVIL MODE ENABLED' in message:
            print("  Disabling evil mode (was enabled via settings)...", flush=True)
            # In evil NORMAL mode, use ':' for command prompt (not M-X)
            emu.child.send(':evil-mode' + ENTER)
            time.sleep(0.3)
            emu._read_output()

            # Verify evil mode is disabled
            message = emu.get_message_line()
            if 'EVIL MODE ENABLED' in message:
                # Try once more with different timing
                emu.child.send(':evil-mode' + ENTER)
                time.sleep(0.5)
                emu._read_output()
                message = emu.get_message_line()
                if 'EVIL MODE ENABLED' in message:
                    print(f"FAIL: Could not disable evil mode. Message: {message}", flush=True)
                    emu.dump_screen()
                    return False

        print("  Starting in default μEmacs mode (evil disabled)", flush=True)

        print("\n--- PHASE 1: μEmacs Mode (Lines 1-50) ---", flush=True)
        total_typos = 0
        lines_typed = 0

        # Phase 1: Type lines 1-50 in μEmacs mode
        for i, line in enumerate(poe_lines[:50], start=1):
            typos = type_line_uemacs(emu, line, i)
            total_typos += typos
            lines_typed += 1

            if i % 10 == 0:
                print(f"  Typed {i} lines ({total_typos} typos corrected)")

                # Periodic corruption check
                corrupt, issues = emu.check_all_corruption()
                if corrupt:
                    print(f"  WARNING: Display corruption at line {i}: {issues}")

        print(f"Phase 1 complete: {lines_typed} lines, {total_typos} typos corrected")

        # Phase 2: Switch to evil-mode
        print("\n--- PHASE 2: Switch to Evil Mode ---")

        # Use M-X evil-mode
        emu.child.send(ESC + 'X')  # Meta-X
        time.sleep(0.2)
        emu._read_output()
        emu.child.send('evil-mode')
        time.sleep(0.1)
        emu._read_output()
        emu.child.send(ENTER)
        time.sleep(0.3)
        emu._read_output()

        # Verify evil mode enabled (check message line, not status line)
        message = emu.get_message_line()
        if 'EVIL MODE ENABLED' not in message:
            print(f"FAIL: Evil mode not enabled. Message: {message}")
            emu.dump_screen()
            return False

        print("  Evil mode enabled successfully")

        # Phase 3: Type lines 51-100 in evil mode
        print("\n--- PHASE 3: Evil Mode (Lines 51-100) ---")
        phase2_typos = 0

        for i, line in enumerate(poe_lines[50:], start=51):
            is_first = (i == 51)
            typos = type_line_evil(emu, line, i, is_first=is_first)
            phase2_typos += typos
            lines_typed += 1

            if i % 10 == 0:
                print(f"  Typed {i} lines ({phase2_typos} typos in evil mode)")

                # Periodic corruption check
                corrupt, issues = emu.check_all_corruption()
                if corrupt:
                    print(f"  WARNING: Display corruption at line {i}: {issues}")

        total_typos += phase2_typos
        print(f"Phase 3 complete: {lines_typed} total lines, {total_typos} total typos")

        # Exit insert mode
        emu.child.send(ESC)
        time.sleep(0.1)
        emu._read_output()

        # Final verification
        print("\n--- FINAL VERIFICATION ---")

        # Check for corruption
        corrupt, issues = emu.check_all_corruption()
        if corrupt:
            print(f"FAIL: Final corruption check failed: {issues}")
            emu.dump_screen()
            return False

        print(f"  Total lines typed: {lines_typed}")
        print(f"  Total typos corrected: {total_typos}")
        print(f"  No display corruption detected")

        return True

    finally:
        emu.quit()


def run_tests():
    """Run all realistic typing tests."""
    tests = [
        ("realistic_poe_typing", test_realistic_poe_typing),
    ]

    passed = 0
    failed = 0

    for name, test_func in tests:
        print(f"\nRunning: {name}...", end=" ", flush=True)
        try:
            result = test_func()
            if result:
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

    print("\n" + "=" * 60)
    print(f"RESULTS: {passed}/{passed+failed} passed, {failed} failed")
    if failed == 0:
        print("ALL REALISTIC TYPING TESTS PASSED")
    print("=" * 60)

    return failed == 0


if __name__ == '__main__':
    success = run_tests()
    sys.exit(0 if success else 1)
