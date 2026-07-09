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
    void selectAll();

    // Drops selected entities whose layer has become locked or hidden (e.g.
    // after a Layers-panel change), matching click-select's rules.
    void pruneSelectionForLayerState();

    // Clears selection/hover/drag state and re-fits the view. Call after the
    // Document's contents were replaced wholesale (New/Open), since any
    // previously selected/hovered ids may no longer exist.
    void resetViewState();
    bool hasSelection() const { return !m_selection.empty(); }
    std::vector<lcad::EntityId> selectedIds() const { return {m_selection.begin(), m_selection.end()}; }

    // World-space distance corresponding to the pixel click tolerance at the
    // current zoom; commands that pick entities themselves (TRIM/EXTEND) use it.
    double pickToleranceWorld() const { return 6.0 / m_scale; }

    // Drafting aid toggles (F3/F8/F9), mirroring AutoCAD's OSNAP/ORTHO/SNAP.
    bool osnapEnabled() const { return m_osnapEnabled; }
    bool orthoEnabled() const { return m_orthoEnabled; }
    bool gridSnapEnabled() const { return m_gridSnapEnabled; }
    void setOsnapEnabled(bool on);
    void setOrthoEnabled(bool on);
    void setGridSnapEnabled(bool on);

signals:
    void mouseWorldMoved(const lcad::Point2D& pt);
    void selectionChanged();
    void modesChanged();

    // Emitted after this view executes a command on the document's command
    // stack (drag move, grip edit, erase) so the window can mark the file
    // dirty and dependent panels can refresh -- these edits don't go through
    // CommandDispatcher and would otherwise be invisible to them.
    void documentEdited();

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
    void drawSnapMarker(QPainter& painter);
    void updateSelectionFromBox(const QRectF& screenBox, bool crossing);

    lcad::Entity* hitTestEntity(const lcad::Point2D& worldPt) const;
    std::optional<std::pair<lcad::EntityId, std::size_t>> hitTestGrip(const QPointF& screenPt) const;
    double gridSpacing() const;

    // Resolves a screen click/move into the world point a command should
    // actually see: object snap first (if enabled and a candidate is close
    // enough), then ortho (constrains to horizontal/vertical off the active
    // command's anchor point), then grid snap. Updates m_currentSnap for the
    // on-screen marker as a side effect.
    lcad::Point2D resolvePoint(const QPointF& screenPos);
    lcad::Point2D resolvePointWithAnchor(const QPointF& screenPos, const std::optional<lcad::Point2D>& orthoAnchor);
    std::optional<lcad::SnapPoint> findSnapCandidate(const QPointF& screenPos) const;
    lcad::Point2D applyOrtho(const lcad::Point2D& anchor, const lcad::Point2D& pt) const;
    lcad::Point2D snapToGrid(const lcad::Point2D& pt) const;

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

    bool m_osnapEnabled = true;
    bool m_orthoEnabled = false;
    bool m_gridSnapEnabled = false;
    std::optional<lcad::SnapPoint> m_currentSnap;
};
