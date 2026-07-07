#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QColor>
#include <QOpenGLWidget>
#include <QPointF>

#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

class CommandDispatcher;
class QPainter;

// The drafting canvas: renders the Document with QPainter on a QOpenGLWidget
// (GPU-composited, simple immediate-mode drawing — no hand-rolled vertex
// buffers needed at this scale), handles pan/zoom, a crosshair cursor, hover
// highlighting, click/window/crossing selection, click-drag to move selected
// entities, grip-point stretch editing, and forwards clicks/moves to the
// active DrawCommand (if any) via CommandDispatcher.
class DrawingView : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit DrawingView(lcad::Document& document, QWidget* parent = nullptr);

    void setDispatcher(CommandDispatcher* dispatcher) { m_dispatcher = dispatcher; }

    lcad::Point2D screenToWorld(const QPointF& screen) const;
    QPointF worldToScreen(const lcad::Point2D& world) const;

    void zoomExtents();
    void eraseSelection();
    bool hasSelection() const { return !m_selection.empty(); }
    std::vector<lcad::EntityId> selectedIds() const { return {m_selection.begin(), m_selection.end()}; }

signals:
    void mouseWorldMoved(const lcad::Point2D& pt);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class DragMode { None, BoxSelect, MoveSelection, Grip };

    void drawGrid(QPainter& painter);
    void drawEntity(QPainter& painter, const lcad::Entity& entity, const QColor& color, double penWidth);
    void drawGrips(QPainter& painter);
    void drawPreview(QPainter& painter);
    void drawDragPreview(QPainter& painter);
    void drawSelectionBox(QPainter& painter);
    void updateSelectionFromBox(const QRectF& screenBox, bool crossing);

    lcad::Entity* hitTestEntity(const lcad::Point2D& worldPt) const;
    std::optional<std::pair<lcad::EntityId, std::size_t>> hitTestGrip(const QPointF& screenPt) const;
    double pickToleranceWorld() const { return 6.0 / m_scale; }

    lcad::Document& m_document;
    CommandDispatcher* m_dispatcher = nullptr;

    lcad::Point2D m_viewCenter{0.0, 0.0};
    double m_scale = 10.0; // pixels per world unit

    bool m_panning = false;
    QPointF m_lastPanPos;

    DragMode m_dragMode = DragMode::None;
    QPointF m_dragStartScreen;
    QPointF m_dragCurrentScreen;
    lcad::Point2D m_dragStartWorld{0.0, 0.0};
    lcad::Point2D m_dragCurrentWorld{0.0, 0.0};
    lcad::EntityId m_gripEntityId = 0;
    std::size_t m_gripIndex = 0;
    lcad::Point2D m_gripOldPos{0.0, 0.0};

    std::unordered_set<lcad::EntityId> m_selection;
    std::optional<lcad::EntityId> m_hoverEntityId;

    std::optional<lcad::Point2D> m_lastMouseWorld;
};
