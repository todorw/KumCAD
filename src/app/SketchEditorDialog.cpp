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
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Horizontal"), this, &SketchEditorDialog::applyHorizontal);
    toolbar->addAction(QStringLiteral("Vertical"), this, &SketchEditorDialog::applyVertical);
    toolbar->addAction(QStringLiteral("Parallel"), this, &SketchEditorDialog::applyParallel);
    toolbar->addAction(QStringLiteral("Perpendicular"), this, &SketchEditorDialog::applyPerpendicular);
    toolbar->addAction(QStringLiteral("Equal"), this, &SketchEditorDialog::applyEqual);
    toolbar->addAction(QStringLiteral("Tangent"), this, &SketchEditorDialog::applyTangent);
    toolbar->addAction(QStringLiteral("Circle-Circle Tangent"), this, &SketchEditorDialog::applyCircleCircleTangent);
    toolbar->addAction(QStringLiteral("Distance..."), this, &SketchEditorDialog::applyDistance);
    toolbar->addAction(QStringLiteral("Radius..."), this, &SketchEditorDialog::applyRadius);
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
    const auto lines = twoSelectedLines();
    if (!lines) return;
    m_view->sketch().addConstraint({SketchConstraintType::Equal, lines->first, lines->second});
    m_view->resolve();
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
    m_view->sketch().addConstraint({SketchConstraintType::TangentCircleCircle, pair->first, pair->second});
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

    if (lcad::projectExternalEdge(m_view->sketch(), m_document.shapeAt(featureIndex), edgeIndex)) {
        m_view->update();
        m_statusLabel->setText(QStringLiteral("External geometry added (construction, fixed points)"));
    } else {
        m_statusLabel->setText(QStringLiteral("*Invalid edge index*"));
    }
}
