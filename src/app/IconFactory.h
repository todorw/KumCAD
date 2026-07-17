#pragma once

#include <QIcon>

// Small, self-contained vector glyphs for the toolbar so the UI doesn't
// depend on an external icon theme being installed.
namespace IconFactory {

QIcon selectIcon();
QIcon lineIcon();
QIcon circleIcon();
QIcon arcIcon();
QIcon polylineIcon();
QIcon ellipseIcon();
QIcon textIcon();
QIcon rectangleIcon();
QIcon moveIcon();
QIcon copyIcon();
QIcon rotateIcon();
QIcon scaleIcon();
QIcon mirrorIcon();
QIcon offsetIcon();
QIcon trimIcon();
QIcon extendIcon();
QIcon filletIcon();
QIcon dimensionIcon();
QIcon hatchIcon();
QIcon blockIcon();
QIcon eraseIcon();

// The application/window icon (taskbar, alt-tab, title bar) -- unlike the
// tool icons above, this one paints its own colored background rather than
// relying on transparency over a dark canvas, since a host desktop's
// taskbar isn't guaranteed to be dark.
QIcon appIcon();

// Larger glyphs for the welcome screen's mode-picker cards. Drawn at a
// bigger canvas than the toolbar icons above so they stay crisp at
// card size instead of being upscaled.
QIcon mode2DIcon();
QIcon mode3DIcon();
QIcon modePcbIcon();
QIcon modeElectricalIcon();
QIcon modeOtherIcon();

} // namespace IconFactory
