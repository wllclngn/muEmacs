#!/usr/bin/env python3
"""
uep_build.py - μEmacs Universal Extension Builder

Automatically detects and compiles extensions from 20+ programming languages.
Called by μEmacs during extension loading when source is newer than .so.

Usage:
    python3 uep_build.py <extension_dir_or_file>

Exit codes:
    0 = Success (built or already up-to-date)
    1 = Build failed
    2 = No build method found

Supports 17 languages with native C FFI compatibility:
    - Custom build.py (user override, highest priority)
    - Makefile (universal fallback)
    - C, C++, Rust, Go, Zig, D, Nim, Swift, Crystal, V, Odin
    - Fortran, Pascal, Ada (with iso_c_binding / cdecl)
    - Assembly (nasm/as)

C23 extension API compatible.
"""

import sys
import os
import subprocess
import shutil
from pathlib import Path
from typing import Callable, List, Optional, Tuple

# Include path for μEmacs headers (checked in order)
INCLUDE_PATHS = [
    "/usr/local/include",
    "/usr/include",
    # Source tree paths (for development)
    os.path.expanduser("~/.config/muemacs/../../../PROGRAMMING/SYSTEM PROGRAMS/LINUX/μEmacs/include"),
]

# Try to find source tree include path dynamically
_script_dir = Path(__file__).parent.resolve()
_source_include = _script_dir.parent / "include"
if _source_include.exists():
    INCLUDE_PATHS.insert(0, str(_source_include))


def find_include_path() -> Optional[str]:
    """Find valid include path with uep/extension.h."""
    for inc in INCLUDE_PATHS:
        if (Path(inc) / "uep" / "extension.h").exists():
            return inc
    return None


def get_so_name(ext_dir: Path) -> str:
    """Derive .so filename from extension directory or source file."""
    # If it's a file (e.g., foo.c), use its base name
    if ext_dir.is_file():
        return ext_dir.stem + ".so"
    # Otherwise use directory name
    return ext_dir.name + ".so"


def get_newest_mtime(ext_dir: Path, patterns: List[str]) -> float:
    """Get the newest modification time among matching files."""
    newest = 0.0
    for pattern in patterns:
        for f in ext_dir.rglob(pattern) if ext_dir.is_dir() else [ext_dir]:
            try:
                mtime = f.stat().st_mtime
                if mtime > newest:
                    newest = mtime
            except OSError:
                pass
    return newest


def needs_rebuild(ext_dir: Path, so_path: Path, source_patterns: List[str]) -> bool:
    """Check if extension needs rebuilding (source newer than .so)."""
    if not so_path.exists():
        return True

    so_mtime = so_path.stat().st_mtime
    src_mtime = get_newest_mtime(ext_dir, source_patterns)

    return src_mtime > so_mtime


def run_cmd(cmd: List[str], cwd: Path, desc: str) -> int:
    """Run a build command with nice output."""
    print(f"[uep_build] {desc}: {' '.join(cmd[:3])}...")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[uep_build] FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    return result.returncode


# =============================================================================
# BUILD FUNCTIONS - One per language/build system
# =============================================================================

def build_custom(ext_dir: Path, so_name: str) -> int:
    """Run user's custom build.py script."""
    build_py = ext_dir / "build.py"
    return subprocess.run([sys.executable, str(build_py)], cwd=ext_dir).returncode


def build_make(ext_dir: Path, so_name: str) -> int:
    """Build using Makefile."""
    return run_cmd(["make"], ext_dir, "Running make")


def build_c(ext_dir: Path, so_name: str) -> int:
    """Build C extension with gcc."""
    if ext_dir.is_file():
        files = [ext_dir]
        work_dir = ext_dir.parent
        so_path = work_dir / so_name
    else:
        files = list(ext_dir.glob("*.c"))
        work_dir = ext_dir
        so_path = ext_dir / so_name

    inc = find_include_path()
    cmd = ["gcc", "-shared", "-fPIC", "-O2", "-o", str(so_path)]
    if inc:
        cmd.extend(["-I", inc])
    cmd.extend([str(f) for f in files])

    return run_cmd(cmd, work_dir, "Compiling C")


def build_cpp(ext_dir: Path, so_name: str) -> int:
    """Build C++ extension with g++."""
    files = list(ext_dir.glob("*.cpp")) + list(ext_dir.glob("*.cc")) + list(ext_dir.glob("*.cxx"))
    inc = find_include_path()
    cmd = ["g++", "-shared", "-fPIC", "-O2", "-o", str(ext_dir / so_name)]
    if inc:
        cmd.extend(["-I", inc])
    cmd.extend([str(f) for f in files])

    return run_cmd(cmd, ext_dir, "Compiling C++")


def build_cargo(ext_dir: Path, so_name: str) -> int:
    """Build Rust extension with cargo."""
    result = run_cmd(["cargo", "build", "--release"], ext_dir, "Building Rust")
    if result != 0:
        return result

    # Find and copy the .so from target/release/
    release_dir = ext_dir / "target" / "release"
    for so in release_dir.glob("lib*.so"):
        shutil.copy(so, ext_dir / so_name)
        print(f"[uep_build] Copied {so.name} -> {so_name}")
        return 0

    # Also check for .so without lib prefix
    for so in release_dir.glob("*.so"):
        if not so.name.startswith("lib"):
            shutil.copy(so, ext_dir / so_name)
            print(f"[uep_build] Copied {so.name} -> {so_name}")
            return 0

    print("[uep_build] WARNING: Cargo succeeded but no .so found", file=sys.stderr)
    return 1


def build_go(ext_dir: Path, so_name: str) -> int:
    """Build Go extension with go build."""
    cmd = ["go", "build", "-buildmode=c-shared", "-o", str(ext_dir / so_name), "."]
    return run_cmd(cmd, ext_dir, "Building Go")


def build_fortran(ext_dir: Path, so_name: str) -> int:
    """Build Fortran extension with gfortran."""
    files = list(ext_dir.glob("*.f90")) + list(ext_dir.glob("*.f95")) + list(ext_dir.glob("*.f"))
    cmd = ["gfortran", "-shared", "-fPIC", "-O2", "-o", str(ext_dir / so_name)]
    cmd.extend([str(f) for f in files])
    return run_cmd(cmd, ext_dir, "Compiling Fortran")




def build_fpc(ext_dir: Path, so_name: str) -> int:
    """Build Pascal extension with Free Pascal."""
    files = list(ext_dir.glob("*.pas")) + list(ext_dir.glob("*.pp"))
    if not files:
        return 2
    # FPC library compilation
    cmd = ["fpc", "-fPIC", "-Cg", "-o" + str(ext_dir / so_name), str(files[0])]
    return run_cmd(cmd, ext_dir, "Compiling Pascal")


def build_gprbuild(ext_dir: Path, so_name: str) -> int:
    """Build Ada extension with gprbuild."""
    gpr_files = list(ext_dir.glob("*.gpr"))
    if not gpr_files:
        return 2
    cmd = ["gprbuild", "-P", str(gpr_files[0])]
    return run_cmd(cmd, ext_dir, "Building Ada (gprbuild)")


def build_gnat(ext_dir: Path, so_name: str) -> int:
    """Build simple Ada extension with gnatmake."""
    files = list(ext_dir.glob("*.adb"))
    if not files:
        return 2
    cmd = ["gnatmake", "-shared", "-fPIC", "-o", str(ext_dir / so_name), str(files[0])]
    return run_cmd(cmd, ext_dir, "Compiling Ada")


def build_dub(ext_dir: Path, so_name: str) -> int:
    """Build D extension with dub."""
    result = run_cmd(["dub", "build", "--build=release"], ext_dir, "Building D (dub)")
    # TODO: Locate and copy the built .so
    return result


def build_d(ext_dir: Path, so_name: str) -> int:
    """Build simple D extension with ldc2 or dmd."""
    files = list(ext_dir.glob("*.d"))
    # Try ldc2 first (better codegen), fall back to dmd
    if shutil.which("ldc2"):
        cmd = ["ldc2", "--shared", "-O2", "-of=" + str(ext_dir / so_name)]
        cmd.extend([str(f) for f in files])
        return run_cmd(cmd, ext_dir, "Compiling D (ldc2)")
    else:
        cmd = ["dmd", "-shared", "-O", "-of=" + str(ext_dir / so_name)]
        cmd.extend([str(f) for f in files])
        return run_cmd(cmd, ext_dir, "Compiling D (dmd)")


def build_nimble(ext_dir: Path, so_name: str) -> int:
    """Build Nim extension with nimble."""
    return run_cmd(["nimble", "build", "-d:release"], ext_dir, "Building Nim (nimble)")


def build_nim(ext_dir: Path, so_name: str) -> int:
    """Build simple Nim extension with nim c."""
    files = list(ext_dir.glob("*.nim"))
    if not files:
        return 2
    cmd = ["nim", "c", "--app:lib", "-d:release", "-o:" + str(ext_dir / so_name), str(files[0])]
    return run_cmd(cmd, ext_dir, "Compiling Nim")




def build_zig(ext_dir: Path, so_name: str) -> int:
    """Build Zig extension."""
    return run_cmd(["zig", "build", "-Doptimize=ReleaseSafe"], ext_dir, "Building Zig")


def build_swift(ext_dir: Path, so_name: str) -> int:
    """Build Swift extension."""
    return run_cmd(["swift", "build", "-c", "release"], ext_dir, "Building Swift")


def build_crystal(ext_dir: Path, so_name: str) -> int:
    """Build Crystal extension."""
    files = list(ext_dir.glob("*.cr"))
    if not files:
        return 2
    cmd = ["crystal", "build", "--release", "--link-flags", "-shared",
           "-o", str(ext_dir / so_name), str(files[0])]
    return run_cmd(cmd, ext_dir, "Building Crystal")


def build_v(ext_dir: Path, so_name: str) -> int:
    """Build V extension."""
    files = list(ext_dir.glob("*.v"))
    if not files:
        return 2
    cmd = ["v", "-shared", "-prod", "-o", str(ext_dir / so_name), str(files[0])]
    return run_cmd(cmd, ext_dir, "Building V")


def build_odin(ext_dir: Path, so_name: str) -> int:
    """Build Odin extension."""
    cmd = ["odin", "build", ".", "-build-mode:shared", "-o:speed",
           "-out:" + str(ext_dir / so_name)]
    return run_cmd(cmd, ext_dir, "Building Odin")




def build_asm(ext_dir: Path, so_name: str) -> int:
    """Build Assembly extension."""
    asm_files = list(ext_dir.glob("*.asm"))
    s_files = list(ext_dir.glob("*.s"))

    obj_files = []

    # Assemble .asm files with nasm
    for f in asm_files:
        obj = f.with_suffix(".o")
        cmd = ["nasm", "-f", "elf64", "-o", str(obj), str(f)]
        if run_cmd(cmd, ext_dir, f"Assembling {f.name}") != 0:
            return 1
        obj_files.append(obj)

    # Assemble .s files with as
    for f in s_files:
        obj = f.with_suffix(".o")
        cmd = ["as", "-o", str(obj), str(f)]
        if run_cmd(cmd, ext_dir, f"Assembling {f.name}") != 0:
            return 1
        obj_files.append(obj)

    # Link into shared library
    if obj_files:
        cmd = ["ld", "-shared", "-o", str(ext_dir / so_name)]
        cmd.extend([str(o) for o in obj_files])
        return run_cmd(cmd, ext_dir, "Linking assembly")

    return 2


# =============================================================================
# LANGUAGE DETECTION TABLE
# =============================================================================

# Format: (name, detector, builder, source_patterns)
# detector: callable(Path) -> bool
# builder: callable(Path, str) -> int
# source_patterns: list of glob patterns for mtime checking

BuilderType = Callable[[Path, str], int]
DetectorType = Callable[[Path], bool]

LANGUAGES: List[Tuple[str, DetectorType, BuilderType, List[str]]] = [
    # Priority 1: User overrides (always available)
    ("build.py",      lambda d: (d / "build.py").exists(),               build_custom, ["build.py"]),
    ("Makefile",      lambda d: (d / "Makefile").exists(),               build_make,   ["Makefile", "*.c", "*.h"]),

    # Priority 2: Project-based (has manifest file)
    ("Rust",          lambda d: (d / "Cargo.toml").exists(),             build_cargo,  ["Cargo.toml", "src/**/*.rs", "*.rs"]),
    ("Go",            lambda d: (d / "go.mod").exists(),                 build_go,     ["go.mod", "*.go", "**/*.go"]),
    ("D/Dub",         lambda d: (d / "dub.json").exists() or (d / "dub.sdl").exists(), build_dub, ["dub.json", "dub.sdl", "*.d"]),
    ("Nim/Nimble",    lambda d: any(d.glob("*.nimble")),                 build_nimble, ["*.nimble", "*.nim"]),
    ("Zig",           lambda d: (d / "build.zig").exists(),              build_zig,    ["build.zig", "*.zig"]),
    ("Swift",         lambda d: (d / "Package.swift").exists(),          build_swift,  ["Package.swift", "**/*.swift"]),
    ("Crystal",       lambda d: (d / "shard.yml").exists(),              build_crystal, ["shard.yml", "*.cr"]),
    ("V",             lambda d: (d / "v.mod").exists(),                  build_v,      ["v.mod", "*.v"]),
    ("Ada/GPR",       lambda d: any(d.glob("*.gpr")),                    build_gprbuild, ["*.gpr", "*.adb", "*.ads"]),

    # Priority 3: Single-file detection (no manifest)
    ("C",             lambda d: any(d.glob("*.c")),                      build_c,      ["*.c", "*.h"]),
    ("C++",           lambda d: any(d.glob("*.cpp")) or any(d.glob("*.cc")) or any(d.glob("*.cxx")), build_cpp, ["*.cpp", "*.cc", "*.cxx", "*.hpp", "*.h"]),
    ("Fortran",       lambda d: any(d.glob("*.f90")) or any(d.glob("*.f95")) or any(d.glob("*.f")), build_fortran, ["*.f90", "*.f95", "*.f"]),
    ("Pascal",        lambda d: any(d.glob("*.pas")) or any(d.glob("*.pp")), build_fpc, ["*.pas", "*.pp"]),
    ("Ada",           lambda d: any(d.glob("*.adb")),                    build_gnat,   ["*.adb", "*.ads"]),
    ("D",             lambda d: any(d.glob("*.d")),                      build_d,      ["*.d"]),
    ("Nim",           lambda d: any(d.glob("*.nim")),                    build_nim,    ["*.nim"]),
    ("Odin",          lambda d: any(d.glob("*.odin")),                   build_odin,   ["*.odin"]),
    ("Assembly",      lambda d: any(d.glob("*.asm")) or any(d.glob("*.s")), build_asm, ["*.asm", "*.s"]),

    # NOTE: Removed languages with problematic FFI for UEP callbacks:
    # - Haskell (GC + lazy evaluation breaks callback model)
    # - OCaml (requires runtime locks in every callback)
    # - Scheme (Chicken could work but too niche)
    # Users needing these can provide their own build.py
]


def detect_and_build(ext_path: Path) -> int:
    """
    Detect language and build extension.

    Args:
        ext_path: Path to extension directory or single source file

    Returns:
        Exit code (0=success, 1=failed, 2=no method)
    """
    # Handle single file case (e.g., foo.c)
    if ext_path.is_file():
        ext_dir = ext_path.parent
        so_name = ext_path.stem + ".so"
        so_path = ext_dir / so_name

        # Check if rebuild needed
        if so_path.exists() and so_path.stat().st_mtime > ext_path.stat().st_mtime:
            print(f"[uep_build] {so_name} is up-to-date")
            return 0

        # Detect by file extension
        suffix = ext_path.suffix.lower()
        if suffix == ".c":
            return build_c(ext_path, so_name)
        elif suffix in (".cpp", ".cc", ".cxx"):
            return build_cpp(ext_dir, so_name)
        # Add more single-file cases as needed

        print(f"[uep_build] WARNING: Unknown file type: {ext_path}", file=sys.stderr)
        return 2

    # Handle directory case
    ext_dir = ext_path
    so_name = get_so_name(ext_dir)
    so_path = ext_dir / so_name

    # Try each detector in order
    for lang_name, detector, builder, patterns in LANGUAGES:
        if detector(ext_dir):
            print(f"[uep_build] Detected: {lang_name} in {ext_dir.name}")

            # Check if rebuild needed
            if not needs_rebuild(ext_dir, so_path, patterns):
                print(f"[uep_build] {so_name} is up-to-date")
                return 0

            # Build it
            return builder(ext_dir, so_name)

    print(f"[uep_build] WARNING: No build method for {ext_dir}", file=sys.stderr)
    return 2


def main() -> int:
    """Main entry point."""
    if len(sys.argv) < 2:
        print("Usage: uep_build.py <extension_dir_or_file>", file=sys.stderr)
        print("       uep_build.py --help", file=sys.stderr)
        return 1

    if sys.argv[1] in ("--help", "-h"):
        print(__doc__)
        return 0

    ext_path = Path(sys.argv[1]).resolve()

    if not ext_path.exists():
        print(f"[uep_build] ERROR: Path does not exist: {ext_path}", file=sys.stderr)
        return 1

    return detect_and_build(ext_path)


if __name__ == "__main__":
    sys.exit(main())
