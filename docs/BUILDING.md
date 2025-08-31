# Building μEmacs - Cross-Platform Guide

## Quick Start

### Prerequisites by Platform

#### Arch Linux (Primary Target)
```bash
sudo pacman -S base-devel cmake gcc make
```

#### Windows 11
- **Visual Studio 2019+** or **Visual Studio Build Tools**
- **CMake 3.15+** (download from cmake.org)
- **Git** (for version control)

#### Other Linux Distributions
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libncurses-dev

# CentOS/RHEL
sudo yum install gcc make cmake ncurses-devel

# Fedora
sudo dnf install gcc make cmake ncurses-devel
```

#### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake via Homebrew
brew install cmake
```

### Basic Build Commands

```bash
# Clone and enter directory
git clone <repository-url> uemacs
cd uemacs

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .

# Run
./bin/uemacs filename.txt
```

---

## Platform-Specific Instructions

### Arch Linux + Kitty (Optimal Configuration)

Your setup is the primary development target with full optimization:

```bash
# Install dependencies
sudo pacman -S base-devel cmake

# Configure for maximum performance
mkdir build && cd build
cmake -DTERMINAL_DRIVER=ansi \
      -DENABLE_COLOR=ON \
      -DENABLE_UTF8=ON \
      -DENABLE_MODERN_FEATURES=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=gcc \
      ..

# Parallel build using all CPU cores
make -j$(nproc)

# Test with Arch-optimized theme
./bin/uemacs --theme ../config/themes/arch/arch-kitty.theme
```

#### Arch-Specific Optimizations
```bash
# Enable Arch workflow features
cmake -DENABLE_PACMAN_SYNTAX=ON \      # PKGBUILD highlighting
      -DENABLE_SYSTEMD_SYNTAX=ON \     # Unit file support
      -DENABLE_GIT_INTEGRATION=ON \    # Git status in editor
      -DENABLE_PERFORMANCE=ON \        # CPU optimizations
      ..

# Link-time optimization for maximum performance
cmake -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=TRUE ..
```

### Windows 11 + VSCode PowerShell

Fixed configuration for your Windows development needs:

#### Visual Studio (Recommended)
```cmd
# Open Developer Command Prompt for VS 2022
mkdir build && cd build
cmake -G "Visual Studio 16 2019" -A x64 \
      -DTERMINAL_DRIVER=ansi \
      -DENABLE_COLOR=ON \
      -DENABLE_MODERN_FEATURES=ON \
      ..
cmake --build . --config Release
```

#### MinGW/MSYS2 (Alternative)
```bash
# In MSYS2 terminal
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake

mkdir build && cd build
cmake -G "MinGW Makefiles" \
      -DTERMINAL_DRIVER=ansi \
      -DENABLE_COLOR=ON \
      ..
cmake --build .
```

#### Windows-Specific Features
```bash
# Enable Windows 11 integration
cmake -DENABLE_WIN11_FEATURES=ON \     # Modern Windows features
      -DENABLE_VSCODE_INTEGRATION=ON \  # VSCode terminal detection
      -DENABLE_POWERSHELL_PROFILE=ON \  # PowerShell integration
      ..
```

### Linux Desktop Environments

#### Modern Terminals (GNOME Terminal, Konsole, etc.)
```bash
mkdir build && cd build
cmake -DTERMINAL_DRIVER=ansi \
      -DENABLE_COLOR=ON \
      -DENABLE_UTF8=ON \
      ..
make -j$(nproc)
```

#### Legacy Terminal Support
```bash
# For older terminals requiring termcap
cmake -DTERMINAL_DRIVER=termcap \
      -DENABLE_COLOR=ON \
      ..

# Link with termcap/ncurses
# (CMake will auto-detect available libraries)
```

### macOS

#### Terminal.app and iTerm2
```bash
mkdir build && cd build
cmake -DTERMINAL_DRIVER=ansi \
      -DENABLE_COLOR=ON \
      -DENABLE_UTF8=ON \
      -DCMAKE_BUILD_TYPE=Release \
      ..
make -j$(sysctl -n hw.ncpu)
```

#### macOS-Specific Features
```bash
# Enable macOS integration
cmake -DENABLE_MACOS_FEATURES=ON \     # macOS-specific optimizations
      -DENABLE_ITERM_INTEGRATION=ON \  # iTerm2 features
      ..
```

---

## Configuration Options

### Terminal Driver Selection

Choose the optimal driver for your environment:

```bash
# ANSI driver (recommended for modern terminals)
cmake -DTERMINAL_DRIVER=ansi ..

# Windows Console API (for CMD/PowerShell without ANSI)
cmake -DTERMINAL_DRIVER=win32 ..

# Unix termcap/ncurses (for legacy terminals)  
cmake -DTERMINAL_DRIVER=termcap ..

# POSIX termios (modern Unix systems)
cmake -DTERMINAL_DRIVER=posix ..

# VT52 (legacy terminals)
cmake -DTERMINAL_DRIVER=vt52 ..
```

#### Driver Compatibility Matrix

| Driver | Windows | Linux | macOS | Modern Terminals | Legacy Support |
|--------|---------|--------|-------|------------------|----------------|
| **ansi** | ✅ VSCode/WT | ✅ **Primary** | ✅ **Primary** | ✅ **Best** | ⚠️ Limited |
| **win32** | ✅ CMD/PS | ❌ | ❌ | ⚠️ Limited | ✅ Good |
| **termcap** | ❌ | ✅ Good | ✅ Good | ✅ Good | ✅ **Best** |
| **posix** | ❌ | ✅ Good | ✅ Good | ✅ Good | ⚠️ Limited |

### Feature Configuration

```bash
# Core features
cmake -DENABLE_UTF8=ON \              # UTF-8 support (recommended)
      -DENABLE_COLOR=ON \             # Color support (recommended)
      -DENABLE_CRYPT=ON \             # File encryption
      -DENABLE_FILOCK=ON \            # File locking
      ..

# Modern editor features
cmake -DENABLE_MODERN_FEATURES=ON \   # Modern editor functionality
      -DENABLE_SYNTAX_HIGHLIGHTING=ON \ # Syntax highlighting
      -DENABLE_AUTO_INDENT=ON \       # Automatic indentation
      -DENABLE_BRACKET_MATCHING=ON \  # Bracket matching
      ..

# Platform-specific features
cmake -DENABLE_KITTY_PROTOCOL=ON \    # Kitty terminal optimizations
      -DENABLE_TRUE_COLOR=ON \        # 24-bit color support
      -DENABLE_GPU_ACCELERATION=ON \  # GPU rendering hints
      ..
```

### Build Type Configuration

```bash
# Development build (debugging)
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_DEBUG_OUTPUT=ON \
      -DBUILD_TESTS=ON \
      ..

# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      ..

# Performance profiling
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DENABLE_PROFILING=ON \
      ..
```

---

## Advanced Build Options

### Cross-Platform Development

#### Universal Build for Multiple Targets
```bash
# Configure for multiple platforms
cmake -DTERMINAL_DRIVER=ansi \
      -DENABLE_WINDOWS_SUPPORT=ON \
      -DENABLE_LINUX_SUPPORT=ON \
      -DENABLE_MACOS_SUPPORT=ON \
      ..
```

#### Static vs Dynamic Linking
```bash
# Static linking (single executable)
cmake -DBUILD_SHARED_LIBS=OFF \
      -DENABLE_STATIC_LINKING=ON \
      ..

# Dynamic linking (smaller executable)
cmake -DBUILD_SHARED_LIBS=ON ..
```

### Performance Optimization

#### CPU Optimizations
```bash
# Native CPU optimizations
cmake -DCMAKE_C_FLAGS="-march=native -O3" ..

# Specific CPU targets
cmake -DCMAKE_C_FLAGS="-march=skylake -O3" ..     # Intel
cmake -DCMAKE_C_FLAGS="-march=znver2 -O3" ..      # AMD Ryzen
```

#### Memory Optimization
```bash
# Minimize memory usage
cmake -DENABLE_MINIMAL_BUILD=ON \
      -DDISABLE_LARGE_BUFFERS=ON \
      ..

# Optimize for memory speed
cmake -DENABLE_MEMORY_POOL=ON \
      -DENABLE_BUFFER_CACHING=ON \
      ..
```

### Development Features

#### Testing and Debugging
```bash
# Enable test suite
cmake -DBUILD_TESTS=ON \
      -DENABLE_UNIT_TESTS=ON \
      -DENABLE_INTEGRATION_TESTS=ON \
      ..

# Address sanitizer (debugging)
cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" \
      -DCMAKE_BUILD_TYPE=Debug \
      ..

# Memory sanitizer
cmake -DCMAKE_C_FLAGS="-fsanitize=memory -g" ..
```

#### Code Quality Tools
```bash
# Static analysis
cmake -DENABLE_STATIC_ANALYSIS=ON \
      -DENABLE_CLANG_TIDY=ON \
      ..

# Code coverage
cmake -DENABLE_COVERAGE=ON \
      -DCMAKE_C_FLAGS="--coverage" \
      ..
```

---

## Troubleshooting

### Common Build Issues

#### CMake Not Found
```bash
# Windows
# Download from https://cmake.org/download/

# Linux
sudo apt install cmake           # Ubuntu/Debian
sudo yum install cmake           # CentOS/RHEL
sudo pacman -S cmake             # Arch Linux

# macOS
brew install cmake
```

#### Compiler Issues
```bash
# No compiler found
# Windows: Install Visual Studio or MinGW
# Linux: sudo apt install build-essential
# macOS: xcode-select --install

# Wrong compiler version
cmake -DCMAKE_C_COMPILER=gcc-9 ..    # Specific GCC version
cmake -DCMAKE_C_COMPILER=clang ..    # Use Clang instead
```

#### Missing Libraries
```bash
# ncurses/termcap not found (Linux)
sudo apt install libncurses-dev     # Ubuntu/Debian
sudo yum install ncurses-devel      # CentOS/RHEL
sudo pacman -S ncurses              # Arch Linux

# Force fallback driver if libraries missing
cmake -DTERMINAL_DRIVER=ansi -DFORCE_FALLBACK=ON ..
```

### Platform-Specific Issues

#### Windows Terminal Compatibility
```bash
# Issue: Display garbled in CMD
# Solution: Use ANSI driver
cmake -DTERMINAL_DRIVER=ansi ..

# Issue: Colors not working in PowerShell
# Solution: Enable ANSI in PowerShell
# Run: Set-ItemProperty HKCU:\Console VirtualTerminalLevel -Type DWORD 1
```

#### Linux Terminal Issues
```bash
# Issue: No colors in terminal
# Solution: Check TERM environment variable
echo $TERM
export TERM=xterm-256color

# Issue: Unicode characters not displaying
# Solution: Check locale settings
locale
export LC_ALL=en_US.UTF-8
```

#### macOS Terminal Issues
```bash
# Issue: Compilation fails with Xcode
# Solution: Install command line tools
xcode-select --install

# Issue: Terminal colors incorrect
# Solution: Use ANSI driver with true color
cmake -DTERMINAL_DRIVER=ansi -DENABLE_TRUE_COLOR=ON ..
```

### Performance Issues

#### Slow Compilation
```bash
# Use parallel builds
make -j$(nproc)                      # Linux
make -j$(sysctl -n hw.ncpu)         # macOS
cmake --build . --parallel          # Cross-platform

# Use faster linker
cmake -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=gold" ..  # Linux
```

#### Runtime Performance
```bash
# Profile build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_PROFILING=ON ..

# Check for debug symbols in release
strip ./bin/uemacs                   # Remove debug symbols

# Optimize for your CPU
cmake -DCMAKE_C_FLAGS="-march=native -O3 -flto" ..
```

---

## Installation

### System Installation
```bash
# Install to system directories
cmake --install .

# Custom installation directory
cmake --install . --prefix /usr/local

# Package installation
cmake --build . --target package
```

### User Installation
```bash
# Install to user directory
cmake --install . --prefix ~/.local

# Portable installation
cmake -DENABLE_PORTABLE=ON ..
cmake --build .
# All config files will be relative to executable
```

---

## Packaging & Distribution

### Creating Packages
```bash
# Configure packaging
cmake -DCPACK_GENERATOR="DEB;RPM" ..        # Linux packages
cmake -DCPACK_GENERATOR="ZIP;NSIS" ..       # Windows packages
cmake -DCPACK_GENERATOR="TGZ;DragNDrop" ..  # macOS packages

# Build packages
cmake --build . --target package
```

### Cross-Compilation
```bash
# Windows from Linux (MinGW)
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake ..

# Linux ARM from x86
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux.cmake ..
```

---

## Development Workflow

### Quick Development Build
```bash
# Fast iterative development
mkdir debug && cd debug
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DTERMINAL_DRIVER=ansi \
      -DBUILD_TESTS=ON \
      ..
make -j$(nproc)
```

### Release Build for Distribution
```bash
# Optimized release build
mkdir release && cd release
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DTERMINAL_DRIVER=ansi \
      -DENABLE_ALL_FEATURES=ON \
      ..
make -j$(nproc)
strip ./bin/uemacs  # Remove debug symbols
```

This comprehensive build guide ensures μEmacs builds optimally on your Arch Linux + Kitty setup while maintaining excellent cross-platform support.