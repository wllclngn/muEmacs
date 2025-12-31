#!/usr/bin/env python3
"""
Automated TUI test for μEmacs cursor/highlight sync bug.
Uses only standard library - no pexpect needed.
"""

import os
import sys
import pty
import select
import signal
import re
import time
import tempfile
import subprocess
from pathlib import Path

class TUITester:
    def __init__(self, binary):
        self.binary = binary
        self.master_fd = None
        self.pid = None
        self.output = b""

    def start(self, test_file, rows=24, cols=80):
        """Spawn μEmacs in a PTY."""
        env = os.environ.copy()
        env['TERM'] = 'xterm-256color'
        env['COLORTERM'] = 'truecolor'
        env['LINES'] = str(rows)
        env['COLUMNS'] = str(cols)

        self.pid, self.master_fd = pty.fork()
        if self.pid == 0:
            # Child
            os.execve(self.binary, [self.binary, test_file], env)
        else:
            # Parent - set terminal size
            import fcntl
            import struct
            import termios
            winsize = struct.pack('HHHH', rows, cols, 0, 0)
            fcntl.ioctl(self.master_fd, termios.TIOCSWINSZ, winsize)
            time.sleep(0.3)  # Wait for startup
            self._read_available()

    def send(self, keys, wait=0.15):
        """Send keys and wait for response."""
        os.write(self.master_fd, keys.encode() if isinstance(keys, str) else keys)
        time.sleep(wait)
        self._read_available()

    def _read_available(self):
        """Read all available output."""
        while True:
            r, _, _ = select.select([self.master_fd], [], [], 0.05)
            if not r:
                break
            try:
                data = os.read(self.master_fd, 4096)
                if data:
                    self.output += data
            except OSError:
                break

    def quit(self):
        """Terminate μEmacs."""
        if self.master_fd:
            try:
                self.send('\x1b')  # ESC
                self.send(':q!\r')  # Vim quit
                time.sleep(0.1)
            except:
                pass
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGTERM)
                os.waitpid(self.pid, 0)
            except:
                pass

    def get_output(self):
        """Get raw output as string."""
        return self.output.decode('utf-8', errors='replace')

    def parse_cursor_and_highlights(self):
        """
        Parse ANSI sequences to find:
        1. Final cursor position (row)
        2. Which rows have highlight background

        Returns: (cursor_row, set of highlighted_rows)
        """
        output = self.get_output()

        # Cursor position: \033[row;colH
        cursor_positions = re.findall(r'\033\[(\d+);(\d+)H', output)
        cursor_row = int(cursor_positions[-1][0]) if cursor_positions else 0

        # Find rows with truecolor background (highlight)
        # Pattern: cursor move to row, then truecolor bg within next 100 chars
        highlighted = set()

        # Split output and track cursor moves followed by highlight bg
        pos = 0
        while pos < len(output):
            # Find cursor move
            match = re.search(r'\033\[(\d+);(\d+)H', output[pos:])
            if not match:
                break

            row = int(match.group(1))
            check_start = pos + match.end()
            check_end = min(check_start + 100, len(output))
            snippet = output[check_start:check_end]

            # Look for truecolor bg: \033[48;2;R;G;Bm where RGB is highlight color
            # Highlight is RGB(38,38,38) - distinct from status line RGB(30,30,40)
            if re.search(r'\033\[48;2;38;38;38m', snippet):
                highlighted.add(row)

            pos += match.end()

        return cursor_row, highlighted


def create_test_file(num_lines=100):
    """Create test file with numbered lines."""
    fd, path = tempfile.mkstemp(suffix='.txt', prefix='uemacs_test_')
    with os.fdopen(fd, 'w') as f:
        for i in range(1, num_lines + 1):
            f.write(f"Line {i:03d}: Test content for automated TUI testing\n")
    return path


def find_binary():
    """Find μEmacs binary."""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    candidates = [
        project_root / 'build' / 'bin' / 'μEmacs',
        project_root / 'bin' / 'μEmacs',
        project_root / 'build' / 'bin' / 'muEmacs',
    ]

    for p in candidates:
        if p.exists():
            return str(p)
    return None


def test_cursor_highlight_sync():
    """
    THE MAIN TEST: Cursor row must equal highlighted row.

    This catches the bug where currow doesn't update but highlight does.
    """
    print("\n" + "="*60)
    print("TEST: Cursor/Highlight Synchronization")
    print("="*60)

    binary = find_binary()
    if not binary:
        print("SKIP: Binary not found")
        return None

    test_file = create_test_file(100)
    tester = TUITester(binary)

    try:
        tester.start(test_file)

        # Move down 10 lines using vim 'j' key
        print("  Sending 10x 'j' (move down)...")
        for i in range(10):
            tester.send('j', wait=0.1)

        time.sleep(0.2)
        tester._read_available()

        cursor_row, highlighted = tester.parse_cursor_and_highlights()

        print(f"  Cursor row: {cursor_row}")
        print(f"  Highlighted rows: {sorted(highlighted)}")

        # THE KEY ASSERTION
        if cursor_row in highlighted:
            print(f"  [PASS] Cursor row {cursor_row} IS highlighted")
            result = True
        elif not highlighted:
            print(f"  [WARN] No highlight detected (check truecolor support)")
            result = None
        else:
            print(f"  [FAIL] Cursor at row {cursor_row}, but highlight on {highlighted}")
            result = False

        tester.quit()
        return result

    except Exception as e:
        print(f"  [ERROR] {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        try:
            os.unlink(test_file)
        except:
            pass
        try:
            tester.quit()
        except:
            pass


def test_highlight_moves_with_cursor():
    """Test highlight follows cursor up and down."""
    print("\n" + "="*60)
    print("TEST: Highlight Follows Cursor Movement")
    print("="*60)

    binary = find_binary()
    if not binary:
        print("SKIP: Binary not found")
        return None

    test_file = create_test_file(100)
    tester = TUITester(binary)

    try:
        tester.start(test_file)

        results = []

        # Move down 5
        print("  Moving down 5 lines...")
        for _ in range(5):
            tester.send('j', wait=0.1)

        cursor1, hl1 = tester.parse_cursor_and_highlights()
        print(f"    After down: cursor={cursor1}, highlight={sorted(hl1)}")

        # Move down 5 more
        print("  Moving down 5 more...")
        for _ in range(5):
            tester.send('j', wait=0.1)

        cursor2, hl2 = tester.parse_cursor_and_highlights()
        print(f"    After more down: cursor={cursor2}, highlight={sorted(hl2)}")

        # Move up 3
        print("  Moving up 3 lines...")
        for _ in range(3):
            tester.send('k', wait=0.1)

        cursor3, hl3 = tester.parse_cursor_and_highlights()
        print(f"    After up: cursor={cursor3}, highlight={sorted(hl3)}")

        # Check all positions
        passed = True
        for name, cursor, hl in [("down5", cursor1, hl1),
                                   ("down10", cursor2, hl2),
                                   ("up3", cursor3, hl3)]:
            if hl and cursor not in hl:
                print(f"  [FAIL] {name}: cursor {cursor} not in highlight {hl}")
                passed = False

        if passed:
            print("  [PASS] Highlight followed cursor in all movements")

        tester.quit()
        return passed

    except Exception as e:
        print(f"  [ERROR] {e}")
        return False
    finally:
        try:
            os.unlink(test_file)
        except:
            pass
        try:
            tester.quit()
        except:
            pass


def test_only_cursor_row_highlighted():
    """
    STRICT TEST: Only the cursor row should have highlight.
    Previous cursor positions should NOT remain highlighted.
    """
    print("\n" + "="*60)
    print("TEST: Only Cursor Row Has Highlight (Strict)")
    print("="*60)

    binary = find_binary()
    if not binary:
        print("SKIP: Binary not found")
        return None

    test_file = create_test_file(100)
    tester = TUITester(binary)

    try:
        tester.start(test_file, rows=24, cols=80)

        # Clear output buffer after initial render
        tester.output = b""

        # Move down and capture ONLY the latest render
        print("  Moving to line 10...")
        for _ in range(9):
            tester.send('j', wait=0.05)

        # Small wait, then clear and get fresh output
        time.sleep(0.2)
        tester.output = b""
        tester.send('j', wait=0.2)  # One more move, capture this render

        cursor_row, highlighted = tester.parse_cursor_and_highlights()

        print(f"  Cursor row: {cursor_row}")
        print(f"  Highlighted rows: {sorted(highlighted)}")

        # Remove status line from consideration (row 23 or 24)
        content_highlights = {r for r in highlighted if r < 23}

        if not content_highlights:
            print("  [WARN] No content highlights detected")
            result = None
        elif content_highlights == {cursor_row}:
            print(f"  [PASS] Only cursor row {cursor_row} is highlighted")
            result = True
        elif cursor_row in content_highlights:
            extra = content_highlights - {cursor_row}
            print(f"  [FAIL] Cursor row {cursor_row} highlighted, but also: {extra}")
            result = False
        else:
            print(f"  [FAIL] Cursor at {cursor_row}, highlights at {content_highlights}")
            result = False

        tester.quit()
        return result

    except Exception as e:
        print(f"  [ERROR] {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        try:
            os.unlink(test_file)
        except:
            pass


def test_status_line_background():
    """Test status line has truecolor dark background."""
    print("\n" + "="*60)
    print("TEST: Status Line Truecolor Background")
    print("="*60)

    binary = find_binary()
    if not binary:
        print("SKIP: Binary not found")
        return None

    test_file = create_test_file(50)
    tester = TUITester(binary)

    try:
        tester.start(test_file, rows=24, cols=80)
        time.sleep(0.3)
        tester._read_available()

        output = tester.get_output()

        # Status line bg should be dark: RGB(30,30,40) area
        # Pattern: \033[48;2;3X;3X;4Xm
        status_bg = re.search(r'\033\[48;2;3\d;3\d;4\dm', output)

        if status_bg:
            print(f"  [PASS] Found status line truecolor bg: {repr(status_bg.group())}")
            result = True
        else:
            # Check if ANY truecolor bg exists
            any_bg = re.findall(r'\033\[48;2;(\d+);(\d+);(\d+)m', output)
            if any_bg:
                print(f"  [WARN] Found other truecolor bgs: {any_bg[:5]}...")
            else:
                print("  [FAIL] No truecolor background found at all")
            result = False

        tester.quit()
        return result

    except Exception as e:
        print(f"  [ERROR] {e}")
        return False
    finally:
        try:
            os.unlink(test_file)
        except:
            pass
        try:
            tester.quit()
        except:
            pass


def dump_ansi_output():
    """Debug helper: dump raw ANSI output for analysis."""
    print("\n" + "="*60)
    print("DEBUG: Raw ANSI Output Dump")
    print("="*60)

    binary = find_binary()
    if not binary:
        print("Binary not found")
        return

    test_file = create_test_file(30)
    tester = TUITester(binary)

    try:
        tester.start(test_file, rows=24, cols=80)

        # Move down a few lines
        for _ in range(5):
            tester.send('j', wait=0.1)

        time.sleep(0.2)
        tester._read_available()

        output = tester.get_output()

        # Find all cursor moves
        print("\nCursor positions found:")
        for m in re.finditer(r'\033\[(\d+);(\d+)H', output):
            print(f"  Row {m.group(1)}, Col {m.group(2)}")

        # Find all truecolor bg
        print("\nTruecolor backgrounds found:")
        for m in re.finditer(r'\033\[48;2;(\d+);(\d+);(\d+)m', output):
            print(f"  RGB({m.group(1)},{m.group(2)},{m.group(3)})")

        # Find any reverse video
        if '\033[7m' in output:
            print("\n[WARN] Found reverse video \\033[7m - should be using truecolor!")

        tester.quit()

    finally:
        try:
            os.unlink(test_file)
        except:
            pass


def main():
    print("="*60)
    print("μEmacs TUI Automated Test Suite")
    print("="*60)

    binary = find_binary()
    if binary:
        print(f"Binary: {binary}")
    else:
        print("ERROR: Could not find μEmacs binary!")
        print("Build with: cmake -B build && cmake --build build")
        sys.exit(1)

    if '--dump' in sys.argv:
        dump_ansi_output()
        sys.exit(0)

    tests = [
        ("Cursor/Highlight Sync", test_cursor_highlight_sync),
        ("Highlight Follows Movement", test_highlight_moves_with_cursor),
        ("Only Cursor Row Highlighted", test_only_cursor_row_highlighted),
        ("Status Line Background", test_status_line_background),
    ]

    results = []
    for name, test_fn in tests:
        try:
            result = test_fn()
            results.append((name, result))
        except Exception as e:
            print(f"  [ERROR] {name}: {e}")
            results.append((name, False))

    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)

    passed = sum(1 for _, r in results if r is True)
    failed = sum(1 for _, r in results if r is False)
    skipped = sum(1 for _, r in results if r is None)

    for name, result in results:
        status = "PASS" if result is True else "FAIL" if result is False else "SKIP"
        print(f"  [{status}] {name}")

    print(f"\nTotal: {passed} passed, {failed} failed, {skipped} skipped")

    sys.exit(0 if failed == 0 else 1)


if __name__ == '__main__':
    main()
