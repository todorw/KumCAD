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
QIcon eraseIcon();

} // namespace IconFactory
