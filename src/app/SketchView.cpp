#include "SketchView.h"

#include "core/sketch/ConstraintSolver.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using lcad::Point2D;

namespace {
constexpr double kPickPixels = 10.0;

// Sweep from start to end around center going ccw (or cw), wrapped into a
// single increasing angle range -- same wraparound math as
// SketchToFace.cpp's makeArcEdge, kept in sync deliberately rather than
// shared, since this one only ever needs the arc's own stored (start,end)
// order (no traversal-direction reversal to account for).
std::pair<double, double> arcAngleRange(const Point2D& center, const Point2D& start, const Point2D& end, bool ccw) {
    double startAngle = std::atan2(start.y - center.y, start.x - center.x);
    double endAngle = std::atan2(end.y - center.y, end.x - center.x);
    if (ccw) {
        while (endAngle < startAngle) endAngle += 2.0 * M_PI;
    } else {
        while (endAngle > startAngle) endAngle -= 2.0 * M_PI;
    }
    return {std::min(startAngle, endAngle), std::max(startAngle, endAngle)};
}
}

SketchView::SketchView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 400);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void SketchView::setTool(Tool tool) {
    m_tool = tool;
    m_pendingLineStart.reset();
    m_pendingArcCenter.reset();
    m_pendingArcStart.reset();
    m_pendingSplinePoints.clear();
}

void SketchView::clearSelection() {
    m_selection.clear();
    emit selectionChanged();
    update();
}

void SketchView::resolve() {
    const lcad::SolveResult result = solveSketch(m_sketch);
    // Real rank-based diagnosis (analyzeRedundancy) supersedes the naive
    // equation-count check for display purposes: when it can name specific
    // redundant/conflicting constraints, that's strictly more useful than
    // just "over-constrained". It still falls back to a DOF count otherwise.
    const lcad::RedundancyReport redundancy = lcad::analyzeRedundancy(m_sketch);
    QString dofText;
    if (!redundancy.redundant.empty()) {
        QStringList parts;
        for (const lcad::RedundantConstraint& rc : redundancy.redundant) {
            parts << QStringLiteral("#%1 %2").arg(rc.constraintIndex)
                         .arg(rc.conflicting ? QStringLiteral("(conflicting)") : QStringLiteral("(redundant)"));
        }
        dofText = QStringLiteral(" — constraint %1").arg(parts.join(QStringLiteral(", ")));
    } else if (redundancy.overConstrained) {
        dofText = QStringLiteral(" — over-constrained (rank %1 for %2 equations)").arg(redundancy.rank).arg(redundancy.totalEquations);
    } else {
        dofText = QStringLiteral(" — %1 DOF remaining").arg(redundancy.trueRemainingDof);
    }
    emit statusMessage((result.converged ? QStringLiteral("Solved (residual %1)").arg(result.finalResidualNorm, 0, 'e', 2)
                                         : QStringLiteral("Did not converge (residual %1) — check for conflicting "
                                                          "or redundant constraints")
                                               .arg(result.finalResidualNorm, 0, 'e', 2)) +
                       dofText);
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
    for (std::size_t i = 0; i < m_sketch.arcs().size(); ++i) {
        const auto& arc = m_sketch.arcs()[i];
        const Point2D& center = m_sketch.points()[static_cast<std::size_t>(arc.center)];
        if (std::abs(sketchPos.distanceTo(center) - arc.radius) > tolSketch) continue;
        const Point2D& start = m_sketch.points()[static_cast<std::size_t>(arc.start)];
        const Point2D& end = m_sketch.points()[static_cast<std::size_t>(arc.end)];
        const auto [lo, hi] = arcAngleRange(center, start, end, arc.ccw);
        double angle = std::atan2(sketchPos.y - center.y, sketchPos.x - center.x);
        while (angle < lo) angle += 2.0 * M_PI;
        if (angle <= hi) return Selection{Selection::Kind::Arc, static_cast<int>(i)};
    }
    for (std::size_t i = 0; i < m_sketch.splines().size(); ++i) {
        const auto& spline = m_sketch.splines()[i];
        if (spline.controlPoints.size() < 2) continue;
        std::vector<Point2D> ctrl;
        ctrl.reserve(spline.controlPoints.size());
        for (int idx : spline.controlPoints) ctrl.push_back(m_sketch.points()[static_cast<std::size_t>(idx)]);
        constexpr int kSegments = 24;
        Point2D previous = lcad::evaluateSketchSpline(ctrl, 0.0);
        for (int s = 1; s <= kSegments; ++s) {
            const Point2D next = lcad::evaluateSketchSpline(ctrl, static_cast<double>(s) / kSegments);
            const Point2D dir = next - previous;
            const double lenSq = dir.dot(dir);
            double t = lenSq < 1e-12 ? 0.0 : (sketchPos - previous).dot(dir) / lenSq;
            t = std::clamp(t, 0.0, 1.0);
            const Point2D closest = previous + dir * t;
            if (sketchPos.distanceTo(closest) <= tolSketch) return Selection{Selection::Kind::Spline, static_cast<int>(i)};
            previous = next;
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

    for (std::size_t i = 0; i < m_sketch.arcs().size(); ++i) {
        const auto& arc = m_sketch.arcs()[i];
        QPen pen(isSelected(Selection::Kind::Arc, static_cast<int>(i)) ? QColor(255, 200, 0) : QColor(120, 255, 180));
        if (arc.construction) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        const Point2D& center = m_sketch.points()[static_cast<std::size_t>(arc.center)];
        const Point2D& start = m_sketch.points()[static_cast<std::size_t>(arc.start)];
        const Point2D& end = m_sketch.points()[static_cast<std::size_t>(arc.end)];
        const auto [lo, hi] = arcAngleRange(center, start, end, arc.ccw);
        // Tessellated into a polyline rather than QPainter::drawArc -- this
        // sidesteps Qt's own arc-angle sign convention entirely (toScreen
        // already flips Y for sketch-space-vs-screen-space), matching
        // SketchToFace.cpp's angle math exactly instead of a second,
        // easy-to-get-backwards convention.
        constexpr int kSegments = 24;
        QPointF previous = toScreen(Point2D(center.x + arc.radius * std::cos(lo), center.y + arc.radius * std::sin(lo)));
        for (int s = 1; s <= kSegments; ++s) {
            const double t = lo + (hi - lo) * (static_cast<double>(s) / kSegments);
            const QPointF next = toScreen(Point2D(center.x + arc.radius * std::cos(t), center.y + arc.radius * std::sin(t)));
            painter.drawLine(previous, next);
            previous = next;
        }
    }

    for (std::size_t i = 0; i < m_sketch.splines().size(); ++i) {
        const auto& spline = m_sketch.splines()[i];
        if (spline.controlPoints.size() < 2) continue;
        QPen pen(isSelected(Selection::Kind::Spline, static_cast<int>(i)) ? QColor(255, 200, 0) : QColor(255, 150, 220));
        if (spline.construction) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        std::vector<Point2D> ctrl;
        ctrl.reserve(spline.controlPoints.size());
        for (int idx : spline.controlPoints) ctrl.push_back(m_sketch.points()[static_cast<std::size_t>(idx)]);
        constexpr int kSegments = 32;
        QPointF previous = toScreen(lcad::evaluateSketchSpline(ctrl, 0.0));
        for (int s = 1; s <= kSegments; ++s) {
            const QPointF next = toScreen(lcad::evaluateSketchSpline(ctrl, static_cast<double>(s) / kSegments));
            painter.drawLine(previous, next);
            previous = next;
        }
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
    } else if (m_tool == Tool::Arc) {
        if (!m_pendingArcCenter) {
            m_pendingArcCenter = findOrCreatePoint(sketchPos);
            emit statusMessage(QStringLiteral("Specify arc start point"));
        } else if (!m_pendingArcStart) {
            m_pendingArcStart = findOrCreatePoint(sketchPos);
            emit statusMessage(QStringLiteral("Specify arc end point"));
        } else {
            const int endIdx = findOrCreatePoint(sketchPos);
            const Point2D& center = m_sketch.points()[static_cast<std::size_t>(*m_pendingArcCenter)];
            const Point2D& start = m_sketch.points()[static_cast<std::size_t>(*m_pendingArcStart)];
            const double radius = center.distanceTo(start);
            if (radius > 1e-6 && endIdx != *m_pendingArcStart) {
                m_sketch.addArc(*m_pendingArcCenter, *m_pendingArcStart, endIdx, radius, true);
                resolve();
            }
            m_pendingArcCenter.reset();
            m_pendingArcStart.reset();
        }
    } else if (m_tool == Tool::Spline) {
        m_pendingSplinePoints.push_back(findOrCreatePoint(sketchPos));
        emit statusMessage(QStringLiteral("Specify next control point (Enter to finish, Escape to cancel) -- %1 so far")
                               .arg(m_pendingSplinePoints.size()));
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
    const bool anyPending =
        m_pendingLineStart || m_pendingArcCenter || m_pendingArcStart || !m_pendingSplinePoints.empty();
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_tool == Tool::Spline &&
        m_pendingSplinePoints.size() >= 2) {
        m_sketch.addSpline(m_pendingSplinePoints);
        m_pendingSplinePoints.clear();
        resolve();
        emit statusMessage(QStringLiteral("Spline added"));
    } else if (event->key() == Qt::Key_Escape && anyPending) {
        m_pendingLineStart.reset();
        m_pendingArcCenter.reset();
        m_pendingArcStart.reset();
        m_pendingSplinePoints.clear();
        emit statusMessage(QStringLiteral("Ready"));
    } else {
        QWidget::keyPressEvent(event);
    }
}
