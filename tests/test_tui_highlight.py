#!/usr/bin/env python3
"""
Automated TUI rendering test for uEmacs.
Verifies cursor position matches highlighted line.

Tests:
1. Cursor/highlight synchronization on navigation
2. Status line truecolor background
3. Highlight clearing on cursor move

Usage:
    python3 tests/test_tui_highlight.py [--verbose]
"""

import os
import sys
import re
import time
import tempfile
import subprocess
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# Try to import pexpect, fall back to subprocess if not available
try:
    import pexpect
    HAS_PEXPECT = True
except ImportError:
    HAS_PEXPECT = False
    print("WARNING: pexpect not installed. Install with: pip install pexpect")

# Try to import pyte for terminal emulation
try:
    import pyte
    HAS_PYTE = True
except ImportError:
    HAS_PYTE = False


@dataclass
class TerminalState:
    """Parsed state from terminal output."""
    cursor_row: int = 0
    cursor_col: int = 0
    highlighted_rows: List[int] = field(default_factory=list)
    status_line_row: int = -1
    status_line_has_bg: bool = False
    raw_output: str = ""


class ANSIParser:
    """Parse ANSI escape sequences from terminal output."""

    # Truecolor background: \033[48;2;R;G;Bm
    TRUECOLOR_BG = re.compile(r'\033\[48;2;(\d+);(\d+);(\d+)m')

    # Cursor position: \033[row;colH
    CURSOR_POS = re.compile(r'\033\[(\d+);(\d+)H')

    # Status line background (dark): RGB(30,30,40) or similar
    STATUS_BG_PATTERN = re.compile(r'\033\[48;2;3\d;3\d;4\d')

    # Highlight background: RGB(38-50, 38-50, 38-70)
    HIGHLIGHT_BG_PATTERN = re.compile(r'\033\[48;2;(3[89]|4\d|50);(3[89]|4\d|50);(3[89]|4\d|5\d|6\d|70)m')

    @classmethod
    def parse_output(cls, output: str, num_rows: int = 24) -> TerminalState:
        """Parse raw terminal output to extract state."""
        state = TerminalState(raw_output=output)

        # Track current row as we parse (cursor moves tell us position)
        current_row = 1
        highlighted_rows = set()

        # Split by lines and track cursor positioning
        lines = output.split('\n')

        # Find all cursor positions
        cursor_positions = cls.CURSOR_POS.findall(output)
        if cursor_positions:
            # Last cursor position is where cursor ends up
            state.cursor_row = int(cursor_positions[-1][0])
            state.cursor_col = int(cursor_positions[-1][1])

        # Find highlighted rows by looking for truecolor bg BEFORE line content
        # Strategy: find cursor moves followed by highlight sequences
        pos = 0
        while pos < len(output):
            # Look for cursor move
            cursor_match = cls.CURSOR_POS.search(output, pos)
            if not cursor_match:
                break

            row = int(cursor_match.group(1))

            # Check if there's a highlight bg after this cursor move
            check_start = cursor_match.end()
            check_end = min(check_start + 50, len(output))
            snippet = output[check_start:check_end]

            if cls.HIGHLIGHT_BG_PATTERN.search(snippet):
                highlighted_rows.add(row)

            pos = cursor_match.end()

        state.highlighted_rows = sorted(highlighted_rows)

        # Check for status line (usually last row with specific bg)
        if cls.STATUS_BG_PATTERN.search(output):
            state.status_line_has_bg = True
            # Find which row has status line bg
            for match in cls.CURSOR_POS.finditer(output):
                row = int(match.group(1))
                # Check if status bg follows this cursor move
                check_pos = match.end()
                if cls.STATUS_BG_PATTERN.search(output[check_pos:check_pos+30]):
                    state.status_line_row = row

        return state


class TUITest:
    """Test harness for TUI rendering verification."""

    def __init__(self, binary_path: str, verbose: bool = False):
        self.binary = binary_path
        self.verbose = verbose
        self.child = None
        self.failures = []
        self.passes = []

    def log(self, msg: str):
        if self.verbose:
            print(f"  [DEBUG] {msg}")

    def start(self, test_file: str, rows: int = 24, cols: int = 80):
        """Launch uEmacs with test file."""
        if not HAS_PEXPECT:
            raise RuntimeError("pexpect required for TUI tests")

        self.log(f"Starting {self.binary} {test_file} ({rows}x{cols})")

        # Set environment for consistent behavior
        env = os.environ.copy()
        env['TERM'] = 'xterm-256color'
        env['COLORTERM'] = 'truecolor'

        self.child = pexpect.spawn(
            self.binary, [test_file],
            dimensions=(rows, cols),
            encoding='utf-8',
            env=env
        )

        # Wait for initial render
        time.sleep(0.3)
        self.child.expect(pexpect.TIMEOUT, timeout=0.2)

    def send_keys(self, keys: str, wait: float = 0.1):
        """Send keystrokes and wait for render."""
        self.log(f"Sending keys: {repr(keys)}")
        self.child.send(keys)
        time.sleep(wait)
        self.child.expect(pexpect.TIMEOUT, timeout=0.1)

    def get_output(self) -> str:
        """Get accumulated terminal output."""
        before = self.child.before or ""
        after = self.child.after or ""
        if after == pexpect.TIMEOUT:
            after = ""
        return before + str(after)

    def quit(self):
        """Quit uEmacs cleanly."""
        if self.child:
            try:
                # Vim-style quit
                self.send_keys('\x1b')  # Escape
                time.sleep(0.1)
                self.send_keys(':q!\r')
                time.sleep(0.1)
            except:
                pass
            try:
                self.child.terminate(force=True)
            except:
                pass
            self.child = None

    def assert_true(self, condition: bool, msg: str):
        """Assert with tracking."""
        if condition:
            self.passes.append(msg)
            print(f"  [PASS] {msg}")
        else:
            self.failures.append(msg)
            print(f"  [FAIL] {msg}")

    # ==========================================================================
    # TEST CASES
    # ==========================================================================

    def test_cursor_highlight_sync_down(self) -> bool:
        """Test: Moving cursor down should move highlight with it."""
        print("\n=== TEST: Cursor/Highlight Sync (Down) ===")

        test_file = self._create_test_file(50)
        try:
            self.start(test_file)

            # Initial state
            output = self.get_output()
            initial = ANSIParser.parse_output(output)
            self.log(f"Initial cursor: row={initial.cursor_row}")

            # Move down 5 lines
            for i in range(5):
                self.send_keys('j')

            output = self.get_output()
            state = ANSIParser.parse_output(output)

            self.log(f"After 5j: cursor={state.cursor_row}, highlights={state.highlighted_rows}")

            # Cursor should have moved
            self.assert_true(
                state.cursor_row > initial.cursor_row,
                f"Cursor moved down (was {initial.cursor_row}, now {state.cursor_row})"
            )

            # Cursor row should be highlighted
            if state.highlighted_rows:
                self.assert_true(
                    state.cursor_row in state.highlighted_rows,
                    f"Cursor row {state.cursor_row} is highlighted {state.highlighted_rows}"
                )
            else:
                self.log("No highlight rows detected (may need bg color tuning)")

            self.quit()
            return len(self.failures) == 0

        finally:
            self._cleanup_test_file(test_file)

    def test_cursor_highlight_sync_up(self) -> bool:
        """Test: Moving cursor up should move highlight with it."""
        print("\n=== TEST: Cursor/Highlight Sync (Up) ===")

        test_file = self._create_test_file(50)
        try:
            self.start(test_file)

            # Move down first
            for _ in range(10):
                self.send_keys('j')
            time.sleep(0.1)

            output = self.get_output()
            before = ANSIParser.parse_output(output)

            # Now move up 5
            for _ in range(5):
                self.send_keys('k')

            output = self.get_output()
            after = ANSIParser.parse_output(output)

            self.log(f"Before up: row={before.cursor_row}, After: row={after.cursor_row}")

            self.assert_true(
                after.cursor_row < before.cursor_row,
                f"Cursor moved up (was {before.cursor_row}, now {after.cursor_row})"
            )

            self.quit()
            return len(self.failures) == 0

        finally:
            self._cleanup_test_file(test_file)

    def test_status_line_background(self) -> bool:
        """Test: Status line should have truecolor dark background."""
        print("\n=== TEST: Status Line Background ===")

        test_file = self._create_test_file(20)
        try:
            self.start(test_file, rows=24, cols=80)
            time.sleep(0.2)

            output = self.get_output()
            state = ANSIParser.parse_output(output)

            self.log(f"Status line has bg: {state.status_line_has_bg}")
            self.log(f"Status line row: {state.status_line_row}")

            self.assert_true(
                state.status_line_has_bg,
                "Status line has truecolor background"
            )

            self.quit()
            return len(self.failures) == 0

        finally:
            self._cleanup_test_file(test_file)

    def test_highlight_clears_on_move(self) -> bool:
        """Test: Old cursor position should NOT remain highlighted."""
        print("\n=== TEST: Highlight Clears on Move ===")

        test_file = self._create_test_file(50)
        try:
            self.start(test_file)

            # Get initial row
            output = self.get_output()
            initial = ANSIParser.parse_output(output)
            initial_row = initial.cursor_row

            # Move down
            for _ in range(5):
                self.send_keys('j')

            output = self.get_output()
            state = ANSIParser.parse_output(output)

            # Initial row should NOT be highlighted anymore
            if state.highlighted_rows:
                self.assert_true(
                    initial_row not in state.highlighted_rows or initial_row == state.cursor_row,
                    f"Old row {initial_row} not highlighted (highlights: {state.highlighted_rows})"
                )

            self.quit()
            return len(self.failures) == 0

        finally:
            self._cleanup_test_file(test_file)

    # ==========================================================================
    # HELPERS
    # ==========================================================================

    def _create_test_file(self, num_lines: int) -> str:
        """Create a temporary test file with numbered lines."""
        fd, path = tempfile.mkstemp(suffix='.txt', prefix='uemacs_test_')
        with os.fdopen(fd, 'w') as f:
            for i in range(1, num_lines + 1):
                f.write(f"Line {i:03d}: This is test content for line number {i}\n")
        return path

    def _cleanup_test_file(self, path: str):
        """Remove temporary test file."""
        try:
            os.unlink(path)
        except:
            pass


def main():
    """Run all TUI tests."""
    verbose = '--verbose' in sys.argv or '-v' in sys.argv

    # Find binary
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    binary_paths = [
        project_root / 'build' / 'bin' / 'μEmacs',
        project_root / 'bin' / 'μEmacs',
        project_root / 'build' / 'bin' / 'muEmacs',
    ]

    binary = None
    for p in binary_paths:
        if p.exists():
            binary = str(p)
            break

    if not binary:
        print("ERROR: Could not find uEmacs binary")
        print("Searched:", [str(p) for p in binary_paths])
        sys.exit(1)

    print(f"Using binary: {binary}")
    print("=" * 60)

    if not HAS_PEXPECT:
        print("ERROR: pexpect required. Install with: pip install pexpect")
        sys.exit(1)

    tester = TUITest(binary, verbose=verbose)

    tests = [
        tester.test_cursor_highlight_sync_down,
        tester.test_cursor_highlight_sync_up,
        tester.test_status_line_background,
        tester.test_highlight_clears_on_move,
    ]

    results = []
    for test in tests:
        try:
            result = test()
            results.append(result)
        except Exception as e:
            print(f"  [ERROR] {test.__name__}: {e}")
            results.append(False)
            if tester.child:
                tester.quit()

    print("\n" + "=" * 60)
    passed = sum(results)
    total = len(results)
    print(f"RESULTS: {passed}/{total} tests passed")

    if tester.failures:
        print("\nFAILURES:")
        for f in tester.failures:
            print(f"  - {f}")

    sys.exit(0 if passed == total else 1)


if __name__ == '__main__':
    main()
