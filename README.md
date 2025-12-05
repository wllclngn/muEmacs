# μEmacs v0.0.23 - Modern C23 Linux Text Editor

**Linus Torvalds' original μEmacs, transformed into a pure C23 implementation for modern Linux systems.**

## Overview

μEmacs v0.0.23 is a minimalist text editor preserving Linus Torvalds' original keybindings and philosophy, now implemented with modern C23 standards. This version removes all legacy platform code, focusing exclusively on Linux performance and reliability.

This project follows the uEmacs spirit favored by Linus Torvalds:
- Minimal surface area: small codebase, straightforward C, no frameworks.
- Drop-in build: clone, compile fast, run.
- Native on Linux utility.
- Text-first, TUI-only: no GUI layers; reliable terminal behavior.
- Predictable install: single binary plus data directory; no background services.

## Build & Installation

Quick install options
- Auto-detect: `./scripts/system_install.sh`
- Arch (AUR-style, local PKGBUILD): `./scripts/system_install.sh --mode aur`
- Generic CMake install: `./scripts/system_install.sh --mode cmake --prefix /usr/local`
- Debian/Ubuntu (.deb via CPack): `./scripts/system_install.sh --mode deb`

Manual build from source
```bash
# Clone and build
git clone [repository] && cd muEmacs
make distclean && make && sudo make install

# Run from build tree
./build/bin/μEmacs [FILE]
```
After system install
- Commands: `μEmacs` and `muEmacs` (ASCII alias)
- Data files: `/usr/share/muemacs`
- Desktop entry: `/usr/share/applications/muEmacs.desktop`
- Man page: `/usr/share/man/man1/muEmacs.1`

# Usage

Run the editor using either the Unicode or ASCII-friendly command:

- `μEmacs [FILE ...]`
- `muEmacs [FILE ...]`
- 
Install notes
- Arch/AUR mode builds from the current working tree with a locally generated PKGBUILD and source tarball (no network fetches).
- The installed binary is `/usr/bin/μEmacs` with an ASCII alias symlink `/usr/bin/muEmacs`.

## Requirements

- Linux (kernel 5.0+)
- GCC 12+ or Clang 15+ with C23 support
- ncursesw library
- 4MB RAM minimum
  
Optional tools
- xclip or xsel (recommended) for system clipboard integration

## Key Features

**Core Implementation** (~30K lines of C23 code)
- **C23 Implementation**: Modern atomic operations, thread safety, and memory management
- **Linus's Original Keybindings**: Preserved exactly as designed, with undo/redo on available keys
- **O(1) Keymap System**: Hash-based lookup for instant command execution
- **Zero Legacy Code**: 500+ lines of obsolete platform code removed (MSDOS, VMS, etc.)
- **Modern Encryption**: Shell-out to gpg/age/openssl

**Advanced Text Editing**
- **VSCode-Style Undo/Redo**: Atomic circular buffer with intelligent operation grouping, 400ms coalescing window, and 10,000 operation capacity
- **Modern Kill Ring**: 32-entry atomic circular buffer (8KB per entry) with system clipboard integration via xclip/xsel
	- Copy (Meta+W) and Kill (Ctrl+W, etc.) automatically mirror the selection into the system clipboard
	- Use "yank-clipboard" (M-x yank-clipboard) to paste directly from the system clipboard
- **Multiple Search Engines**: Boyer-Moore-Horspool (O(n/m) performance) and Thompson NFA regex engine with zero-heap runtime
- **UTF-8 Support**: Full Unicode handling with proper terminal integration, bidirectional conversion, and minimality validation

**Terminal Integration** 
- **Modern Terminal Support**: 24-bit true color, bracketed paste mode, GPU terminal optimization (Alacritty, Kitty)
- **Signal-Safe Operations**: Atomic terminal state changes with generation counting and concurrent access protection
- **Customizable Status Line**: Real-time git integration, atomic buffer statistics, and configurable mode indicators
- **Clean Exit UX**: Proper alternate screen restore on exit (no split history; no manual Ctrl+L needed)
- **ESC Meta Processing**: Reliable ESC-as-Meta with C23 atomic key transformation and instantaneous case conversion

**Performance & Reliability**
- **Gap Buffer**: O(1) insert/delete at cursor with dynamic resizing and UTF-8 awareness (integrated into line structure for 5000x speedup on large lines)
- **Display Matrix**: Dirty region tracking with hardware-accelerated scrolling and selection highlighting
- **Memory Safety**: Centralized allocation system with leak tracking and overflow protection
- **Security Hardened**: TOCTOU-safe file locking, secure temp files with XDG support, thread-safe inotify

## Removed Features

**Legacy platform code:**
- All non-Linux platform support removed (MSDOS, VMS, Amiga, etc.)
- Conditional compilation simplified - features are always enabled
- Cleaner codebase with no preprocessor maze

## Commands

**Modern commands:**
- `M-x set-column-width` (or `C-x W`) → prompts for a number, sets width and enables wrap
- `M-x save-settings` → writes current values to `~/.config/muemacs/settings.json`
- `M-x open-user-config` → opens settings.json for editing (creates file/directories if needed)
- `M-x list-settings` → shows current values in the modeline/message area
- `M-x encrypt-buffer` - Encrypt with GPG (AES-256)
- `M-x decrypt-file` - Decrypt GPG file
- `M-x encrypt-buffer-age` - Encrypt with age (modern alternative)
- `M-x decrypt-file-age` - Decrypt age file
- `M-x encrypt-buffer-auto` - Auto-detect best tool
- `M-x show-encryption-tools` - Check what's installed

**Workflow:**
1. Edit plaintext file normally
2. `M-x encrypt-buffer` - creates encrypted .gpg file
3. Later: `M-x decrypt-file` - opens encrypted file for editing
4. Edit, save normally (WARNING: saves as plaintext unless you encrypt again)

**Alternative approaches:**
- Pipe: `gpg -d secrets.gpg | μEmacs -` (read-only)
- Filesystem encryption: LUKS, eCryptfs, fscrypt (transparent)

**Tools required:** Install `gpg` (recommended), `age`, or `openssl`

## Key Bindings

**Meta Key**: `Ctrl+[` (ESC key) or `Ctrl+]` for prefix
	- ESC works reliably as Meta with C23 atomic case conversion for all letter combinations

### Essential Commands
- `Ctrl+X Ctrl+C` - Exit μEmacs
- `Ctrl+X Ctrl+S` - Save file
- `Ctrl+X Ctrl+F` - Find/open file
- `Ctrl+G` - Abort command
- `Meta+Z` - Quick exit (Linus's original)

### Movement
- `Ctrl+A` - Beginning of line
- `Ctrl+E` - End of line
- `Ctrl+F` - Forward character
- `Ctrl+B` - Backward character
- `Ctrl+N` - Next line
- `Ctrl+P` - Previous line
- `Ctrl+V` - Next page
- `Ctrl+Z` - Previous page (Linus's original)
- `Meta+<` - Beginning of file
- `Meta+>` - End of file

### Editing
- `Ctrl+D` - Delete next character
- `Ctrl+H` - Delete previous character
- `Ctrl+K` - Kill to end of line
- `Ctrl+O` - Open line
- `Ctrl+T` - Transpose characters
- `Ctrl+Space` - Set mark
- `Ctrl+W` - Kill region (cut)
- `Meta+W` - Copy region
- `Ctrl+Y` - Yank (paste)
- `Meta+Y` - Yank pop (cycle kill ring)
  
#### Clipboard
- Clipboard is integrated by default:
	- `Meta+W` (Copy) and `Ctrl+W` (Kill region) push text to the system clipboard
	- `M-x yank-clipboard` pastes from the system clipboard (useful when content wasn’t produced inside μEmacs)

### Undo/Redo
- `Ctrl+_` - Undo
- `Ctrl+/` - Undo (alternative)
- `Ctrl+X R` - Redo
- `Meta+_` - Redo (alternative)

### Search & Replace
- `Ctrl+S` - Search forward
- `Ctrl+R` - Search backward
- `Meta+R` - Replace string
- `Meta+Ctrl+R` - Query replace

### Windows & Buffers
- `Ctrl+X 0` - Delete window
- `Ctrl+X 1` - Delete other windows
- `Ctrl+X 2` - Split window
- `Ctrl+X O` - Next window
- `Ctrl+X B` - Select buffer
- `Ctrl+X Ctrl+B` - List buffers
- `Ctrl+X K` - Kill buffer

### Words
- `Meta+F` - Forward word
- `Meta+B` - Backward word
- `Meta+D` - Delete next word
- `Meta+Ctrl+H` - Delete previous word
- `Meta+U` - Uppercase word
- `Meta+L` - Lowercase word
- `Meta+C` - Capitalize word

### Advanced
- `Ctrl+X (` - Begin macro
- `Ctrl+X )` - End macro
- `Ctrl+X E` - Execute macro
- `Meta+X` - Execute named command
- `Meta+K` - Bind to key
- `Ctrl+X =` - Show cursor position
- `Meta+G` - Go to line
- `Ctrl+L` - Redraw screen

### Numeric Args + M‑x Prompt
- `Meta+X` opens a simple prompt (`: `) that accepts a command name.
- Many commands take numeric prefix arguments (no inline typing at the prompt).
  - Example (writing mode at 100 cols): `ESC 1 0 0 ESC x writing-mode RET`.

### Help Prefix on C-h (Optional)

- By default, `C-h` is Backspace (delete previous character), matching historical μEmacs behavior.
- Help is available via `M-?` and `M-x describe-*` commands by default.
- You can enable an Emacs-style help prefix on `C-h` with:
  - `M-x enable-help-prefix` → `C-h k` (describe-key), `C-h b` (describe-bindings)
  - `M-x disable-help-prefix` → restores `C-h` as Backspace
- To persist this behavior, add `enable-help-prefix` to your startup file (e.g., `emacs.rc`).

## Execute Named Commands (Meta+X)

All commands available via `Meta+X` followed by command name:

### Core Commands
- `abort-command` - Cancel current operation
- `exit-emacs` - Exit editor
- `quick-exit` - Quick save and exit
- `help` - Display help
- `execute-named-command` - Prompt for command

### Encryption Commands
- `encrypt-buffer` - Encrypt current buffer with GPG (AES-256)
- `decrypt-file` - Decrypt and open GPG-encrypted file
- `encrypt-buffer-age` - Encrypt with age (modern alternative)
- `decrypt-file-age` - Decrypt age-encrypted file
- `encrypt-buffer-auto` - Auto-detect best encryption tool
- `show-encryption-tools` - Display available crypto tools

### Buffer Management
- `find-file` - Open file
- `save-file` - Save current buffer
- `write-file` - Write buffer to file
- `read-file` - Insert file contents
- `view-file` - Open file read-only
- `insert-file` - Insert file at cursor
- `list-buffers` - Show buffer list
- `select-buffer` - Switch to buffer
- `next-buffer` - Next buffer
- `delete-buffer` - Kill buffer
- `name-buffer` - Rename buffer

### Navigation
- `beginning-of-file` - Go to start
- `end-of-file` - Go to end
- `beginning-of-line` - Go to line start
- `end-of-line` - Go to line end
- `goto-line` - Go to line number
- `next-line` - Move down
- `previous-line` - Move up
- `forward-character` - Move right
- `backward-character` - Move left
- `next-word` - Next word
- `previous-word` - Previous word
- `next-subword` - Next subword (camelCase)
- `previous-subword` - Previous subword
- `goto-matching-fence` - Jump to matching bracket

### Editing
- `delete-next-character` - Delete char forward
- `delete-previous-character` - Delete char backward
- `delete-next-word` - Delete word forward
- `delete-previous-word` - Delete word backward
- `kill-to-end-of-line` - Kill to line end
- `kill-region` - Kill selected region
- `copy-region` - Copy region
- `yank` - Paste
- `yank-pop` - Cycle kill ring
- `yank-clipboard` - Paste from clipboard
- `quote-character` - Insert literal char
- `transpose-characters` - Swap chars
- `duplicate-line` - Duplicate current line
- `move-line-up` - Move line up
- `move-line-down` - Move line down

### Text Formatting
- `case-word-upper` - Uppercase word
- `case-word-lower` - Lowercase word
- `case-word-capitalize` - Capitalize word
- `case-region-upper` - Uppercase region
- `case-region-lower` - Lowercase region
- `fill-paragraph` - Wrap paragraph
- `justify-paragraph` - Justify text
- `count-words` - Count words in region

### Search & Replace
- `search-forward` - Search forward
- `search-reverse` - Search backward
- `incremental-search` - Interactive search
- `reverse-incremental-search` - Reverse isearch
- `replace-string` - Replace all
- `query-replace-string` - Interactive replace
- `hunt-forward` - Repeat search forward
- `hunt-backward` - Repeat search backward

### Window Management
- `split-current-window` - Split window
- `delete-window` - Close window
- `delete-other-windows` - Keep only current
- `next-window` - Switch to next window
- `previous-window` - Switch to previous window
- `grow-window` - Enlarge window
- `shrink-window` - Shrink window
- `resize-window` - Resize window

### Macros & Programming
- `begin-macro` - Start macro recording
- `end-macro` - End macro recording
- `execute-macro` - Run macro
- `store-macro` - Save macro
- `execute-buffer` - Execute buffer as commands
- `execute-file` - Execute file as commands
- `execute-command-line` - Execute shell command
- `execute-program` - Run external program
- `shell-command` - Run shell command
- `pipe-command` - Pipe region through command
- `filter-buffer` - Filter buffer through command

### Configuration
- `bind-to-key` - Bind command to key
- `unbind-key` - Remove key binding
- `describe-key` - Show key binding
- `describe-bindings` - Show all bindings
- `add-mode` - Add buffer mode
- `delete-mode` - Remove buffer mode
- `add-global-mode` - Add global mode
- `delete-global-mode` - Remove global mode
- `set` - Set variable
- `set-column-width` - Set wrap column (prompts for number)
- `writing-mode` - Enable a writing session (sets wrap to 80 or numeric arg, turns on wrap)
- `exit-writing-mode` - Restore prior wrap/fill settings
- `set-mark` - Set mark
- `exchange-point-and-mark` - Swap cursor and mark
- `unmark-buffer` - Clear mark

### Advanced Operations
- `undo` - Undo last operation
- `redo` - Redo last undone operation
- `universal-argument` - Repeat next command
- `clear-and-redraw` - Refresh display
- `redraw-display` - Redraw screen
- `update-screen` - Update display
- `apropos` - Search command help

## User Settings (JSON)
## WARNING: The customization system is still a Work in Progress...

## Notes on Configuration

- **XDG compliant**: User settings in `~/.config/muemacs/` (honors `$XDG_CONFIG_HOME`)
- **No sudo required**: User config takes precedence over system defaults
- **Simple by design**: Flat JSON, human-readable, easy to edit manually or via commands
- **Fallback chain**: Missing user config → system defaults → in-tree defaults

## Configuration Hierarchy

μEmacs follows the XDG Base Directory specification for user configuration:

**Priority order (highest to lowest):**
1. **User config**: `~/.config/muemacs/settings.json` (or `$XDG_CONFIG_HOME/muemacs/settings.json`)
2. **System defaults**: `/usr/share/muemacs/editor/settings.json`
3. **In-tree fallback**: `config/editor/settings.json` (development builds)

**Behavior:**
- Settings are read from the first location found (user config takes precedence)
- `M-x save-settings` writes to user config directory (creates it if needed)
- No elevated permissions needed for user customization

Example settings.json

```json
{
  "column_width": 80,
  "wrap": true,
  "ruler": true,
  "rulercol": 80,
  "highlightline": true,
  "hilinestyle": 4,
  "rulerstyle": 4,
  "highlight_intensity": 15,
  "ruler_intensity": 10,
  "intersection_intensity": 20,
  "highlight_strategy": 0,
  "modeline_show_git": true,
  "modeline_show_stats": true,
  "modeline_show_modes": true,
  "modeline_show_position": true
}
```

## Modern Features Deep Dive

### Undo/Redo System
- **Intelligent Grouping**: Auto-coalesces character insertions within 400ms windows and word boundaries
- **Atomic Operations**: Thread-safe with `_Atomic` operations and circular buffer architecture
- **Dynamic Capacity**: Starts at 100 operations, expands to 10,000 with 2x growth factor
- **Version Tracking**: Each operation has unique 64-bit timestamp and version ID
- **Memory Safety**: Prevents recursive undo/redo, tracks resize failures

### Status Line Customization
- **Real-time Git Integration**: Shows branch name and dirty state (`*`) with background pthread updates
- **Atomic Buffer Statistics**: Line count, column position, file size with lock-free updates
- **Mode Indicators**: Configurable display of buffer modes (CMODE, VIEW, etc.)
- **Terminal-Adaptive**: Automatically adjusts for terminal width and capabilities
- **Performance Metrics**: Optional display of search timing and memory usage
- **Clean/Dirty Baseline**: Accurate saved-state delta; undo back to saved state clears the dirty indicator

### Advanced Search
- **Boyer-Moore-Horspool**: Sublinear O(n/m) average case with 256-character bad-character table
- **Thompson NFA**: Zero-heap regex engine supporting `.`, `[char-class]`, `^$` anchors, `*` closure
- **Hybrid Selection**: Automatically chooses optimal algorithm based on pattern complexity
- **Case-Insensitive**: Optional case folding for both search engines
- **Cross-line Patterns**: Multi-line regex support with proper anchoring

### Terminal Integration
- **Bracketed Paste**: Automatic detection of `ESC[200~...ESC[201~` sequences with state machine parser
- **Alt Screen Restore**: Leaves the terminal history intact after exit, avoiding the “split screen” artifact
- **24-bit Color**: RGB color support `\033[38;2;r;g;b`m for modern terminals
- **Cursor Shapes**: Block, underline, bar cursor styles with capability detection
- **GPU Terminal Optimization**: Designed for Alacritty, Kitty, WezTerm performance
- **Unicode Locale**: Automatic UTF-8 locale initialization and validation

### Plugin System
- **Event Hooks**: Register callbacks for editor events (file load, buffer switch, etc.)
- **Multiple Handlers**: Up to 8 hooks per event type with context passing
- **Dynamic Registration**: Runtime hook registration/deregistration
- **Type Safety**: Enumerated event types with validation

### C23 Architecture
- **Atomic Data Structures**: Kill ring, undo stack, terminal state, display matrix
- **Memory Safety**: Centralized allocation with leak tracking and overflow protection
- **Thread Safety**: Signal-safe operations throughout with generation counting
- **Zero-Copy Operations**: Efficient string handling with gap buffer and UTF-8 awareness

## Encryption Support
**Modern encryption via external tools:**
- Removed insecure DLH-POLY-86-B cipher (1986)
- Replaced with shell-out to battle-tested crypto tools
- Unix philosophy: compose tools, don't reinvent crypto

These updates focus on everyday usability while preserving the uEmacs ethos:
- UTF‑8 everywhere: robust handling and terminal fidelity.
- Safer defaults: stack protections, hardened flags; no runtime overhead.
- Terminal niceties: bracketed paste, clean alt‑screen restore, truecolor support.
- Packaging quality of life: optional AUR/DEB packaging; still a single binary at runtime.
- Discoverability: desktop entry and man page; launch remains TUI, terminal‑native.
- Practical alias: `muEmacs` symlink to avoid typing the micro symbol.

## Testing

Recommended for CI and non-interactive environments:

```bash
# Run the non-interactive integration suite (fast, no PTY/expect required)
./build/bin/full_integration_test

# Or via CMake target
cmake --build . --target api_tests -j
```
Interactive Expect-based tests are optional and disabled by default. To run them on a real terminal:

```bash
# Enable interactive tests explicitly and run
cmake --build . --target itests -j

# Equivalent manual invocation
UEMACS_INTERACTIVE=1 ENABLE_EXPECT=1 ./build/bin/full_integration_test
```

Notes
- Interactive tests require a real TTY/PTY. In CI or headless shells they will be skipped automatically.
- The helper `interactive_stress_test.exp` will auto-exit unless `UEMACS_INTERACTIVE=1` is set.

Performance benchmarks

```bash
make bench
```

Heavy Stress Testing (non-interactive)

```bash
# Run extreme stress suite with large operation counts
cmake --build . --target stress -j

# Scale intensity (default factor=1). Example: 3x intensity with longer phase timeouts
UEMACS_STRESS=3 UEMACS_PHASE_TIMEOUT=3600 ./build/bin/full_integration_test

# Enable the separate ultra-stress suite only when desired
EXTREME_STRESS=1 ./build/bin/full_integration_test
```

## Config Snippets

Add to your startup file (e.g., `emacs.rc`):

  enable-help-prefix
  # popular rebindings
  bind-to-key kill-region ^W
  bind-to-key yank ^Y

Writing at fixed columns (keyboard-only)
- One-shot session: `M-x writing-mode` (wraps at 80). With a numeric argument, uses that width (e.g., `M-100 M-x writing-mode`).
- Exit and restore prior settings: `M-x exit-writing-mode`.
- Persist defaults (always start wrapped at 80):

  set-column-width 80
  add-global-mode wrap

Interactive tips
- `M-x` is bound to both `Meta+X` and `Meta+x`.

- Optional convenience (pick your own keys):
  # bind-to-key writing-mode ^X W
  # bind-to-key exit-writing-mode M-Q

Current line and column guide (visual helpers)
- Highlight current line (reverse video row):

  set highlightline 1

- Enable a vertical column ruler (reverse video on a specific column):

  set ruler 1
  set rulercol 80   # change to your preferred column

- Disable either at any time:

  set highlightline 0
  set ruler 0

Styles (inherit terminal theme; no fixed colors)
- Choose styles: 0 none, 1 underline (default), 2 bold, 3 dim

  set hilinestyle 1   # underline the current line
  set rulerstyle 1    # underline the ruler column

- Example: dim current line, bold ruler:

  set hilinestyle 3
  set rulerstyle 2

## Terminal Fidelity

- Font rendering & smoothing
  - Inherited from your terminal emulator (FreeType/Pango on Linux). μEmacs (a TUI) does not control antialiasing. Configure smoothing and font choice in your terminal.

- Color depth
  - Truecolor: set `COLORTERM=truecolor` and use a terminal that supports 24-bit color (Kitty/WezTerm/Alacritty). μEmacs will detect and honor capabilities but does not force a scheme.
  - Palette: when colors are needed, prefer terminal palette indices and defaults; μEmacs avoids hardcoded RGB and reverse video for theme fidelity.

- TERM and compatibility
  - Use a sensible `TERM` (e.g., `xterm-256color` or terminal-specific like `xterm-kitty`).
  - ESC as Meta is fully supported; Kitty “CSI u” keyboard protocol is decoded for precise modifiers (Alt/Meta, Ctrl). Shift is not encoded in legacy mappings by design.

- Keyboard-only design
  - Mouse interactions are intentionally unsupported in this build. All focus is on fast, low-latency keyboard editing.

- Visual helpers
  - Current line and column ruler use ANSI SGR attributes (underline/bold/dim) to inherit your terminal’s theme. Configure with `set highlightline`, `set ruler`, `set rulercol`, `set hilinestyle`, and `set rulerstyle`.

### Optional: Quick Terminal Tips
- Truecolor: export `COLORTERM=truecolor` in your shell profile and use a terminal that supports 24‑bit color (Kitty/WezTerm/Alacritty). Example:

  export COLORTERM=truecolor

- Fonts and smoothing: pick a monospaced font you like (e.g., JetBrains Mono, Fira Code, Iosevka Mono) in your terminal settings. Smoothing/antialiasing is controlled by the terminal/OS.

- TERM: use `xterm-256color` or a terminal‑specific `$TERM` (e.g., `xterm-kitty`) for accurate capabilities.

- Sample terminal configs (minimal):
  - Alacritty: set `env: { COLORTERM: truecolor }`, pick a font in `font:` block, choose a theme under `colors:`.
  - Kitty: add `allow_hyperlinks no`, pick a font under `font_family`, and select a `color_theme`.
  - WezTerm: in `wezterm.lua`, set `enable_wayland = true` (Linux), `font = wezterm.font("Your Mono Font")`, and `color_scheme = "YourTheme"`.

### Optional: Visual Presets (rc examples)
- Minimal underline (default):

  set highlightline 1
  set hilinestyle 1
  set ruler 1
  set rulerstyle 1
  set rulercol 80

- Subtle dim:

  set highlightline 1
  set hilinestyle 3   # dim current line
  set ruler 1
  set rulerstyle 1    # underline ruler

- High‑contrast (bold ruler):

  set highlightline 1
  set hilinestyle 1   # underline current line
  set ruler 1
  set rulerstyle 2    # bold ruler column

- No helpers:

  set highlightline 0
  set ruler 0

## History

μEmacs traces a straight line from MicroEMACS (1985) through Petri Kutvonen’s uEmacs/PK to Linus Torvalds’ tiny personal uEmacs. This project keeps that lineage intact — modernizing just enough for 2025 while preserving the small, fast, drop‑in editor Linus prefers.

μEmacs builds on a small, portable lineage:
- MicroEMACS (1985): Dave G. Conroy; later Daniel M. Lawrence.
- uEmacs/PK (1990s): Petri H. Kutvonen’s enhanced MicroEMACS 3.9e.
- Linus Torvalds’ uEmacs: a simple, personal fork maintained as a tiny, fast editor.

Why small matters (uEmacs/PK README): “Creeping featurism, growing size, and reduced portability made [later versions] less attractive… [uEmacs/PK] adds some new functionality and comfort but does not sacrifice the best things (small size and portability).”

References
- uEmacs (Torvalds): https://github.com/torvalds/uemacs
- uEmacs/PK README (history and goals): https://github.com/torvalds/uemacs/blob/master/README
- MicroEMACS (overview): https://en.wikipedia.org/wiki/MicroEMACS

## Credits

- Original μEmacs: Linus Torvalds
- C23 Modernization: Will Clingan
- Version 0.0.23: Homage to C23 standard implementation

## License

Original μEmacs license terms apply. Modernization preserves Linus Torvalds' vision and minimalist philosophy.
See also
- docs/TERMINAL_COMPATIBILITY.md
- docs/UPGRADING.md
