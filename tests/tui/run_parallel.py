#!/usr/bin/env python3
"""
Parallel TUI test runner for μEmacs.

Runs all test_*.py files concurrently using multiprocessing.
Each test gets its own temp directory to prevent file collisions.
No external dependencies required.

Usage:
    python3 tests/tui/run_parallel.py          # auto-detect worker count
    python3 tests/tui/run_parallel.py -j4       # 4 workers
    python3 tests/tui/run_parallel.py test_highlight.py test_evil_visual.py  # subset
"""

import subprocess
import sys
import os
import time
import glob
import shutil
import multiprocessing
import argparse


def run_test_file(filepath):
    """Run a single test file and return (name, passed, duration, output)."""
    name = os.path.basename(filepath)
    stem = name.replace('.py', '')

    # Unique temp dir per test file to prevent collisions
    tmpdir = f"/tmp/muemacs_tui_{stem}_{os.getpid()}"
    os.makedirs(tmpdir, exist_ok=True)

    env = os.environ.copy()
    env['MUEMACS_TEST_TMPDIR'] = tmpdir

    t0 = time.monotonic()
    try:
        result = subprocess.run(
            [sys.executable, filepath],
            capture_output=True, text=True, timeout=120,
            cwd=os.path.dirname(filepath),
            env=env,
        )
        elapsed = time.monotonic() - t0
        passed = result.returncode == 0
        output = result.stdout
        if result.stderr:
            output += result.stderr
        return (name, passed, elapsed, output)
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - t0
        return (name, False, elapsed, "TIMEOUT after 120s")
    except Exception as e:
        elapsed = time.monotonic() - t0
        return (name, False, elapsed, str(e))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Parallel TUI test runner")
    parser.add_argument("-j", "--jobs", type=int, default=0,
                        help="Number of parallel workers (0 = auto)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show output from all tests, not just failures")
    parser.add_argument("files", nargs="*",
                        help="Specific test files to run (default: all)")
    args = parser.parse_args()

    tui_dir = os.path.dirname(os.path.abspath(__file__))

    if args.files:
        test_files = []
        for f in args.files:
            path = os.path.join(tui_dir, f) if not os.path.isabs(f) else f
            if os.path.exists(path):
                test_files.append(path)
            else:
                print(f"Warning: {f} not found, skipping")
    else:
        test_files = sorted(glob.glob(os.path.join(tui_dir, "test_*.py")))

    if not test_files:
        print("No test files found")
        return 1

    workers = args.jobs if args.jobs > 0 else min(len(test_files), os.cpu_count() or 4)
    print(f"Running {len(test_files)} test files with {workers} workers\n")

    t_start = time.monotonic()

    with multiprocessing.Pool(workers) as pool:
        results = pool.map(run_test_file, test_files)

    t_total = time.monotonic() - t_start

    passed_files = []
    failed_files = []

    for name, passed, elapsed, output in sorted(results, key=lambda r: r[0]):
        status = "PASS" if passed else "FAIL"
        print(f"[{status}] {name:<40s} {elapsed:6.2f}s")

        if passed:
            passed_files.append(name)
        else:
            failed_files.append(name)

        if args.verbose or not passed:
            for line in output.strip().split('\n'):
                print(f"       {line}")

    serial_time = sum(r[2] for r in results)
    print(f"\n{len(passed_files)} passed, {len(failed_files)} failed  "
          f"({t_total:.1f}s wall / {serial_time:.1f}s serial)")

    if failed_files:
        print(f"\nFailed: {', '.join(failed_files)}")

    return 0 if not failed_files else 1


if __name__ == "__main__":
    sys.exit(main())
