#!/usr/bin/env python3
"""
Test rg-search extension in muEmacs via PTY.
Find "Any port in a storm..." and open the file.
"""

import sys
import os
import time

# Add parent to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pexpect
import pyte

# Derive paths relative to this test file
TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TEST_DIR, "../.."))
BINARY = os.path.join(PROJECT_ROOT, "build/bin/μEmacs")

# Fallback to ASCII name if μEmacs doesn't exist
if not os.path.exists(BINARY):
    BINARY = os.path.join(PROJECT_ROOT, "build/bin/muEmacs")


class MuEmacsTest:
    def __init__(self, rows=24, cols=100):
        self.rows = rows
        self.cols = cols
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)
        self.child = None

    def start(self, cwd=None):
        if cwd is None:
            cwd = PROJECT_ROOT
        # Use command list to handle paths with spaces
        self.child = pexpect.spawn(
            command=BINARY,
            args=[],
            dimensions=(self.rows, self.cols),
            env={**os.environ, 'TERM': 'xterm-256color'},
            encoding='utf-8',
            timeout=10,
            cwd=cwd
        )
        time.sleep(0.5)
        self._read_output()

    def send(self, keys, delay=0.2):
        self.child.send(keys)
        time.sleep(delay)
        self._read_output()

    def _read_output(self):
        try:
            while True:
                data = self.child.read_nonblocking(size=4096, timeout=0.1)
                self.stream.feed(data)
        except (pexpect.TIMEOUT, pexpect.EOF):
            pass

    def get_screen(self):
        return '\n'.join(line.rstrip() for line in self.screen.display)

    def get_line(self, row):
        return self.screen.display[row].rstrip()

    def close(self):
        if self.child:
            self.child.close()


def test_rg_search():
    print("=" * 60)
    print("Testing rg-search extension in muEmacs")
    print("=" * 60)
    print(f"Binary: {BINARY}")
    print(f"Project root: {PROJECT_ROOT}")

    if not os.path.exists(BINARY):
        print(f"[ERROR] Binary not found: {BINARY}")
        print("Run: cmake -B build && cmake --build build")
        return False

    t = MuEmacsTest(rows=30, cols=120)
    t.start(cwd=PROJECT_ROOT)

    print("\n[1] muEmacs started. Screen:")
    print("-" * 40)
    for i, line in enumerate(t.screen.display[:5]):
        print(f"  {i}: {line.rstrip()[:80]}")
    print("-" * 40)

    # Invoke M-x rg-search
    print("\n[2] Sending M-x rg-search...")
    t.send('\x1bx')  # M-x (ESC + x)
    time.sleep(0.3)
    t._read_output()

    print("  Minibuffer prompt:")
    print(f"    {t.get_line(29)}")

    t.send('rg-search\r', delay=0.5)

    print("\n[3] Sent 'rg-search<Enter>'. Minibuffer:")
    print(f"    {t.get_line(29)}")

    # Enter the search pattern
    print("\n[4] Entering search pattern: 'TODO'")
    t.send('TODO\r', delay=1.0)

    print("\n[5] Results (waiting for rg to complete)...")
    time.sleep(1.0)
    t._read_output()

    # Show the results buffer
    print("\n[6] Screen after search:")
    print("-" * 60)
    for i, line in enumerate(t.screen.display):
        content = line.rstrip()
        if content:
            print(f"  {i:2}: {content[:100]}")
    print("-" * 60)

    # Clean exit
    print("\n[7] Exiting muEmacs...")
    t.send('\x18\x03')  # C-x C-c
    time.sleep(0.3)
    t._read_output()

    # May need to confirm quit
    t.send('y')
    time.sleep(0.2)

    t.close()
    print("\n[DONE] Test complete!")
    return True


if __name__ == '__main__':
    try:
        test_rg_search()
    except Exception as e:
        print(f"\n[ERROR] {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
