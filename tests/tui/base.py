#!/usr/bin/env python3
"""
Base test harness for μEmacs TUI tests using pyte terminal emulator.

Enhanced with exhaustive corruption detection and cursor tracking for
fully automated verification - no manual testing required.
"""

import pyte
import pexpect
import time
import os
import re


class CursorTracker:
    """Track cursor position history and detect unexpected jumps."""

    def __init__(self, emu):
        self.emu = emu
        self.history = []
        self.record()

    def record(self):
        """Record current cursor position."""
        pos = (self.emu.screen.cursor.y, self.emu.screen.cursor.x)
        self.history.append(pos)
        return pos

    def check_movement(self, expected_delta_row=None, expected_delta_col=None, max_teleport=20):
        """Check if last movement was as expected.

        Args:
            expected_delta_row: Expected row change (None = any)
            expected_delta_col: Expected col change (None = any)
            max_teleport: Maximum row jump before considered teleportation

        Returns:
            Tuple of (has_issues, issue_list)
        """
        if len(self.history) < 2:
            return (False, [])

        prev_row, prev_col = self.history[-2]
        curr_row, curr_col = self.history[-1]

        delta_row = curr_row - prev_row
        delta_col = curr_col - prev_col

        issues = []

        if expected_delta_row is not None and delta_row != expected_delta_row:
            issues.append(f"Expected row delta {expected_delta_row}, got {delta_row}")

        if expected_delta_col is not None and delta_col != expected_delta_col:
            issues.append(f"Expected col delta {expected_delta_col}, got {delta_col}")

        # Detect teleportation (large unexpected jumps)
        if expected_delta_row is None and abs(delta_row) > max_teleport:
            issues.append(f"Cursor teleported {delta_row} rows ({prev_row} -> {curr_row})")

        return (len(issues) > 0, issues)

    def get_last_position(self):
        """Get most recent recorded position."""
        return self.history[-1] if self.history else None

    def clear(self):
        """Clear history."""
        self.history = []


class UEmacsTest:
    """Test harness for μEmacs using pyte terminal emulator."""

    def __init__(self, rows=24, cols=80):
        self.rows = rows
        self.cols = cols
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)
        self.child = None
        self.debug = os.environ.get('DEBUG', '0') == '1'

    def create_test_file(self, content=None, filename=None):
        """Create a temporary test file with specified content.

        Args:
            content: String content to write. If None, creates default test content.
            filename: Path for the file. If None, uses default temp path.

        Returns:
            The filename that was created.
        """
        if filename is None:
            filename = '/tmp/muemacs_tui_test.txt'
        if content is None:
            content = "This is a test file.\n" + "".join(f"Line {i+2}\n" for i in range(20))
        with open(filename, 'w') as f:
            f.write(content)
        return filename

    def start(self, filename=None, evil_mode=False):
        """Launch μEmacs in a PTY.

        Args:
            filename: File to open (creates temp file if None)
            evil_mode: If True, enable evil mode on startup
        """
        if filename is None:
            filename = self.create_test_file()

        env = os.environ.copy()
        env['LINES'] = str(self.rows)
        env['COLUMNS'] = str(self.cols)
        env['TERM'] = 'xterm-256color'

        # Find μEmacs binary
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(os.path.dirname(script_dir))
        binary = os.path.join(project_root, 'build', 'bin', 'μEmacs')

        if not os.path.exists(binary):
            raise FileNotFoundError(f"μEmacs binary not found at {binary}. Run 'cmake --build build' first.")

        if self.debug:
            print(f"Starting: {binary} {filename}")
            print(f"Terminal: {self.cols}x{self.rows}")

        self.child = pexpect.spawn(
            binary, [filename],
            dimensions=(self.rows, self.cols),
            env=env,
            encoding='utf-8',
            timeout=5
        )
        self.wait_for_startup()

        if evil_mode:
            self.enable_evil_mode()
        else:
            self.disable_evil_mode()

    def wait_for_startup(self, timeout=2):
        """Wait for initial render."""
        time.sleep(0.5)
        self._read_output()

    def enable_evil_mode(self):
        """Enable evil mode.

        Note: Evil mode may already be enabled via settings.toml.
        This function checks if it's enabled and tries to enable it if not.
        """
        # Check if evil mode is already enabled (shows NORMAL in modeline)
        status_line = self.get_line(self.rows - 2)  # Second to last row is modeline
        if 'NORMAL' in status_line:
            if self.debug:
                print("Evil mode already enabled (from settings)")
            return

        # Send ESC then x separately (requires pause for meta-key processing)
        self.child.send('\x1b')  # ESC
        time.sleep(0.1)
        self.child.send('x')  # x (M-x = execute-named-command)
        time.sleep(0.2)
        self._read_output()

        self.child.send('evil-mode\r')
        time.sleep(0.3)
        self._read_output()

        if self.debug:
            print(f"After enabling, message line: '{self.get_message_line()}'")
            self.dump_screen()

    def disable_evil_mode(self):
        """Disable evil mode if enabled.

        Checks if evil mode is on and toggles it off.
        """
        # Check if evil mode is enabled (shows NORMAL in modeline)
        status_line = self.get_line(self.rows - 2)  # Second to last row is modeline
        if 'NORMAL' not in status_line:
            if self.debug:
                print("Evil mode not enabled, nothing to disable")
            return

        # Toggle evil mode off via M-x evil-mode
        self.child.send('\x1b')  # ESC
        time.sleep(0.1)
        self.child.send('x')  # x (M-x = execute-named-command)
        time.sleep(0.2)
        self._read_output()

        self.child.send('evil-mode\r')
        time.sleep(0.3)
        self._read_output()

        if self.debug:
            print(f"After disabling, status line: '{self.get_line(self.rows - 2)}'")
            self.dump_screen()

    def send(self, keys):
        """Send keystrokes to μEmacs."""
        if self.debug:
            print(f"Sending: {repr(keys)}")
        self.child.send(keys)
        time.sleep(0.1)
        self._read_output()

    def send_key(self, key):
        """Send special key (ESC, Enter, etc)."""
        key_map = {
            'ESC': '\x1b',
            'ENTER': '\r',
            'UP': '\x1b[A',
            'DOWN': '\x1b[B',
            'LEFT': '\x1b[D',
            'RIGHT': '\x1b[C',
            'CTRL_X': '\x18',
            'CTRL_C': '\x03',
            'CTRL_G': '\x07',
            'META_X': '\x1bx',  # ESC x for M-x
        }
        self.send(key_map.get(key, key))

    def _read_output(self, timeout=0.01):
        """Read and parse terminal output through pyte.

        Args:
            timeout: Read timeout in seconds. Use smaller values (0.01) for
                     frequent polling, larger values (0.1) when waiting for
                     specific output.
        """
        try:
            while True:
                data = self.child.read_nonblocking(size=16384, timeout=timeout)
                if self.debug:
                    print(f"Read {len(data)} bytes")
                self.stream.feed(data)
        except pexpect.TIMEOUT:
            pass
        except pexpect.EOF:
            pass

    def get_line(self, row):
        """Get text content of a row."""
        return self.screen.display[row].rstrip()

    def get_cursor_pos(self):
        """Get current cursor position (row, col)."""
        return (self.screen.cursor.y, self.screen.cursor.x)

    def get_cursor_row(self):
        """Get current cursor row."""
        return self.screen.cursor.y

    def has_highlight_at_row(self, row):
        """Check if row has cursor-line highlight (gray background)."""
        highlight_colors = ['7f7f7f', '808080', '8']
        for col in range(self.cols):
            char = self.screen.buffer[row][col]
            if char.bg in highlight_colors:
                return True
        return False

    def get_editing_cursor_row(self):
        """Get the actual editing cursor row by finding highlighted row.

        This is more reliable than get_cursor_pos() when a message is
        being displayed, since mlwrite() moves terminal cursor to message line.
        """
        highlight_colors = ['7f7f7f', '808080', '8']
        for row in range(self.rows - 2):  # Skip status and message lines
            for col in range(self.cols):
                char = self.screen.buffer[row][col]
                if char.bg in highlight_colors:
                    return row
        # Fallback to terminal cursor
        return self.screen.cursor.y

    def get_row_bg_colors(self, row):
        """Get list of background colors for a row (for debugging)."""
        colors = []
        for col in range(self.cols):
            char = self.screen.buffer[row][col]
            if char.bg != 'default' and char.bg not in colors:
                colors.append(char.bg)
        return colors

    def get_message_line(self):
        """Get the message/status line content (last row)."""
        return self.get_line(self.rows - 1)

    def get_screen_text(self):
        """Get all visible text content as a single string."""
        return '\n'.join(self.screen.display[:self.rows - 2])

    def dump_screen(self):
        """Print screen contents for debugging."""
        print("=" * 40)
        print(f"Cursor at: row={self.screen.cursor.y}, col={self.screen.cursor.x}")
        print("=" * 40)
        for i, line in enumerate(self.screen.display):
            marker = ">>>" if i == self.screen.cursor.y else "   "
            bg_colors = self.get_row_bg_colors(i)
            bg_info = f" [bg: {bg_colors}]" if bg_colors else ""
            print(f"{marker} {i:2d}: {line[:60]}{bg_info}")
        print("=" * 40)

    def dump_screen_with_attrs(self):
        """Print screen with all pyte attributes for debugging."""
        print("=" * 60)
        print(f"Cursor: row={self.screen.cursor.y}, col={self.screen.cursor.x}")
        print("=" * 60)
        for row in range(self.rows):
            line = self.screen.display[row]
            marker = ">>>" if row == self.screen.cursor.y else "   "

            # Collect unique attributes on this row
            attrs = set()
            for col in range(min(len(line), self.cols)):
                char = self.screen.buffer[row][col]
                if char.bg != 'default':
                    attrs.add(f"bg:{char.bg}")
                if char.fg != 'default':
                    attrs.add(f"fg:{char.fg}")
                if char.reverse:
                    attrs.add("rev")
                if char.bold:
                    attrs.add("bold")

            attr_str = f" [{', '.join(sorted(attrs))}]" if attrs else ""
            print(f"{marker} {row:2d}: {line[:50]}{attr_str}")
        print("=" * 60)

    def get_cell_attributes(self, row, col):
        """Get full pyte.Char attributes for a cell.

        Returns:
            dict with keys: data, fg, bg, bold, reverse, italics, underscore, strikethrough, blink
        """
        if row < 0 or row >= self.rows or col < 0 or col >= self.cols:
            return None
        char = self.screen.buffer[row][col]
        return {
            'data': char.data,
            'fg': char.fg,
            'bg': char.bg,
            'bold': char.bold,
            'reverse': char.reverse,
            'italics': char.italics,
            'underscore': char.underscore,
            'strikethrough': char.strikethrough,
            'blink': char.blink,
        }

    def check_all_corruption(self):
        """Exhaustive corruption detection - catches ALL rendering issues.

        Detects:
        1. Raw ESC (0x1B) visible in text
        2. 0xFF corruption (from EOF cast bugs)
        3. CSI fragment patterns ([0-9, [?, [<, [>, [=)
        4. C0 control characters (0x00-0x1F except tab)
        5. C1 control characters (0x80-0x9F)
        6. Partial/malformed escape sequences

        Returns:
            Tuple of (has_corruption, list_of_issues)
        """
        issues = []

        for row in range(self.rows):
            line = self.screen.display[row]

            # 1. Raw escape character (0x1B) visible
            if '\x1b' in line:
                pos = line.index('\x1b')
                issues.append(f"Row {row}, Col {pos}: Raw ESC (0x1B) visible")

            # 2. 0xFF corruption (from EOF bug - (char)(-1) = 0xFF)
            if '\xff' in line:
                pos = line.index('\xff')
                issues.append(f"Row {row}, Col {pos}: 0xFF corruption (EOF bug)")

            # 3. CSI fragment patterns at suspicious positions
            # Look for patterns like [1;, [?, [<, etc. that indicate partial CSI
            csi_fragments = re.findall(r'\[[\d;?<>=]*[A-Za-z]?', line[:30])
            for frag in csi_fragments:
                # Exclude legitimate content like [1], [2], etc.
                if not re.match(r'\[\d+\]', frag):
                    # Also exclude common text patterns
                    if len(frag) > 2 or frag in ['[?', '[<', '[>', '[=']:
                        issues.append(f"Row {row}: Possible CSI fragment: {repr(frag)}")

            # 4. C0 control characters (0x00-0x1F) except tab (0x09)
            for col, c in enumerate(line):
                code = ord(c)
                if code < 0x20 and code != 0x09:  # Allow tab
                    issues.append(f"Row {row}, Col {col}: C0 control 0x{code:02x}")

            # 5. C1 control characters (0x80-0x9F) - never valid in display
            for col, c in enumerate(line):
                code = ord(c)
                if 0x80 <= code <= 0x9F:
                    issues.append(f"Row {row}, Col {col}: C1 control 0x{code:02x}")

            # 6. Check for sequences that look like partial SGR (e.g., "m" after digits)
            if re.search(r'\d+m', line[:20]):
                match = re.search(r'\d+m', line[:20])
                issues.append(f"Row {row}: Possible SGR fragment: {repr(match.group())}")

        return (len(issues) > 0, issues)

    def verify_highlight_correct(self, expected_row=None):
        """Verify highlight is on exactly one row and matches cursor.

        This is the key test for "highlight sticks to wrong row" bug.

        Args:
            expected_row: If provided, verify highlight is on this specific row

        Returns:
            Tuple of (has_issues, list_of_issues)
        """
        issues = []
        highlighted_rows = []

        # Check content rows (skip status and message lines)
        for row in range(self.rows - 2):
            if self.has_highlight_at_row(row):
                highlighted_rows.append(row)

        # Issue 1: No highlight when expected
        if len(highlighted_rows) == 0:
            issues.append("No highlighted row found")
            return (True, issues)

        # Issue 2: Multiple rows highlighted (the "bleed" bug)
        if len(highlighted_rows) > 1:
            issues.append(f"Multiple rows highlighted: {highlighted_rows}")

        # Issue 3: Highlight doesn't match cursor position
        # Note: When a message is displayed, cursor goes to message line.
        # So we use get_editing_cursor_row() which finds the highlighted row.
        cursor_row = self.screen.cursor.y

        # If cursor is in content area (not on status/message line)
        if cursor_row < self.rows - 2:
            if len(highlighted_rows) == 1:
                if highlighted_rows[0] != cursor_row:
                    issues.append(
                        f"Highlight on row {highlighted_rows[0]} but cursor on row {cursor_row}"
                    )

        # Issue 4: If expected_row provided, verify it matches
        if expected_row is not None and len(highlighted_rows) >= 1:
            if highlighted_rows[0] != expected_row:
                issues.append(
                    f"Expected highlight on row {expected_row}, got {highlighted_rows[0]}"
                )

        return (len(issues) > 0, issues)

    def verify_no_ghost_highlights(self):
        """Check for "ghost" highlights - stale highlight remnants.

        Looks for rows that have partial highlighting (some cells highlighted,
        others not) which indicates incomplete screen updates.

        Returns:
            Tuple of (has_ghosts, list_of_issues)
        """
        issues = []
        highlight_colors = ['7f7f7f', '808080', '8']

        for row in range(self.rows - 2):
            highlighted_cols = []
            for col in range(self.cols):
                char = self.screen.buffer[row][col]
                if char.bg in highlight_colors:
                    highlighted_cols.append(col)

            # A properly highlighted row should have continuous highlighting
            # from col 0 to some point (or entire row)
            if highlighted_cols:
                # Check for gaps in highlighting
                if highlighted_cols[0] > 5:  # Starts too late (allow small margin)
                    issues.append(
                        f"Row {row}: Highlight starts at col {highlighted_cols[0]}, not 0"
                    )

                # Check for non-contiguous highlighting
                for i in range(1, len(highlighted_cols)):
                    if highlighted_cols[i] != highlighted_cols[i-1] + 1:
                        issues.append(
                            f"Row {row}: Gap in highlight at col {highlighted_cols[i-1]+1}"
                        )
                        break

        return (len(issues) > 0, issues)

    def full_integrity_check(self):
        """Run all integrity checks - the ultimate test.

        Returns:
            Tuple of (all_ok, dict_of_issues)
        """
        all_issues = {}

        corrupt, c_issues = self.check_all_corruption()
        if c_issues:
            all_issues['corruption'] = c_issues

        highlight_bad, h_issues = self.verify_highlight_correct()
        if h_issues:
            all_issues['highlight'] = h_issues

        ghosts, g_issues = self.verify_no_ghost_highlights()
        if g_issues:
            all_issues['ghost_highlights'] = g_issues

        return (len(all_issues) == 0, all_issues)

    def quit(self):
        """Exit μEmacs cleanly."""
        # Try vim-style quit first
        self.send_key('ESC')
        time.sleep(0.1)
        self.send(':q!')
        self.send_key('ENTER')
        time.sleep(0.2)

        # If still alive, force kill
        if self.child.isalive():
            self.child.terminate(force=True)
        self.child.close()
