# μEmacs Packaging Status

**Status: Needs verification pass.** The pieces below exist but none of
them has been re-verified against a clean v3.1.0 install. Do not treat
this directory as release-ready until each item is checked.

## What Exists

### Install rules (CMake)
- `muEmacs` binary and `muemacs-ext-runner` (install TARGETS)
- `packaging/muEmacs.desktop` - desktop entry
- `packaging/muEmacs.1` - manual page
- `packaging/muemacs-icon.svg` - application icon
- `config/` defaults, `scripts/uep_build.py`, `include/uep` headers,
  `README.md`

### Distribution files
- `debian/` - control, rules, changelog, copyright, compat
- `PKGBUILD` - Arch build script (pkgname muemacs, pkgver 3.1.0)
- `_aur_local/PKGBUILD` - local-tarball variant

### Legal
- Icon attribution documented (ICON_ATTRIBUTION.txt, CC BY-SA 4.0)
- Packaging license set to GPL-2.0; lineage vs the original MicroEMACS
  license is pending owner verification

## Verification Checklist (open)

1. Clean-chroot build of the PKGBUILD (`makepkg`, then `namcap`)
2. `dpkg-buildpackage` run against `debian/` on a current Debian/Ubuntu
3. Confirm installed paths: binary, desktop file, icon, man page
4. Fill the TODO homepage/url placeholders with the real project URL
5. Generate real sha256sums (both PKGBUILDs currently use SKIP)
6. Resolve the license lineage question, then finalize license fields

## Repository Submission (after verification)

- Debian: File ITP via `reportbug wnpp`, find a sponsor on
  debian-mentors, upload to mentors.debian.net
- AUR: Set up SSH key on aur.archlinux.org, push PKGBUILD and .SRCINFO
