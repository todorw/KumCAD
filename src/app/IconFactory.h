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
QIcon moveIcon();
QIcon copyIcon();
QIcon rotateIcon();
QIcon scaleIcon();
QIcon eraseIcon();

} // namespace IconFactory
