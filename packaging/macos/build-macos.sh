#!/usr/bin/env bash
# Builds KumCAD in Release mode and packages it as a kumcad.app bundle plus
# a .dmg for distribution, using Homebrew's Qt6 and OpenCASCADE. Output
# lands in dist/.
#
# Requires: Homebrew, with `brew install qt opencascade ninja` (this script
# runs that for you). Run on macOS only -- unlike packaging/linux and
# packaging/windows, this can't be cross-built from Linux, since macdeployqt
# and iconutil are themselves macOS-only tools.
#
# Honesty note: this script is written and reasoned through carefully, and
# .github/workflows/ci.yml's macos job exercises the cmake configure/build/
# ctest steps on every push -- but the packaging steps below (icon
# generation, macdeployqt, dmg creation) are NOT covered by that CI job and
# have never been run on physical Apple hardware, since this project is
# developed on Linux. Treat the .app/.dmg output as unverified until someone
# actually runs this on a Mac and checks it launches.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pkg_dir="$repo_root/packaging/macos"
build_dir="$repo_root/build"
dist_dir="$repo_root/dist"
version="${KUMCAD_VERSION:-0.1.0}"

command -v brew >/dev/null || {
    echo "error: Homebrew not found -- install it from https://brew.sh first" >&2
    exit 1
}

echo "==> Installing Qt6 + OpenCASCADE (brew)"
brew install qt opencascade ninja >/dev/null

mkdir -p "$dist_dir"

echo "==> Generating kumcad.icns from the app's own icon"
# Reuses packaging/linux/kumcad.png (rendered from src/app/IconFactory.cpp,
# see packaging/linux/generate-icon.sh) as the source for every iconset
# size, rather than committing a separate binary .icns. Sizes above 256px
# are upscaled from that same 256x256 source, so they won't be as crisp as
# a purpose-rendered 1024px icon -- acceptable for now, revisit if it looks
# soft in the Dock.
iconset="$pkg_dir/kumcad.iconset"
rm -rf "$iconset"
mkdir -p "$iconset"
src_png="$repo_root/packaging/linux/kumcad.png"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$src_png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$src_png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$pkg_dir/kumcad.icns"
rm -rf "$iconset"

echo "==> Building KumCAD (Release)"
qt_prefix="$(brew --prefix qt)"
cmake -S "$repo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt_prefix"
cmake --build "$build_dir" -j

app_bundle="$build_dir/src/app/kumcad.app"
[[ -d "$app_bundle" ]] || {
    echo "error: expected app bundle not found at $app_bundle" >&2
    exit 1
}

echo "==> Deploying Qt frameworks + creating .dmg"
dmg_out="$dist_dir/KumCAD-${version}-macOS.dmg"
rm -f "$dmg_out"
"$qt_prefix/bin/macdeployqt" "$app_bundle" -dmg

built_dmg="$build_dir/src/app/kumcad.dmg"
mv "$built_dmg" "$dmg_out"

echo "==> Done: $dmg_out"
