#include "SketchEditorDialog.h"
#include "SketchView.h"

#include "core/sketch/SketchGeometry.h"

#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QToolBar>
#include <QVBoxLayout>

using Kind = SketchView::Selection::Kind;
using lcad::SketchConstraint;
using lcad::SketchConstraintType;

SketchEditorDialog::SketchEditorDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Sketch Editor"));
    resize(900, 700);

    auto* layout = new QVBoxLayout(this);

    auto* toolbar = new QToolBar(this);
    m_view = new SketchView(this);
    toolbar->addAction(QStringLiteral("Select"), this, [this] { m_view->setTool(SketchView::Tool::Select); });
    toolbar->addAction(QStringLiteral("Line"), this, [this] { m_view->setTool(SketchView::Tool::Line); });
    toolbar->addAction(QStringLiteral("Circle"), this, [this] { m_view->setTool(SketchView::Tool::Circle); });
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Horizontal"), this, &SketchEditorDialog::applyHorizontal);
    toolbar->addAction(QStringLiteral("Vertical"), this, &SketchEditorDialog::applyVertical);
    toolbar->addAction(QStringLiteral("Parallel"), this, &SketchEditorDialog::applyParallel);
    toolbar->addAction(QStringLiteral("Perpendicular"), this, &SketchEditorDialog::applyPerpendicular);
    toolbar->addAction(QStringLiteral("Equal"), this, &SketchEditorDialog::applyEqual);
    toolbar->addAction(QStringLiteral("Tangent"), this, &SketchEditorDialog::applyTangent);
    toolbar->addAction(QStringLiteral("Distance..."), this, &SketchEditorDialog::applyDistance);
    toolbar->addAction(QStringLiteral("Radius..."), this, &SketchEditorDialog::applyRadius);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Toggle Construction"), this, &SketchEditorDialog::toggleConstruction);
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
