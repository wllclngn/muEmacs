#!/usr/bin/env python3
"""State persistence smoke test.

Verifies the UEP state API wiring end-to-end:
  1. Launches μEmacs with isolated XDG_CACHE_HOME.
  2. Executes a search (populates pat[]).
  3. Quits via C-x C-c so state_core_save_all() runs.
  4. Asserts core/search.bin exists with a valid STT1 envelope.
  5. Relaunches and confirms load path does not abort startup.
"""

import os
import sys
import tempfile
import time
import shutil
import struct

import pexpect

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(PROJECT_ROOT, 'build', 'bin', 'μEmacs')

STATE_MAGIC = b'STT1'


def _spawn(cache_dir, config_dir, file_path):
    env = os.environ.copy()
    env.pop('XDG_CONFIG_HOME', None)
    env['XDG_CACHE_HOME'] = cache_dir
    env['XDG_CONFIG_HOME'] = config_dir
    env['LINES'] = '24'
    env['COLUMNS'] = '80'
    env['TERM'] = 'xterm-256color'
    return pexpect.spawn(
        BINARY, [file_path],
        dimensions=(24, 80),
        env=env,
        encoding='utf-8',
        timeout=8,
    )


def _drain(child, wait=0.3):
    try:
        child.read_nonblocking(size=65536, timeout=wait)
    except (pexpect.TIMEOUT, pexpect.EOF):
        pass


def _quit_clean(child):
    # C-x C-c; respond 'y' if a "Modified buffers" prompt appears.
    child.send('\x18\x03')
    time.sleep(0.3)
    try:
        child.read_nonblocking(size=4096, timeout=0.3)
    except (pexpect.TIMEOUT, pexpect.EOF):
        pass
    # Best-effort: answer yes in case there is a modified-buffer prompt.
    try:
        child.send('y')
    except Exception:
        pass
    time.sleep(0.3)
    try:
        child.expect(pexpect.EOF, timeout=3)
    except Exception:
        child.terminate(force=True)


def run_test():
    if not os.path.exists(BINARY):
        print(f'FAIL: binary not found at {BINARY}', file=sys.stderr)
        return 1

    tmp = tempfile.mkdtemp(prefix='muemacs-state-')
    cache_dir = os.path.join(tmp, 'cache')
    config_dir = os.path.join(tmp, 'config')
    os.makedirs(cache_dir, exist_ok=True)
    os.makedirs(os.path.join(config_dir, 'muemacs'), exist_ok=True)

    # Copy the shipped default settings.toml so keybindings (C-s = search-forward)
    # are bound. Without this the test config is just "[state]" and C-s is unbound.
    shipped_toml = os.path.join(PROJECT_ROOT, 'config', 'editor', 'settings.toml')
    test_toml = os.path.join(config_dir, 'muemacs', 'settings.toml')
    shutil.copyfile(shipped_toml, test_toml)

    test_file = os.path.join(tmp, 'text.txt')
    with open(test_file, 'w') as f:
        f.write('alpha beta gamma\nfoo bar baz\nquux\n')

    state_dir = os.path.join(cache_dir, 'muemacs', 'state')
    search_bin = os.path.join(state_dir, 'core', 'search.bin')

    print(f'[state-test] using cache: {cache_dir}')

    # Session 1: search for 'foo', quit.
    child = _spawn(cache_dir, config_dir, test_file)
    _drain(child, 1.0)

    # C-s = search-forward (per shipped settings.toml).
    child.send('\x13')  # C-s
    time.sleep(0.3)
    _drain(child)
    child.send('foo\r')
    time.sleep(0.3)
    _drain(child)

    _quit_clean(child)

    if not os.path.exists(search_bin):
        print(f'FAIL: state file missing: {search_bin}', file=sys.stderr)
        print(f'cache dir contents: {os.listdir(cache_dir) if os.path.exists(cache_dir) else "missing"}', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1

    with open(search_bin, 'rb') as f:
        data = f.read()

    if len(data) < 24:
        print(f'FAIL: state file too small ({len(data)} bytes)', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1

    if data[:4] != STATE_MAGIC:
        print(f'FAIL: bad magic: {data[:4]!r} (expected {STATE_MAGIC!r})', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1

    version = struct.unpack('<I', data[4:8])[0]
    payload_len = struct.unpack('<Q', data[8:16])[0]

    if version != 1:
        print(f'FAIL: unexpected version {version}', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1

    if payload_len == 0 or payload_len != len(data) - 24:
        print(f'FAIL: payload_len {payload_len} vs actual {len(data) - 24}', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1

    # Payload: u8 version, u16 pat_len, pat, u16 rpat_len, rpat.
    payload = data[24:]
    if len(payload) < 3:
        print('FAIL: payload too short for search record', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1
    sub_ver = payload[0]
    pat_len = struct.unpack('<H', payload[1:3])[0]
    if pat_len == 0 or pat_len > len(payload) - 3:
        print(f'FAIL: pat_len {pat_len} invalid (payload {len(payload)} bytes)', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1
    pat = payload[3:3 + pat_len].decode('utf-8', errors='replace')
    if pat != 'foo':
        print(f'FAIL: expected pat="foo", got {pat!r}', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1

    print(f'[state-test] search.bin OK: sub_ver={sub_ver} pat={pat!r} ({len(data)} bytes)')

    # Session 2: relaunch — state_core_load_all should consume the file.
    child = _spawn(cache_dir, config_dir, test_file)
    _drain(child, 1.0)
    if not child.isalive():
        print('FAIL: relaunch died', file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return 1
    _quit_clean(child)

    shutil.rmtree(tmp, ignore_errors=True)
    print('[state-test] PASS')
    return 0


if __name__ == '__main__':
    sys.exit(run_test())
