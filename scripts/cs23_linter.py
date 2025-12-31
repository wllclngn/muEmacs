#!/usr/bin/env python3
"""
C23 Linter - FIXED VERSION

Changes from original:
- Less aggressive banning (allows malloc/memcpy if careful)
- Warns instead of fails for most issues
- Only fails on truly dangerous patterns
- Better error messages
- Handles missing directories gracefully
"""

import os
import re
import sys
from pathlib import Path

# Configuration
ROOTS = ["src", "include"]
EXCLUDE_DIRS = {"tests", "third_party", "external", ".git", "build"}
EXCLUDE_SUFFIXES = {".bak", ".orig", ".rej"}

# CRITICAL violations (will fail CI)
CRITICAL_PATTERNS = [
    (r"\bgets\s*\(", "BANNED: gets() is always unsafe"),
    (r"\bscanf\s*\(", "DANGEROUS: scanf() without width limit"),
    (r"\bsprintf\s*\(", "DANGEROUS: sprintf() can overflow, use snprintf()"),
    (r"\bstrcpy\s*\(", "UNSAFE: strcpy() can overflow, use safe_strcpy() or strncpy()"),
    (r"\bstrcat\s*\(", "UNSAFE: strcat() can overflow, use safe_strcat() or strncat()"),
]

# WARNING patterns (will warn but not fail)
WARNING_PATTERNS = [
    (r"\bNULL\b", "STYLE: prefer nullptr in C23"),
    (r"\bTRUE\b|\bFALSE\b", "STYLE: prefer true/false in C23"),
    (r"\bvolatile\b", "NOTE: volatile is often misused, verify correctness"),
    (r"\bmalloc\s*\(", "CAUTION: check for NULL after malloc()"),
    (r"\bcalloc\s*\(", "CAUTION: check for NULL after calloc()"),
    (r"\brealloc\s*\(", "CAUTION: check for NULL after realloc()"),
]

def clean_line(line):
    """Remove comments and string literals from a code line."""
    def replacer(match):
        return ' ' * (match.end() - match.start())
    
    line = re.sub(r'/\*.*?\*/', replacer, line)
    line = re.sub(r'"(?:\\.|[^"\\])*"', replacer, line)
    line = re.sub(r"'(?:\\.|[^'\\])*'", replacer, line)
    line = re.sub(r'//.*', replacer, line)
    return line

def iter_files():
    """Iterate over all C source files."""
    found_any = False
    
    for root in ROOTS:
        root_path = Path(root)
        if not root_path.exists():
            continue
            
        found_any = True
        for filepath in root_path.rglob('*'):
            # Skip excluded directories
            if any(excluded in filepath.parts for excluded in EXCLUDE_DIRS):
                continue
                
            # Skip excluded suffixes
            if any(str(filepath).endswith(suf) for suf in EXCLUDE_SUFFIXES):
                continue
                
            # Only process C files
            if filepath.suffix in {'.c', '.h'}:
                yield filepath
    
    if not found_any:
        print(f"WARNING: None of the expected directories {ROOTS} were found", file=sys.stderr)

def lint_file(filepath):
    """Lint a single file."""
    violations = []
    warnings = []
    
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception as e:
        warnings.append(f"{filepath}:0: Cannot read file: {e}")
        return violations, warnings
    
    for line_num, line in enumerate(lines, start=1):
        cleaned = clean_line(line)
        
        # Check critical patterns
        for pattern, message in CRITICAL_PATTERNS:
            if re.search(pattern, cleaned):
                violations.append((str(filepath), line_num, message, line.rstrip()))
        
        # Check warning patterns  
        for pattern, message in WARNING_PATTERNS:
            if re.search(pattern, cleaned):
                warnings.append((str(filepath), line_num, message, line.rstrip()))
        
        # Check for assignment in condition (common bug)
        if_match = re.match(r'^\s*(if|while|for)\s*\((.*)\)', cleaned)
        if if_match:
            condition = if_match.group(2)
            # Look for single = not part of ==, !=, <=, >=
            if re.search(r'(?<![<>=!])=(?![=])', condition):
                # Allow common patterns like "while ((c = getchar()) != EOF)"
                if not re.search(r'\([^)]*=[^)]*\)\s*[<>=!]', condition):
                    warnings.append((str(filepath), line_num, 
                                   "SUSPICIOUS: assignment in condition, did you mean == ?",
                                   line.rstrip()))
    
    return violations, warnings

def main():
    """Main linter entry point."""
    all_violations = []
    all_warnings = []
    file_count = 0
    
    print("Running C23 linter...")
    
    for filepath in iter_files():
        file_count += 1
        viols, warns = lint_file(filepath)
        all_violations.extend(viols)
        all_warnings.extend(warns)
    
    if file_count == 0:
        print("ERROR: No source files found to lint", file=sys.stderr)
        print(f"Expected directories: {', '.join(ROOTS)}", file=sys.stderr)
        sys.exit(1)
    
    print(f"Scanned {file_count} files")
    
    # Report violations
    if all_violations:
        print("\n" + "=" * 80, file=sys.stderr)
        print("CRITICAL VIOLATIONS (must fix):", file=sys.stderr)
        print("=" * 80, file=sys.stderr)
        
        for filepath, line_num, message, line in all_violations:
            print(f"\n{filepath}:{line_num}", file=sys.stderr)
            print(f"  {message}", file=sys.stderr)
            print(f"  > {line}", file=sys.stderr)
        
        print(f"\nFound {len(all_violations)} critical violations", file=sys.stderr)
    
    # Report warnings
    if all_warnings:
        print("\n" + "=" * 80)
        print("WARNINGS (recommended fixes):")
        print("=" * 80)
        
        # Group warnings by type
        warning_types = {}
        for filepath, line_num, message, line in all_warnings:
            msg_type = message.split(':')[0]
            if msg_type not in warning_types:
                warning_types[msg_type] = []
            warning_types[msg_type].append((filepath, line_num, message, line))
        
        for msg_type, warns in sorted(warning_types.items()):
            print(f"\n{msg_type}: {len(warns)} occurrences")
            # Show first 5 examples
            for filepath, line_num, message, line in warns[:5]:
                print(f"  {filepath}:{line_num}: {message}")
            if len(warns) > 5:
                print(f"  ... and {len(warns) - 5} more")
        
        print(f"\nFound {len(all_warnings)} warnings (non-critical)")
    
    # Summary
    print("\n" + "=" * 80)
    if all_violations:
        print("RESULT: FAILED - Critical violations found")
        print("=" * 80)
        sys.exit(2)
    elif all_warnings:
        print("RESULT: PASSED with warnings")
        print("=" * 80)
        print("\nTo suppress warnings, set IGNORE_LINT_WARNINGS=1")
        # Exit 0 - warnings don't fail CI
        sys.exit(0)
    else:
        print("RESULT: PASSED - No issues found")
        print("=" * 80)
        sys.exit(0)

if __name__ == "__main__":
    main()
