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

// 3D modeling toolbar (Window3D's "Features" toolbar) -- primitives and
// the three boolean ops, the highest-frequency actions there. The
// remaining, more specialized 3D actions (List Edges, Variables, the
// Lisp console, etc.) stay text-only, matching how even FreeCAD's own
// toolbars mix icon and text-only actions rather than iconifying every
// single one.
QIcon box3DIcon();
QIcon cylinder3DIcon();
QIcon sphere3DIcon();
QIcon cone3DIcon();
QIcon torus3DIcon();
QIcon wedge3DIcon();
QIcon union3DIcon();
QIcon cut3DIcon();
QIcon intersect3DIcon();

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
