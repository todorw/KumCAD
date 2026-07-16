#include "Window3D.h"
#include "Viewport3D.h"

#include <BRepPrimAPI_MakeBox.hxx>

#include <QStatusBar>

Window3D::Window3D(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("KumCAD — 3D Modeling (early preview)"));
    resize(1024, 768);

    m_viewport = new Viewport3D(this);
    setCentralWidget(m_viewport);

    if (m_viewport->isAvailable()) {
        BRepPrimAPI_MakeBox makeBox(50.0, 50.0, 50.0);
        m_viewport->displayShape(makeBox.Shape());
        statusBar()->showMessage(QStringLiteral("Test box loaded — left-drag to orbit, wheel to zoom, "
                                                "middle-drag to pan"));
    } else {
        statusBar()->showMessage(QStringLiteral("3D viewport unavailable on this system (no usable display)"));
    }
}
