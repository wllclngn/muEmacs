# Terminal Compatibility

Targets: Linux terminals (Kitty, Alacritty, WezTerm, xterm-256color, VSCode)

- Bracketed Paste: ESC[200~ ... ESC[201~ supported and fuzz-tested. Paste content is not recorded into macros.
- Kitty CSI u: Unicode + modifiers decoded (Alt→META, Ctrl→CONTROL), leaving non-ASCII codepoints intact. Shift is not encoded in legacy mapping.
 - Mouse: Not supported; editor is keyboard-only.
- Truecolor: Detected via $COLORTERM and probe; falls back gracefully.

Recommendations:
- Set TERM to `xterm-256color` or `xterm-kitty` on Kitty for best results.
- On remote shells, ensure bracketed paste is not stripped by intermediary tools.
