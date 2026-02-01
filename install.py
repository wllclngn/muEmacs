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
import sys
import shutil
import subprocess
from pathlib import Path
from datetime import datetime


# =============================================================================
# LOGGING
# =============================================================================

def _timestamp() -> str:
    """Get current timestamp in [HH:MM:SS] format."""
    return datetime.now().strftime("[%H:%M:%S]")


def log_info(msg: str) -> None:
    print(f"{_timestamp()} [INFO]   {msg}")


def log_warn(msg: str) -> None:
    print(f"{_timestamp()} [WARN]   {msg}")


def log_error(msg: str) -> None:
    print(f"{_timestamp()} [ERROR]  {msg}")


# =============================================================================
# COMMAND EXECUTION
# =============================================================================

def run_cmd(cmd: list, cwd: Path | None = None) -> int:
    """
    Run a command with real-time output to terminal.
    Returns the exit code.
    """
    print(f">>> {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    return result.returncode


def run_cmd_capture(cmd: list, cwd: Path | None = None) -> tuple[int, str, str]:
    """Run a command and capture output."""
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return result.returncode, result.stdout, result.stderr


def run_cmd_sudo(cmd: list, cwd: Path | None = None) -> int:
    """Run a command with sudo."""
    return run_cmd(["sudo"] + cmd, cwd=cwd)


# =============================================================================
# DEPENDENCY CHECKS
# =============================================================================

def check_cmake() -> bool:
    """Check if cmake is available."""
    ret, _, _ = run_cmd_capture(["which", "cmake"])
    return ret == 0


def check_compiler() -> bool:
    """Check if a C compiler is available."""
    for compiler in ["gcc", "clang"]:
        ret, _, _ = run_cmd_capture(["which", compiler])
        if ret == 0:
            return True
    return False


# =============================================================================
# COMMANDS
# =============================================================================

def cmd_build(args, source_dir: Path) -> bool:
    """Build μEmacs."""
    build_dir = source_dir / "build"

    log_info("CONFIGURING BUILD")

    cmake_args = ["cmake", "-S", str(source_dir), "-B", str(build_dir)]

    if args.debug or args.debug_log:
        cmake_args.append("-DCMAKE_BUILD_TYPE=Debug")
        log_info("Build type: Debug")
    else:
        cmake_args.append("-DCMAKE_BUILD_TYPE=Release")
        log_info("Build type: Release")

    if args.debug_log:
        cmake_args.append("-DUEMACS_DEBUG_LOG=ON")
        log_info("Debug logging: enabled")

    if args.prefix:
        cmake_args.append(f"-DCMAKE_INSTALL_PREFIX={args.prefix}")
        log_info(f"Install prefix: {args.prefix}")

    ret = run_cmd(cmake_args)
    if ret != 0:
        log_error("cmake configure failed!")
        return False
    log_info("Configuration complete")

    # Build
    import multiprocessing
    jobs = multiprocessing.cpu_count()

    log_info("BUILDING")
    log_info(f"Using {jobs} parallel jobs")

    build_cmd = ["cmake", "--build", str(build_dir), f"-j{jobs}"]

    ret = run_cmd(build_cmd)
    if ret != 0:
        log_error("Build failed!")
        return False

    # Check for binary
    bin_dir = build_dir / "bin"
    binary = bin_dir / "μEmacs"
    if binary.exists():
        size = binary.stat().st_size
        log_info(f"Built {binary} ({size} bytes)")

        # Create symlink for ASCII-friendly name
        symlink = bin_dir / "muEmacs"
        if not symlink.exists():
            try:
                symlink.symlink_to("μEmacs")
                log_info("Created muEmacs symlink")
            except OSError:
                pass

    return True


def cmd_install(args, source_dir: Path) -> bool:
    """Build and install μEmacs."""
    if not cmd_build(args, source_dir):
        return False

    build_dir = source_dir / "build"
    prefix = Path(args.prefix) if args.prefix else Path("/usr/local")
    install_path = prefix / "bin" / "μEmacs"

    log_info("INSTALLING")
    log_info(f"Destination: {install_path}")

    ret = run_cmd(["sudo", "cmake", "--install", str(build_dir)])
    if ret != 0:
        log_error("Install failed!")
        return False

    # Create ASCII-friendly symlink in install location
    symlink = prefix / "bin" / "muEmacs"
    target = prefix / "bin" / "μEmacs"

    if target.exists() and not symlink.exists():
        run_cmd_sudo(["ln", "-sf", "μEmacs", str(symlink)])
        log_info(f"Created symlink: {symlink}")

    if install_path.exists():
        size = install_path.stat().st_size
        log_info(f"Installed {install_path} ({size} bytes)")

    print()
    log_info("SUCCESS. Installation complete.")
    log_info("RUN COMMAND: μEmacs (or muEmacs)")

    return True


def cmd_clean(args, source_dir: Path) -> bool:
    """Clean build directory."""
    build_dir = source_dir / "build"

    log_info("CLEANING")

    if build_dir.exists():
        log_info(f"Removing {build_dir}")
        shutil.rmtree(build_dir)
        log_info("Clean complete")
    else:
        log_info("Nothing to clean")

    return True


def cmd_uninstall(args, source_dir: Path) -> bool:
    """Remove installed files."""
    prefix = Path(args.prefix) if args.prefix else Path("/usr/local")

    files = [
        prefix / "bin" / "μEmacs",
        prefix / "bin" / "muEmacs",
        prefix / "bin" / "muemacs-ext-runner",
    ]

    log_info("UNINSTALLING")

    removed = False
    for f in files:
        if f.exists() or f.is_symlink():
            log_info(f"Removing {f}")
            ret = run_cmd_sudo(["rm", "-f", str(f)])
            if ret == 0:
                log_info("Removed")
                removed = True
            else:
                log_error("Failed to remove")

    if not removed:
        log_warn("No installed files found")

    return True


def cmd_test(args, source_dir: Path) -> bool:
    """Run tests."""
    build_dir = source_dir / "build"
    test_binary = build_dir / "bin" / "full_integration_test"

    log_info("RUNNING TESTS")

    if not test_binary.exists():
        log_info("Tests not built. Building first...")
        if not cmd_build(args, source_dir):
            return False

    if not test_binary.exists():
        log_error("Test binary not found")
        return False

    log_info(f"Executing {test_binary}")
    ret = run_cmd([str(test_binary)])

    if ret == 0:
        log_info("All tests passed")
    else:
        log_error("Some tests failed")

    return ret == 0


# =============================================================================
# MAIN
# =============================================================================

def main() -> int:
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

    print()
    log_info("μEmacs installer")
    log_info(f"Source: {source_dir}")
    print()

    # Check dependencies
    if args.command in ["install", "build", "test"]:
        log_info("CHECKING DEPENDENCIES")

        if not check_cmake():
            log_error("cmake not found!")
            print()
            log_info("Install it first:")
            print("         Arch Linux:    sudo pacman -S cmake")
            print("         Debian/Ubuntu: sudo apt install cmake build-essential")
            print("         Fedora:        sudo dnf install cmake gcc")
            print()
            return 1
        log_info("cmake found")

        if not check_compiler():
            log_error("C compiler not found!")
            print()
            log_info("Install it first:")
            print("         Arch Linux:    sudo pacman -S gcc")
            print("         Debian/Ubuntu: sudo apt install build-essential")
            print("         Fedora:        sudo dnf install gcc")
            print()
            return 1
        log_info("C compiler found")
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
    return 0 if success else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
        sys.exit(130)
