#include "SketchEditorDialog.h"
#include "SketchView.h"

#include "core/core3d/Document3D.h"
#include "core/core3d/ExternalGeometry.h"
#include "core/sketch/SketchGeometry.h"

#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>

using Kind = SketchView::Selection::Kind;
using lcad::SketchConstraint;
using lcad::SketchConstraintType;

SketchEditorDialog::SketchEditorDialog(lcad::Document3D& document, lcad::SketchPlane plane, QWidget* parent)
    : QDialog(parent), m_document(document) {
    setWindowTitle(QStringLiteral("Sketch Editor"));
    resize(900, 700);

    auto* layout = new QVBoxLayout(this);

    auto* toolbar = new QToolBar(this);
    m_view = new SketchView(this);
    m_view->sketch().setPlacement(plane);
    toolbar->addAction(QStringLiteral("Select"), this, [this] { m_view->setTool(SketchView::Tool::Select); });
    toolbar->addAction(QStringLiteral("Line"), this, [this] { m_view->setTool(SketchView::Tool::Line); });
    toolbar->addAction(QStringLiteral("Circle"), this, [this] { m_view->setTool(SketchView::Tool::Circle); });
    toolbar->addAction(QStringLiteral("Arc"), this, [this] { m_view->setTool(SketchView::Tool::Arc); });
    toolbar->addAction(QStringLiteral("Spline"), this, [this] { m_view->setTool(SketchView::Tool::Spline); });
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Horizontal"), this, &SketchEditorDialog::applyHorizontal);
    toolbar->addAction(QStringLiteral("Vertical"), this, &SketchEditorDialog::applyVertical);
    toolbar->addAction(QStringLiteral("Parallel"), this, &SketchEditorDialog::applyParallel);
    toolbar->addAction(QStringLiteral("Perpendicular"), this, &SketchEditorDialog::applyPerpendicular);
    toolbar->addAction(QStringLiteral("Equal"), this, &SketchEditorDialog::applyEqual);
    toolbar->addAction(QStringLiteral("Fillet..."), this, &SketchEditorDialog::applyFillet);
    toolbar->addAction(QStringLiteral("Tangent"), this, &SketchEditorDialog::applyTangent);
    toolbar->addAction(QStringLiteral("Circle-Circle Tangent"), this, &SketchEditorDialog::applyCircleCircleTangent);
    toolbar->addAction(QStringLiteral("Distance..."), this, &SketchEditorDialog::applyDistance);
    toolbar->addAction(QStringLiteral("Distance X..."), this, &SketchEditorDialog::applyDistanceX);
    toolbar->addAction(QStringLiteral("Distance Y..."), this, &SketchEditorDialog::applyDistanceY);
    toolbar->addAction(QStringLiteral("Radius..."), this, &SketchEditorDialog::applyRadius);
    toolbar->addAction(QStringLiteral("Diameter..."), this, &SketchEditorDialog::applyDiameter);
    toolbar->addAction(QStringLiteral("Arc Radius..."), this, &SketchEditorDialog::applyArcRadius);
    toolbar->addAction(QStringLiteral("Angle..."), this, &SketchEditorDialog::applyAngle);
    toolbar->addAction(QStringLiteral("Point On Line"), this, &SketchEditorDialog::applyPointOnLine);
    toolbar->addAction(QStringLiteral("Point On Circle"), this, &SketchEditorDialog::applyPointOnCircle);
    toolbar->addAction(QStringLiteral("Midpoint"), this, &SketchEditorDialog::applyMidpoint);
    toolbar->addAction(QStringLiteral("Symmetric"), this, &SketchEditorDialog::applySymmetric);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Toggle Construction"), this, &SketchEditorDialog::toggleConstruction);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("External Geometry..."), this, &SketchEditorDialog::addExternalGeometry);
    toolbar->addAction(QStringLiteral("Refresh External Geometry"), this, &SketchEditorDialog::refreshExternalGeometry);
    layout->addWidget(toolbar);

    layout->addWidget(m_view, 1);

    m_statusLabel = new QLabel(QStringLiteral("Ready — Line/Circle tools snap onto existing points; "
                                              "Select tool picks geometry (Shift-click to add to selection)"),
                               this);
    layout->addWidget(m_statusLabel);
    connect(m_view, &SketchView::statusMessage, m_statusLabel, &QLabel::setText);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

std::optional<int> SketchEditorDialog::oneSelectedLine() {
    const auto& sel = m_view->selection();
    if (sel.size() == 1 && sel[0].kind == Kind::Line) return sel[0].index;
    m_statusLabel->setText(QStringLiteral("Select exactly one line first"));
    return std::nullopt;
}

std::optional<std::pair<int, int>> SketchEditorDialog::twoSelectedLines() {
    const auto& sel = m_view->selection();
    if (sel.size() == 2 && sel[0].kind == Kind::Line && sel[1].kind == Kind::Line) {
        return std::make_pair(sel[0].index, sel[1].index);
    }
    m_statusLabel->setText(QStringLiteral("Select exactly two lines first"));
    return std::nullopt;
}

std::optional<std::pair<int, int>> SketchEditorDialog::twoPointsForDistance() {
    const auto& sel = m_view->selection();
    if (sel.size() == 2 && sel[0].kind == Kind::Point && sel[1].kind == Kind::Point) {
        return std::make_pair(sel[0].index, sel[1].index);
    }
    if (sel.size() == 1 && sel[0].kind == Kind::Line) {
        const auto& line = m_view->sketch().lines()[static_cast<std::size_t>(sel[0].index)];
        return std::make_pair(line.p1, line.p2);
    }
    m_statusLabel->setText(QStringLiteral("Select two points, or one line, to dimension"));
    return std::nullopt;
}

std::optional<int> SketchEditorDialog::oneSelectedCircle() {
    const auto& sel = m_view->selection();
    if (sel.size() == 1 && sel[0].kind == Kind::Circle) return sel[0].index;
    m_statusLabel->setText(QStringLiteral("Select exactly one circle first"));
    return std::nullopt;
}

std::optional<std::pair<int, int>> SketchEditorDialog::lineAndCircle() {
    const auto& sel = m_view->selection();
    if (sel.size() == 2) {
        if (sel[0].kind == Kind::Line && sel[1].kind == Kind::Circle) return std::make_pair(sel[0].index, sel[1].index);
        if (sel[0].kind == Kind::Circle && sel[1].kind == Kind::Line) return std::make_pair(sel[1].index, sel[0].index);
    }
    m_statusLabel->setText(QStringLiteral("Select one line and one circle first"));
    return std::nullopt;
}

std::optional<int> SketchEditorDialog::oneSelectedArc() {
    const auto& sel = m_view->selection();
    if (sel.size() == 1 && sel[0].kind == Kind::Arc) return sel[0].index;
    m_statusLabel->setText(QStringLiteral("Select exactly one arc first"));
    return std::nullopt;
}

std::optional<std::pair<int, int>> SketchEditorDialog::twoSelectedCircles() {
    const auto& sel = m_view->selection();
    if (sel.size() == 2 && sel[0].kind == Kind::Circle && sel[1].kind == Kind::Circle) {
        return std::make_pair(sel[0].index, sel[1].index);
    }
    m_statusLabel->setText(QStringLiteral("Select exactly two circles first"));
    return std::nullopt;
}

std::optional<std::pair<int, int>> SketchEditorDialog::pointAndLine() {
    const auto& sel = m_view->selection();
    if (sel.size() == 2) {
        if (sel[0].kind == Kind::Point && sel[1].kind == Kind::Line) return std::make_pair(sel[0].index, sel[1].index);
        if (sel[0].kind == Kind::Line && sel[1].kind == Kind::Point) return std::make_pair(sel[1].index, sel[0].index);
    }
    m_statusLabel->setText(QStringLiteral("Select one point and one line first"));
    return std::nullopt;
}

std::optional<std::pair<int, int>> SketchEditorDialog::pointAndCircle() {
    const auto& sel = m_view->selection();
    if (sel.size() == 2) {
        if (sel[0].kind == Kind::Point && sel[1].kind == Kind::Circle) return std::make_pair(sel[0].index, sel[1].index);
        if (sel[0].kind == Kind::Circle && sel[1].kind == Kind::Point) return std::make_pair(sel[1].index, sel[0].index);
    }
    m_statusLabel->setText(QStringLiteral("Select one point and one circle first"));
    return std::nullopt;
}

std::optional<std::tuple<int, int, int>> SketchEditorDialog::twoPointsAndLine() {
    const auto& sel = m_view->selection();
    if (sel.size() == 3) {
        int p1 = -1, p2 = -1, line = -1;
        int pointCount = 0, lineCount = 0;
        for (const auto& s : sel) {
            if (s.kind == Kind::Point) {
                if (pointCount == 0) p1 = s.index;
                else p2 = s.index;
                ++pointCount;
            } else if (s.kind == Kind::Line) {
                line = s.index;
                ++lineCount;
            }
        }
        if (pointCount == 2 && lineCount == 1) return std::make_tuple(p1, p2, line);
    }
    m_statusLabel->setText(QStringLiteral("Select exactly two points and one line (the symmetry axis) first"));
    return std::nullopt;
}

void SketchEditorDialog::applyHorizontal() {
    const auto line = oneSelectedLine();
    if (!line) return;
    m_view->sketch().addConstraint({SketchConstraintType::Horizontal, *line});
    m_view->resolve();
}

void SketchEditorDialog::applyVertical() {
    const auto line = oneSelectedLine();
    if (!line) return;
    m_view->sketch().addConstraint({SketchConstraintType::Vertical, *line});
    m_view->resolve();
}

void SketchEditorDialog::applyParallel() {
    const auto lines = twoSelectedLines();
    if (!lines) return;
    m_view->sketch().addConstraint({SketchConstraintType::Parallel, lines->first, lines->second});
    m_view->resolve();
}

void SketchEditorDialog::applyPerpendicular() {
    const auto lines = twoSelectedLines();
    if (!lines) return;
    m_view->sketch().addConstraint({SketchConstraintType::Perpendicular, lines->first, lines->second});
    m_view->resolve();
}

void SketchEditorDialog::applyEqual() {
    // Mirrors real Sketcher's single "Equal" tool: line pairs get equal
    // length, circle pairs and arc pairs get equal radius -- which
    // interpretation applies is read off the current selection's kind
    // rather than needing separate tools per geometry type.
    const auto& sel = m_view->selection();
    if (sel.size() == 2 && sel[0].kind == Kind::Circle && sel[1].kind == Kind::Circle) {
        m_view->sketch().addConstraint({SketchConstraintType::EqualCircleRadius, sel[0].index, sel[1].index});
        m_view->resolve();
        return;
    }
    if (sel.size() == 2 && sel[0].kind == Kind::Arc && sel[1].kind == Kind::Arc) {
        m_view->sketch().addConstraint({SketchConstraintType::EqualArcRadius, sel[0].index, sel[1].index});
        m_view->resolve();
        return;
    }
    const auto lines = twoSelectedLines();
    if (!lines) return;
    m_view->sketch().addConstraint({SketchConstraintType::Equal, lines->first, lines->second});
    m_view->resolve();
}

void SketchEditorDialog::applyFillet() {
    const auto lines = twoSelectedLines();
    if (!lines) return;
    bool ok = false;
    const double radius = QInputDialog::getDouble(this, QStringLiteral("Fillet"), QStringLiteral("Radius:"), 2.0,
                                                   0.001, 1e6, 3, &ok);
    if (!ok) return;
    if (lcad::sketchFillet(m_view->sketch(), lines->first, lines->second, radius)) {
        m_view->clearSelection();
        m_view->resolve();
        m_statusLabel->setText(QStringLiteral("Filleted with radius %1").arg(radius));
    } else {
        m_statusLabel->setText(QStringLiteral("*Could not fillet -- lines may be parallel/collinear or "
                                              "the radius too large*"));
    }
}

void SketchEditorDialog::applyTangent() {
    const auto pair = lineAndCircle();
    if (!pair) return;
    m_view->sketch().addConstraint({SketchConstraintType::Tangent, pair->first, pair->second});
    m_view->resolve();
}

void SketchEditorDialog::applyCircleCircleTangent() {
    const auto pair = twoSelectedCircles();
    if (!pair) return;

    // Infer external vs. internal tangency from the CURRENT geometry --
    // one circle nested inside the other reads as internal-tangency
    // intent, side-by-side reads as external -- same convention
    // CommandDispatcher's own GCTANGENT handling uses.
    const lcad::Sketch& sketch = m_view->sketch();
    const lcad::SketchCircle& circleA = sketch.circles()[static_cast<std::size_t>(pair->first)];
    const lcad::SketchCircle& circleB = sketch.circles()[static_cast<std::size_t>(pair->second)];
    const double dist = sketch.points()[static_cast<std::size_t>(circleA.center)].distanceTo(
        sketch.points()[static_cast<std::size_t>(circleB.center)]);
    const double externalResidual = std::abs(dist - (circleA.radius + circleB.radius));
    const double internalResidual = std::abs(dist - std::abs(circleA.radius - circleB.radius));
    const auto type = internalResidual < externalResidual ? SketchConstraintType::InternalTangentCircleCircle
                                                           : SketchConstraintType::TangentCircleCircle;
    m_view->sketch().addConstraint({type, pair->first, pair->second});
    m_view->resolve();
}

void SketchEditorDialog::applyDistance() {
    const auto points = twoPointsForDistance();
    if (!points) return;
    bool ok = false;
    const double value = QInputDialog::getDouble(this, QStringLiteral("Distance"), QStringLiteral("Value:"), 10.0,
                                                  0.0, 1e6, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::Distance;
    c.pointA = points->first;
    c.pointB = points->second;
    c.value = value;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyDistanceX() {
    const auto points = twoPointsForDistance();
    if (!points) return;
    bool ok = false;
    const double value = QInputDialog::getDouble(this, QStringLiteral("Distance X"), QStringLiteral("Value:"), 10.0,
                                                  -1e6, 1e6, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::DistanceX;
    c.pointA = points->first;
    c.pointB = points->second;
    c.value = value;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyDistanceY() {
    const auto points = twoPointsForDistance();
    if (!points) return;
    bool ok = false;
    const double value = QInputDialog::getDouble(this, QStringLiteral("Distance Y"), QStringLiteral("Value:"), 10.0,
                                                  -1e6, 1e6, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::DistanceY;
    c.pointA = points->first;
    c.pointB = points->second;
    c.value = value;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::toggleConstruction() {
    // Sprint 3's Revolve feature needs an axis line that isn't itself part
    // of the extruded profile -- construction geometry (solved like real
    // geometry, just not consumed as a boundary) is how that's expressed.
    const auto& sel = m_view->selection();
    if (sel.empty()) {
        m_statusLabel->setText(QStringLiteral("Select one or more lines/circles first"));
        return;
    }
    for (const auto& s : sel) {
        if (s.kind == Kind::Line) {
            auto& line = m_view->sketch().lines()[static_cast<std::size_t>(s.index)];
            line.construction = !line.construction;
        } else if (s.kind == Kind::Circle) {
            auto& circle = m_view->sketch().circles()[static_cast<std::size_t>(s.index)];
            circle.construction = !circle.construction;
        } else if (s.kind == Kind::Arc) {
            auto& arc = m_view->sketch().arcs()[static_cast<std::size_t>(s.index)];
            arc.construction = !arc.construction;
        } else if (s.kind == Kind::Spline) {
            auto& spline = m_view->sketch().splines()[static_cast<std::size_t>(s.index)];
            spline.construction = !spline.construction;
        }
    }
    m_view->update();
}

void SketchEditorDialog::applyRadius() {
    const auto circle = oneSelectedCircle();
    if (!circle) return;
    bool ok = false;
    const double value = QInputDialog::getDouble(this, QStringLiteral("Radius"), QStringLiteral("Value:"), 10.0, 0.0,
                                                  1e6, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::Radius;
    c.geomA = *circle;
    c.value = value;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyDiameter() {
    const auto circle = oneSelectedCircle();
    if (!circle) return;
    bool ok = false;
    const double value = QInputDialog::getDouble(this, QStringLiteral("Diameter"), QStringLiteral("Value:"), 20.0,
                                                  0.0, 1e6, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::Diameter;
    c.geomA = *circle;
    c.value = value;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyArcRadius() {
    const auto arc = oneSelectedArc();
    if (!arc) return;
    bool ok = false;
    const double value = QInputDialog::getDouble(this, QStringLiteral("Arc Radius"), QStringLiteral("Value:"), 10.0,
                                                  0.0, 1e6, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::ArcRadius;
    c.geomA = *arc;
    c.value = value;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyAngle() {
    const auto lines = twoSelectedLines();
    if (!lines) return;
    bool ok = false;
    const double degrees = QInputDialog::getDouble(this, QStringLiteral("Angle"), QStringLiteral("Value (degrees):"),
                                                    90.0, -360.0, 360.0, 3, &ok);
    if (!ok) return;
    SketchConstraint c;
    c.type = SketchConstraintType::Angle;
    c.geomA = lines->first;
    c.geomB = lines->second;
    c.value = degrees * M_PI / 180.0;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyPointOnLine() {
    const auto pair = pointAndLine();
    if (!pair) return;
    SketchConstraint c;
    c.type = SketchConstraintType::PointOnLine;
    c.geomA = pair->second;
    c.pointA = pair->first;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyPointOnCircle() {
    const auto pair = pointAndCircle();
    if (!pair) return;
    SketchConstraint c;
    c.type = SketchConstraintType::PointOnCircle;
    c.geomA = pair->second;
    c.pointA = pair->first;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applyMidpoint() {
    const auto pair = pointAndLine();
    if (!pair) return;
    SketchConstraint c;
    c.type = SketchConstraintType::Midpoint;
    c.geomA = pair->second;
    c.pointA = pair->first;
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::applySymmetric() {
    const auto triple = twoPointsAndLine();
    if (!triple) return;
    SketchConstraint c;
    c.type = SketchConstraintType::Symmetric;
    c.geomA = std::get<2>(*triple);
    c.pointA = std::get<0>(*triple);
    c.pointB = std::get<1>(*triple);
    m_view->sketch().addConstraint(c);
    m_view->resolve();
}

void SketchEditorDialog::addExternalGeometry() {
    const int featureCount = static_cast<int>(m_document.features().size());
    if (featureCount == 0) {
        m_statusLabel->setText(QStringLiteral("No features in the 3D document yet"));
        return;
    }

    bool ok = false;
    const int featureIndex = QInputDialog::getInt(
        this, QStringLiteral("External Geometry"),
        QStringLiteral("Feature index (0-%1, see the main window's feature tree):").arg(featureCount - 1), 0, 0,
        featureCount - 1, 1, &ok);
    if (!ok) return;
    if (!m_document.isValid(featureIndex)) {
        m_statusLabel->setText(QStringLiteral("*That feature isn't valid*"));
        return;
    }

    const int edgeIndex = QInputDialog::getInt(
        this, QStringLiteral("External Geometry"),
        QStringLiteral("Edge index (see the main window's \"List Edges...\"):"), 0, 0, 1000000, 1, &ok);
    if (!ok) return;

    lcad::ExternalGeometryRef ref;
    if (lcad::projectExternalEdgeTracked(m_view->sketch(), m_document.shapeAt(featureIndex), edgeIndex, ref)) {
        m_externalRefs.emplace_back(featureIndex, ref);
        m_view->update();
        m_statusLabel->setText(QStringLiteral("External geometry added (construction, fixed points) -- use "
                                              "\"Refresh External Geometry\" after editing feature %1")
                                    .arg(featureIndex));
    } else {
        m_statusLabel->setText(QStringLiteral("*Invalid edge index*"));
    }
}

void SketchEditorDialog::refreshExternalGeometry() {
    if (m_externalRefs.empty()) {
        m_statusLabel->setText(QStringLiteral("No external geometry added yet this session"));
        return;
    }
    int succeeded = 0;
    for (const auto& [featureIndex, ref] : m_externalRefs) {
        if (lcad::refreshExternalGeometry(m_view->sketch(), ref, m_document.shapeAt(featureIndex))) ++succeeded;
    }
    m_view->resolve();
    m_view->update();
    m_statusLabel->setText(QStringLiteral("Refreshed %1/%2 external geometry reference(s)")
                                .arg(succeeded)
                                .arg(m_externalRefs.size()));
}
