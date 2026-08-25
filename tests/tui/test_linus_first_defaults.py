#!/usr/bin/env python3
"""
Regression test: the shipped config/editor/settings.toml must carry the
Linus-first defaults (task #10 option c).

This test reads the settings file as TOML-ish text and asserts the exact
key/value pairs — it does not launch the editor. The goal is to catch
regressions where a future edit silently flips one of these back to a
"modern" default.

Locked-in Linus-first defaults:
- [editor].column_width      = 72    (was 80)
- [editor].tab_width         = 8
- [editor].auto_save_interval = 256
- [visual].ruler             = false
- [visual].highlight_line    = false
- [extension.c_linus].enabled = true (ships on by default)
"""

import os
import re
import sys


SETTINGS_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..',
    'config', 'editor', 'settings.toml'
)


def parse_simple_toml(text):
    """Minimal TOML reader — handles [section] headers, `key = value`, and
    strips inline `# comment` tails. Good enough for this settings file.
    Returns dict of {section: {key: value_str}}."""
    sections = {}
    current = None
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        m = re.match(r'^\[([^\]]+)\]', stripped)
        if m:
            current = m.group(1)
            sections.setdefault(current, {})
            continue
        m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)(?:\s+#.*)?$', stripped)
        if m and current is not None:
            sections[current][m.group(1)] = m.group(2).strip().strip('"')
    return sections


def require(cfg, section, key, expected, label):
    val = cfg.get(section, {}).get(key)
    if val is None:
        raise AssertionError(f"FAIL: {label} — [{section}].{key} is missing from settings.toml")
    if val != expected:
        raise AssertionError(
            f"FAIL: {label} — [{section}].{key} = {val!r}, expected {expected!r}"
        )
    print(f"  PASS {label}: [{section}].{key} = {val}")


def test_linus_first_defaults():
    with open(SETTINGS_PATH, encoding='utf-8') as f:
        cfg = parse_simple_toml(f.read())

    require(cfg, 'editor', 'column_width',        '72',    'fillcol = 72')
    require(cfg, 'editor', 'tab_width',           '8',     'tab_width = 8')
    require(cfg, 'editor', 'auto_save_interval',  '256',   'gasave = 256')
    require(cfg, 'visual', 'ruler',               'false', 'ruler off')
    require(cfg, 'visual', 'highlight_line',      'false', 'highlight_line off')
    require(cfg, 'extension.c_linus', 'enabled',  'true',  'c_linus enabled')


def run_tests():
    print("Running Linus-first defaults test...")
    test_linus_first_defaults()
    print("Linus-first defaults locked in.")


if __name__ == '__main__':
    run_tests()
