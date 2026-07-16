#!/usr/bin/env bash
# Builds KumCAD in Release mode and packages it as a single portable
# AppImage that runs on any modern x86_64 Linux distro without installing
# anything (Qt6 + all shared deps bundled in). Output lands in dist/.
#
# Known quirks this script works around (discovered building this on
# CachyOS with continuous-build linuxdeploy/appimagetool, 2026-07):
#
#   1. The `strip` binary bundled inside linuxdeploy's and
#      linuxdeploy-plugin-qt's AppImages is an older binutils that doesn't
#      understand the `.relr.dyn` relative-relocation ELF section modern
#      glibc/binutils produce -- it fails on essentially every shared
#      library and aborts the whole deploy. Fix: extract both tools and
#      swap in the system `strip`.
#   2. This system's `kimageformats` package ships a few Qt image-format
#      plugins (kimg_avif/heif/jxr) with *optional* dependencies
#      (libavif/libheif/libjxrglue) that aren't installed -- harmless at
#      runtime (Qt just skips the plugin), but linuxdeploy-plugin-qt's
#      static dependency scanner treats a missing dependency as fatal.
#      KumCAD doesn't use any of these formats. Fix: satisfy the scanner
#      with empty stub .so files of the right SONAME on LD_LIBRARY_PATH,
#      so it copies something (never loaded) instead of erroring out.
#
# Re-run this any time; it downloads tools into a local cache (once) and
# rebuilds from scratch otherwise.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pkg_dir="$repo_root/packaging/linux"
cache_dir="$pkg_dir/.cache"
build_dir="$repo_root/build"
dist_dir="$repo_root/dist"
version="${KUMCAD_VERSION:-0.1.0}"

mkdir -p "$cache_dir" "$dist_dir"

echo "==> Building KumCAD (Release)"
cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j

echo "==> Fetching AppImage tooling (cached in $cache_dir)"
fetch_tool() {
    local name="$1" url="$2"
    if [[ ! -x "$cache_dir/$name" ]]; then
        curl -sSL -o "$cache_dir/$name" "$url"
        chmod +x "$cache_dir/$name"
    fi
}
fetch_tool linuxdeploy-x86_64.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
fetch_tool linuxdeploy-plugin-qt-x86_64.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
fetch_tool appimagetool-x86_64.AppImage \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

# Work around quirk #1: extract both deploy tools and swap in system strip.
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    extracted="$cache_dir/${tool}-extracted"
    if [[ ! -d "$extracted" ]]; then
        ( cd "$cache_dir" && "./${tool}-x86_64.AppImage" --appimage-extract > /dev/null )
        mv "$cache_dir/squashfs-root" "$extracted"
        cp "$(command -v strip)" "$extracted/usr/bin/strip"
        chmod +x "$extracted/AppRun"
    fi
done
# linuxdeploy discovers the Qt plugin by searching PATH for this exact name.
ln -sf "$cache_dir/linuxdeploy-plugin-qt-extracted/AppRun" "$cache_dir/linuxdeploy-plugin-qt"

# Work around quirk #2: stub out optional image-format libs that aren't
# installed on this system and that KumCAD never uses.
stub_dir="$cache_dir/stublibs"
mkdir -p "$stub_dir"
if [[ ! -f "$stub_dir/libavif.so.16" ]]; then
    printf '' > "$stub_dir/empty.c"
    for soname in libavif.so.16 libheif.so.1 libjxrglue.so.0; do
        gcc -shared -fPIC -o "$stub_dir/$soname" -Wl,-soname,"$soname" "$stub_dir/empty.c"
    done
fi

echo "==> Staging AppDir"
appdir="$build_dir/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
cp "$build_dir/src/app/kumcad" "$appdir/usr/bin/kumcad"
cp "$pkg_dir/kumcad.png" "$appdir/usr/share/icons/hicolor/256x256/apps/kumcad.png"
cp "$pkg_dir/kumcad.desktop" "$appdir/usr/share/applications/kumcad.desktop"

echo "==> Running linuxdeploy + Qt plugin"
export PATH="$cache_dir:$PATH"
export QMAKE="$(command -v qmake6 || command -v qmake)"
export LD_LIBRARY_PATH="$stub_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export VERSION="$version"
"$cache_dir/linuxdeploy-extracted/AppRun" --appdir "$appdir" --plugin qt

echo "==> Building final AppImage"
out="$dist_dir/KumCAD-$version-x86_64.AppImage"
rm -f "$out"
ARCH=x86_64 "$cache_dir/appimagetool-x86_64.AppImage" "$appdir" "$out"

echo "==> Done: $out"
