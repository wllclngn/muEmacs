#!/usr/bin/env bash
set -euo pipefail

# μEmacs system installer with auto-detection.
# Modes:
#   - auto  (default): detect distro and choose best path
#   - aur   (Arch/AUR-style): build + install via packaging/PKGBUILD
#   - cmake (generic): cmake --install to a prefix (default /usr/local)
#   - deb   (Debian/Ubuntu): build DEB with CPack and apt install
#
# Examples:
#   ./scripts/system_install.sh                  # auto-detect
#   ./scripts/system_install.sh --mode aur       # force AUR flow
#   ./scripts/system_install.sh --mode cmake --prefix /usr/local
#   ./scripts/system_install.sh --mode deb       # generate + install .deb

prefix="/usr/local"
build_dir="build"
mode="auto"
build_type="Release"
noconfirm=0

usage() {
  cat <<EOF
Usage: $0 [--mode auto|aur|cmake|deb] [--prefix DIR] [--debug] [--noconfirm]

Builds/installs μEmacs system-wide. Auto mode prefers AUR on Arch systems,
DEB on Debian-like systems, otherwise falls back to cmake install.

Options:
  --mode M        Install mode: auto|aur|cmake|deb (default: auto)
  --prefix DIR    Install prefix for cmake mode (default: /usr/local)
  --debug         Build Debug (sanitizers) instead of Release
  --noconfirm     Non-interactive install where supported (Arch/apt)
  -h, --help      Show this help
EOF
}

have() { command -v "$1" >/dev/null 2>&1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode) mode="$2"; shift 2;;
    --prefix) prefix="$2"; shift 2;;
    --debug) build_type="Debug"; shift;;
    --noconfirm) noconfirm=1; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown option: $1"; usage; exit 2;;
  esac
done

detect_distro() {
  local id="" like=""
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    id="${ID:-}"
    like="${ID_LIKE:-}"
  fi
  echo "${id};${like}"
}

aur_install() {
  echo "[μEmacs] AUR-style install"
  if ! have makepkg || ! have pacman; then
    echo "[ERROR] makepkg/pacman not found. Install base-devel and pacman." >&2
    exit 3
  fi

  # Prefer a local, network-free PKGBUILD that packages this working tree.
  # Resolve repository root to create absolute staging paths
  local repo_root
  repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
  local workdir="${repo_root}/packaging/_aur_local"
  rm -rf "${workdir}" && mkdir -p "${workdir}"

  # Derive version from CMakeLists.txt (fallback to 0.0.0)
  local ver
  ver=$(grep -Eo 'project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)' CMakeLists.txt | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n1 || true)
  [[ -z "${ver}" ]] && ver="0.0.0"

  # Create source tarball from current tree with top-level folder muemacs-${ver}
  local tarbase="muemacs-${ver}"
  local tarball="${workdir}/${tarbase}.tar.gz"
  echo "[μEmacs] Creating source tarball: ${tarball}"
  tar --exclude-vcs \
      --exclude=build \
      --exclude=_install \
      --exclude=packaging/_aur_local \
      --transform "s,^,${tarbase}/,S" \
      -czf "${tarball}" .

  # Generate a minimal PKGBUILD that uses the local tarball
  cat > "${workdir}/PKGBUILD" <<'EOF'
pkgname=muemacs
pkgver=0.0.0
pkgrel=1
pkgdesc="Modern Linux terminal text editor (μEmacs)"
arch=('x86_64')
url="https://github.com/wllclngn/muEmacs"
license=('MIT')
depends=('ncurses')
makedepends=('cmake' 'gcc')
source=("muemacs-${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
  cd "${srcdir}/muemacs-${pkgver}"
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF
  cmake --build build --target muEmacs -j"${CI_CPUS:-$(nproc)}"
}

package() {
  cd "${srcdir}/muemacs-${pkgver}"
  DESTDIR="${pkgdir}" cmake --install build --prefix /usr
}
EOF

  # Inject detected version into PKGBUILD
  sed -i "s/^pkgver=.*/pkgver=${ver}/" "${workdir}/PKGBUILD"

  # Build and install the package locally via pacman
  # Build in a temp dir without spaces to avoid -ffile-prefix-map issues
  local tmpdir
  tmpdir=$(mktemp -d -t muemacs-aur-XXXXXX)
  cp "${workdir}/PKGBUILD" "${tmpdir}/PKGBUILD"
  cp "${tarball}" "${tmpdir}/muemacs-${ver}.tar.gz"
  pushd "${tmpdir}" >/dev/null
  local flags=( -s )
  (( noconfirm )) && flags+=(--noconfirm)
  # Ensure UTF-8 locale so bsdtar handles 'μ' paths without warnings
  local utf8_locale="en_US.UTF-8"
  if locale -a 2>/dev/null | grep -qiE '^en_US\.utf-?8$'; then
    utf8_locale="en_US.UTF-8"
  elif locale -a 2>/dev/null | grep -qiE '^C\.utf-?8$'; then
    utf8_locale="C.UTF-8"
  fi
  echo "[μEmacs] Running: LANG=${utf8_locale} LC_ALL=${utf8_locale} makepkg -si ${flags[*]}"
  LANG="${utf8_locale}" LC_ALL="${utf8_locale}" LC_CTYPE="${utf8_locale}" makepkg -si "${flags[@]}" || true
  # Collect built packages for manual installation if needed
  mkdir -p "${workdir}/out"
  shopt -s nullglob
  for pkg in "${tmpdir}"/*.pkg.tar.*; do
    cp -f "$pkg" "${workdir}/out/" || true
  done
  shopt -u nullglob
  popd >/dev/null
  echo "[μEmacs] AUR packages staged under: ${workdir}/out"
  echo "[μEmacs] To install manually: sudo pacman -U ${workdir}/out/*.pkg.tar.*"
}

cmake_install() {
  echo "[μEmacs] Configuring (${build_type})..."
  cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE="${build_type}" >/dev/null
  echo "[μEmacs] Building..."
  cmake --build "${build_dir}" -j"${CI_CPUS:-$(nproc)}"
  echo "[μEmacs] Installing to ${prefix} (sudo may prompt)..."
  cmake --install "${build_dir}" --prefix "${prefix}"
  echo "[μEmacs] Done. Try running: muEmacs"
}

deb_install() {
  # Build and package a .deb via CPack, then install via apt
  if ! have cpack; then
    echo "[WARN] cpack not found; falling back to cmake install" >&2
    cmake_install
    return
  fi
  echo "[μEmacs] Configuring (${build_type}) for DEB packaging..."
  cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE="${build_type}" >/dev/null
  echo "[μEmacs] Building..."
  cmake --build "${build_dir}" -j"${CI_CPUS:-$(nproc)}"
  echo "[μEmacs] Creating .deb package..."
  ( cd "${build_dir}" && cpack -G DEB >/dev/null )
  deb_pkg=$(ls -1 "${build_dir}"/*.deb 2>/dev/null | head -n1 || true)
  if [[ -z "${deb_pkg}" ]]; then
    echo "[ERROR] No .deb produced; falling back to cmake install" >&2
    cmake_install
    return
  fi
  echo "[μEmacs] Installing package: ${deb_pkg}"
  if have apt; then
    if (( noconfirm )); then sudo apt install -y "${deb_pkg}"; else sudo apt install "${deb_pkg}"; fi
  else
    echo "[INFO] 'apt' not found; use your package manager to install: ${deb_pkg}"
  fi
}

run_auto() {
  IFS=';' read -r id like <<<"$(detect_distro)"
  if have pacman || [[ "${id}" == "arch" || "${like}" == *arch* ]]; then
    aur_install; return
  fi
  if have apt || [[ "${id}" == "debian" || "${id}" == "ubuntu" || "${like}" == *debian* ]]; then
    deb_install; return
  fi
  cmake_install
}

case "${mode}" in
  auto) run_auto ;;
  aur) aur_install ;;
  cmake) cmake_install ;;
  deb) deb_install ;;
  *) echo "[ERROR] Unknown mode: ${mode}"; usage; exit 2 ;;
esac
