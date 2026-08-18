#pragma once

#include "core/core3d/ExternalGeometry.h"
#include "core/sketch/SketchGeometry.h"

#include <QDialog>

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class SketchView;
class QLabel;

namespace lcad {
class Document3D;
}

// Phase 2 Sprint 2's sketch editor: draw lines/circles/arcs, select
// geometry, apply constraints, see the solver resolve live.
class SketchEditorDialog : public QDialog {
    Q_OBJECT
public:
    // document is where "External Geometry..." looks up existing
    // features' shapes to project an edge from; plane is applied to the
    // new sketch immediately (not just on accept), so External Geometry
    // can project onto it while the dialog is still open.
    explicit SketchEditorDialog(lcad::Document3D& document, lcad::SketchPlane plane, QWidget* parent = nullptr);

    SketchView* view() const { return m_view; }

private:
    // EXTERNALGEO: prompts for a feature index (into document.features())
    // and an edge index (into that feature's own shape, TopExp::MapShapes
    // ordering -- the same typed-index convention Pick3D.h's own
    // EdgePickResult documents), then projects it into the sketch as
    // fixed-point construction geometry (see core/core3d/
    // ExternalGeometry.h).
    void addExternalGeometry();
    // Re-resolves every External Geometry ref added this session against
    // its source feature's CURRENT shape (m_document.shapeAt) and updates
    // the sketch's projected geometry in place -- turns
    // ExternalGeometry.h's own disclosed "one-shot copy" limitation into
    // an explicit re-sync instead of a stale copy or a re-run that leaves
    // duplicates. Session-only: see ExternalGeometryRef's own comment on
    // why this isn't persisted across a save/reload.
    void refreshExternalGeometry();

    lcad::Document3D& m_document;
    // Parallel to each addExternalGeometry call this session: which
    // document feature it came from, and the ref refreshExternalGeometry
    // needs to re-resolve and re-apply it.
    std::vector<std::pair<int, lcad::ExternalGeometryRef>> m_externalRefs;
    void applyHorizontal();
    void applyVertical();
    void applyParallel();
    void applyPerpendicular();
    void applyEqual();
    void applyFillet();
    void applyTangent();
    void applyDistance();
    void applyDistanceX();
    void applyDistanceY();
    void applyRadius();
    void applyDiameter();
    void applyArcRadius();
    void applyCircleCircleTangent();
    void toggleConstruction();
    void applyAngle();
    void applyPointOnLine();
    void applyPointOnCircle();
    void applyMidpoint();
    void applySymmetric();

    // Resolves the current selection to exactly one selected line's index,
    // or nullopt (with a status message) if that's not what's selected.
    std::optional<int> oneSelectedLine();
    std::optional<std::pair<int, int>> twoSelectedLines();
    // Either two selected points, or one selected line (using its own two
    // endpoints) -- both are valid ways to specify a Distance dimension.
    std::optional<std::pair<int, int>> twoPointsForDistance();
    std::optional<int> oneSelectedCircle();
    std::optional<std::pair<int, int>> lineAndCircle();
    std::optional<int> oneSelectedArc();
    std::optional<std::pair<int, int>> twoSelectedCircles();
    // One selected point plus one selected line (in either click order),
    // point index first -- for PointOnLine and Midpoint.
    std::optional<std::pair<int, int>> pointAndLine();
    // One selected point plus one selected circle (in either click order),
    // point index first -- for PointOnCircle.
    std::optional<std::pair<int, int>> pointAndCircle();
    // Two selected points plus one selected line, points first (in
    // selection order) then the line -- for Symmetric.
    std::optional<std::tuple<int, int, int>> twoPointsAndLine();

    SketchView* m_view = nullptr;
    QLabel* m_statusLabel = nullptr;
};
