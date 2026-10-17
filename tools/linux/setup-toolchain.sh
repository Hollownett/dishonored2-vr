#!/usr/bin/env bash
# Idempotent, no-sudo setup of the Linux harness toolchain:
#   - LLVM (clang-cl, lld-link, llvm-lib, llvm-rc) from an official release tarball
#   - xwin-splatted Microsoft CRT + Windows SDK (you accept Microsoft's license by running this)
# Everything lives under $D2VR_TOOLCHAIN (default ~/.local/share/d2vr-toolchain),
# except cargo's own registry cache under ~/.cargo.
set -euo pipefail

D2VR_TOOLCHAIN="${D2VR_TOOLCHAIN:-$HOME/.local/share/d2vr-toolchain}"

LLVM_VERSION="23.1.3"
LLVM_TARBALL="LLVM-${LLVM_VERSION}-Linux-X64.tar.xz"
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${LLVM_TARBALL}"
# SHA-256 published by GitHub for the release asset (observed 2026-10-17).
LLVM_SHA256="ca9fe385a5d9e2d88132dc0e23a00e9f87f38fa75f67207b5e457d208e71d013"

XWIN_VERSION="0.10.0"

die() { echo "setup-toolchain: $*" >&2; exit 1; }
command -v curl >/dev/null || die "curl is required"
command -v tar >/dev/null || die "tar is required"
command -v sha256sum >/dev/null || die "sha256sum is required"

mkdir -p "$D2VR_TOOLCHAIN"

# ---- LLVM -------------------------------------------------------------------
if [[ -x "$D2VR_TOOLCHAIN/llvm/bin/clang-cl" ]] \
   && [[ "$(cat "$D2VR_TOOLCHAIN/llvm/.d2vr-version" 2>/dev/null)" == "$LLVM_VERSION" ]]; then
  echo "LLVM $LLVM_VERSION already installed"
else
  dl="$D2VR_TOOLCHAIN/downloads-llvm"   # untrusted download lives in its own directory
  mkdir -p "$dl"
  if [[ ! -f "$dl/$LLVM_TARBALL" ]] || ! echo "$LLVM_SHA256  $dl/$LLVM_TARBALL" | sha256sum -c --status; then
    echo "Downloading $LLVM_URL"
    curl -fL --no-progress-meter --retry 3 -C - -o "$dl/$LLVM_TARBALL" "$LLVM_URL"
  fi
  echo "$LLVM_SHA256  $dl/$LLVM_TARBALL" | sha256sum -c - || die "LLVM tarball checksum mismatch"
  rm -rf "$D2VR_TOOLCHAIN/llvm.tmp"; mkdir -p "$D2VR_TOOLCHAIN/llvm.tmp"
  tar -xf "$dl/$LLVM_TARBALL" -C "$D2VR_TOOLCHAIN/llvm.tmp" --strip-components=1
  echo "$LLVM_VERSION" > "$D2VR_TOOLCHAIN/llvm.tmp/.d2vr-version"
  rm -rf "$D2VR_TOOLCHAIN/llvm"; mv "$D2VR_TOOLCHAIN/llvm.tmp" "$D2VR_TOOLCHAIN/llvm"
  rm -rf "$dl"
fi
for t in clang-cl lld-link llvm-lib llvm-rc; do
  [[ -x "$D2VR_TOOLCHAIN/llvm/bin/$t" ]] || die "LLVM tool missing: $t"
done

# ---- xwin -------------------------------------------------------------------
XWIN_BIN="$D2VR_TOOLCHAIN/xwin-bin/bin/xwin"
if [[ ! -x "$XWIN_BIN" ]] || ! "$XWIN_BIN" --version | grep -q "$XWIN_VERSION"; then
  command -v cargo >/dev/null || die "cargo is required to install xwin"
  cargo install xwin --version "$XWIN_VERSION" --locked --root "$D2VR_TOOLCHAIN/xwin-bin"
fi

if [[ -f "$D2VR_TOOLCHAIN/xwin/.d2vr-done" ]]; then
  echo "xwin CRT/SDK already splatted"
else
  "$XWIN_BIN" --accept-license --http-retry 5 --timeout 300s \
      --cache-dir "$D2VR_TOOLCHAIN/xwin-cache" --arch x86_64 --variant desktop \
      splat --output "$D2VR_TOOLCHAIN/xwin"
  touch "$D2VR_TOOLCHAIN/xwin/.d2vr-done"
fi

mkdir -p "$D2VR_TOOLCHAIN/wineprefix"

cat <<OUT

Toolchain ready under $D2VR_TOOLCHAIN
  LLVM  : $D2VR_TOOLCHAIN/llvm/bin ($("$D2VR_TOOLCHAIN/llvm/bin/clang-cl" --version | head -1))
  xwin  : $D2VR_TOOLCHAIN/xwin/{crt,sdk}
  wine  : WINEPREFIX=$D2VR_TOOLCHAIN/wineprefix (created on first run)
Use: python3 tools/linux/run_build_cmd.py --keep-going
(export D2VR_TOOLCHAIN=... first if you used a non-default location)
OUT
