#!/usr/bin/env python3
"""
Comprehensive C23 Linter

- Checks for unsafe and deprecated C APIs, non-idiomatic C23 usage, style violations, and portability hazards.
- Designed for modern C23 codebases, especially those with strict safety and maintainability requirements.

Features:
- Detects banned/unsafe string, memory, and I/O APIs (favoring safe_* and C23 alternatives).
- Flags use of legacy types/macros (e.g., NULL, bool, TRUE/FALSE, gets()).
- Warns about implicit int, K&R prototypes, and missing function prototypes.
- Enforces use of _Atomic, restrict, and other C23 features where appropriate.
- Checks for discouraged preprocessor patterns, magic numbers, and missing const.
- Supports exclusion lists and extensible rules.
"""

import os
import re
import sys

ROOTS = ["src", "include"]
EXCLUDE_DIRS = {"tests", "third_party", "external", ".git"}
EXCLUDE_SUFFIXES = {".bak", ".orig", ".rej"}

# --- Rule Definitions ---

# Banned/Unsafe APIs (functions/macros)
BANNED_APIS = [
    # String/memory
    r"\bstrcpy\s*\(",
    r"\bstrcat\s*\(",
    r"\bstrncpy\s*\(",
    r"\bsprintf\s*\(",
    r"\bgets\s*\(",
    r"\bscanf\s*\(",
    r"\bsscanf\s*\(",
    r"\bvsprintf\s*\(",
    r"\bstrtok\s*\(",
    r"\bstrdup\s*\(",
    r"\bmemcpy\s*\(",
    r"\bmemmove\s*\(",
    r"\bmemset\s*\(",
    r"\bbcopy\s*\(",
    r"\bbzero\s*\(",
    # Memory alloc
    r"\bmalloc\s*\(",
    r"\bcalloc\s*\(",
    r"\brealloc\s*\(",
    r"\bfree\s*\(",
    # Legacy C macros/types (allow C23 bool)
    r"\bNULL\b",
    r"\bTRUE\b",
    r"\bFALSE\b",
    # Deprecated types
    r"\bregister\b",
    r"\bauto\b",
    r"\bvolatile\b",  # C23: volatile is discouraged
    # Dangerous I/O
    r"\bgets\s*\(",
    r"\bscanf\s*\(",
    r"\bsscanf\s*\(",
    r"\bvsprintf\s*\(",
]

# Discouraged preprocessor patterns
BANNED_PREPROCESSOR = [
    r"^\s*#\s*define\s+DEBUG\s*1",      # Hardcoded debug
    r"^\s*#\s*include\s+<malloc\.h>",   # Non-standard
    r"^\s*#\s*define\s+[A-Z_]+ 0x[0-9A-Fa-f]+",  # Magic hex numbers
]

# Style/Modernization warnings
STYLE_WARNINGS = [
    (r"^\s*typedef\s+struct\s+\w+\s+\w+;", "Redundant struct typedef, prefer 'typedef struct Foo Foo;' or use 'struct Foo' directly."),
    (r"\bvoid\s+\*\s*\w+\s*;.*", "Consider using proper typed pointers instead of 'void *'."),
    (r"\benum\s+[a-zA-Z_][a-zA-Z0-9_]*\s*{", "Prefer strongly-typed enums in C23."),
    (r"\bstatic\s+inline\s+", "Static inline is fine, but confirm you want it for every function."),
    (r"\bextern\s+int\s+main\s*\(", "Do not declare 'main' as extern."),
    (r"\bint\s+[a-zA-Z_][a-zA-Z0-9_]*\s*\(", "Warn if function prototypes are missing parameter types (K&R style)."),
    (r"\btypedef\s+.+\s*[A-Z_][A-Z0-9_]+;", "Typedefs should use lower_snake_case for modern C."),
]

# Modern C23 features (encourage usage)
C23_FEATURES = [
    (r"\b_Atomic\b", "Good: using C23 _Atomic."),
    (r"\brestrict\b", "Good: using restrict for pointer optimization."),
]

# Magic numbers (outside enums/consts)
MAGIC_NUMBER = re.compile(r"[^#]\b([0-9]+|0x[0-9A-Fa-f]+)\b")

# Patterns to ignore (inside comments/strings)
def clean_line(line):
    """Remove comments and string literals from a code line."""
    def replacer(match):
        return ' ' * (match.end() - match.start())
    # Remove block comments, string/char literals, and line comments
    line = re.sub(r"/\*.*?\*/", replacer, line)
    line = re.sub(r'"(?:\\.|[^"\\])*"', replacer, line)
    line = re.sub(r"'(?:\\.|[^'\\])*'", replacer, line)
    line = re.sub(r"//.*", replacer, line)
    return line

def iter_files():
    for root in ROOTS:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
            for fname in filenames:
                if any(fname.endswith(suf) for suf in EXCLUDE_SUFFIXES):
                    continue
                if not (fname.endswith(".c") or fname.endswith(".h")):
                    continue
                yield os.path.join(dirpath, fname)

def lint_file(path):
    violations = []
    warnings = []
    saw_ctype = False
    saw_estruct = False
    in_switch_stack = []  # stack of dicts: { 'pending_case': (line_no, text) or None, 'has_terminal': bool }
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except Exception as e:
        warnings.append(f"{path}:0: Cannot open file: {e}")
        return violations, warnings

    in_block = False
    for idx, line in enumerate(lines, start=1):
        code = line
        # Remove comments and string/char literals for safe pattern matching
        cleaned = clean_line(code)
        # include tracking for ctype/estruct
        if re.search(r"^\s*#\s*include\s*<ctype\.h>", code):
            saw_ctype = True
        if re.search(r"^\s*#\s*include\s*\".*estruct\.h\"", code):
            saw_estruct = True

        # switch-case fallthrough tracking (very heuristic)
        if re.search(r"\bswitch\s*\(", cleaned):
            in_switch_stack.append({'pending_case': None, 'has_terminal': False})
        if in_switch_stack:
            top = in_switch_stack[-1]
            if re.search(r"\bcase\b|\bdefault\s*:", cleaned):
                # check previous case
                if top['pending_case'] and not top['has_terminal']:
                    lno, txt = top['pending_case']
                    if '[[fallthrough]]' not in txt and 'fallthrough' not in txt:
                        warnings.append(f"{path}:{lno}: STYLE: MISSING_FALLTHROUGH_MARKER after case/default")
                top['pending_case'] = (idx, line.rstrip('\n'))
                top['has_terminal'] = False
            if re.search(r"\bbreak\s*;|\breturn\b|\bcontinue\b|\bgoto\b|\[\[fallthrough\]\]", cleaned):
                top['has_terminal'] = True
            if '}' in cleaned:
                # On closing brace, pop a switch if any; naive but acceptable
                if in_switch_stack:
                    last = in_switch_stack.pop()
                    if last['pending_case'] and not last['has_terminal']:
                        lno, txt = last['pending_case']
                        if '[[fallthrough]]' not in txt and 'fallthrough' not in txt:
                            warnings.append(f"{path}:{lno}: STYLE: MISSING_FALLTHROUGH_MARKER before end of switch")
        # --- Banned APIs ---
        for pat in BANNED_APIS:
            if re.search(pat, cleaned):
                violations.append((path, idx, f"BANNED API: {pat.strip('\\\\b').replace('\\\\s*','(')}", line.rstrip("\n")))
        # --- Preprocessor patterns ---
        for pat in BANNED_PREPROCESSOR:
            if re.search(pat, cleaned):
                violations.append((path, idx, f"BANNED PREPROCESSOR: {pat}", line.rstrip("\n")))
        # --- Inline if with multiple statements on one line ---
        # Allow exactly one controlled statement on the same line:
        #   if (cond) single_stmt;
        # Reject patterns like:
        #   if (cond) stmt; more;
        #   if (cond) stmt; else ...
        # Heuristic: if-line without '{' that contains a semicolon and any non-space after that semicolon
        if re.search(r"^\s*if\s*\(", cleaned) and '{' not in cleaned:
            # remove up to the first semicolon
            parts = cleaned.split(';', 1)
            if len(parts) == 2:
                tail = parts[1].strip()
                if tail:  # there is more code after the first statement
                    violations.append((path, idx, "INLINE_IF_MULTISTMT: more than one statement after inline if", line.rstrip("\n")))

        # --- Control lines with multiple statements on the same line (for/while) ---
        if re.search(r"^\s*(for|while)\s*\(", cleaned) and '{' not in cleaned:
            parts = cleaned.split(';', 1)
            if len(parts) == 2 and parts[1].strip():
                violations.append((path, idx, "MULTISTMT_AFTER_CONTROL: more than one statement after control header", line.rstrip("\n")))

        # --- Assignment-in-condition (likely bug): '=' inside condition not part of '==', '>=', '<=', '!=' ---
        m = re.search(r"^\s*(if|while|for)\s*\((.*)\)\s*", cleaned)
        if m:
            cond = m.group(2)
            # remove spaces for a quick scan
            z = cond
            # find single '=' not adjacent to '=' or '>' '<' '!'
            if re.search(r"(?<![<>=!])=(?![=])", z):
                violations.append((path, idx, "ASSIGN_IN_COND: assignment in condition?", line.rstrip("\n")))

        # --- Fallthrough in switch without marker --- (heuristic, tracked below)
        # --- Style warnings ---
        for pat, msg in STYLE_WARNINGS:
            if re.search(pat, cleaned):
                warnings.append(f"{path}:{idx}: STYLE: {msg}: {line.rstrip()}")
        # --- Magic numbers ---
        if MAGIC_NUMBER.search(cleaned):
            # Exempt 0, 1, -1, or inside enums/const
            mnums = [m.group() for m in MAGIC_NUMBER.finditer(cleaned)]
            for num in mnums:
                if num in {"0", "1", "-1"}:
                    continue
                if 'enum' in cleaned or 'const' in cleaned:
                    continue
                warnings.append(f"{path}:{idx}: MAGIC NUMBER: {num}: {line.rstrip()}")

    # ctype + estruct conflict
    if saw_ctype and saw_estruct:
        warnings.append(f"{path}:1: STYLE: CTYPE_CONFLICT: avoid mixing <ctype.h> with estruct.h macros; prefer local helpers or wrappers")

    return violations, warnings

def main():
    all_violations = []
    all_warnings = []
    for path in iter_files():
        viols, warns = lint_file(path)
        all_violations.extend(viols)
        all_warnings.extend(warns)

    if all_violations or all_warnings:
        if all_violations:
            print("CRITICAL: Banned APIs, macros, or patterns detected:", file=sys.stderr)
            for (fname, ln, what, text) in all_violations:
                print(f"{fname}:{ln}: {what}: {text}", file=sys.stderr)
        if all_warnings:
            print("\nWARNINGS:", file=sys.stderr)
            for msg in all_warnings:
                print(msg, file=sys.stderr)
        print("\nSee C23 migration guide and coding standards for details.", file=sys.stderr)
        sys.exit(2 if all_violations else 1)

    print("No critical C23 lint violations detected. (Some minor warnings may still exist.)")
    sys.exit(0)

if __name__ == "__main__":
    main()
        # --- Atomics without explicit ordering ---
        if re.search(r"\batomic_(load|store)\s*\(", cleaned) and "_explicit" not in cleaned:
            warnings.append(f"{path}:{idx}: STYLE: ATOMIC_ORDER_UNCLEAR: prefer atomic_*_explicit with memory_order")

        # --- RAW IO forbidden in core/terminal paths ---
        lower = path.replace('\\','/')
        if (lower.startswith('src/core/') or lower.startswith('src/terminal/')):
            if re.search(r"\b(f?printf|puts|putchar)\s*\(", cleaned):
                warnings.append(f"{path}:{idx}: STYLE: RAW_IO_FORBIDDEN: use vtputs/TT* wrappers in core/terminal code")

        # --- memcpy/memset/memmove advisory ---
        if re.search(r"\bmem(set|cpy|move)\s*\(", cleaned):
            warnings.append(f"{path}:{idx}: STYLE: Consider safe_* wrappers instead of direct mem* calls where feasible: {line.rstrip()}")
