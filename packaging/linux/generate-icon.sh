#!/usr/bin/env bash
# Regenerates kumcad.png from the app's own IconFactory::appIcon() code
# (src/app/IconFactory.cpp), so the packaged icon always matches the one the
# app actually shows in its window/taskbar rather than drifting out of sync
# with a hand-made asset. Requires Qt6 dev headers; needs no display
# (renders via QT_QPA_PLATFORM=offscreen).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="$repo_root/packaging/linux/kumcad.png"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat > "$work/gen_icon.cpp" << 'EOF'
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include "IconFactory.h"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QPixmap px = IconFactory::appIcon().pixmap(64, 64);
    px.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(argv[1], "PNG");
    return 0;
}
EOF

# -mno-direct-extern-access: needed for ad-hoc standalone Qt6 builds on this
# toolchain, see memory note "kumcad-standalone-qt-harness-flag".
g++ -std=c++20 -fPIC -mno-direct-extern-access \
    "$work/gen_icon.cpp" "$repo_root/src/app/IconFactory.cpp" \
    -I"$repo_root/src/app" \
    "$(pkg-config --cflags Qt6Widgets Qt6Gui Qt6Core)" \
    "$(pkg-config --libs Qt6Widgets Qt6Gui Qt6Core)" \
    -o "$work/gen_icon"

QT_QPA_PLATFORM=offscreen "$work/gen_icon" "$out"
echo "Wrote $out"
