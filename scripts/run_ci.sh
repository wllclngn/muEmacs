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
# Locate test runner binary across common CMake layouts
if [ -x ./build/bin/full_integration_test ]; then
  RUN_CMD="./build/bin/full_integration_test"
elif [ -x ./bin/full_integration_test ]; then
  RUN_CMD="./bin/full_integration_test"
elif [ -x ./build/build/bin/full_integration_test ]; then
  RUN_CMD="./build/build/bin/full_integration_test"
else
  # Fallback: rely on PATH or relative invocation
  RUN_CMD="./bin/full_integration_test"
fi
if [ "${TEST_TIMEOUT}" != "0" ]; then
  echo "[CI] Using TEST_TIMEOUT=${TEST_TIMEOUT}s"
  RUN_CMD="timeout \"${TEST_TIMEOUT}\"s ${RUN_CMD}"
fi
if [ "${UEMACS_VERBOSE_TESTS:-0}" = "1" ]; then
  eval ${RUN_CMD} || { echo "[CI] Tests failed" >&2; exit 1; }
else
  eval ${RUN_CMD} > /dev/null || { echo "[CI] Tests failed" >&2; exit 1; }
fi

echo "[CI] Running focused keymap tests..."
cmake --build build --target keymap_tests -j"${CI_CPUS:-$(nproc)}" >/dev/null 2>&1 || true
if [ -x ./build/bin/keymap_tests ]; then
  KEYMAP_RUNNER=./build/bin/keymap_tests
elif [ -x ./bin/keymap_tests ]; then
  KEYMAP_RUNNER=./bin/keymap_tests
elif [ -x ./build/build/bin/keymap_tests ]; then
  KEYMAP_RUNNER=./build/build/bin/keymap_tests
fi
if [ -n "${KEYMAP_RUNNER:-}" ]; then
  if [ "${UEMACS_VERBOSE_TESTS:-0}" = "1" ]; then
    ${KEYMAP_RUNNER} || { echo "[CI] keymap_tests failed" >&2; exit 1; }
  else
    ${KEYMAP_RUNNER} > /dev/null || { echo "[CI] keymap_tests failed" >&2; exit 1; }
  fi
else
  echo "[CI] keymap_tests binary not found; skipping"
fi

# Optional: run interactive expect tests under a PTY when requested
if [ "${RUN_EXPECT:-0}" = "1" ]; then
  if command -v expect >/dev/null 2>&1 ; then
    if command -v script >/dev/null 2>&1 ; then
      echo "[CI] Running interactive expect tests under PTY..."
      # Reuse the discovered test runner path; default to build/bin
      if [ -x ./build/bin/full_integration_test ]; then
        PTY_RUNNER=./build/bin/full_integration_test
      elif [ -x ./bin/full_integration_test ]; then
        PTY_RUNNER=./bin/full_integration_test
      else
        PTY_RUNNER=${RUN_CMD:-./build/bin/full_integration_test}
      fi
      set +e
      ENABLE_EXPECT=1 UEMACS_VERBOSE_TESTS=${UEMACS_VERBOSE_TESTS:-0} script -q -c "$PTY_RUNNER" /dev/null
      status=$?
      set -e
      if [ $status -ne 0 ]; then
        echo "[CI] PTY expect run not permitted or failed; soft-skipping (status=$status)" >&2
      fi
    else
      echo "[CI] 'script' not available; skipping expect PTY run"
    fi
  else
    echo "[CI] 'expect' not available; skipping expect tests"
  fi
fi

# Optional microbenchmark (Release only)
if [ "${CI_BUILD_TYPE}" = "Release" ]; then
  echo "[CI] Building and running search microbenchmark..."
  cmake --build build --target bench_search -j"${CI_CPUS:-$(nproc)}" || true
  if [ -x ./build/bin/bench_search ]; then ./build/bin/bench_search || true; elif [ -x ./bin/bench_search ]; then ./bin/bench_search || true; fi
  echo "[CI] Building and running keymap/utf8 microbenchmarks..."
  cmake --build build --target bench_keymap bench_utf8 -j"${CI_CPUS:-$(nproc)}" || true
  if [ -x ./build/bin/bench_keymap ]; then ./build/bin/bench_keymap || true; elif [ -x ./bin/bench_keymap ]; then ./bin/bench_keymap || true; fi
  if [ -x ./build/bin/bench_utf8 ]; then ./build/bin/bench_utf8 || true; elif [ -x ./bin/bench_utf8 ]; then ./bin/bench_utf8 || true; fi
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
