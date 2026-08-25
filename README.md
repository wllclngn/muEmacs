# μEmacs

μEmacs is a C23 text editor for Linux terminals in one small static binary, descended from MicroEMACS (1985) through Petri Kutvonen's uEmacs/PK to Linus Torvalds' personal fork. The modernization keeps what that lineage optimized for forty years — a fast, keyboard-driven editor that starts before your finger leaves the key — and rebuilds everything around it: C23 throughout with atomics and sanitizer-clean memory discipline, a raw-ANSI terminal driver with no ncurses, dual text storage (gap buffer and mmap piece table, selected by heuristic), a built-in terminal and REPL, and the μEmacs Extension Protocol, a polyglot extension system where even vim editing is an extension rather than a mode compiled into core. Search and matching ride sublimation, montauk's adaptive sort, search and learn core, linked as a static library from the montauk tree: One engine with literal, regex and fuzzy k-mismatch faces behind every search command.

![Editor](2025-12-31_14-03.png)

## Components

| Component | Where | Role |
|---|---|---|
| **editor core** | this repo, `src/` | Buffers, windows, display, undo, keymaps, terminal driver, TOML settings. One static binary (`μEmacs`, ASCII alias `muEmacs`) plus `muemacs-ext-runner`, the out-of-process extension host. |
| **c_evil** | [μEmacs-extensions] | Vim modal editing as a UEP extension: Nine C sources against the public API, loaded like any other extension, covered by a 109-test PTY suite that drives the real binary. Not compiled into core; core keeps only the empty keymaps and an ESC fallback. |
| **UEP** | `src/uep/` + [μEmacs-extensions] | The extension protocol: C, Rust and Zig in-process over `dlopen`; Go, Ada, Haskell, Crystal and Pascal out-of-process over memfd/eventfd IPC with seccomp-bpf and landlock sandboxing. The API headers (`include/uep/`) are the published contract; extensions live in their own repository. |
| **terminal / REPL** | `src/terminal/` | `C-x t` opens a shell in a split window: VT100/xterm emulation over a managed PTY, build-error navigation (`build-next-error`), REPL evaluation (`repl-eval-line`, `-region`, `-buffer`). |
| **sublimation** | linked from the [montauk] tree | The search core. μEmacs links `libsublimation.a` directly (C23, pure C ABI); the editor's scanners feed it per line and every search command — incremental, hunt, replace, fuzzy — runs through one cached compiled program. |

[μEmacs-extensions]: ../μEmacs-extensions
[montauk]: ../montauk

## Installation

μEmacs links sublimation out of the montauk tree at build time — the one
external requirement, stated first because configure fails without it:
Build montauk once (`cmake -B build && cmake --build build` in its tree),
or point `-DMONTAUK_BUILD_DIR` at any build tree containing
`libsublimation.a`. Everything else is libc.

```bash
./install.py
```

**Run**: `μEmacs [FILE]` or `muEmacs [FILE]` (ASCII alias).

### Advanced install (CMake)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build   # optional
```

### Other commands

```bash
./install.py build       # Build only, don't install
./install.py clean       # Clean build directory
./install.py uninstall   # Remove installed binaries
./install.py test        # Run tests
./install.py --debug     # Debug build (ASan/UBSan, no logging)
./install.py --debug-log # Debug build with logging to /tmp/muemacs_debug.log
```

### Dependencies

- **montauk tree** beside this one, built, for `libsublimation.a` and the
  `sublimation/src/include` headers (override with `-DMONTAUK_BUILD_DIR`)
- **Compiler**: GCC 13+ or Clang 16+ with C23 support
- **Build**: CMake 3.20+
- **Libraries**: None — pure raw-ANSI terminal driver, no ncurses
- **Optional**: liburing for the io_uring event-loop backend (off by
  default; epoll and poll need nothing), xclip/xsel/wl-copy for
  clipboard fallback beyond OSC 52, gpg or age for encryption
- **Extensions**: Each out-of-process extension needs its language
  toolchain to rebuild; prebuilt `.so` files load without one

### Quickstart

Open a file, `C-x C-s` saves, `C-x C-c` quits, `M-x` runs a named
command, `C-g` aborts anything. Vim users: `evil_mode = true` in
`~/.config/muemacs/settings.toml` (or `M-x evil-mode`) and the c_evil
extension announces `[EVIL: ON]` when its background load completes —
from there it is normal/insert/visual with the motions, operators, text
objects, registers, marks and search you expect. Full tables below.

## Verification

The claims in this README are backed by gates, in the montauk style of
the sibling projects:

- **Integration suite**: 84 tests, each forked into an isolated
  subprocess with per-test timeouts and crash detection. Green in
  Release and green under a Debug ASan/UBSan build with **zero
  sanitizer reports**. Any argument to the runner acts as a name
  filter: `./build/bin/full_integration_test search` runs the search
  suites alone.
- **c_evil PTY suite**: 109 tests across 16 files driving the real
  binary through pexpect + pyte — mode switching, motions, operators,
  counts, find-char, text objects, marks, registers, search, undo,
  visual modes. This suite is the editor's behavioral spec for modal
  editing; in one sweep it surfaced six core display and dispatch bugs
  the unit tests could not see.
- **Both build configurations compile warning-free** under `-Wall
  -Wextra` (Debug adds `-Werror`).
- The search engine itself carries sublimation's own gates upstream:
  Byte-parity against Python `re` for regex, a brute k-mismatch oracle
  for fuzzy, 843k checks green, ASan and TSan clean (see montauk's
  README).

## Performance

Measured on an AMD Ryzen 5 3600 (Zen 2), kernel 7.1.4-arch1-1, gcc
16.1, powersave governor, Release `-O2`. Harnesses are in-tree:
`build/bin/bench_*` and the PTY startup probe. Startup numbers are
end-to-end — process spawn under a PTY to the first frame byte the
editor writes, median of 20, spawn overhead included.

| Metric | Result | Method |
|---|---|---|
| Startup to first frame | **2.6 ms** median (2.3 min) | PTY spawn probe, n=20, empty buffer |
| 49,592-line file to first frame | **2.6 ms** median | Same probe on `tests/data/poe-collected-fictions.txt`: The mmap piece-table load costs nothing measurable over an empty start |
| Full 80×24 redraw | **21 µs** | `bench_editor`, 200 forced `update()` iterations |
| Character insert | **0.83 µs** | `bench_editor`, 10,000 inserts on one line |
| Keymap lookup | **9.3 ns** | `bench_keymap`, 1M hash-table lookups |
| UTF-8 encode/decode | **4.0 ns** | `bench_utf8`, 500k round-trips |
| Search dispatch | **~0.15 µs**/invocation | `bench_search`, 2,000 `scanner()` calls over a 2,000-line buffer, first-line match |
| Binary size | 1.8 MB | Release, unstripped |

No comparative multipliers against other editors: Earlier revisions of
this README carried a "~35x faster than Vim" line with no named
methodology, and it does not survive the house rule that a claim
carries its proof.

## The search engine

Every search command — incremental (`C-s`/`C-r`), non-incremental,
hunt, replace, query-replace and `search-fuzzy` — runs through one
engine: A cached `sublimation_search` program, recompiled only when the
pattern text, case mode (`MDEXACT`) or magic mode (`MDMAGIC`) changes.
Magic mode selects the Glushkov bit-parallel regex face; plain mode the
literal face with its rare-byte prefilter; an invalid regex degrades to
literal, matching the historical fall-through. `search-fuzzy` selects
the k-mismatch face — a numeric argument sets k (capped at 3), so
`C-u 2 M-x search-fuzzy` finds within two mismatched bytes. Matching is
fed per buffer line, so `^` and `$` anchor to line boundaries exactly
as they always did; patterns containing newlines take a multi-line
literal walk instead.

What was retired, and what deliberately was not: v3.1.0 removed the
in-tree Thompson NFA engine, the Boyer-Moore-Horspool paths (including
the private copies inside both storage backends) and the legacy
`mcscanner` magic interpreter — three engines, one of which disagreed
with another about where reverse search should leave the cursor
depending on pattern length. The replacement-side metacharacter
machinery survives: `query-replace` still expands `&` to the matched
text through `rmcpat`, because that half was never the scanner's
problem. Match spans are recorded exactly, so a variable-length regex
replace deletes precisely what matched — the old NFA path left the
length stale.

## Display

Double-buffered in the classic uEmacs shape — a virtual screen the
editor writes and a physical screen mirroring the terminal — with
line-granularity damage tracking, line hashing to skip clean rows and
the whole frame flushed as one atomic write (the montauk pattern: Build
everything, write once, no incremental flicker). Soft wrap uses a
two-phase segment cache: Per-line wrap segments computed once on edit,
layout derived by arithmetic on resize, no backtracking and no fixed
line-length ceiling. The modeline's line/column indicators are kept
current by a chokepoint in the cursor-position pass that catches every
dot move — core motion, search jump or extension `set_point` alike —
because three separate stale-cache bugs taught us that per-call-site
invalidation does not survive contact with reality. Cursor-line
highlight, 256-color and truecolor theming with terminal-palette
integration, and kitty keyboard protocol (CSI-u) plus SGR mouse
decoding on the input side.

## Text storage

Two backends behind one vtable: A gap buffer (O(1) edits at the
cursor, line index, character cache) for files under 2 MB, and an mmap
piece table (O(1) load, view lines referencing buffer-level storage)
above it, with conversion back to gap buffer when edit density passes
20%. UTF-8 native throughout — multi-byte input inserts full sequences
(a single-byte truncation bug here once corrupted real journals; the
regression test types U+2019/U+25CF/U+2014 through a PTY and asserts
the saved bytes). The undo system groups operations VSCode-style within
400 ms windows and at word boundaries, in an atomic circular buffer of
10,000 operations.

## Vim editing is an extension

c_evil implements modal editing entirely against the public UEP API:
Nine C sources (bridge, mode, motion, ops, find, search, marks,
text_objects, edit) resolving 38 editor functions by name at load, an
`input:raw` pre-dispatch hook for counts, prefixes and operator-pending
state, and its own modeline segment. Core retains exactly two pieces of
vim infrastructure — empty keymaps the extension populates and an ESC
fallback — and nothing else: The old core `evil-mode` stub is deleted,
so without the extension `M-x evil-mode` reports an honest unknown
command instead of half-activating. With `evil_mode = true` in
settings, the state arms at startup and the extension announces
`[EVIL: ON]` when its background load completes.

The surface: Normal, insert, visual, visual-line, visual-block and
replace modes; motions (`h/j/k/l`, `w/b/e`, `0/$/^`, `gg/G`, `f/F/t/T`
with `;`/`,`, `+`/`-`); operators `d`/`c`/`y` composing with motions
and text objects (`iw/aw`, `i"/a"`, `i(/a(`); counts everywhere; named
and numbered registers with `"x` targeting (`"ay`, `"0p` — yanks write
register 0); marks (`m`, `` ` ``, `'`); search (`/`, `?`, `n`, `N`,
`*`, `#`) riding the same sublimation engine as core; scrolls
(`C-d/C-u/C-f/C-b/C-e/C-y`, `zz/zt/zb`); repeat (`.`) and undo/redo.
Every behavior listed here has a passing PTY test.

## Extension protocol (UEP)

Extensions today span nine languages — C, Fortran, Rust, Zig, Go, Ada,
Haskell, Crystal and Pascal — under one rule: The runtime picks the
isolation. C, Rust and Zig load in-process over `dlopen` for lowest
latency; garbage-collected and runtime-heavy languages run
out-of-process behind `muemacs-ext-runner`, talking over memfd shared
memory and eventfd signaling, with pidfd lifecycle management,
per-runtime seccomp-bpf syscall whitelists and landlock filesystem
restriction. A version handshake on the IPC layer fails closed on
mismatch rather than corrupting frames.

| Mode | Languages | Mechanism | Trade-off |
|---|---|---|---|
| In-process | C, Fortran (via C bridge), Rust, Zig | `dlopen()` | Lowest latency, shared memory |
| Out-of-process | Go, Ada, Haskell, Crystal, Pascal | memfd + eventfd IPC | Crash isolation, GC freedom |

The API (v4) is ABI-stable named lookup: `api->get_function("name")`
against a registry of 90+ functions, so extensions survive editor
upgrades without recompiling against struct layouts. Extensions
register commands (which override same-named core commands — that
resolution order is what lets c_evil own `evil-mode`), subscribe to the
event bus (`buffer:load/save/close`, `input:raw`, `input:key`,
`char:insert`, idle hooks), contribute modeline segments and read their
own `[extension.name]` settings blocks. Loading is non-blocking at
startup and completes from the input idle loop, so an idle editor
finishes loading without waiting for a keystroke.

The extensions themselves — c_evil, c_git (C + Fortran core), c_lint,
c_linus (the uEmacs/PK look, on by default), c_minibuffer, c_mouse,
c_org, c_write_edit, rust_re2, zig_treesitter, go_lsp, go_chess,
go_sam, go_dfs, go_sudoku, ada_fuzzy, haskell_calc, haskell_project,
crystal_ai, pascal_multicursor — live in the [μEmacs-extensions]
repository, deliberately separate: The API headers are the published
contract, and the editor repo carries no language toolchain burden.
A script layer (`~/.config/muemacs/scripts/`) runs external
executable scripts in any interpreter as named commands.

## Kernel integration

The event loop is a backend-agnostic abstraction: epoll by default,
poll as the floor, io_uring as an opt-in build (`-DENABLE_IO_URING=ON`
with liburing; off by default). timerfd drives auto-save and debounce
timers; pidfd watches out-of-process extensions race-free; seccomp-bpf
and landlock sandbox them. Signal handling is `sigaction`-based across
all 24 operational handlers, with a signalfd path available as the
documented migration target — stated here the way the code audit
states it, not as a finished migration. The piece table advises the
kernel (`madvise`) about its mmap access patterns.

## Configuration

TOML at `~/.config/muemacs/settings.toml` (XDG-compliant, with
installed-default and in-tree fallbacks). Fresh installs boot into
Linus Torvalds' uEmacs/PK defaults — the c_linus extension enabled,
72-column fill, no ruler, no line highlight — and one block turns the
modern chrome back on. The shipped defaults, abbreviated:

```toml
[editor]
column_width = 72           # Linus's fillcol
wrap = false
evil_mode = false           # Vim modal editing via c_evil
tab_width = 8

[visual]
ruler = false               # Linus-first; set true for the ruler
highlight_line = false

[terminal]
enabled = true              # C-x t split terminal
height_percent = 45

[clipboard]
provider = "auto"           # auto, osc52, xclip, xsel, wl-copy
osc52_enabled = true        # Works over SSH and tmux

[extension.c_linus]
enabled = true              # The uEmacs/PK look; false for modern chrome

[extension.c_mouse]
enabled = true
scroll_lines = 3
```

`M-x set-variable` changes values live; `open-user-config`,
`list-settings` and `save-settings` manage the file. The settings
watcher reloads on change.

## Keybindings

Emacs bindings by default; the vim surface is the section above. `ESC`
or `Alt` is Meta.

### Essential
| Key | Action |
|---|---|
| `C-x C-c` | Quit |
| `C-x C-s` | Save |
| `C-x C-f` | Open file |
| `C-g` | Abort |
| `M-x` | Execute named command |

### Navigation
| Key | Action |
|---|---|
| `C-a` / `C-e` | Beginning / end of line |
| `C-f` / `C-b` | Forward / backward character |
| `C-n` / `C-p` | Next / previous line |
| `C-v` / `M-v` | Page down / up |
| `M-<` / `M->` | Beginning / end of buffer |
| `M-g` | Go to line |

### Editing
| Key | Action |
|---|---|
| `C-d` | Delete forward |
| `C-k` | Kill to end of line |
| `C-w` | Kill region (cut) |
| `M-w` | Copy region |
| `C-y` | Yank (paste) |
| `C-_` | Undo |
| `M-_` | Redo |
| `M-q` | Fill paragraph (soft-wrap-aware hybrid fill) |

### Windows, buffers, terminal
| Key | Action |
|---|---|
| `C-x 2` | Split window |
| `C-x o` | Next window |
| `C-x 0` | Close window |
| `C-x b` | Switch buffer |
| `C-x t` | Open terminal |

### Search and replace
| Key | Action |
|---|---|
| `C-s` / `C-r` | Incremental search forward / backward |
| `M-R` | Replace string |
| `M-%` | Query replace |
| `M-x search-fuzzy` | Fuzzy search within k mismatches (numeric arg sets k) |

## Shell integration

Spawned commands and the split terminal see the editor's state:

| Variable | Content |
|---|---|
| `$UE_FILENAME` | Current file path |
| `$UE_BUFNAME` | Current buffer name |
| `$UE_LINE` | Cursor line (1-indexed) |
| `$UE_COL` | Cursor column (0-indexed) |
| `$UE_FILETYPE` | Detected type (c, python, rust, ...) |
| `$UE_MODIFIED` | "1" if buffer modified |
| `$UE_TERMINAL` | "1" inside the μEmacs terminal |

## Encryption and clipboard

Encryption shells out to battle-tested tools — GPG (`encrypt-buffer`),
age (`encrypt-buffer-age`), auto-detection (`encrypt-buffer-auto`) —
rather than shipping crypto. Clipboard walks a provider chain: OSC 52
escape sequences first (works over SSH and tmux on kitty, alacritty,
foot, wezterm, iTerm2), then wl-copy, xclip, xsel.

## Testing

```bash
./build/bin/full_integration_test            # the whole fork-isolated suite
./build/bin/full_integration_test search     # name-filtered subset
cd tests/tui && python3 test_search_replace.py   # PTY suites, per file
cd ../μEmacs-extensions/extensions/c_evil/tests && python3 run_all.py
./build/bin/bench_editor                     # and bench_search, bench_keymap, bench_utf8
```

The integration runner forks every test into an isolated subprocess
with a timeout and crash detection; arguments are substring filters
over test names. The PTY suites (pexpect + pyte) drive the real binary
in a real pseudo-terminal — screen-buffer assertions, not mocks. The
c_evil suite's 109 tests double as the vim-behavior specification.
Debug builds carry ASan/UBSan by default; `build_noasan/` exists for
the inverse.

## Architecture

```
src/
├── core/       Buffer, window, display, undo, keymap, gap buffer, piece table, event bus
├── terminal/   Raw-ANSI driver, input state machine, PTY, capability detection, palette
├── text/       Search (sublimation-backed), isearch, word operations, region
├── config/     Bindings, names table, settings (TOML), exec, vim keymap infrastructure
├── io/         File I/O, preload, locking, encryption, io_uring batch read
├── platform/   Event loop backends, timers, sandbox, clipboard, spawn
├── syntax/     Lexer, per-language highlighting
├── uep/        Extension system: API registry, loader, IPC, out-of-process host
└── util/       UTF-8, display width, memory, strings, logger, profiler
```

## History

```
MicroEMACS (1985)
    ↓
uEmacs/PK (Petri Kutvonen)
    ↓
μEmacs (Linus Torvalds' personal fork)
    ↓
μEmacs v3.x (C23 modernization)
```

- **v1.0**: Terminal emulator, vim mode, polyglot extensions, modern
  keymap system.
- **v2.1.0**: Linus baseline defaults, vim extracted toward the c_evil
  extension, piece table with mmap, text storage abstraction, event
  bus, kernel integration (io_uring, epoll, timerfd, signalfd, pidfd,
  seccomp-bpf, landlock).
- **v3.0.0**: Wrap segment cache (two-phase prepare/layout), storage
  backend hardening, warning sweep.
- **v3.1.0**: The search engine moved onto sublimation and the old
  engines were deleted — Thompson NFA, Boyer-Moore-Horspool and the
  legacy magic interpreter, with the replacement-meta half preserved.
  `search-fuzzy` added. The c_evil extraction completed: Standalone
  build, 109-test PTY suite, core stub retired so absence is honest.
  Extension-command override resolution fixed (registration now
  actually overrides), extension init no longer starves waiting for a
  keystroke, modeline line/column made current within the key cycle.
  UEP API extended (public vim state typedefs, `input:raw`, register
  and undo-boundary primitives, OSC 52 bridge). Linus-first shipped as
  default. UTF-8 multi-byte insert truncation fixed. A quality sweep
  behind an ASan/UBSan gate: A use-after-free in the extension API's
  buffer-clear, an input-ring overwrite guard, a SIGWINCH ordering
  fix, dead-code deletion across the tree and a uniform 4-space
  reindent.

## Credits

- **MicroEMACS**: Dave G. Conroy, Daniel M. Lawrence
- **uEmacs/PK**: Petri H. Kutvonen
- **Original μEmacs**: Linus Torvalds
- **C23 modernization**: Will Clingan
- **sublimation**: The search core, from the montauk project

## License

GPL-2.0, matching the packaging. One open item, stated the way the
packaging states it: The lineage from the original MicroEMACS license
is pending verification, and the license declaration follows that
check's outcome.
