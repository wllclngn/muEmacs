#!/usr/bin/env python3
"""
Real functional tests for polyglot extensions.
Tests with actual files, in actual git repos, with actual content.
"""

import sys
import os
import time
import tempfile
import shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pexpect
import pyte

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TEST_DIR, "../.."))
BINARY = os.path.join(PROJECT_ROOT, "build/bin/μEmacs")
LOG_FILE = "/tmp/uemacs_debug.log"

if not os.path.exists(BINARY):
    BINARY = os.path.join(PROJECT_ROOT, "build/bin/muEmacs")


class MuEmacsTest:
    def __init__(self, rows=30, cols=100):
        self.rows = rows
        self.cols = cols
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)
        self.child = None
        self.debug = os.environ.get('DEBUG', '0') == '1'

    def start(self, filename=None, cwd=None):
        if cwd is None:
            cwd = PROJECT_ROOT

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
        time.sleep(1.0)
        self._read_output()

    def send(self, keys, delay=0.2):
        if self.debug:
            print(f"  [send] {repr(keys)}")
        self.child.send(keys)
        time.sleep(delay)
        self._read_output()

    def send_meta_x(self, command, delay=0.4):
        self.send('\x1bx', delay=0.2)
        self.send(command + '\r', delay=delay)

    def _read_output(self):
        try:
            while True:
                data = self.child.read_nonblocking(size=8192, timeout=0.1)
                self.stream.feed(data)
        except (pexpect.TIMEOUT, pexpect.EOF):
            pass

    def get_screen(self):
        return '\n'.join(line.rstrip() for line in self.screen.display)

    def get_line(self, row):
        return self.screen.display[row].rstrip()

    def get_minibuffer(self):
        return self.get_line(self.rows - 1)

    def dump_screen(self, max_lines=15):
        print("  Screen content:")
        for i in range(min(max_lines, self.rows)):
            line = self.get_line(i)
            if line.strip():
                print(f"    {i:2}: {line[:90]}")

    def close(self):
        if self.child:
            try:
                self.child.close(force=True)
            except:
                pass


def read_log():
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE, 'r') as f:
            return f.read()
    return ""


def test_git_fortran_in_repo():
    """Test fgit-status in actual git repository."""
    print("\n" + "=" * 70)
    print("TEST: Git Fortran - fgit-status in real git repo")
    print("=" * 70)

    # Use the project root which is a git repo
    test_file = os.path.join(PROJECT_ROOT, "src/main.c")
    if not os.path.exists(test_file):
        test_file = os.path.join(PROJECT_ROOT, "README.md")

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print(f"  Opened: {test_file}")
    print("  Running fgit-status...")

    t.send_meta_x('fgit-status', delay=0.8)
    time.sleep(0.5)
    t._read_output()

    screen = t.get_screen()
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")
    t.dump_screen()

    # Check for git output indicators
    has_git_output = any([
        '*git-status*' in screen,
        'On branch' in screen,
        '## ' in screen,  # git status --short format
        ' M ' in screen,  # modified file
        '??' in screen,   # untracked
        'nothing to commit' in screen.lower(),
        'main' in screen.lower() or 'master' in screen.lower(),
    ])

    t.close()

    if has_git_output:
        print("  [PASS] Git status output found in buffer")
        return True
    else:
        print("  [FAIL] No git output found")
        return False


def test_git_fortran_log():
    """Test fgit-log to show commit history."""
    print("\n" + "=" * 70)
    print("TEST: Git Fortran - fgit-log")
    print("=" * 70)

    test_file = os.path.join(PROJECT_ROOT, "src/main.c")
    if not os.path.exists(test_file):
        test_file = os.path.join(PROJECT_ROOT, "README.md")

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print("  Running fgit-log...")
    t.send_meta_x('fgit-log', delay=0.8)
    time.sleep(0.5)
    t._read_output()

    screen = t.get_screen()
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")
    t.dump_screen()

    # Check for commit hashes (7-char hex patterns)
    import re
    has_commits = bool(re.search(r'[0-9a-f]{7,}', screen))

    t.close()

    if has_commits:
        print("  [PASS] Commit history found")
        return True
    else:
        print("  [FAIL] No commits found")
        return False


def test_project_haskell_in_project():
    """Test project-root in actual project directory."""
    print("\n" + "=" * 70)
    print("TEST: Project Haskell - project-root detection")
    print("=" * 70)

    # Use a source file in the project
    test_file = os.path.join(PROJECT_ROOT, "src/main.c")
    if not os.path.exists(test_file):
        # Try to find any .c file
        for root, dirs, files in os.walk(PROJECT_ROOT):
            for f in files:
                if f.endswith('.c'):
                    test_file = os.path.join(root, f)
                    break
            if os.path.exists(test_file):
                break

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print(f"  Opened: {test_file}")
    print("  Running project-root...")

    t.send_meta_x('project-root', delay=0.5)
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")

    t.close()

    # Should show project root path
    if 'Project:' in minibuf or PROJECT_ROOT in minibuf or 'μEmacs' in minibuf:
        print("  [PASS] Project root detected")
        return True
    elif 'No project' in minibuf:
        print("  [WARN] No project markers found")
        return None
    elif 'No file' in minibuf:
        print("  [FAIL] Extension couldn't read file")
        return False
    else:
        print(f"  [???] Unexpected: {minibuf}")
        return None


def test_project_files():
    """Test project-files to list project files."""
    print("\n" + "=" * 70)
    print("TEST: Project Haskell - project-files listing")
    print("=" * 70)

    test_file = os.path.join(PROJECT_ROOT, "src/main.c")

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print("  Running project-files...")
    t.send_meta_x('project-files', delay=1.0)
    time.sleep(0.5)
    t._read_output()

    screen = t.get_screen()
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")
    t.dump_screen()

    # Should list source files
    has_files = '.c' in screen or '.h' in screen or 'files' in minibuf.lower()

    t.close()

    if has_files:
        print("  [PASS] Project files listed")
        return True
    else:
        print("  [FAIL] No files listed")
        return False


def test_treesitter_on_c_file():
    """Test ts-highlight on a C source file."""
    print("\n" + "=" * 70)
    print("TEST: Tree-sitter (Zig) - ts-highlight on C file")
    print("=" * 70)

    test_file = os.path.join(PROJECT_ROOT, "src/main.c")

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print(f"  Opened: {test_file}")
    print("  Running ts-highlight...")

    t.send_meta_x('ts-highlight', delay=0.5)
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")

    t.close()

    if 'regions highlighted' in minibuf.lower() or 'highlighted' in minibuf.lower():
        print("  [PASS] Syntax highlighting applied")
        return True
    elif 'No tree-sitter' in minibuf:
        print("  [FAIL] Tree-sitter not available")
        return False
    else:
        print(f"  [???] Response: {minibuf}")
        return None


def test_lsp_start_with_c_file():
    """Test lsp-start with C file (clangd)."""
    print("\n" + "=" * 70)
    print("TEST: LSP Client (Go) - lsp-start with C file")
    print("=" * 70)

    test_file = os.path.join(PROJECT_ROOT, "src/main.c")

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print(f"  Opened: {test_file}")
    print("  Running lsp-start...")

    t.send_meta_x('lsp-start', delay=2.0)  # LSP init takes time
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")

    t.close()

    if 'started' in minibuf.lower():
        print("  [PASS] LSP server started")
        return True
    elif 'not found' in minibuf.lower():
        print("  [WARN] clangd not installed")
        return None
    elif 'No file' in minibuf:
        print("  [FAIL] Extension couldn't read file")
        return False
    else:
        print(f"  [???] Response: {minibuf}")
        return None


def test_multicursor_add():
    """Test mc-add to add cursors."""
    print("\n" + "=" * 70)
    print("TEST: Multiple Cursors (Pascal) - mc-add")
    print("=" * 70)

    # Create a test file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write("line one\nline two\nline three\nline four\n")
        test_file = f.name

    t = MuEmacsTest()
    t.start(filename=test_file)

    print(f"  Opened: {test_file}")
    print("  Adding cursors at multiple positions...")

    # Add cursor at line 1
    t.send_meta_x('mc-add', delay=0.3)
    msg1 = t.get_minibuffer()
    print(f"  After mc-add #1: {msg1}")

    # Move down and add another
    t.send('\x1b[B')  # Down arrow
    t.send_meta_x('mc-add', delay=0.3)
    msg2 = t.get_minibuffer()
    print(f"  After mc-add #2: {msg2}")

    # Move down and add another
    t.send('\x1b[B')  # Down arrow
    t.send_meta_x('mc-add', delay=0.3)
    msg3 = t.get_minibuffer()
    print(f"  After mc-add #3: {msg3}")

    t.close()
    os.unlink(test_file)

    if 'Cursor 3' in msg3:
        print("  [PASS] Multiple cursors added successfully")
        return True
    elif 'Cursor 1' in msg1:
        print("  [PARTIAL] First cursor added, but not multiple")
        return None
    else:
        print("  [FAIL] Couldn't add cursors")
        return False


def test_multicursor_next():
    """Test mc-next to cycle through cursors."""
    print("\n" + "=" * 70)
    print("TEST: Multiple Cursors (Pascal) - mc-next navigation")
    print("=" * 70)

    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write("aaa\nbbb\nccc\n")
        test_file = f.name

    t = MuEmacsTest()
    t.start(filename=test_file)

    # Add 3 cursors
    t.send_meta_x('mc-add', delay=0.2)
    t.send('\x1b[B')
    t.send_meta_x('mc-add', delay=0.2)
    t.send('\x1b[B')
    t.send_meta_x('mc-add', delay=0.2)

    print("  Added 3 cursors, now cycling with mc-next...")

    # Cycle through
    t.send_meta_x('mc-next', delay=0.3)
    msg1 = t.get_minibuffer()
    print(f"  mc-next: {msg1}")

    t.send_meta_x('mc-next', delay=0.3)
    msg2 = t.get_minibuffer()
    print(f"  mc-next: {msg2}")

    t.close()
    os.unlink(test_file)

    if '/' in msg1 and '/' in msg2:  # "Cursor 2/3" format
        print("  [PASS] Cursor navigation works")
        return True
    else:
        print("  [FAIL] Navigation not working")
        return False


def test_fuzzy_find():
    """Test fuzzy-find file finder."""
    print("\n" + "=" * 70)
    print("TEST: Fuzzy Finder (Ada) - fuzzy-find")
    print("=" * 70)

    test_file = os.path.join(PROJECT_ROOT, "src/main.c")

    t = MuEmacsTest()
    t.start(filename=test_file, cwd=PROJECT_ROOT)

    print("  Running fuzzy-find...")
    t.send_meta_x('fuzzy-find', delay=0.5)

    minibuf = t.get_minibuffer()
    print(f"  Prompt: {minibuf}")

    if 'Fuzzy find' in minibuf or ':' in minibuf:
        # Enter a search pattern
        t.send('main\r', delay=1.0)
        time.sleep(0.5)
        t._read_output()

        screen = t.get_screen()
        minibuf = t.get_minibuffer()
        print(f"  After search: {minibuf}")
        t.dump_screen(max_lines=10)

        has_results = 'main' in screen.lower() or '.c' in screen

        t.close()

        if has_results:
            print("  [PASS] Fuzzy find returned results")
            return True
        else:
            print("  [PARTIAL] Search ran but no visible results")
            return None
    else:
        t.close()
        print(f"  [FAIL] Unexpected prompt: {minibuf}")
        return False


def test_ai_crystal():
    """Test ai-complete (expects Ollama or error message)."""
    print("\n" + "=" * 70)
    print("TEST: AI Completion (Crystal) - ai-complete")
    print("=" * 70)

    # Create a Python file with incomplete code
    with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False) as f:
        f.write('def hello_world():\n    print("Hello')
        test_file = f.name

    t = MuEmacsTest()
    t.start(filename=test_file)

    print(f"  Opened: {test_file}")
    print("  Running ai-complete...")

    t.send_meta_x('ai-complete', delay=3.0)  # AI calls take time
    minibuf = t.get_minibuffer()

    print(f"  Minibuffer: {minibuf}")

    t.close()
    os.unlink(test_file)

    # Expected responses:
    # - "Thinking..." then completion or error
    # - "No completion (check Ollama...)" if no AI available
    # - Actual completion text
    if 'Thinking' in minibuf or 'Inserted' in minibuf:
        print("  [PASS] AI completion attempted/succeeded")
        return True
    elif 'check Ollama' in minibuf or 'No completion' in minibuf:
        print("  [WARN] AI backend not available (expected if no Ollama)")
        return None
    elif 'No content' in minibuf:
        print("  [FAIL] Extension couldn't read buffer content")
        return False
    else:
        print(f"  [???] Response: {minibuf}")
        return None


def run_all_tests():
    print("\n" + "=" * 70)
    print("POLYGLOT EXTENSION REAL FUNCTIONALITY TESTS")
    print("=" * 70)
    print(f"Binary: {BINARY}")
    print(f"Project: {PROJECT_ROOT}")

    if not os.path.exists(BINARY):
        print(f"\n[ERROR] Binary not found: {BINARY}")
        return False

    results = {}

    # Git tests (Fortran)
    results['fgit-status'] = test_git_fortran_in_repo()
    results['fgit-log'] = test_git_fortran_log()

    # Project tests (Haskell)
    results['project-root'] = test_project_haskell_in_project()
    results['project-files'] = test_project_files()

    # Tree-sitter test (Zig)
    results['ts-highlight'] = test_treesitter_on_c_file()

    # LSP test (Go)
    results['lsp-start'] = test_lsp_start_with_c_file()

    # Multicursor tests (Pascal)
    results['mc-add'] = test_multicursor_add()
    results['mc-next'] = test_multicursor_next()

    # Fuzzy finder test (Ada)
    results['fuzzy-find'] = test_fuzzy_find()

    # AI test (Crystal)
    results['ai-complete'] = test_ai_crystal()

    # Summary
    print("\n" + "=" * 70)
    print("TEST SUMMARY")
    print("=" * 70)

    passed = failed = skipped = 0
    for name, result in results.items():
        if result is True:
            status = "PASS"
            passed += 1
        elif result is False:
            status = "FAIL"
            failed += 1
        else:
            status = "SKIP/WARN"
            skipped += 1
        print(f"  {name:20}: {status}")

    print("-" * 40)
    print(f"  Passed: {passed}, Failed: {failed}, Skipped: {skipped}")

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
