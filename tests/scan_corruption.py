#!/usr/bin/env python3
"""
Comprehensive corruption scanner for μEmacs codebase.

Checks for damage patterns from the bad uppercase script:
1. Recursive function calls (like the writeout bug)
2. strcmp mismatches (uppercase vs lowercase prompts)
3. Duplicate variable declarations in nested scopes
4. Functions with missing implementations
5. Nested function definitions
6. Suspicious brace imbalances
"""

import re
from pathlib import Path
from collections import defaultdict

class CorruptionScanner:
    def __init__(self):
        self.issues = []
        self.stats = defaultdict(int)
        
    def scan_file(self, filepath):
        """Scan a single C file for corruption patterns."""
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                lines = content.split('\n')
        except Exception as e:
            return
        
        # Skip test files
        if '/tests/' in str(filepath) or '\\tests\\' in str(filepath):
            return
            
        self.check_recursive_calls(filepath, lines, content)
        self.check_strcmp_mismatches(filepath, lines)
        self.check_duplicate_variables(filepath, lines, content)
        self.check_function_structure(filepath, lines, content)
        self.check_brace_balance(filepath, lines)
        
    def check_recursive_calls(self, filepath, lines, content):
        """Find functions that call themselves (like writeout did)."""
        # Extract all function definitions
        func_pattern = r'^(?:static\s+)?(?:int|void|bool|char\s*\*|struct\s+\w+\s*\*)\s+(\w+)\s*\('
        functions = {}
        
        for i, line in enumerate(lines, 1):
            match = re.match(func_pattern, line.strip())
            if match:
                func_name = match.group(1)
                functions[func_name] = i
        
        # Check each function for self-calls
        for func_name, start_line in functions.items():
            # Find function body
            brace_count = 0
            in_function = False
            func_lines = []
            
            for i in range(start_line - 1, len(lines)):
                line = lines[i]
                if '{' in line:
                    in_function = True
                if in_function:
                    brace_count += line.count('{')
                    brace_count -= line.count('}')
                    func_lines.append((i + 1, line))
                    
                    if brace_count == 0:
                        break
            
            # Check for self-calls in function body
            for line_num, line in func_lines:
                # Skip comments
                if '//' in line:
                    line = line[:line.index('//')]
                if '/*' in line:
                    continue
                    
                # Look for function calls
                if re.search(r'\b' + func_name + r'\s*\(', line):
                    # Make sure it's not the function definition itself
                    if line_num != start_line:
                        self.issues.append({
                            'file': str(filepath),
                            'line': line_num,
                            'type': 'RECURSIVE_CALL',
                            'severity': 'CRITICAL',
                            'function': func_name,
                            'detail': f"Function {func_name} calls itself",
                            'context': line.strip()
                        })
                        self.stats['recursive_calls'] += 1
    
    def check_strcmp_mismatches(self, filepath, lines):
        """Find strcmp calls that won't match due to case differences."""
        # Collect all strcmp/strncmp calls
        strcmp_calls = {}
        for i, line in enumerate(lines, 1):
            matches = re.finditer(r'str(?:n)?cmp\([^,]+,\s*"([^"]+)"', line)
            for match in matches:
                string = match.group(1)
                if string not in strcmp_calls:
                    strcmp_calls[string] = []
                strcmp_calls[string].append(i)
        
        # Collect all mlreply/mlwrite/getstring calls
        actual_prompts = {}
        for i, line in enumerate(lines, 1):
            matches = re.finditer(r'ml(?:reply|write|prompt)\s*\(\s*"([^"]+)"', line)
            for match in matches:
                string = match.group(1)
                if string not in actual_prompts:
                    actual_prompts[string] = []
                actual_prompts[string].append(i)
        
        # Find mismatches - strcmp looking for lowercase when uppercase exists
        for strcmp_str in strcmp_calls:
            # Check if there's an uppercase version that won't match
            upper_version = strcmp_str.upper()
            if strcmp_str != upper_version and upper_version in actual_prompts:
                self.issues.append({
                    'file': str(filepath),
                    'line': strcmp_calls[strcmp_str][0],
                    'type': 'STRCMP_MISMATCH',
                    'severity': 'HIGH',
                    'detail': f'strcmp checks for "{strcmp_str}" but actual prompt is "{upper_version}"',
                    'context': f'Lines with strcmp: {strcmp_calls[strcmp_str]}, prompts at: {actual_prompts[upper_version]}'
                })
                self.stats['strcmp_mismatches'] += 1
    
    def check_duplicate_variables(self, filepath, lines, content):
        """Find duplicate variable declarations in nested scopes (like writeout had)."""
        for i, line in enumerate(lines, 1):
            # Look for variable declarations in nested braces
            if re.search(r'^\s+\{\s*$', line):  # Opening brace
                # Check next few lines for variable declarations
                scope_vars = set()
                brace_count = 1
                
                for j in range(i, min(i + 50, len(lines))):
                    check_line = lines[j]
                    brace_count += check_line.count('{')
                    brace_count -= check_line.count('}')
                    
                    if brace_count == 0:
                        break
                    
                    # Look for variable declarations
                    var_match = re.match(r'\s+(?:int|char|struct\s+\w+\s*\*?)\s+(\w+)\s*[;=]', check_line)
                    if var_match:
                        var_name = var_match.group(1)
                        if var_name in scope_vars:
                            self.issues.append({
                                'file': str(filepath),
                                'line': j + 1,
                                'type': 'DUPLICATE_VARIABLE',
                                'severity': 'MEDIUM',
                                'detail': f'Variable "{var_name}" redeclared in nested scope',
                                'context': check_line.strip()
                            })
                            self.stats['duplicate_vars'] += 1
                        scope_vars.add(var_name)
    
    def check_function_structure(self, filepath, lines, content):
        """Check for functions with suspicious structure."""
        func_pattern = r'^(?:static\s+)?(?:int|void|bool|char\s*\*|struct\s+\w+\s*\*)\s+(\w+)\s*\('
        
        for i, line in enumerate(lines, 1):
            if re.match(func_pattern, line.strip()):
                func_name = re.match(func_pattern, line.strip()).group(1)
                
                # Find function body
                brace_count = 0
                in_function = False
                has_opening_brace = False
                nested_braces = 0
                
                for j in range(i - 1, min(i + 200, len(lines))):
                    check_line = lines[j]
                    
                    if '{' in check_line:
                        has_opening_brace = True
                        in_function = True
                    
                    if in_function:
                        brace_count += check_line.count('{')
                        brace_count -= check_line.count('}')
                        
                        # Check for immediate nested scope (suspicious like writeout)
                        if j == i and check_line.strip() == '{':
                            next_line = lines[j + 1] if j + 1 < len(lines) else ''
                            if next_line.strip() == '{':
                                self.issues.append({
                                    'file': str(filepath),
                                    'line': i,
                                    'type': 'SUSPICIOUS_NESTING',
                                    'severity': 'MEDIUM',
                                    'detail': f'Function {func_name} has immediate nested brace block',
                                    'context': line.strip()
                                })
                                self.stats['suspicious_nesting'] += 1
                        
                        if brace_count == 0:
                            break
    
    def check_brace_balance(self, filepath, lines):
        """Check for unbalanced braces in functions."""
        open_braces = 0
        
        for i, line in enumerate(lines, 1):
            # Skip strings
            cleaned = re.sub(r'"[^"]*"', '', line)
            # Skip comments
            cleaned = re.sub(r'//.*$', '', cleaned)
            
            open_braces += cleaned.count('{')
            open_braces -= cleaned.count('}')
            
            if open_braces < 0:
                self.issues.append({
                    'file': str(filepath),
                    'line': i,
                    'type': 'BRACE_IMBALANCE',
                    'severity': 'HIGH',
                    'detail': 'More closing braces than opening braces',
                    'context': line.strip()
                })
                self.stats['brace_imbalance'] += 1
                open_braces = 0  # Reset to continue checking
        
        if open_braces > 0:
            self.issues.append({
                'file': str(filepath),
                'line': len(lines),
                'type': 'BRACE_IMBALANCE',
                'severity': 'HIGH',
                'detail': f'{open_braces} unclosed braces in file',
                'context': f'File ends with {open_braces} unclosed braces'
            })
            self.stats['brace_imbalance'] += 1

    def print_report(self):
        """Print comprehensive report."""
        print("=" * 80)
        print("μEMACS CORRUPTION ANALYSIS REPORT")
        print("=" * 80)
        print()
        
        # Group by severity
        critical = [i for i in self.issues if i['severity'] == 'CRITICAL']
        high = [i for i in self.issues if i['severity'] == 'HIGH']
        medium = [i for i in self.issues if i['severity'] == 'MEDIUM']
        
        if critical:
            print(f"\n{'='*80}")
            print(f"CRITICAL ISSUES ({len(critical)}) - MUST FIX IMMEDIATELY")
            print('='*80)
            for issue in critical:
                print(f"\n{issue['file']}:{issue['line']}")
                print(f"  Type: {issue['type']}")
                print(f"  Detail: {issue['detail']}")
                print(f"  Context: {issue['context']}")
        
        if high:
            print(f"\n{'='*80}")
            print(f"HIGH PRIORITY ISSUES ({len(high)}) - FIX SOON")
            print('='*80)
            for issue in high:
                print(f"\n{issue['file']}:{issue['line']}")
                print(f"  Type: {issue['type']}")
                print(f"  Detail: {issue['detail']}")
                if 'context' in issue:
                    print(f"  Context: {issue['context']}")
        
        if medium:
            print(f"\n{'='*80}")
            print(f"MEDIUM PRIORITY ISSUES ({len(medium)}) - REVIEW")
            print('='*80)
            for issue in medium:
                print(f"\n{issue['file']}:{issue['line']}")
                print(f"  Type: {issue['type']}")
                print(f"  Detail: {issue['detail']}")
        
        print(f"\n{'='*80}")
        print("STATISTICS")
        print('='*80)
        for stat_type, count in sorted(self.stats.items()):
            print(f"  {stat_type}: {count}")
        
        print(f"\n{'='*80}")
        print(f"TOTAL ISSUES FOUND: {len(self.issues)}")
        print('='*80)
        
        if not self.issues:
            print("\n✓ NO CORRUPTION DETECTED - CODEBASE LOOKS CLEAN!")

def main():
    scanner = CorruptionScanner()
    
    src_dir = Path('/home/claude/src')
    include_dir = Path('/home/claude/include')
    
    print("Scanning codebase for corruption patterns...")
    print()
    
    file_count = 0
    for directory in [src_dir, include_dir]:
        if directory.exists():
            for filepath in directory.rglob('*.c'):
                scanner.scan_file(filepath)
                file_count += 1
            for filepath in directory.rglob('*.h'):
                scanner.scan_file(filepath)
                file_count += 1
    
    print(f"Scanned {file_count} files\n")
    scanner.print_report()

if __name__ == '__main__':
    main()
