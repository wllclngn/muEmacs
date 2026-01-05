#!/usr/bin/env python3
"""
Focused debug test for Ada fuzzy-find extension.

This test instruments the fuzzy-find command and captures logs to identify
the exact crash point in the Ada/C bridge.

Usage:
    python3 tests/tui/test_fuzzy_ada_debug.py

After running, check /tmp/uemacs_debug.log for the crash point.
"""

import sys
import os
import time

# Add tests/tui to path for base module
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from base import UEmacsTest

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def test_fuzzy_find_crash():
    """Attempt to trigger fuzzy-find and capture crash point via logs."""
    print("=" * 60)
    print("FUZZY-FIND ADA DEBUG TEST")
    print("=" * 60)

    # Clear old log
    log_path = "/tmp/uemacs_debug.log"
    if os.path.exists(log_path):
        os.remove(log_path)
        print(f"Cleared old log: {log_path}")

    print(f"\nStarting editor from: {PROJECT_ROOT}")

    # Change to project root so fuzzy-find can find files
    os.chdir(PROJECT_ROOT)

    t = UEmacsTest(rows=24, cols=80)

    try:
        t.start()
        print("Editor started successfully")
        time.sleep(0.5)
        t._read_output()

        # Step 1: Send M-x
        print("\n[STEP 1] Sending M-x (ESC x)...")
        t.send('\x1bx')
        time.sleep(0.3)
        t._read_output()
        minibuf = t.get_line(t.rows - 1)
        print(f"  Minibuffer: {minibuf[:60]}")

        # Step 2: Type fuzzy-find command
        print("\n[STEP 2] Typing 'fuzzy-find' + Enter...")
        t.send('fuzzy-find\r')
        time.sleep(0.5)
        t._read_output()
        minibuf = t.get_line(t.rows - 1)
        print(f"  Minibuffer: {minibuf[:60]}")

        # Check if we got a prompt
        if 'Fuzzy' in minibuf or 'find' in minibuf.lower() or ':' in minibuf:
            print("  Got prompt, typing search pattern...")

            # Step 3: Type search pattern
            print("\n[STEP 3] Typing 'main' as search pattern...")
            t.send('main')
            time.sleep(0.3)
            t._read_output()
            minibuf = t.get_line(t.rows - 1)
            print(f"  Minibuffer: {minibuf[:60]}")

            # Step 4: Press Enter to confirm
            print("\n[STEP 4] Pressing Enter to confirm...")
            t.send('\r')
            time.sleep(1.0)
            t._read_output()

            # Show screen state
            print("\n[SCREEN STATE]")
            for i in range(min(10, t.rows)):
                line = t.get_line(i)
                if line.strip():
                    print(f"  {i:2d}: {line[:70]}")

            minibuf = t.get_line(t.rows - 1)
            print(f"\n  Final minibuffer: {minibuf[:60]}")
        else:
            print(f"  Unexpected state: {minibuf}")

        # Step 5: Try to quit cleanly
        print("\n[STEP 5] Attempting to quit...")
        t.send('\x1bx')  # M-x
        time.sleep(0.2)
        t._read_output()
        t.send('quit\r')
        time.sleep(0.3)

    except Exception as e:
        print(f"\n[ERROR] Exception: {e}")
        import traceback
        traceback.print_exc()
    finally:
        try:
            t.close()
            print("\nEditor closed")
        except:
            print("\nEditor may have crashed")

    # Analyze logs
    print("\n" + "=" * 60)
    print("LOG ANALYSIS")
    print("=" * 60)

    if os.path.exists(log_path):
        with open(log_path, 'r') as f:
            log_content = f.read()

        # Find fuzzy-related lines
        fuzzy_lines = [l for l in log_content.split('\n') if 'fuzzy' in l.lower()]

        if fuzzy_lines:
            print(f"\nFound {len(fuzzy_lines)} fuzzy-related log entries:")
            print("-" * 50)
            for line in fuzzy_lines[-30:]:  # Last 30 entries
                print(line)
            print("-" * 50)

            # Look for the last log entry to identify crash point
            if fuzzy_lines:
                print(f"\nLAST FUZZY LOG ENTRY:")
                print(f"  {fuzzy_lines[-1]}")

                # Check what was happening
                last = fuzzy_lines[-1].lower()
                if 'enter' in last:
                    print("  -> Crash likely in: entering bridge function")
                elif 'calling' in last:
                    print("  -> Crash likely in: Ada function being called")
                elif 'exit' in last:
                    print("  -> Function completed successfully before crash")
                else:
                    print("  -> Analyze the sequence above to find crash point")
        else:
            print("\nNo fuzzy-related log entries found!")
            print("Extension may not have loaded or logging not enabled.")

            # Show all log content for debugging
            if len(log_content) < 5000:
                print("\nFull log content:")
                print(log_content)
            else:
                print(f"\nLog has {len(log_content)} bytes, showing last 2000:")
                print(log_content[-2000:])
    else:
        print(f"\nNo log file found at {log_path}")
        print("Make sure μEmacs was built with -DUEMACS_DEBUG_LOG=ON")

    print("\n" + "=" * 60)
    print("TEST COMPLETE")
    print("=" * 60)


def test_extension_loads():
    """Simple test to verify the extension loads at all."""
    print("Testing if fuzzy_ada extension loads...")

    log_path = "/tmp/uemacs_debug.log"
    if os.path.exists(log_path):
        os.remove(log_path)

    os.chdir(PROJECT_ROOT)
    t = UEmacsTest(rows=24, cols=80)
    try:
        t.start()
        time.sleep(1.0)
        t._read_output()
        t.close()
    except:
        pass

    if os.path.exists(log_path):
        with open(log_path, 'r') as f:
            content = f.read()

        if 'fuzzy_ada' in content:
            print("[PASS] Extension loaded")
            # Show init logs
            for line in content.split('\n'):
                if 'fuzzy' in line.lower():
                    print(f"  {line}")
            return True
        else:
            print("[FAIL] Extension did not load")
            return False
    else:
        print("[FAIL] No log file - logging not enabled?")
        return False


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--load-only':
        test_extension_loads()
    else:
        test_fuzzy_find_crash()
