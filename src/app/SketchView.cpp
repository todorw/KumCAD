#include "SketchView.h"

#include "core/sketch/ConstraintSolver.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using lcad::Point2D;

namespace {
constexpr double kPickPixels = 10.0;
}

SketchView::SketchView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 400);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void SketchView::setTool(Tool tool) {
    m_tool = tool;
    m_pendingLineStart.reset();
}

void SketchView::clearSelection() {
    m_selection.clear();
    emit selectionChanged();
    update();
}

void SketchView::resolve() {
    const lcad::SolveResult result = solveSketch(m_sketch);
    emit statusMessage(result.converged ? QStringLiteral("Solved (residual %1)").arg(result.finalResidualNorm, 0, 'e', 2)
                                        : QStringLiteral("Did not converge (residual %1) — check for conflicting "
                                                         "or redundant constraints")
                                              .arg(result.finalResidualNorm, 0, 'e', 2));
    update();
}

QPointF SketchView::toScreen(const Point2D& p) const {
    return {width() / 2.0 + m_panOffset.x() + p.x * m_scale, height() / 2.0 + m_panOffset.y() - p.y * m_scale};
}

Point2D SketchView::toSketch(const QPointF& p) const {
    return Point2D((p.x() - width() / 2.0 - m_panOffset.x()) / m_scale,
                   -(p.y() - height() / 2.0 - m_panOffset.y()) / m_scale);
}

int SketchView::findOrCreatePoint(const Point2D& sketchPos) {
    const QPointF screenPos = toScreen(sketchPos);
    for (std::size_t i = 0; i < m_sketch.points().size(); ++i) {
        const QPointF candidate = toScreen(m_sketch.points()[i]);
        const double dx = candidate.x() - screenPos.x();
        const double dy = candidate.y() - screenPos.y();
        if (std::sqrt(dx * dx + dy * dy) <= kPickPixels) return static_cast<int>(i);
    }
    return m_sketch.addPoint(sketchPos);
}

std::optional<SketchView::Selection> SketchView::pickEntity(const Point2D& sketchPos) const {
    const double tolSketch = kPickPixels / m_scale;

    for (std::size_t i = 0; i < m_sketch.points().size(); ++i) {
        if (sketchPos.distanceTo(m_sketch.points()[i]) <= tolSketch) {
            return Selection{Selection::Kind::Point, static_cast<int>(i)};
        }
    }
    for (std::size_t i = 0; i < m_sketch.lines().size(); ++i) {
        const auto& line = m_sketch.lines()[i];
        const Point2D& a = m_sketch.points()[static_cast<std::size_t>(line.p1)];
        const Point2D& b = m_sketch.points()[static_cast<std::size_t>(line.p2)];
        const Point2D dir = b - a;
        const double lenSq = dir.dot(dir);
        double t = lenSq < 1e-12 ? 0.0 : (sketchPos - a).dot(dir) / lenSq;
        t = std::clamp(t, 0.0, 1.0);
        const Point2D closest = a + dir * t;
        if (sketchPos.distanceTo(closest) <= tolSketch) return Selection{Selection::Kind::Line, static_cast<int>(i)};
    }
    for (std::size_t i = 0; i < m_sketch.circles().size(); ++i) {
        const auto& circle = m_sketch.circles()[i];
        const Point2D& center = m_sketch.points()[static_cast<std::size_t>(circle.center)];
        if (std::abs(sketchPos.distanceTo(center) - circle.radius) <= tolSketch) {
            return Selection{Selection::Kind::Circle, static_cast<int>(i)};
        }
    }
    return std::nullopt;
}

void SketchView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(30, 30, 30));

    auto isSelected = [&](Selection::Kind kind, int index) {
        return std::any_of(m_selection.begin(), m_selection.end(),
                           [&](const Selection& s) { return s.kind == kind && s.index == index; });
    };

    for (std::size_t i = 0; i < m_sketch.circles().size(); ++i) {
        const auto& circle = m_sketch.circles()[i];
        const Point2D& center = m_sketch.points()[static_cast<std::size_t>(circle.center)];
        QPen pen(isSelected(Selection::Kind::Circle, static_cast<int>(i)) ? QColor(255, 200, 0) : QColor(120, 200, 255));
        if (circle.construction) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        const QPointF c = toScreen(center);
        painter.drawEllipse(c, circle.radius * m_scale, circle.radius * m_scale);
    }

    for (std::size_t i = 0; i < m_sketch.lines().size(); ++i) {
        const auto& line = m_sketch.lines()[i];
        QPen pen(isSelected(Selection::Kind::Line, static_cast<int>(i)) ? QColor(255, 200, 0) : QColor(224, 224, 224));
        if (line.construction) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(toScreen(m_sketch.points()[static_cast<std::size_t>(line.p1)]),
                         toScreen(m_sketch.points()[static_cast<std::size_t>(line.p2)]));
    }

    for (std::size_t i = 0; i < m_sketch.points().size(); ++i) {
        const bool selected = isSelected(Selection::Kind::Point, static_cast<int>(i));
        const QColor color = selected ? QColor(255, 200, 0) : (m_sketch.pointFixed()[i] ? QColor(220, 80, 80) : QColor(90, 170, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(toScreen(m_sketch.points()[i]), 4, 4);
    }
}

void SketchView::mousePressEvent(QMouseEvent* event) {
    const Point2D sketchPos = toSketch(event->pos());

    if (m_tool == Tool::Line) {
        const int idx = findOrCreatePoint(sketchPos);
        if (!m_pendingLineStart) {
            m_pendingLineStart = idx;
            emit statusMessage(QStringLiteral("Specify line end point (Escape to stop)"));
        } else {
            if (*m_pendingLineStart != idx) {
                m_sketch.addLine(*m_pendingLineStart, idx);
                resolve();
            }
            m_pendingLineStart = idx; // continue chaining, like PLINE
        }
    } else if (m_tool == Tool::Circle) {
        if (!m_pendingLineStart) {
            m_pendingLineStart = findOrCreatePoint(sketchPos);
            emit statusMessage(QStringLiteral("Specify a point on the circle"));
        } else {
            const Point2D& center = m_sketch.points()[static_cast<std::size_t>(*m_pendingLineStart)];
            const double radius = center.distanceTo(sketchPos);
            if (radius > 1e-6) {
                m_sketch.addCircle(*m_pendingLineStart, radius);
                resolve();
            }
            m_pendingLineStart.reset();
        }
    } else {
        const auto picked = pickEntity(sketchPos);
        if (event->modifiers() & Qt::ShiftModifier) {
            if (picked) m_selection.push_back(*picked);
        } else {
            m_selection.clear();
            if (picked) m_selection.push_back(*picked);
        }
        emit selectionChanged();
        update();
    }
}

void SketchView::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    m_scale = std::clamp(m_scale * factor, 1.0, 500.0);
    update();
}

void SketchView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && m_pendingLineStart) {
        m_pendingLineStart.reset();
        emit statusMessage(QStringLiteral("Ready"));
    } else {
        QWidget::keyPressEvent(event);
    }
}
