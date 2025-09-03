#!/usr/bin/env bash
set -euo pipefail

# muEmacs CI runner (non-interactive by default)
# - Skips expect tests and MAGIC/NFA unless explicitly enabled
# - Runs safety linter when available

export ENABLE_EXPECT=${ENABLE_EXPECT:-0}
export ENABLE_NFA_TESTS=${ENABLE_NFA_TESTS:-0}
# Optional run-time knobs
export TEST_TIMEOUT=${TEST_TIMEOUT:-0}   # seconds; 0 disables
export STRESS=${STRESS:-0}               # 1 enables heavier scenarios (where supported)

CI_BUILD_TYPE=${CI_BUILD_TYPE:-Release}
echo "[CI] Building (${CI_BUILD_TYPE})..."
cmake -S . -B build -DCMAKE_BUILD_TYPE="${CI_BUILD_TYPE}" >/dev/null
make -s -j"${CI_CPUS:-$(nproc)}" -C build full_integration_test muEmacs

echo "[CI] Running non-interactive tests..."
if [ "${TEST_TIMEOUT}" != "0" ]; then
  echo "[CI] Using TEST_TIMEOUT=${TEST_TIMEOUT}s"
  timeout "${TEST_TIMEOUT}"s ./bin/full_integration_test || {
    echo "[CI] Tests failed or timed out" >&2
    exit 1
  }
else
  ./bin/full_integration_test || {
  echo "[CI] Tests failed" >&2
  exit 1
  }
fi

# Optional microbenchmark (Release only)
if [ "${CI_BUILD_TYPE}" = "Release" ]; then
  echo "[CI] Building and running search microbenchmark..."
  cmake --build build --target bench_search -j"${CI_CPUS:-$(nproc)}" || true
  ./bin/bench_search || true
fi

if command -v python3 >/dev/null 2>&1 ; then
  echo "[CI] Running C23 safety linter..."
  python3 scripts/cs23_linter.py || {
    echo "[CI] Linter failed" >&2
    exit 1
  }
else
  echo "[CI] Python3 not found; skipping linter"
fi

# Ban unsafe libc calls where wrappers exist. Exclude wrapper internals and tests.
echo "[CI] Checking for banned unsafe libc calls..."
if rg -n "\\b(strcpy|strcat|strncpy|sprintf|strdup)\\s*\(" src include | rg -v "src/util/(memory|wrapper)\\.c" | rg -v "^$" ; then
  echo "[CI] ERROR: Found banned unsafe libc calls above. Use safe_* wrappers." >&2
  exit 1
fi

# Guard against stray backup files in repo
if find . -type f -name "*.bak" | rg . ; then
  echo "[CI] ERROR: Found stray backup files (*.bak)." >&2
  exit 1
fi

echo "[CI] Done."
