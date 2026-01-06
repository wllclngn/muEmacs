#!/usr/bin/env python3
"""
μEmacs installer

Builds and installs μEmacs (micro Emacs).

Usage:
    ./install.py              # Build and install (default)
    ./install.py --debug      # Debug build
    ./install.py --debug-log  # Debug build with logging enabled
    ./install.py --prefix /usr # Install to /usr instead of /usr/local
    ./install.py build        # Build only, don't install
    ./install.py clean        # Clean build directory
    ./install.py uninstall    # Remove installed files
    ./install.py test         # Run tests
"""

import argparse
import os
import sys
import shutil
import subprocess
from pathlib import Path

def run(cmd, check=True, capture=False, sudo=False, cwd=None):
    """Run a command, optionally with sudo."""
    if sudo:
        cmd = ["sudo"] + cmd
    if capture:
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
        return result.returncode, result.stdout, result.stderr
    else:
        result = subprocess.run(cmd, cwd=cwd)
        return result.returncode, None, None

def check_cmake():
    """Check if cmake is available."""
    ret, _, _ = run(["which", "cmake"], capture=True)
    return ret == 0

def check_compiler():
    """Check if a C compiler is available."""
    for compiler in ["gcc", "clang"]:
        ret, _, _ = run(["which", compiler], capture=True)
        if ret == 0:
            return True
    return False

def cmd_build(args, source_dir):
    """Build μEmacs."""
    build_dir = source_dir / "build"

    # Configure
    cmake_args = ["cmake", "-S", str(source_dir), "-B", str(build_dir)]

    if args.debug or args.debug_log:
        cmake_args.append("-DCMAKE_BUILD_TYPE=Debug")
    else:
        cmake_args.append("-DCMAKE_BUILD_TYPE=Release")

    if args.debug_log:
        cmake_args.append("-DUEMACS_DEBUG_LOG=ON")

    if args.prefix:
        cmake_args.append(f"-DCMAKE_INSTALL_PREFIX={args.prefix}")

    print("Configuring...")
    ret, _, stderr = run(cmake_args, capture=True)
    if ret != 0:
        print(f"ERROR: cmake configure failed!")
        print(stderr)
        return False

    # Build
    import multiprocessing
    jobs = multiprocessing.cpu_count()

    print(f"Building (using {jobs} jobs)...")
    ret, _, stderr = run(["cmake", "--build", str(build_dir), f"-j{jobs}"], capture=True)
    if ret != 0:
        print(f"ERROR: Build failed!")
        print(stderr)
        return False

    # Create symlink for ASCII-friendly name
    bin_dir = build_dir / "bin"
    if bin_dir.exists():
        symlink = bin_dir / "muEmacs"
        target = bin_dir / "μEmacs"
        if target.exists() and not symlink.exists():
            try:
                symlink.symlink_to("μEmacs")
            except:
                pass

    print("  OK: Build complete")
    return True

def cmd_install(args, source_dir):
    """Build and install μEmacs."""
    if not cmd_build(args, source_dir):
        return False

    build_dir = source_dir / "build"

    print("Installing...")
    ret, _, stderr = run(["cmake", "--install", str(build_dir)], sudo=True, capture=True)
    if ret != 0:
        print(f"ERROR: Install failed!")
        print(stderr)
        return False

    # Create ASCII-friendly symlink in install location
    prefix = Path(args.prefix) if args.prefix else Path("/usr/local")
    bin_path = prefix / "bin"
    symlink = bin_path / "muEmacs"
    target = bin_path / "μEmacs"

    if target.exists() and not symlink.exists():
        ret, _, _ = run(["ln", "-sf", "μEmacs", str(symlink)], sudo=True)

    print("  OK: Installed")
    print()
    print("=" * 50)
    print("SUCCESS!")
    print("=" * 50)
    print()
    print("Run:")
    print("  μEmacs        # Unicode name")
    print("  muEmacs       # ASCII-friendly alias")
    print()

    return True

def cmd_clean(args, source_dir):
    """Clean build directory."""
    build_dir = source_dir / "build"

    if build_dir.exists():
        print(f"Removing {build_dir}...")
        shutil.rmtree(build_dir)
        print("  OK: Cleaned")
    else:
        print("Nothing to clean")

    return True

def cmd_uninstall(args, source_dir):
    """Remove installed files."""
    prefix = Path(args.prefix) if args.prefix else Path("/usr/local")

    files = [
        prefix / "bin" / "μEmacs",
        prefix / "bin" / "muEmacs",
        prefix / "bin" / "uemacs-ext-runner",
    ]

    removed = False
    for f in files:
        if f.exists() or f.is_symlink():
            print(f"Removing {f}...")
            ret, _, _ = run(["rm", "-f", str(f)], sudo=True)
            if ret == 0:
                print("  OK: Removed")
                removed = True
            else:
                print("  ERROR: Failed to remove")

    if not removed:
        print("No installed files found")

    return True

def cmd_test(args, source_dir):
    """Run tests."""
    build_dir = source_dir / "build"
    test_binary = build_dir / "bin" / "full_integration_test"

    if not test_binary.exists():
        print("Tests not built. Building first...")
        if not cmd_build(args, source_dir):
            return False

    if not test_binary.exists():
        print("ERROR: Test binary not found")
        return False

    print("Running tests...")
    ret, _, _ = run([str(test_binary)])
    return ret == 0

def main():
    parser = argparse.ArgumentParser(
        description="Build and install μEmacs (micro Emacs)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Commands:
  (default)   Build and install
  build       Build only
  clean       Remove build directory
  uninstall   Remove installed binaries
  test        Run integration tests

Examples:
  ./install.py                    # Build and install to /usr/local
  ./install.py --debug            # Debug build
  ./install.py --debug-log        # Debug build with logging
  ./install.py --prefix /usr      # Install to /usr
  ./install.py build              # Build only
  ./install.py clean              # Clean build
"""
    )

    parser.add_argument("command", nargs="?", default="install",
                       choices=["install", "build", "clean", "uninstall", "test"],
                       help="Command to run (default: install)")
    parser.add_argument("--debug", action="store_true",
                       help="Build with debug symbols")
    parser.add_argument("--debug-log", action="store_true",
                       help="Build with debug symbols and logging enabled")
    parser.add_argument("--prefix", type=str, default=None,
                       help="Installation prefix (default: /usr/local)")

    args = parser.parse_args()

    source_dir = Path(__file__).parent.resolve()

    print("μEmacs installer")
    print("================")
    print(f"Source: {source_dir}")
    print()

    # Check dependencies
    if args.command in ["install", "build", "test"]:
        print("Checking dependencies...")

        if not check_cmake():
            print()
            print("ERROR: cmake not found!")
            print()
            print("Install it first:")
            print("  Arch Linux:    sudo pacman -S cmake")
            print("  Debian/Ubuntu: sudo apt install cmake build-essential")
            print("  Fedora:        sudo dnf install cmake gcc")
            print()
            sys.exit(1)
        print("  OK: cmake found")

        if not check_compiler():
            print()
            print("ERROR: C compiler not found!")
            print()
            print("Install it first:")
            print("  Arch Linux:    sudo pacman -S gcc")
            print("  Debian/Ubuntu: sudo apt install build-essential")
            print("  Fedora:        sudo dnf install gcc")
            print()
            sys.exit(1)
        print("  OK: C compiler found")
        print()

    # Run command
    commands = {
        "install": cmd_install,
        "build": cmd_build,
        "clean": cmd_clean,
        "uninstall": cmd_uninstall,
        "test": cmd_test,
    }

    success = commands[args.command](args, source_dir)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
