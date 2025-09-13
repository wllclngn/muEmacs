# Upgrading from torvalds/uemacs

This project is a Linux‑only C23 modernization of μEmacs, keeping core UX while updating internals.

- Keymaps: legacy `keytab` is imported once at startup, then modern hash maps handle lookups. Runtime binding (`bind-to-key` / `unbind-key`) now updates modern keymaps too.
- Help prefix: `C-h` defaults to Backspace. Use `M-x enable-help-prefix` to enable `C-h k`/`C-h b`, and `M-x disable-help-prefix` to restore Backspace.
- Terminal input: Bracketed paste is supported. Kitty “CSI u” decoding is implemented for precise modifiers; non-ASCII keys are preserved.
 - Mouse: Not supported; editor is keyboard-only by design.

Example rc snippet:

  enable-help-prefix
  # Popular rebindings
  bind-to-key kill-region ^W
  bind-to-key yank ^Y

Notes:
- `NBINDS`/linear scans are deprecated from hot paths. Use `M-x keymap-stats` to view lookup stats.
