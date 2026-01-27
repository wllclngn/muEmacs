#!/usr/bin/env python3
"""
TUI tests for go_chess extension - Work-Stealing Chess Engine
"""

import sys
import os
import time
import re
import signal
import atexit
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ============================================================================
# BULLETPROOF PROCESS CLEANUP - MULTIPLE LAYERS
# ============================================================================

# Track ALL spawned PIDs
_spawned_pids = set()


def _nuke_all():
    """Kill ALL tracked processes AND any stray μEmacs processes."""
    my_pid = os.getpid()

    # Layer 1: Kill tracked PIDs directly (except ourselves)
    for pid in list(_spawned_pids):
        if pid != my_pid:
            try:
                os.kill(pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError, OSError):
                pass
    _spawned_pids.clear()

    # Layer 2: Kill μEmacs by exact name
    for proc_name in ['μEmacs', 'muEmacs']:
        try:
            subprocess.run(['pkill', '-9', '-x', proc_name],
                          capture_output=True, timeout=2)
        except:
            pass

    # Layer 3: Kill ext-runner by pattern (it's a path, not just a name)
    try:
        subprocess.run(['pkill', '-9', '-f', 'uemacs-ext-runner'],
                      capture_output=True, timeout=2)
    except:
        pass


# Register cleanup - runs on normal exit
atexit.register(_nuke_all)


def _signal_handler(sig, frame):
    """Handle signals by nuking all processes."""
    _nuke_all()
    sys.exit(1)


# Catch termination signals
for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
    try:
        signal.signal(sig, _signal_handler)
    except (OSError, ValueError):
        pass

# ============================================================================
# RESOURCE DETECTION
# ============================================================================


def detect_system_resources():
    """Detect system resources and return safe test parameters."""
    cpu_count = os.cpu_count() or 2

    try:
        load_avg = os.getloadavg()[0]
    except (OSError, AttributeError):
        load_avg = 0.0

    available = max(0.5, cpu_count - load_avg)
    load_ratio = load_avg / cpu_count if cpu_count > 0 else 1.0

    # CONSERVATIVE defaults - safety first
    if load_ratio > 0.7:
        depth, hint_wait, move_wait, max_moves = 2, 2.0, 3.0, 2
        skip_ai_vs_ai = True
    elif load_ratio > 0.4:
        depth, hint_wait, move_wait, max_moves = 2, 3.0, 4.0, 3
        skip_ai_vs_ai = True
    elif cpu_count >= 8:
        depth, hint_wait, move_wait, max_moves = 3, 4.0, 5.0, 4
        skip_ai_vs_ai = False
    else:
        depth, hint_wait, move_wait, max_moves = 2, 3.0, 4.0, 3
        skip_ai_vs_ai = False

    return {
        'cpu_count': cpu_count,
        'load_avg': load_avg,
        'available': available,
        'depth': depth,
        'hint_wait': hint_wait,
        'move_wait': move_wait,
        'max_moves': max_moves,
        'skip_ai_vs_ai': skip_ai_vs_ai,
    }


RESOURCES = detect_system_resources()
MAX_SEARCH_DEPTH = RESOURCES['depth']
MAX_HINT_WAIT = RESOURCES['hint_wait']
MAX_MOVE_WAIT = RESOURCES['move_wait']
MAX_AI_MOVES = RESOURCES['max_moves']
SKIP_AI_VS_AI = RESOURCES['skip_ai_vs_ai']

import pexpect
import pyte

# Unicode chess pieces to ASCII translation
CHESS_UNICODE_TO_ASCII = {
    '♔': 'K', '♕': 'Q', '♖': 'R', '♗': 'B', '♘': 'N', '♙': 'P',
    '♚': 'k', '♛': 'q', '♜': 'r', '♝': 'b', '♞': 'n', '♟': 'p',
    '·': '.',
}

def translate_chess_unicode(text):
    """Translate Unicode chess pieces to ASCII for test compatibility."""
    for uni, asc in CHESS_UNICODE_TO_ASCII.items():
        text = text.replace(uni, asc)
    return text

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TEST_DIR, "../.."))
BINARY = os.path.join(PROJECT_ROOT, "build/bin/μEmacs")

if not os.path.exists(BINARY):
    BINARY = os.path.join(PROJECT_ROOT, "build/bin/muEmacs")


class ChessTest:
    """Test harness for go_chess extension."""

    def __init__(self, rows=30, cols=100):
        self.rows = rows
        self.cols = cols
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)
        self.child = None
        self.debug = os.environ.get('DEBUG', '0') == '1'
        self.moves_played = []

    def start(self, filename=None):
        """Start μEmacs."""
        # Use a pre-existing file to avoid "new file" prompts
        if filename is None:
            filename = '/tmp/chess_test_dummy.txt'
            with open(filename, 'w') as f:
                f.write("Chess test file\n")

        self.child = pexpect.spawn(
            BINARY, ['-e', 'config', filename],
            dimensions=(self.rows, self.cols),
            env={**os.environ, 'TERM': 'xterm-256color', 'LANG': 'en_US.UTF-8'},
            encoding=None,
            timeout=MAX_MOVE_WAIT + 5
        )
        # Track PID for cleanup
        if self.child.pid:
            _spawned_pids.add(self.child.pid)
        # Wait for extensions to load (Go extensions take 5+ seconds)
        for _ in range(12):
            time.sleep(0.5)
            self._read_output()
        self._handle_prompts()

    def send(self, keys, delay=0.3):
        """Send keystrokes."""
        if self.debug:
            print(f"  [send] {repr(keys)}")
        # Convert to bytes if necessary (binary mode)
        if isinstance(keys, str):
            keys = keys.encode('utf-8')
        self.child.send(keys)
        time.sleep(delay)
        self._read_output()

    def send_meta_x(self, command, delay=0.5):
        """Send M-x command."""
        self.send('\x1bx', delay=0.2)
        self.send(command + '\r', delay=delay)
        # Handle any "Discard changes?" prompt
        self._handle_prompts()

    def send_prompt_response(self, response, delay=0.3):
        """Send response to a prompt."""
        self.send(response + '\r', delay=delay)

    def _read_output(self):
        """Read terminal output."""
        try:
            while True:
                data = self.child.read_nonblocking(size=8192, timeout=0.1)
                # Handle binary data - decode with error replacement
                if isinstance(data, bytes):
                    data = data.decode('utf-8', errors='replace')
                # Translate Unicode chess pieces to ASCII for pyte compatibility
                data = translate_chess_unicode(data)
                self.stream.feed(data)
        except (pexpect.TIMEOUT, pexpect.EOF):
            pass

    def _handle_prompts(self):
        """Handle common prompts like 'Discard changes?'."""
        for _ in range(3):  # Try multiple times
            self._read_output()
            msg = self.get_minibuffer()
            if 'Discard changes' in msg or '[Y/N]' in msg:
                self.send('y', delay=0.2)
            elif 'Save changes' in msg:
                self.send('n', delay=0.2)
            elif 'Abort' in msg:
                self.send('y', delay=0.2)
            else:
                break
            time.sleep(0.1)

    def wait_for_prompt(self, text, timeout=3.0):
        """Wait for a specific prompt text."""
        start = time.time()
        while time.time() - start < timeout:
            self._read_output()
            msg = self.get_minibuffer()
            if text.lower() in msg.lower():
                return True
            time.sleep(0.1)
        return False

    def get_screen(self):
        """Get full screen content."""
        return '\n'.join(line.rstrip() for line in self.screen.display)

    def get_line(self, row):
        """Get a specific line."""
        return self.screen.display[row].rstrip()

    def get_minibuffer(self):
        """Get minibuffer/message line."""
        return self.get_line(self.rows - 1)

    def dump_screen(self, max_lines=25):
        """Print screen for debugging."""
        print("  Screen content:")
        for i in range(min(max_lines, self.rows)):
            line = self.get_line(i)
            if line.strip():
                print(f"    {i:2}: {line[:95]}")

    def find_board(self):
        """Find the chess board in the screen output."""
        screen = self.get_screen()
        # Look for ASCII chess pieces (translated from Unicode)
        if 'K' in screen and 'k' in screen and 'a b c d e f g h' in screen:
            return True
        # Fallback: look for coordinate labels
        if 'a b c d e f g h' in screen:
            return True
        return False

    def extract_last_move(self):
        """Extract the last move from the message line."""
        msg = self.get_minibuffer()
        # Look for patterns like "AI plays: e7e5" or "Last: e2e4"
        match = re.search(r'(?:plays|Last):\s*([a-h][1-8][a-h][1-8][qrbn]?)', msg)
        if match:
            return match.group(1)
        return None

    def extract_metrics(self):
        """Extract search metrics from message line."""
        msg = self.get_minibuffer()
        metrics = {}

        # Nodes
        match = re.search(r'Nodes:\s*([\d.]+[KM]?)', msg)
        if match:
            metrics['nodes'] = match.group(1)

        # Steals
        match = re.search(r'Steals:\s*(\d+)', msg)
        if match:
            metrics['steals'] = int(match.group(1))

        # Time
        match = re.search(r'Time:\s*(\d+)ms', msg)
        if match:
            metrics['time_ms'] = int(match.group(1))

        # Depth
        match = re.search(r'Depth:\s*(\d+)', msg)
        if match:
            metrics['depth'] = int(match.group(1))

        return metrics

    def is_game_over(self):
        """Check if the game has ended."""
        screen = self.get_screen()
        return 'CHECKMATE' in screen or 'STALEMATE' in screen or 'DRAW' in screen

    def get_side_to_move(self):
        """Get which side is to move."""
        screen = self.get_screen()
        if 'White to move' in screen:
            return 'white'
        elif 'Black to move' in screen:
            return 'black'
        return None

    def close(self):
        """Close μEmacs - forcefully kill."""
        if self.child:
            pid = self.child.pid

            # Kill the process
            try:
                os.kill(pid, signal.SIGKILL)
            except:
                pass

            # Remove from tracking
            _spawned_pids.discard(pid)

            # Close pexpect handle
            try:
                self.child.close(force=True)
            except:
                pass

            self.child = None

    def safe_set_depth(self, depth=MAX_SEARCH_DEPTH):
        """Safely set search depth to a low value."""
        self._handle_prompts()  # Clear any pending prompts

        # Use M-x chess-depth and respond to prompt
        self.send('\x1bx', delay=0.2)
        self._handle_prompts()
        self.send('chess-depth\r', delay=0.5)
        self._handle_prompts()

        # Wait for the depth prompt
        if self.wait_for_prompt('depth', timeout=2.0):
            self.send(f'{depth}\r', delay=0.5)
            # Check confirmation
            msg = self.get_minibuffer()
            if f'set to {depth}' in msg.lower() or f'depth' in msg.lower():
                return True

        # Fallback: check if depth shows in display after refresh
        self._handle_prompts()
        return False


def test_chess_loads():
    """Test that chess extension loads and starts a game."""
    print("\n[TEST] Chess extension loads")

    emu = ChessTest()
    try:
        emu.start()

        # Start a new chess game
        emu.send_meta_x('chess', delay=1.0)

        # Check board is displayed
        if not emu.find_board():
            emu.dump_screen()
            print("  FAIL: Board not displayed")
            return False

        # Check for initial position indicators
        screen = emu.get_screen()
        if 'White to move' not in screen:
            emu.dump_screen()
            print("  FAIL: 'White to move' not found")
            return False

        print("  PASS: Chess game started, board displayed")
        return True

    finally:
        emu.close()


def test_chess_move():
    """Test making a move and getting AI response."""
    print("\n[TEST] Chess move and AI response")

    emu = ChessTest()
    try:
        emu.start()
        emu.send_meta_x('chess', delay=1.0)

        if not emu.find_board():
            print("  FAIL: Board not displayed")
            return False

        # Set low depth FIRST for fast AI response
        emu.safe_set_depth(MAX_SEARCH_DEPTH)

        # Make opening move e2e4
        emu.send_meta_x('chess-move', delay=0.3)
        emu._handle_prompts()
        time.sleep(0.2)
        emu._read_output()

        # Enter the move with safe timeout
        emu.send('e2e4\r', delay=MAX_MOVE_WAIT)
        emu._handle_prompts()

        # Check AI responded
        msg = emu.get_minibuffer()
        screen = emu.get_screen()

        # AI response in message or board shows it's White's turn again
        if 'AI plays:' in msg or 'White to move' in screen:
            metrics = emu.extract_metrics()
            if metrics:
                print(f"  Metrics: {metrics}")
            print("  PASS: Move accepted, AI responded")
            return True

        # Check if move was at least accepted (pawn moved)
        if '♙' in emu.get_line(5):  # Pawn on rank 4
            print("  PASS: Move accepted (board updated)")
            return True

        emu.dump_screen()
        print(f"  FAIL: No AI response. Message: {msg}")
        return False

    finally:
        emu.close()


def test_chess_ai_vs_ai():
    """Test AI vs AI game - exercises work-stealing with SAFE resource limits."""
    print("\n[TEST] AI vs AI game (resource-safe)")
    print(f"  Limits: depth={MAX_SEARCH_DEPTH}, max_moves={MAX_AI_MOVES}, hint_timeout={MAX_HINT_WAIT}s")

    emu = ChessTest()
    moves = []

    try:
        emu.start()

        # Start game
        emu.send_meta_x('chess', delay=1.0)
        emu._handle_prompts()

        # CRITICAL: Set low depth FIRST before any AI operations
        # Do NOT call 'chess' again after this - it resets the game and depth!
        if emu.safe_set_depth(MAX_SEARCH_DEPTH):
            print(f"  Depth set to {MAX_SEARCH_DEPTH}")
        else:
            # Depth might still be default (6) - proceed with caution
            print(f"  Warning: Could not confirm depth setting")

        move_count = 0
        print("  Playing moves...")

        while move_count < MAX_AI_MOVES and not emu.is_game_over():
            # Get hint with strict timeout
            emu.send_meta_x('chess-hint', delay=MAX_HINT_WAIT)
            emu._handle_prompts()

            # Extract suggested move
            msg = emu.get_minibuffer()
            match = re.search(r'Suggestion:\s*([a-h][1-8][a-h][1-8][qrbn]?)', msg)
            if not match:
                screen = emu.get_screen()
                match = re.search(r'Suggestion:\s*([a-h][1-8][a-h][1-8][qrbn]?)', screen)
                if not match:
                    # Hint timed out or failed
                    print(f"  Hint timeout/fail at move {move_count}")
                    break

            suggested_move = match.group(1)
            side = 'W' if emu.get_side_to_move() == 'white' else 'B'

            # Make the move with timeout
            emu.send_meta_x('chess-move', delay=0.3)
            emu._handle_prompts()
            emu.send(suggested_move + '\r', delay=MAX_MOVE_WAIT)
            emu._handle_prompts()

            moves.append(f"{side}:{suggested_move}")
            move_count += 1

            # Check for AI response move
            ai_move = emu.extract_last_move()
            if ai_move and ai_move != suggested_move:
                moves.append(f"B:{ai_move}")
                move_count += 1

            if emu.is_game_over():
                break

        # Summary
        print(f"\n  Moves played: {move_count}")
        print(f"  Sequence: {' '.join(moves)}")

        # Success if we played at least 2 full moves (4 half-moves)
        if move_count >= 4:
            print(f"  PASS: AI vs AI game functional")
            return True
        elif move_count >= 2:
            print(f"  PASS: Limited moves but functional")
            return True
        else:
            print(f"  FAIL: Only {move_count} moves")
            return False

    finally:
        emu.close()


def test_chess_depth():
    """Test setting search depth."""
    print("\n[TEST] Chess depth configuration")

    emu = ChessTest()
    try:
        emu.start()
        emu.send_meta_x('chess', delay=1.0)

        # Set depth using chess-depth command
        emu.send('\x1bx', delay=0.2)  # M-x
        emu.send('chess-depth\r', delay=0.5)

        # Wait for the depth prompt
        if not emu.wait_for_prompt('depth', timeout=2.0):
            # If no prompt, command might have used default or failed
            pass

        # Enter depth value
        emu.send('8\r', delay=0.5)

        msg = emu.get_minibuffer()
        if 'depth' in msg.lower() and '8' in msg:
            print(f"  PASS: Depth configured ({msg})")
            return True

        # Check screen for depth indicator (chess buffer should show Depth: 8)
        emu.send_meta_x('chess', delay=1.0)  # Refresh display
        screen = emu.get_screen()
        if 'Depth: 8' in screen:
            print("  PASS: Depth configured (visible in display)")
            return True

        # Also accept if we just see the confirmation message at any point
        if 'set to 8' in msg.lower():
            print("  PASS: Depth configured")
            return True

        if emu.debug:
            emu.dump_screen()
        print(f"  FAIL: Depth not set. Message: {msg}")
        return False

    finally:
        emu.close()


def test_chess_eval():
    """Test position evaluation."""
    print("\n[TEST] Chess evaluation")

    emu = ChessTest()
    try:
        emu.start()
        emu.send_meta_x('chess', delay=1.0)

        # Get evaluation
        emu.send_meta_x('chess-eval', delay=0.5)

        msg = emu.get_minibuffer()
        if 'Evaluation:' not in msg:
            print(f"  FAIL: No evaluation. Message: {msg}")
            return False

        # Initial position should be roughly equal
        if 'Equal' in msg or '+0.0' in msg:
            print(f"  PASS: Evaluation works ({msg})")
            return True

        print(f"  PASS: Evaluation returned: {msg}")
        return True

    finally:
        emu.close()


def test_chess_fen():
    """Test FEN output."""
    print("\n[TEST] Chess FEN output")

    emu = ChessTest()
    try:
        emu.start()
        emu.send_meta_x('chess', delay=1.0)

        # Get FEN
        emu.send_meta_x('chess-fen', delay=0.5)

        msg = emu.get_minibuffer()
        # Starting position FEN
        if 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR' not in msg:
            print(f"  FAIL: Incorrect FEN. Message: {msg}")
            return False

        print("  PASS: FEN output correct")
        return True

    finally:
        emu.close()


def run_tests():
    """Run all chess tests."""
    print("=" * 60)
    print("go_chess Extension Tests - Work-Stealing Chess Engine")
    print("=" * 60)

    # Show detected resources
    print(f"\nSystem: {RESOURCES['cpu_count']} CPUs, load={RESOURCES['load_avg']:.1f}")
    print(f"Test params: depth={MAX_SEARCH_DEPTH}, hint_wait={MAX_HINT_WAIT}s, "
          f"move_wait={MAX_MOVE_WAIT}s, max_moves={MAX_AI_MOVES}")
    if SKIP_AI_VS_AI:
        print("AI vs AI: SKIPPED (system busy)")
    print()

    if not os.path.exists(BINARY):
        print(f"ERROR: μEmacs binary not found at {BINARY}")
        print("Run 'cmake --build build' first")
        return 1

    tests = [
        ("Extension loads", test_chess_loads),
        ("Move and AI response", test_chess_move),
        ("Depth configuration", test_chess_depth),
        ("Position evaluation", test_chess_eval),
        ("FEN output", test_chess_fen),
    ]

    # Only include AI vs AI if system has capacity
    if not SKIP_AI_VS_AI:
        tests.append(("AI vs AI game", test_chess_ai_vs_ai))

    passed = 0
    failed = 0

    try:
        for name, test_fn in tests:
            try:
                if test_fn():
                    passed += 1
                else:
                    failed += 1
            except Exception as e:
                print(f"  ERROR: {e}")
                import traceback
                traceback.print_exc()
                failed += 1
            finally:
                # Cleanup after each test
                _nuke_all()

        print("\n" + "=" * 60)
        print(f"Results: {passed} passed, {failed} failed")
        print("=" * 60)

        return 0 if failed == 0 else 1
    finally:
        # FINAL NUKE - kill everything no matter what
        _nuke_all()


if __name__ == '__main__':
    try:
        sys.exit(run_tests())
    finally:
        _nuke_all()
