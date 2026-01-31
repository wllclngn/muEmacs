#!/usr/bin/env python3
"""
uep_build.py - μEmacs Universal Extension Builder

Automatically detects and compiles extensions from 20+ programming languages.
Called by μEmacs during extension loading when source is newer than .so.

Usage:
    python3 uep_build.py <extension_dir_or_file>
    python3 uep_build.py --install [--force]     # Build all & install to ~/.config/muemacs/extensions
    python3 uep_build.py --sync                  # Symlink source extensions to install dir

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

# Debug logging (enabled with --debug-log)
DEBUG_LOG = False

def debug(msg: str) -> None:
    """Print debug message if DEBUG_LOG is enabled."""
    if DEBUG_LOG:
        print(f"[uep_build:DEBUG] {msg}")

# Install directory for extensions
INSTALL_DIR = Path(os.path.expanduser("~/.config/muemacs/extensions"))

# Source extensions directory (sibling repo)
SOURCE_EXTENSIONS_DIR = Path(__file__).parent.parent.parent / "μEmacs-extensions" / "extensions"

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


def get_api_version() -> int:
    """Read MUEMACS_API_VERSION from extension.h header."""
    inc = find_include_path()
    if not inc:
        return 4  # Default fallback
    header = Path(inc) / "uep" / "extension.h"
    try:
        content = header.read_text()
        for line in content.splitlines():
            if line.startswith("#define MUEMACS_API_VERSION "):
                parts = line.split()
                if len(parts) >= 3:
                    return int(parts[2])
    except (OSError, ValueError):
        pass
    return 4  # Default fallback


def get_build_env() -> dict:
    """Get environment with API version set for subprocesses."""
    env = os.environ.copy()
    env["MUEMACS_API_VERSION"] = str(get_api_version())
    return env


def get_api_header_mtime() -> float:
    """Get mtime of extension_api.h - forces rebuild when API changes."""
    inc = find_include_path()
    if not inc:
        return 0.0
    api_header = Path(inc) / "uep" / "extension_api.h"
    try:
        return api_header.stat().st_mtime
    except OSError:
        return 0.0


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


def needs_rebuild(ext_dir: Path, so_path: Path, source_patterns: List[str]) -> tuple[bool, bool]:
    """Check if extension needs rebuilding.

    Returns:
        (needs_rebuild, api_changed) tuple
        - needs_rebuild: True if rebuild is needed
        - api_changed: True if API header change triggered rebuild (forces cache clear)
    """
    if not so_path.exists():
        debug(f"No .so exists at {so_path}")
        # No .so exists - must rebuild. Assume API might have changed since we can't compare.
        # This forces cargo clean for Rust, etc.
        return (True, True)

    so_mtime = so_path.stat().st_mtime
    src_mtime = get_newest_mtime(ext_dir, source_patterns)
    api_mtime = get_api_header_mtime()

    debug(f"Timestamps: so={so_mtime:.0f} src={src_mtime:.0f} api={api_mtime:.0f}")

    # Rebuild if source is newer
    if src_mtime > so_mtime:
        debug(f"Source newer than .so by {src_mtime - so_mtime:.0f}s")
        return (True, False)
    # Rebuild if API header changed (need to clear build caches)
    if api_mtime > so_mtime:
        print(f"[uep_build] API header changed - forcing rebuild")
        debug(f"API header newer than .so by {api_mtime - so_mtime:.0f}s")
        return (True, True)
    return (False, False)


def run_cmd(cmd: List[str], cwd: Path, desc: str, env: dict = None) -> int:
    """Run a build command with nice output."""
    if env is None:
        env = get_build_env()
    debug(f"Running: {' '.join(cmd)}")
    debug(f"  cwd: {cwd}")
    print(f"[uep_build] {desc}: {' '.join(cmd[:3])}...")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        print(f"[uep_build] FAILED: {result.stderr or result.stdout}", file=sys.stderr)
    else:
        debug(f"Command succeeded")
    return result.returncode


def deep_clean_extension(ext_dir: Path, lang_name: str, so_name: str) -> None:
    """
    Deep clean build artifacts for a language when API header changes.
    This is necessary because ABI-breaking changes require full rebuilds,
    not just incremental recompilation.
    """
    print(f"[uep_build] Deep cleaning {lang_name} build artifacts...")

    so_path = ext_dir / so_name
    if so_path.exists():
        so_path.unlink()
        print(f"[uep_build]   Deleted {so_name}")

    if lang_name == "Rust":
        target_dir = ext_dir / "target"
        if target_dir.exists():
            shutil.rmtree(target_dir)
            print(f"[uep_build]   Deleted target/")

    elif lang_name == "Go":
        # Delete the .h file generated by -buildmode=c-shared
        h_file = ext_dir / so_name.replace(".so", ".h")
        if h_file.exists():
            h_file.unlink()
            print(f"[uep_build]   Deleted {h_file.name}")
        # Also run go clean to clear module cache for this package
        subprocess.run(["go", "clean", "-cache"], cwd=ext_dir, capture_output=True)
        print(f"[uep_build]   Cleared Go build cache")

    elif lang_name == "Zig":
        for cache_dir in ["zig-out", ".zig-cache"]:
            cache_path = ext_dir / cache_dir
            if cache_path.exists():
                shutil.rmtree(cache_path)
                print(f"[uep_build]   Deleted {cache_dir}/")

    elif lang_name == "Crystal":
        # Crystal caches in .crystal/ and lib/
        for cache_dir in [".crystal", "lib"]:
            cache_path = ext_dir / cache_dir
            if cache_path.exists():
                shutil.rmtree(cache_path)
                print(f"[uep_build]   Deleted {cache_dir}/")

    elif lang_name in ("D/Dub", "D"):
        # Dub uses .dub/ for cache
        dub_cache = ext_dir / ".dub"
        if dub_cache.exists():
            shutil.rmtree(dub_cache)
            print(f"[uep_build]   Deleted .dub/")

    elif lang_name in ("Nim/Nimble", "Nim"):
        # Nim caches in nimcache/
        nimcache = ext_dir / "nimcache"
        if nimcache.exists():
            shutil.rmtree(nimcache)
            print(f"[uep_build]   Deleted nimcache/")

    elif lang_name == "Swift":
        # Swift uses .build/
        build_dir = ext_dir / ".build"
        if build_dir.exists():
            shutil.rmtree(build_dir)
            print(f"[uep_build]   Deleted .build/")

    elif lang_name in ("Ada/GPR", "Ada"):
        # GPRbuild uses obj/
        obj_dir = ext_dir / "obj"
        if obj_dir.exists():
            shutil.rmtree(obj_dir)
            print(f"[uep_build]   Deleted obj/")

    elif lang_name == "Odin":
        # Odin doesn't have a standard cache, but delete .o files
        for o_file in ext_dir.glob("*.o"):
            o_file.unlink()

    elif lang_name in ("C", "C++", "Fortran", "Pascal", "Assembly"):
        # Delete all .o files for compiled languages
        for o_file in ext_dir.glob("*.o"):
            o_file.unlink()
            print(f"[uep_build]   Deleted {o_file.name}")


# =============================================================================
# BUILD FUNCTIONS - One per language/build system
# =============================================================================

def build_custom(ext_dir: Path, so_name: str) -> int:
    """Run user's custom build.py script."""
    build_py = ext_dir / "build.py"
    return subprocess.run([sys.executable, str(build_py)], cwd=ext_dir, env=get_build_env()).returncode


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
    api_ver = get_api_version()
    cmd = ["gcc", "-shared", "-fPIC", "-O2", f"-DMUEMACS_API_VERSION_BUILD={api_ver}", "-o", str(so_path)]
    if inc:
        cmd.extend(["-I", inc])
    cmd.extend([str(f) for f in files])

    return run_cmd(cmd, work_dir, "Compiling C")


def build_cpp(ext_dir: Path, so_name: str) -> int:
    """Build C++ extension with g++."""
    files = list(ext_dir.glob("*.cpp")) + list(ext_dir.glob("*.cc")) + list(ext_dir.glob("*.cxx"))
    inc = find_include_path()
    api_ver = get_api_version()
    cmd = ["g++", "-shared", "-fPIC", "-O2", f"-DMUEMACS_API_VERSION_BUILD={api_ver}", "-o", str(ext_dir / so_name)]
    if inc:
        cmd.extend(["-I", inc])
    cmd.extend([str(f) for f in files])

    return run_cmd(cmd, ext_dir, "Compiling C++")


def build_cargo(ext_dir: Path, so_name: str) -> int:
    """Build Rust extension with cargo."""
    # Note: deep_clean_extension() handles deleting target/ when API changes
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
    api_ver = get_api_version()
    result = run_cmd(["zig", "build", "-Doptimize=ReleaseSafe", f"-Dapi_version={api_ver}"], ext_dir, "Building Zig")
    if result != 0:
        return result

    # Zig outputs to zig-out/lib/ - copy to extension root
    zig_out = ext_dir / "zig-out" / "lib"
    if zig_out.exists():
        for so in zig_out.glob("*.so"):
            dest = ext_dir / so_name
            shutil.copy(so, dest)
            print(f"[uep_build] Copied {so.name} -> {so_name}")
            return 0

    print("[uep_build] WARNING: Zig build succeeded but no .so found in zig-out/lib/", file=sys.stderr)
    return 1


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

        # Check if rebuild needed (source mtime OR api header mtime)
        if so_path.exists():
            so_mtime = so_path.stat().st_mtime
            src_mtime = ext_path.stat().st_mtime
            api_mtime = get_api_header_mtime()
            if src_mtime <= so_mtime and api_mtime <= so_mtime:
                print(f"[uep_build] {so_name} is up-to-date")
                return 0
            if api_mtime > so_mtime:
                print(f"[uep_build] API header changed - forcing rebuild")

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
            rebuild_needed, api_changed = needs_rebuild(ext_dir, so_path, patterns)
            if not rebuild_needed:
                print(f"[uep_build] {so_name} is up-to-date")
                return 0

            # If API header changed, do a deep clean to purge all cached artifacts
            # This is critical for ABI-breaking changes to struct muemacs_api
            if api_changed:
                deep_clean_extension(ext_dir, lang_name, so_name)

            # Build it
            return builder(ext_dir, so_name)

    print(f"[uep_build] WARNING: No build method for {ext_dir}", file=sys.stderr)
    return 2


# =============================================================================
# INSTALL / SYNC COMMANDS
# =============================================================================

def get_extension_dirs(source_dir: Path) -> List[Path]:
    """Find all extension directories (those with buildable source)."""
    ext_dirs = []
    if not source_dir.exists():
        return ext_dirs

    for d in sorted(source_dir.iterdir()):
        if not d.is_dir():
            continue
        if d.name.startswith((".", "_")):
            continue
        # Check if it has any buildable source
        for _, detector, _, _ in LANGUAGES:
            if detector(d):
                ext_dirs.append(d)
                break
    return ext_dirs


def install_extensions(force: bool = False) -> int:
    """
    Build all extensions from source and install to ~/.config/muemacs/extensions.

    This ensures installed extensions are always in sync with source.
    Use --force to rebuild even if up-to-date.
    """
    if not SOURCE_EXTENSIONS_DIR.exists():
        print(f"[uep_build] ERROR: Source extensions not found: {SOURCE_EXTENSIONS_DIR}", file=sys.stderr)
        print(f"[uep_build] Expected sibling directory: μEmacs-extensions/extensions/", file=sys.stderr)
        return 1

    INSTALL_DIR.mkdir(parents=True, exist_ok=True)

    ext_dirs = get_extension_dirs(SOURCE_EXTENSIONS_DIR)
    if not ext_dirs:
        print(f"[uep_build] No extensions found in {SOURCE_EXTENSIONS_DIR}")
        return 0

    print(f"[uep_build] Installing {len(ext_dirs)} extension(s) to {INSTALL_DIR}")
    print(f"[uep_build] Source: {SOURCE_EXTENSIONS_DIR}")
    print()

    failed = 0
    for ext_dir in ext_dirs:
        ext_name = ext_dir.name
        so_name = ext_name + ".so"
        src_so = ext_dir / so_name
        dst_dir = INSTALL_DIR / ext_name
        dst_so = dst_dir / so_name

        # Check if rebuild/reinstall needed
        needs_build = force or not src_so.exists()
        if not needs_build and src_so.exists() and dst_so.exists():
            src_mtime = src_so.stat().st_mtime
            dst_mtime = dst_so.stat().st_mtime
            api_mtime = get_api_header_mtime()
            if src_mtime <= dst_mtime and api_mtime <= dst_mtime:
                print(f"  [{ext_name}] Up-to-date")
                continue

        # Build the extension
        print(f"  [{ext_name}] Building...")
        result = detect_and_build(ext_dir)
        if result != 0:
            print(f"  [{ext_name}] FAILED")
            failed += 1
            continue

        # Copy to install dir
        if not src_so.exists():
            print(f"  [{ext_name}] WARNING: No .so after build")
            failed += 1
            continue

        dst_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_so, dst_so)
        print(f"  [{ext_name}] Installed -> {dst_so}")

    print()
    print(f"[uep_build] Done: {len(ext_dirs) - failed}/{len(ext_dirs)} installed")
    return 1 if failed > 0 else 0


def sync_extensions() -> int:
    """
    Create symlinks from install dir to source extensions AND rebuild stale ones.

    This is the RECOMMENDED approach for development:
    - Extensions are always loaded from source
    - Automatically rebuilds when source or API header changes
    - No copy/sync issues ever
    """
    if not SOURCE_EXTENSIONS_DIR.exists():
        print(f"[uep_build] ERROR: Source extensions not found: {SOURCE_EXTENSIONS_DIR}", file=sys.stderr)
        return 1

    INSTALL_DIR.mkdir(parents=True, exist_ok=True)

    ext_dirs = get_extension_dirs(SOURCE_EXTENSIONS_DIR)
    if not ext_dirs:
        print(f"[uep_build] No extensions found in {SOURCE_EXTENSIONS_DIR}")
        return 0

    print(f"[uep_build] Syncing {len(ext_dirs)} extension(s)")
    print(f"[uep_build] Source: {SOURCE_EXTENSIONS_DIR}")
    print(f"[uep_build] Target: {INSTALL_DIR}")
    print()

    failed = 0
    for ext_dir in ext_dirs:
        ext_name = ext_dir.name
        link_path = INSTALL_DIR / ext_name

        # Remove existing (file, dir, or symlink) and replace with symlink
        if link_path.exists() or link_path.is_symlink():
            if link_path.is_symlink():
                # Already a symlink - check if pointing to right place
                if link_path.resolve() == ext_dir.resolve():
                    pass  # Good symlink, keep it
                else:
                    link_path.unlink()
                    link_path.symlink_to(ext_dir)
                    debug(f"[{ext_name}] Fixed symlink")
            elif link_path.is_dir():
                shutil.rmtree(link_path)
                link_path.symlink_to(ext_dir)
                print(f"  [{ext_name}] Replaced dir with symlink")
            else:
                link_path.unlink()
                link_path.symlink_to(ext_dir)
        else:
            link_path.symlink_to(ext_dir)
            debug(f"[{ext_name}] Created symlink")

        # NOW rebuild if needed (source or API header changed)
        result = detect_and_build(ext_dir)
        if result == 0:
            print(f"  [{ext_name}] OK")
        elif result == 2:
            print(f"  [{ext_name}] Skipped (no build method)")
        else:
            print(f"  [{ext_name}] FAILED")
            failed += 1

    print()
    if failed:
        print(f"[uep_build] Synced with {failed} failure(s)")
        return 1
    print(f"[uep_build] Synced {len(ext_dirs)} extensions")
    return 0


def main() -> int:
    """Main entry point.

    Supports batch mode: python3 uep_build.py path1 path2 path3 ...
    Returns 0 if ALL builds succeed, 1 if ANY fail.
    """
    global DEBUG_LOG

    # Handle --debug-log flag (can appear anywhere)
    if "--debug-log" in sys.argv:
        DEBUG_LOG = True
        sys.argv.remove("--debug-log")
        debug("Debug logging enabled")

    if len(sys.argv) < 2:
        print("Usage: uep_build.py <extension_dir_or_file> [more paths...]", file=sys.stderr)
        print("       uep_build.py --install [--force] [--debug-log]", file=sys.stderr)
        print("       uep_build.py --sync [--debug-log]", file=sys.stderr)
        print("       uep_build.py --help", file=sys.stderr)
        return 1

    if sys.argv[1] in ("--help", "-h"):
        print(__doc__)
        return 0

    if sys.argv[1] == "--install":
        force = "--force" in sys.argv
        return install_extensions(force)

    if sys.argv[1] == "--sync":
        return sync_extensions()

    # Batch mode: process all provided paths
    paths = sys.argv[1:]
    failed = 0
    total = len(paths)

    if total > 1:
        print(f"[uep_build] Batch building {total} extension(s)")

    for path_str in paths:
        ext_path = Path(path_str).resolve()

        if not ext_path.exists():
            print(f"[uep_build] ERROR: Path does not exist: {ext_path}", file=sys.stderr)
            failed += 1
            continue

        result = detect_and_build(ext_path)
        if result != 0:
            failed += 1

    if total > 1:
        print(f"[uep_build] Batch complete: {total - failed}/{total} succeeded")

    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
