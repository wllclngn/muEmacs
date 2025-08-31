# μEmacs v0.0.23 - Modern C23 Linux Text Editor

**Linus Torvalds' original μEmacs, transformed into a pure C23 implementation for modern Linux systems.**

## Overview

μEmacs is a minimalist text editor preserving Linus Torvalds' original keybindings and philosophy, now implemented with modern C23 standards. This version removes all legacy platform code, focusing exclusively on Linux performance and reliability.

## Key Features

**Core Implementation** (~28,255 lines of C23 code)
- **C23 Implementation**: Modern atomic operations, thread safety, and memory management
- **Linus's Original Keybindings**: Preserved exactly as designed, with undo/redo on available keys
- **O(1) Keymap System**: Hash-based lookup for instant command execution
- **Zero Legacy Code**: 500+ lines of obsolete platform code removed (MSDOS, VMS, etc.)

**Advanced Text Editing**
- **VSCode-Style Undo/Redo**: Atomic circular buffer with intelligent operation grouping, 400ms coalescing window, and 10,000 operation capacity
- **Modern Kill Ring**: 32-entry atomic circular buffer (8KB per entry) with system clipboard integration via xclip/xsel
- **Multiple Search Engines**: Boyer-Moore-Horspool (O(n/m) performance) and Thompson NFA regex engine with zero-heap runtime
- **UTF-8 Support**: Full Unicode handling with proper terminal integration, bidirectional conversion, and minimality validation

**Terminal Integration** 
- **Modern Terminal Support**: 24-bit true color, bracketed paste mode, GPU terminal optimization (Alacritty, Kitty)
- **Signal-Safe Operations**: Atomic terminal state changes with generation counting and concurrent access protection
- **Customizable Status Line**: Real-time git integration, atomic buffer statistics, and configurable mode indicators

**Performance & Reliability**
- **Gap Buffer**: O(1) insert/delete at cursor with dynamic resizing and UTF-8 awareness  
- **Display Matrix**: Dirty region tracking with hardware-accelerated scrolling and selection highlighting
- **Memory Safety**: Centralized allocation system with leak tracking and overflow protection

## Build & Installation

```bash
# Clone and build
git clone [repository] && cd μEmacs
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./build/bin/μEmacs [filename]
```

## Requirements

- Linux (kernel 5.0+)
- GCC 12+ or Clang 15+ with C23 support
- ncursesw library
- 4MB RAM minimum

## Testing

```bash
# Run test suite
./build/bin/full_integration_test

# Performance benchmarks
make bench
```

## Key Bindings

**Meta Key**: `Ctrl+[` (ESC key) or `Ctrl+]` for prefix

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

### Arrow Keys
- `↑` `↓` `←` `→` - Navigate (when terminal supports)

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

### Advanced Search
- **Boyer-Moore-Horspool**: Sublinear O(n/m) average case with 256-character bad-character table
- **Thompson NFA**: Zero-heap regex engine supporting `.`, `[char-class]`, `^$` anchors, `*` closure
- **Hybrid Selection**: Automatically chooses optimal algorithm based on pattern complexity
- **Case-Insensitive**: Optional case folding for both search engines
- **Cross-line Patterns**: Multi-line regex support with proper anchoring

### Terminal Integration
- **Bracketed Paste**: Automatic detection of `ESC[200~...ESC[201~` sequences with state machine parser
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


## Credits

- Original μEmacs: Linus Torvalds
- C23 Modernization: Will Clingan
- Version 0.0.23: Homage to C23 standard implementation

## License

Original μEmacs license terms apply. Modernization preserves Linus Torvalds' vision and minimalist philosophy.
