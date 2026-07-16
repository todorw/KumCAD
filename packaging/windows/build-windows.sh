#!/usr/bin/env bash
# Cross-compiles KumCAD for Windows x86_64 from Linux, using the system's
# mingw-w64-gcc cross-compiler against a Qt-for-Windows/MinGW build fetched
# straight from download.qt.io, then stages a portable, no-installer .zip
# (kumcad.exe + Qt DLLs + plugins + MinGW runtime) in dist/.
#
# Requires (Arch/CachyOS package names): mingw-w64-gcc mingw-w64-crt
# mingw-w64-winpthreads mingw-w64-headers, plus curl and either the `7z`
# CLI (pacman package `7zip`) or Python3 (py7zr gets pip-installed into a
# throwaway venv as a fallback). Needs a native Qt6 install too (`qt6-base`)
# -- its moc/rcc/uic run on this machine as "host tools" during the build.
#
# Known quirks this script works around (discovered building this on
# CachyOS with mingw-w64-gcc 16.1 + Qt 6.11.1, 2026-07):
#
#   1. Qt's CMake cross-compile glue expects the *host* Qt (used to run
#      moc/rcc/uic, found via QT_HOST_PATH) and the *target* Qt (the
#      Windows/MinGW libs kumcad actually links against) to be the same
#      version -- Qt6CoreToolsTargetsPrecheck.cmake calls a CMake function
#      that's only defined by a matching-version Qt6/QtPublicCMakeHelpers.
#      cmake. Fix: fetch the Windows/MinGW Qt build matching this machine's
#      installed host Qt6 version exactly, not whatever's cached/guessed.
#   2. As of Qt 6.11, download.qt.io splits the Windows desktop kit into
#      per-compiler subfolders (qt6_<ver>_mingw, _msvc2022_64, _llvm_mingw,
#      arm64_cross_compiled) that older aqtinstall releases don't know how
#      to map -- so this script fetches the qtbase/opengl32sw/d3dcompiler
#      archives directly via curl instead of going through `aqt install-qt`,
#      trying both the pre-6.11 (flat) and 6.11+ (split) URL layouts.
#   3. mingw-w64-gcc's default `-lmsvcrt` import lib only exposes the
#      __p___argc()-style accessors, not a plain data symbol -- but Qt's
#      prebuilt static EntryPoint (needed for the WIN32 GUI subsystem, so
#      kumcad.exe doesn't pop a console window) wants __argc directly.
#      Fixed in src/app/CMakeLists.txt by linking `-lmsvcrt-os` inside a
#      --start-group/--end-group, since Qt appends its own EntryPoint
#      dependency to the link line *after* this file's own
#      target_link_libraries() calls, so ld would never rescan it there.
#   4. Poppler-Qt6 (PDF underlay) and LASzip (LAZ point clouds) have no
#      MinGW-w64 builds available here, so this script always configures
#      with -DLCAD_ENABLE_PDF=OFF; LASzip disables itself automatically
#      since find_library() can't find a Windows-target one while
#      cross-compiling (it would otherwise silently pick up the host's).
#
# Re-run this any time; it downloads Qt into a local cache (once, ~40MB) and
# rebuilds from scratch otherwise.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pkg_dir="$repo_root/packaging/windows"
cache_dir="$pkg_dir/.cache"
build_dir="$pkg_dir/build"
dist_dir="$repo_root/dist"
version="${KUMCAD_VERSION:-0.1.0}"
mingw_triple="x86_64-w64-mingw32"

mkdir -p "$cache_dir" "$dist_dir"

command -v "${mingw_triple}-g++" >/dev/null || {
    echo "error: ${mingw_triple}-g++ not found -- install mingw-w64-gcc (see header comment)" >&2
    exit 1
}

echo "==> Detecting host Qt6 version (for host/target version match, see quirk #1)"
qt_version="${KUMCAD_WIN_QT_VERSION:-}"
if [ -z "$qt_version" ]; then
    qt_version="$(pkg-config --modversion Qt6Core 2>/dev/null || true)"
fi
if [ -z "$qt_version" ]; then
    echo "error: couldn't detect host Qt6 version; set KUMCAD_WIN_QT_VERSION=X.Y.Z" >&2
    exit 1
fi
echo "    host Qt6 version: $qt_version"

qt_dir="$cache_dir/qt-$qt_version-mingw"
if [ ! -f "$qt_dir/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "==> Fetching Qt $qt_version for Windows/MinGW (one-time, cached in $cache_dir)"

    extract() {
        # Prefers the system `7z` CLI; falls back to a throwaway py7zr venv.
        if command -v 7z >/dev/null; then
            7z x -o"$2" "$1" >/dev/null
        else
            local venv="$cache_dir/py7zr-venv"
            [ -d "$venv" ] || { python3 -m venv "$venv"; "$venv/bin/pip" -q install py7zr; }
            "$venv/bin/python" -m py7zr x "$1" "$2"
        fi
    }

    no_dots="${qt_version//./}"
    dl_dir="$cache_dir/dl-$qt_version"
    mkdir -p "$dl_dir" "$qt_dir"

    # Try the 6.11+ per-compiler-subfolder layout first, then the older flat
    # one (see quirk #2). Both are probed with a HEAD request.
    base=""
    for candidate in \
        "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_${no_dots}/qt6_${no_dots}_mingw/qt.qt6.${no_dots}.win64_mingw" \
        "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_${no_dots}/qt.qt6.${no_dots}.win64_mingw"; do
        if curl -sSL --head --max-time 15 -o /dev/null -w "%{http_code}" "$candidate/" | grep -q "^200$"; then
            base="$candidate"
            break
        fi
    done
    [ -n "$base" ] || {
        echo "error: couldn't find a win64_mingw Qt $qt_version package on download.qt.io" >&2
        echo "       (set KUMCAD_WIN_QT_VERSION to a version known to have one, e.g. 6.9.3)" >&2
        exit 1
    }

    listing="$dl_dir/listing.html"
    curl -sSL --max-time 15 "$base/" -o "$listing"
    archives=$(grep -oE 'href="[^"]*(qtbase|d3dcompiler_47|opengl32sw)[^"]*\.7z"' "$listing" \
        | sed -E 's/href="(.*)"/\1/' | sort -u)
    [ -n "$archives" ] || { echo "error: no archives found at $base/" >&2; exit 1; }

    while IFS= read -r f; do
        echo "    downloading $f"
        curl -sSL --max-time 600 -o "$dl_dir/$f" "$base/$f"
        extract "$dl_dir/$f" "$qt_dir"
    done <<< "$archives"
fi

echo "==> Configuring (Release, cross-compiling for Windows x86_64)"
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE="$pkg_dir/mingw-toolchain.cmake" \
    -DCMAKE_PREFIX_PATH="$qt_dir" \
    -DQT_HOST_PATH=/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DLCAD_BUILD_TESTS=OFF \
    -DLCAD_ENABLE_PDF=OFF

echo "==> Building"
cmake --build "$build_dir" -j"$(nproc)"

echo "==> Staging portable dist directory"
stage_dir="$dist_dir/KumCAD-windows"
rm -rf "$stage_dir"
mkdir -p "$stage_dir/platforms" "$stage_dir/styles" "$stage_dir/imageformats"

cp "$build_dir/src/app/kumcad.exe" "$stage_dir/"

for dll in Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets Qt6PrintSupport; do
    cp "$qt_dir/bin/$dll.dll" "$stage_dir/"
done
# These two extract to the Qt prefix root rather than bin/ (their archives
# have a flat internal layout, unlike qtbase's).
cp "$qt_dir/opengl32sw.dll" "$stage_dir/" 2>/dev/null || true
cp "$qt_dir/d3dcompiler_47.dll" "$stage_dir/" 2>/dev/null || true
cp "$qt_dir/plugins/platforms/qwindows.dll" "$stage_dir/platforms/"
cp "$qt_dir/plugins/styles/qmodernwindowsstyle.dll" "$stage_dir/styles/"
cp "$qt_dir/plugins/imageformats/"{qjpeg,qico,qgif}.dll "$stage_dir/imageformats/"

for dll in libgcc_s_seh-1 libstdc++-6 libwinpthread-1; do
    cp "/usr/${mingw_triple}/bin/$dll.dll" "$stage_dir/"
done

zip_path="$dist_dir/KumCAD-${version}-windows-x86_64.zip"
rm -f "$zip_path"
if command -v zip >/dev/null; then
    (cd "$dist_dir" && zip -qr "$(basename "$zip_path")" "$(basename "$stage_dir")")
else
    (cd "$dist_dir" && python3 -c "
import shutil, sys
shutil.make_archive(sys.argv[1][:-4], 'zip', '.', sys.argv[2])
" "$zip_path" "$(basename "$stage_dir")")
fi

echo "==> Done: $zip_path"
echo "    (targets Windows 10+ -- relies on OS-provided api-ms-win-crt-* forwarder DLLs)"
