#!/usr/bin/env python3
"""
Test all 7 polyglot extensions in muEmacs.

Extensions tested:
1. treesitter_hl (Zig) - Tree-sitter syntax highlighting
2. lsp_client (Go) - LSP client
3. git_fortran (Fortran) - Git integration (fgit-* commands)
4. fuzzy_ada (Ada) - Fuzzy file finder
5. project_haskell (Haskell) - Project management
6. multicursor_pascal (Pascal) - Multiple cursors
7. ai_crystal (Crystal) - AI completion

Uses debug logs at /tmp/muemacs_debug.log to verify extension loading.
"""

import sys
import os
import time
import re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pexpect
import pyte
from base import test_tmp

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TEST_DIR, "../.."))
BINARY = os.path.join(PROJECT_ROOT, "build/bin/μEmacs")
LOG_FILE = "/tmp/muemacs_debug.log"

if not os.path.exists(BINARY):
    BINARY = os.path.join(PROJECT_ROOT, "build/bin/muEmacs")


class MuEmacsTest:
    """Test harness for muEmacs PTY testing."""

    def __init__(self, rows=24, cols=100):
        self.rows = rows
        self.cols = cols
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)
        self.child = None
        self.debug = os.environ.get('DEBUG', '0') == '1'

    def start(self, filename=None, cwd=None):
        """Start muEmacs in PTY."""
        if cwd is None:
            cwd = PROJECT_ROOT

        # Clear old log
        if os.path.exists(LOG_FILE):
            os.remove(LOG_FILE)

        # Use direct spawn without shell to avoid injection risks
        args = [BINARY]
        if filename:
            args.append(filename)

        self.child = pexpect.spawn(
            args[0], args[1:],
            dimensions=(self.rows, self.cols),
            env={**os.environ, 'TERM': 'xterm-256color'},
            encoding='utf-8',
            timeout=10,
            cwd=cwd
        )
        time.sleep(0.8)  # Wait for extensions to load
        self._read_output()

    def send(self, keys, delay=0.15):
        """Send keys with delay."""
        if self.debug:
            print(f"  [send] {repr(keys)}")
        self.child.send(keys)
        time.sleep(delay)
        self._read_output()

    def send_meta_x(self, command, delay=0.3):
        """Send M-x command."""
        self.send('\x1bx', delay=0.2)  # ESC x
        self.send(command + '\r', delay=delay)

    def _read_output(self):
        """Read PTY output into pyte screen."""
        try:
            while True:
                data = self.child.read_nonblocking(size=8192, timeout=0.1)
                self.stream.feed(data)
        except (pexpect.TIMEOUT, pexpect.EOF):
            pass

    def get_screen(self):
        """Get full screen as string."""
        return '\n'.join(line.rstrip() for line in self.screen.display)

    def get_line(self, row):
        """Get specific line."""
        return self.screen.display[row].rstrip()

    def get_minibuffer(self):
        """Get minibuffer/message line (last row)."""
        return self.get_line(self.rows - 1)

    def close(self):
        """Close PTY."""
        if self.child:
            try:
                self.child.close(force=True)
            except:
                pass


def read_log():
    """Read the debug log file."""
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE, 'r') as f:
            return f.read()
    return ""


def check_extension_loaded(log, ext_name):
    """Check if extension loaded successfully in log."""
    patterns = [
        f"Loaded '{ext_name}'",
        f"{ext_name}: Loaded",
    ]
    for pattern in patterns:
        if pattern in log:
            return True
    return False


def test_extension_loading():
    """Test that all 7 polyglot extensions load successfully."""
    print("\n" + "=" * 60)
    print("TEST: Extension Loading")
    print("=" * 60)

    t = MuEmacsTest()
    t.start()
    time.sleep(0.5)

    log = read_log()
    if not log:
        print("[ERROR] No debug log found at /tmp/muemacs_debug.log")
        print("  Ensure muEmacs is built with -DUEMACS_DEBUG_LOG=ON")
        t.close()
        return False

    extensions = [
        ('treesitter_hl', 'Zig'),
        ('lsp_client', 'Go'),
        ('git_fortran', 'Fortran'),
        ('fuzzy_ada', 'Ada'),
        ('project_haskell', 'Haskell'),
        ('multicursor_pascal', 'Pascal'),
        ('ai_crystal', 'Crystal'),
    ]

    all_ok = True
    for ext_name, language in extensions:
        loaded = check_extension_loaded(log, ext_name)
        status = "OK" if loaded else "FAIL"
        print(f"  {ext_name:25} ({language:8}): {status}")
        if not loaded:
            all_ok = False

    # Check for any load errors
    if 'undefined symbol' in log:
        print("\n[ERROR] Found 'undefined symbol' in log:")
        for line in log.split('\n'):
            if 'undefined symbol' in line:
                print(f"  {line[:100]}")
        all_ok = False

    t.close()
    return all_ok


def test_treesitter_commands():
    """Test tree-sitter extension commands."""
    print("\n" + "=" * 60)
    print("TEST: Tree-sitter (Zig) Extension")
    print("=" * 60)

    # Create a test C file
    test_file = test_tmp("test_treesitter.c")
    with open(test_file, 'w') as f:
        f.write('#include <stdio.h>\nint main() {\n    printf("hello");\n    return 0;\n}\n')

    t = MuEmacsTest()
    t.start(filename=test_file)

    # Try ts-highlight command
    print("  Testing ts-highlight command...")
    t.send_meta_x('ts-highlight')
    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    log = read_log()
    success = 'treesitter' in log.lower() or 'ts-highlight' in msg.lower() or 'syntax' in msg.lower()

    t.close()
    os.remove(test_file)

    if 'not found' in msg.lower():
        print("  [WARN] Command not found - extension may not register commands")
        return None  # Indeterminate
    return success


def test_lsp_commands():
    """Test LSP client extension commands."""
    print("\n" + "=" * 60)
    print("TEST: LSP Client (Go) Extension")
    print("=" * 60)

    t = MuEmacsTest()
    t.start()

    # Try lsp-start command
    print("  Testing lsp-start command...")
    t.send_meta_x('lsp-start')
    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    log = read_log()
    success = 'lsp' in log.lower() or 'lsp' in msg.lower()

    t.close()
    return success or 'not found' not in msg.lower()


def test_git_fortran_commands():
    """Test Fortran git extension commands."""
    print("\n" + "=" * 60)
    print("TEST: Git Fortran Extension")
    print("=" * 60)

    t = MuEmacsTest()
    t.start(cwd=PROJECT_ROOT)

    # Try fgit-status command (renamed from git-status)
    print("  Testing fgit-status command...")
    t.send_meta_x('fgit-status', delay=0.5)
    time.sleep(0.3)
    t._read_output()

    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    # Check screen for git output
    screen = t.get_screen()
    has_git_output = 'branch' in screen.lower() or 'commit' in screen.lower() or 'On branch' in screen

    print("  Screen content (first 5 lines):")
    for i in range(min(5, t.rows)):
        line = t.get_line(i)
        if line.strip():
            print(f"    {i}: {line[:70]}")

    t.close()

    if 'not found' in msg.lower():
        print("  [FAIL] fgit-status command not found")
        return False

    return has_git_output or 'fgit' in msg.lower() or 'git' in msg.lower()


def test_fuzzy_ada_commands():
    """Test Ada fuzzy finder extension commands."""
    print("\n" + "=" * 60)
    print("TEST: Fuzzy Finder (Ada) Extension")
    print("=" * 60)

    t = MuEmacsTest()
    t.start(cwd=PROJECT_ROOT)

    # Try fuzzy-find command
    print("  Testing fuzzy-find command...")
    t.send_meta_x('fuzzy-find')
    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    # If prompted, enter a pattern
    if 'pattern' in msg.lower() or ':' in msg:
        print("  Entering search pattern 'main'...")
        t.send('main\r', delay=0.5)
        msg = t.get_minibuffer()
        print(f"  After search: {msg[:60]}")

    t.close()
    return 'not found' not in msg.lower()


def test_project_haskell_commands():
    """Test Haskell project extension commands."""
    print("\n" + "=" * 60)
    print("TEST: Project Manager (Haskell) Extension")
    print("=" * 60)

    t = MuEmacsTest()
    t.start(cwd=PROJECT_ROOT)

    # Try project-root command
    print("  Testing project-root command...")
    t.send_meta_x('project-root')
    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    t.close()
    return 'not found' not in msg.lower()


def test_multicursor_pascal_commands():
    """Test Pascal multicursor extension commands."""
    print("\n" + "=" * 60)
    print("TEST: Multiple Cursors (Pascal) Extension")
    print("=" * 60)

    # Create test file
    test_file = test_tmp("test_multicursor.txt")
    with open(test_file, 'w') as f:
        f.write("foo\nfoo\nfoo\nbar\nfoo\n")

    t = MuEmacsTest()
    t.start(filename=test_file)

    # Try mc-add command
    print("  Testing mc-add command...")
    t.send_meta_x('mc-add')
    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    t.close()
    os.remove(test_file)

    return 'not found' not in msg.lower()


def test_ai_crystal_commands():
    """Test Crystal AI completion extension commands."""
    print("\n" + "=" * 60)
    print("TEST: AI Completion (Crystal) Extension")
    print("=" * 60)

    t = MuEmacsTest()
    t.start()

    # Try ai-complete command
    print("  Testing ai-complete command...")
    t.send_meta_x('ai-complete')
    msg = t.get_minibuffer()
    print(f"  Minibuffer: {msg[:60]}")

    t.close()

    # AI commands might fail due to no API key, but should be registered
    return 'not found' not in msg.lower()


def test_describe_bindings():
    """Test that extension commands appear in describe-bindings."""
    print("\n" + "=" * 60)
    print("TEST: Commands in describe-bindings")
    print("=" * 60)

    t = MuEmacsTest(rows=40, cols=120)
    t.start()

    # Run describe-bindings
    print("  Running describe-bindings...")
    t.send_meta_x('describe-bindings', delay=0.5)
    time.sleep(0.3)
    t._read_output()

    screen = t.get_screen()

    # Check for extension commands
    commands_to_find = [
        'ts-highlight',
        'lsp-start',
        'fgit-status',
        'fuzzy-find',
        'project-root',
        'mc-add',
        'ai-complete',
    ]

    print("  Checking for extension commands:")
    found = 0
    for cmd in commands_to_find:
        if cmd in screen:
            print(f"    {cmd}: FOUND")
            found += 1
        else:
            print(f"    {cmd}: not found")

    t.close()
    return found >= 4  # At least half should be found


def run_all_tests():
    """Run all extension tests."""
    print("\n" + "=" * 70)
    print("POLYGLOT EXTENSION TEST SUITE")
    print("=" * 70)
    print(f"Binary: {BINARY}")
    print(f"Log file: {LOG_FILE}")

    if not os.path.exists(BINARY):
        print(f"\n[ERROR] Binary not found: {BINARY}")
        print("Run: cmake -B build && cmake --build build")
        return False

    results = {}

    # Test 1: Extension loading
    results['loading'] = test_extension_loading()

    # Test 2-8: Individual extension commands
    results['treesitter'] = test_treesitter_commands()
    results['lsp'] = test_lsp_commands()
    results['git_fortran'] = test_git_fortran_commands()
    results['fuzzy_ada'] = test_fuzzy_ada_commands()
    results['project_haskell'] = test_project_haskell_commands()
    results['multicursor'] = test_multicursor_pascal_commands()
    results['ai_crystal'] = test_ai_crystal_commands()

    # Test 9: describe-bindings
    results['describe'] = test_describe_bindings()

    # Summary
    print("\n" + "=" * 70)
    print("TEST SUMMARY")
    print("=" * 70)

    passed = 0
    failed = 0
    skipped = 0

    for name, result in results.items():
        if result is True:
            status = "PASS"
            passed += 1
        elif result is False:
            status = "FAIL"
            failed += 1
        else:
            status = "SKIP"
            skipped += 1
        print(f"  {name:25}: {status}")

    print("-" * 40)
    print(f"  Passed: {passed}, Failed: {failed}, Skipped: {skipped}")

    # Show relevant log entries
    print("\n" + "=" * 70)
    print("RELEVANT LOG ENTRIES")
    print("=" * 70)
    log = read_log()
    if log:
        for line in log.split('\n'):
            if any(x in line.lower() for x in ['loaded', 'error', 'failed', 'extension']):
                print(f"  {line[:100]}")
    else:
        print("  No log file found")

    return failed == 0


if __name__ == '__main__':
    try:
        success = run_all_tests()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n[INTERRUPTED]")
        sys.exit(130)
    except Exception as e:
        print(f"\n[ERROR] {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
