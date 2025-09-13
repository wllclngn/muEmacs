# Building μEmacs (Linux‑only)

## Prerequisites
```bash
# Arch Linux
sudo pacman -S base-devel cmake gcc make ncurses

# Ubuntu/Debian
sudo apt install build-essential cmake libncurses-dev

# Fedora
sudo dnf install gcc make cmake ncurses-devel
```

## Build
```bash
git clone <repository-url> μEmacs
cd μEmacs
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# Run
./build/bin/μEmacs filename.txt
```

## Terminal Tips
- Use a modern GPU terminal (Kitty/WezTerm/Alacritty/Konsole).
- TERM: `xterm-kitty` or `xterm-256color`
- Truecolor: `export COLORTERM=truecolor`

## Tests
```bash
# Non‑interactive integration tests
./build/bin/full_integration_test

# Interactive (requires TTY)
cmake --build . --target itests
```

