#pragma once

#include <Standard_Handle.hxx>

#include <QWidget>

class AIS_InteractiveContext;
class V3d_View;
class V3d_Viewer;
class TopoDS_Shape;

// A 3D viewport embedding OCCT's own viewer (V3d_View/AIS_InteractiveContext)
// via a native X11 window handle (Xw_Window) -- the classic OCCT+Qt
// integration approach, distinct from DrawingView's QOpenGLWidget-based 2D
// canvas (which paints entirely through QPainter, unrelated to OCCT).
//
// IMPORTANT: this cannot be verified end-to-end in this dev environment --
// there is no real display here (Qt runs under the "offscreen" platform
// plugin only, confirmed to have no working GL rasterizer even for the 2D
// canvas -- see the DrawingView note in the kumcad-final-pushes memory).
// Construction is wrapped in try/catch so a failure here (e.g. no X11
// display, no GLX) degrades to isAvailable() == false with a placeholder
// message instead of crashing the app, but the actual render/orbit/pan/zoom
// behavior is unverified -- it needs a real display to confirm.
class Viewport3D : public QWidget {
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

    bool isAvailable() const { return m_available; }

    // Displays shape in the viewer. No-op if !isAvailable().
    void displayShape(const TopoDS_Shape& shape);

    // Same, but with an explicit RGB color (each in [0,1]) -- used for
    // per-element heatmap displays (e.g. FEM results, see Fem.h's
    // buildFemVisualization) where every shape needs its own flat color
    // rather than the viewer's default material.
    void displayShape(const TopoDS_Shape& shape, double r, double g, double b);

    // Removes every currently displayed shape.
    void clearShapes();

    // Fits the view to whatever's currently displayed.
    void fitAll();

protected:
    QPaintEngine* paintEngine() const override { return nullptr; } // OCCT paints natively, not via QPainter
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool m_available = false;
    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;

    bool m_rotating = false;
    bool m_panning = false;
    int m_lastX = 0;
    int m_lastY = 0;
};
