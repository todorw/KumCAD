#include "EntityPainter.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Text.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace EntityPainter {

namespace {

// Builds the entity pen, translating an AutoCAD .lin pattern (drawing units;
// positive dash, negative gap, zero dot) into a Qt dash pattern (pen-width
// units). Falls back to solid when the pattern would be denser than ~4px per
// period on screen -- matching how a dashed line reads as solid zoomed out.
QPen makePen(const QColor& color, double penWidth, lcad::LineType linetype, double ltScale, double scale) {
    QPen pen(color, penWidth);
    const auto& pattern = lcad::lineTypePattern(linetype);
    if (pattern.empty()) return pen;

    const double unit = std::max(penWidth, 1.0); // Qt dash values are in pen widths
    double periodPx = 0.0;
    for (double element : pattern) periodPx += std::max(std::abs(element) * ltScale * scale, 1.0);
    if (periodPx < 4.0) return pen;

    QList<qreal> dashes;
    for (double element : pattern) {
        const double px = std::abs(element) * ltScale * scale;
        // Dots and degenerate dashes still need >= about a pen width to show.
        dashes.append(std::max(px, 1.0) / unit);
    }
    pen.setCapStyle(Qt::FlatCap);
    pen.setDashPattern(dashes);
    return pen;
}

} // namespace

void paint(QPainter& painter, const lcad::Entity& entity, const WorldToScreen& toScreen, double scale,
           const QColor& color, double penWidth, lcad::LineType linetype, double ltScale) {
    painter.setPen(makePen(color, penWidth, linetype, ltScale, scale));

    switch (entity.type()) {
    case lcad::EntityType::Line: {
        const auto& line = static_cast<const lcad::LineEntity&>(entity);
        painter.drawLine(toScreen(line.start()), toScreen(line.end()));
        break;
    }
    case lcad::EntityType::Circle: {
        const auto& circle = static_cast<const lcad::CircleEntity&>(entity);
        const QPointF c = toScreen(circle.center());
        const double r = circle.radius() * scale;
        painter.drawEllipse(c, r, r);
        break;
    }
    case lcad::EntityType::Arc: {
        const auto& arc = static_cast<const lcad::ArcEntity&>(entity);
        const QPointF c = toScreen(arc.center());
        const double r = arc.radius() * scale;
        const QRectF bounds(c.x() - r, c.y() - r, 2 * r, 2 * r);

        auto normalize = [](double a) {
            a = std::fmod(a, 2 * M_PI);
            if (a < 0) a += 2 * M_PI;
            return a;
        };
        const double ns = normalize(arc.startAngle());
        const double ne = normalize(arc.endAngle());
        double sweep = ne - ns;
        if (sweep <= 0) sweep += 2 * M_PI;

        // QPainter::drawArc's angle convention is defined visually (0 = visually
        // right/3 o'clock, positive = visually CCW toward 12 o'clock), same as our
        // world angle convention once world points are Y-flipped into screen space
        // by the mapping -- so no extra sign flip is needed here.
        const double startDeg = qRadiansToDegrees(arc.startAngle());
        const double spanDeg = qRadiansToDegrees(sweep);
        painter.drawArc(bounds, static_cast<int>(startDeg * 16), static_cast<int>(spanDeg * 16));
        break;
    }
    case lcad::EntityType::Polyline: {
        const auto& pl = static_cast<const lcad::PolylineEntity&>(entity);
        const auto& verts = pl.vertices();
        if (verts.size() < 2) break;
        QPainterPath path(toScreen(verts.front()));
        pl.forEachSegment([&](const lcad::Point2D& a, const lcad::Point2D& b, double bulge) {
            (void)a;
            if (const auto arc = lcad::bulgeToArc(a, b, bulge)) {
                const QPointF c = toScreen(arc->center);
                const double r = arc->radius * scale;
                // Same visual-angle convention as the ARC case above: the
                // world->screen Y flip makes Qt's CCW-positive arc angles line
                // up with our world angles with no sign change.
                path.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), qRadiansToDegrees(arc->startAngle),
                           qRadiansToDegrees(arc->sweep));
            } else {
                path.lineTo(toScreen(b));
            }
        });
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        break;
    }
    case lcad::EntityType::Ellipse: {
        const auto& ellipse = static_cast<const lcad::EllipseEntity&>(entity);
        painter.save();
        painter.translate(toScreen(ellipse.center()));
        // World CCW rotation is clockwise in raw Y-down screen space, hence
        // the sign flip -- same reasoning as the Text case below.
        painter.rotate(-qRadiansToDegrees(ellipse.rotation()));
        painter.drawEllipse(QPointF(0, 0), ellipse.radiusX() * scale, ellipse.radiusY() * scale);
        painter.restore();
        break;
    }
    case lcad::EntityType::Spline: {
        const auto& spline = static_cast<const lcad::SplineEntity&>(entity);
        const auto pts = spline.sample(96);
        if (pts.size() < 2) break;
        QPainterPath path(toScreen(pts.front()));
        for (std::size_t i = 1; i < pts.size(); ++i) path.lineTo(toScreen(pts[i]));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        break;
    }
    case lcad::EntityType::Dimension: {
        const auto& dim = static_cast<const lcad::DimensionEntity&>(entity);
        const auto geo = dim.geometry();

        painter.drawLine(toScreen(geo.ext1A), toScreen(geo.ext1B));
        painter.drawLine(toScreen(geo.ext2A), toScreen(geo.ext2B));
        painter.drawLine(toScreen(geo.dimA), toScreen(geo.dimB));

        // Arrowheads: slim filled triangles at the dimension line's ends,
        // pointing outward, sized relative to the label text.
        const lcad::Point2D span = geo.dimB - geo.dimA;
        const double spanLen = span.length();
        if (spanLen > 1e-9) {
            const lcad::Point2D dir = span * (1.0 / spanLen);
            const lcad::Point2D normal(-dir.y, dir.x);
            const double arrow = 0.5 * dim.textHeight();
            auto drawArrow = [&](const lcad::Point2D& tip, const lcad::Point2D& inward) {
                QPolygonF tri;
                tri << toScreen(tip) << toScreen(tip + inward * arrow + normal * (arrow / 3.0))
                    << toScreen(tip + inward * arrow - normal * (arrow / 3.0));
                painter.setBrush(color);
                painter.drawPolygon(tri);
                painter.setBrush(Qt::NoBrush);
            };
            drawArrow(geo.dimA, dir);
            drawArrow(geo.dimB, dir * -1.0);
        }

        const QString label = QString::number(geo.value, 'f', 2);
        QFont font = painter.font();
        font.setPixelSize(std::max(1, static_cast<int>(std::round(dim.textHeight() * scale))));
        painter.save();
        painter.setFont(font);
        painter.translate(toScreen(geo.textPos));
        painter.rotate(-qRadiansToDegrees(geo.textAngle)); // same sign flip as the Text case
        const QFontMetricsF metrics(font);
        painter.drawText(QPointF(-metrics.horizontalAdvance(label) / 2.0, metrics.height() / 4.0), label);
        painter.restore();
        break;
    }
    case lcad::EntityType::Text: {
        const auto& text = static_cast<const lcad::TextEntity&>(entity);
        QFont font = painter.font();
        font.setPixelSize(std::max(1, static_cast<int>(std::round(text.height() * scale))));
        painter.save();
        painter.setFont(font);
        painter.translate(toScreen(text.position()));
        // painter.rotate() is clockwise in raw (Y-down) screen space, which is
        // visually clockwise too since we draw directly in that space with no
        // further flip -- our world angle convention is CCW-positive (visually),
        // so it needs the opposite sign here, same reasoning as the ARC case above.
        painter.rotate(-qRadiansToDegrees(text.rotation()));
        painter.drawText(QPointF(0, 0), QString::fromStdString(text.text()));
        painter.restore();
        break;
    }
    case lcad::EntityType::Hatch: {
        const auto& hatch = static_cast<const lcad::HatchEntity&>(entity);
        QPolygonF poly;
        for (const lcad::Point2D& v : hatch.vertices()) poly << toScreen(v);
        painter.setBrush(color);
        painter.drawPolygon(poly);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case lcad::EntityType::Insert: {
        // Blocks render as their transformed children, all in the insert's
        // resolved color (v1 simplification; per-child colors would need
        // ByBlock semantics).
        const auto& insert = static_cast<const lcad::InsertEntity&>(entity);
        for (const auto& child : insert.instantiate()) {
            paint(painter, *child, toScreen, scale, color, penWidth, linetype, ltScale);
        }
        break;
    }
    }
}

} // namespace EntityPainter
