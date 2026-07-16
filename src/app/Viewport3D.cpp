#include "Viewport3D.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Xw_Window.hxx>

#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

Viewport3D::Viewport3D(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);

    try {
        Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection();
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(displayConnection, false);

        m_viewer = new V3d_Viewer(driver);
        m_viewer->SetDefaultLights();
        m_viewer->SetLightOn();

        m_context = new AIS_InteractiveContext(m_viewer);

        m_view = m_viewer->CreateView();
        Handle(Xw_Window) window = new Xw_Window(displayConnection, static_cast<Aspect_Drawable>(winId()));
        m_view->SetWindow(window);
        if (!window->IsMapped()) window->Map();

        m_available = true;
    } catch (const Standard_Failure&) {
        m_available = false;
    }

    if (!m_available) {
        // Degrade to a placeholder instead of a blank/crashed widget --
        // this environment has no working display for OCCT to attach to.
        auto* layout = new QVBoxLayout(this);
        auto* label = new QLabel(QStringLiteral("3D viewport unavailable\n(no usable display)"), this);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        setAttribute(Qt::WA_PaintOnScreen, false);
        setAttribute(Qt::WA_NativeWindow, false);
    }
}

Viewport3D::~Viewport3D() = default;

void Viewport3D::displayShape(const TopoDS_Shape& shape) {
    if (!m_available) return;
    Handle(AIS_Shape) presentation = new AIS_Shape(shape);
    m_context->Display(presentation, Standard_True);
}

void Viewport3D::clearShapes() {
    if (!m_available) return;
    m_context->RemoveAll(Standard_True);
}

void Viewport3D::fitAll() {
    if (!m_available) return;
    m_view->FitAll();
}

void Viewport3D::paintEvent(QPaintEvent* event) {
    if (!m_available) {
        QWidget::paintEvent(event);
        return;
    }
    m_view->Redraw();
}

void Viewport3D::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_available) m_view->MustBeResized();
}

void Viewport3D::mousePressEvent(QMouseEvent* event) {
    if (!m_available) return;
    m_lastX = event->pos().x();
    m_lastY = event->pos().y();
    if (event->button() == Qt::LeftButton) {
        m_rotating = true;
        m_view->StartRotation(m_lastX, m_lastY);
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
    }
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event) {
    if (!m_available) return;
    const int x = event->pos().x();
    const int y = event->pos().y();
    if (m_rotating) {
        m_view->Rotation(x, y);
    } else if (m_panning) {
        m_view->Pan(x - m_lastX, m_lastY - y);
    }
    m_lastX = x;
    m_lastY = y;
    update();
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) m_rotating = false;
    if (event->button() == Qt::MiddleButton) m_panning = false;
}

void Viewport3D::wheelEvent(QWheelEvent* event) {
    if (!m_available) return;
    const int delta = event->angleDelta().y();
    const int x = event->position().toPoint().x();
    const int y = event->position().toPoint().y();
    const int factor = delta > 0 ? 8 : -8;
    m_view->Zoom(x, y, x + factor, y + factor);
    update();
}
