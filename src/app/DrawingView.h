#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QColor>
#include <QOpenGLWidget>
#include <QPointF>

#include <array>
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
    // Replaces the selection wholesale (QSELECT/FIND), dropping ids on
    // hidden/locked layers or that no longer exist, same rule as selectAll().
    void setSelection(const std::vector<lcad::EntityId>& ids);

    // Paper-space layout mode: -1 shows model space, >= 0 the document's
    // layout at that index (paper sheet + viewports). Model editing tools are
    // inactive in layout mode; viewports are click-selected and dragged.
    void setActiveLayoutIndex(int index);
    int activeLayoutIndex() const { return m_layoutIndex; }
    bool inLayoutMode() const { return m_layoutIndex >= 0; }
    int selectedViewportIndex() const { return m_selectedViewport; }

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

    // Drafting aid toggles (F3/F8/F9/F10/F11), mirroring AutoCAD's
    // OSNAP/ORTHO/SNAP/POLAR/OTRACK. Ortho and polar are mutually exclusive:
    // turning one on turns the other off.
    bool osnapEnabled() const { return m_osnapEnabled; }
    bool orthoEnabled() const { return m_orthoEnabled; }
    bool gridSnapEnabled() const { return m_gridSnapEnabled; }
    bool polarEnabled() const { return m_polarEnabled; }
    bool otrackEnabled() const { return m_otrackEnabled; }
    void setOsnapEnabled(bool on);
    void setOrthoEnabled(bool on);
    void setGridSnapEnabled(bool on);
    void setPolarEnabled(bool on);
    void setOtrackEnabled(bool on);

    // LWDISPLAY: when on, entities render at their resolved lineweight
    // (override or layer) instead of a hairline.
    bool lineweightDisplay() const { return m_lineweightDisplay; }
    void setLineweightDisplay(bool on) {
        m_lineweightDisplay = on;
        update();
    }

    // Polar tracking increment angle (AutoCAD's POLARANG), degrees.
    double polarIncrementDeg() const { return m_polarIncrementDeg; }
    void setPolarIncrementDeg(double deg) {
        if (deg > 0.5 && deg <= 90.0) m_polarIncrementDeg = deg;
    }

    // Per-kind object snap enablement (the OSNAP command's checklist).
    bool snapModeEnabled(lcad::SnapKind kind) const { return m_snapModes[static_cast<int>(kind)]; }
    void setSnapModeEnabled(lcad::SnapKind kind, bool on) { m_snapModes[static_cast<int>(kind)] = on; }

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
    enum class DragMode { None, BoxSelect, MoveSelection, Grip, MoveViewport };

    void drawGrid(QPainter& painter);
    void drawLayoutMode(QPainter& painter);
    // Screen rectangle of a viewport on the current sheet.
    QRectF viewportScreenRect(const lcad::Viewport& vp) const;
    int viewportAt(const QPointF& screenPos) const;
    void drawEntity(QPainter& painter, const lcad::Entity& entity, const QColor& color, double penWidth);
    void drawGrips(QPainter& painter);
    void drawPreview(QPainter& painter);
    void drawDragPreview(QPainter& painter);
    void drawSelectionBox(QPainter& painter);
    void drawSnapMarker(QPainter& painter);
    void updateSelectionFromBox(const QRectF& screenBox, bool crossing);

    // Entities of the space being edited: model space, or the active
    // layout's paper entities in layout mode. Selection, hit-testing,
    // snapping, and drawing commands all operate on this set.
    std::vector<lcad::Entity*> spaceEntities() const;

    lcad::Entity* hitTestEntity(const lcad::Point2D& worldPt) const;
    // Click-to-select version of hitTestEntity: when several entities are
    // within pick tolerance, repeated clicks at (about) the same screen spot
    // step through them nearest-first instead of always returning the
    // closest one (AutoCAD's selection cycling by repeated clicking, no
    // popup needed). Any other click resets the cycle.
    lcad::Entity* hitTestEntityCycling(const lcad::Point2D& worldPt, const QPointF& screenPos);
    std::optional<std::pair<lcad::EntityId, std::size_t>> hitTestGrip(const QPointF& screenPt) const;
    double gridSpacing() const;

    // Resolves a screen click/move into the world point a command should
    // actually see: object snap first (if enabled and a candidate is close
    // enough), then ortho (constrains to horizontal/vertical off the active
    // command's anchor point), then grid snap. Updates m_currentSnap for the
    // on-screen marker as a side effect.
    lcad::Point2D resolvePoint(const QPointF& screenPos);
    lcad::Point2D resolvePointWithAnchor(const QPointF& screenPos, const std::optional<lcad::Point2D>& orthoAnchor);
    std::optional<std::pair<lcad::SnapPoint, std::optional<lcad::SnapRef>>>
    findSnapCandidate(const QPointF& screenPos) const;
    lcad::Point2D applyOrtho(const lcad::Point2D& anchor, const lcad::Point2D& pt) const;
    lcad::Point2D snapToGrid(const lcad::Point2D& pt) const;

    // Polar/object-snap tracking: snaps pt onto a ray from origin at the
    // nearest polar increment when the cursor is within tolerance of it.
    std::optional<lcad::Point2D> snapToPolarRay(const lcad::Point2D& origin, const lcad::Point2D& pt,
                                                double tolWorld) const;
    void drawTrackingGuides(QPainter& painter);

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

    // Selection cycling state (see hitTestEntityCycling).
    QPointF m_lastPickScreenPos{-1e9, -1e9};
    std::size_t m_pickCycleIndex = 0;

    std::optional<lcad::Point2D> m_lastMouseWorld;

    bool m_osnapEnabled = true;
    bool m_orthoEnabled = false;
    bool m_gridSnapEnabled = false;
    bool m_lineweightDisplay = false;
    bool m_polarEnabled = false;
    bool m_otrackEnabled = false;
    double m_polarIncrementDeg = 45.0;
    // Endpoint..Quadrant + Node + Intersection + Perpendicular + Tangent on by
    // default; Nearest off (it would swallow every free pick on an entity).
    std::array<bool, lcad::kSnapKindCount> m_snapModes{true, true, true, true, true, true, true, true, false};
    std::optional<lcad::SnapPoint> m_currentSnap;
    // Which entity snap point the last resolvePoint() osnap hit came from,
    // handed to the dispatcher so commands can record associativity. Dynamic
    // snap kinds (intersection/perpendicular/tangent/nearest) never carry one.
    std::optional<lcad::SnapRef> m_currentSnapRef;
    // Object-snap tracking state: the osnap points hovered ("acquired")
    // while a command is active, most-recent last, capped at two (AutoCAD
    // allows more, but two covers the common "line up with both" case and
    // keeps the guides from cluttering up over a long command). Points drop
    // off the front once a third is acquired.
    std::vector<lcad::Point2D> m_trackPoints;
    std::optional<std::pair<lcad::Point2D, lcad::Point2D>> m_polarGuide;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> m_trackGuides;

    int m_layoutIndex = -1;      // -1 = model space
    int m_selectedViewport = -1; // selected viewport in the active layout
    lcad::Point2D m_viewportDragOffset{0.0, 0.0};
};
