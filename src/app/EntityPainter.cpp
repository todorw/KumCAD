#include "EntityPainter.h"

#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/PointCloud.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Table.h"
#include "core/geometry/Text.h"
#include "core/document/Document.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifdef LCAD_HAS_PDF
#include <poppler-qt6.h>
#endif

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

// Font for a TEXT/MTEXT entity: pixel height plus the entity's named text
// style (family, width factor, oblique) when the document is available.
QFont styledFont(const QPainter& painter, const lcad::Document* document, const std::string& styleName,
                 double pixelHeight) {
    QFont font = painter.font();
    font.setPixelSize(std::max(1, static_cast<int>(std::round(pixelHeight))));
    const lcad::TextStyle* style = document ? document->findTextStyle(styleName) : nullptr;
    if (style) {
        if (!style->font.empty()) font.setFamily(QString::fromStdString(style->font));
        font.setStretch(std::clamp(static_cast<int>(std::round(style->widthFactor * 100.0)), 12, 400));
        if (std::abs(style->obliqueDeg) > 0.1) font.setItalic(true);
    }
    return font;
}

// Simplified annotative scaling: an Annotative TextStyle's objects render at
// height * multiplier instead of their nominal height, so they read the
// same physical size once the multiplier matches the relevant plot scale.
// override, when positive, wins over document->annotationScale() -- see
// EntityPainter::paint's header comment for why (per-viewport scale).
double annotationMultiplier(const lcad::Document* document, const std::string& styleName, double override) {
    if (!document) return 1.0;
    const lcad::TextStyle* style = document->findTextStyle(styleName);
    if (!style || !style->annotative) return 1.0;
    return override > 1e-9 ? override : document->annotationScale();
}

// Builds the QBrush for a GRADIENT hatch. Approximates AutoCAD's nine named
// presets with Qt's linear/radial gradients -- not pixel-identical to
// AutoCAD's proprietary curves, but each preset reads as visually distinct:
// LINEAR is a plain two-stop sweep; CYLINDER/CURVED are three-stop sweeps
// with a highlight band (CURVED's band sits off-center instead of at the
// midpoint); SPHERICAL/HEMISPHERICAL are radial, HEMISPHERICAL's center
// shifted toward one edge along the gradient angle instead of the boundary's
// centroid. Every INV* variant just swaps the two colors' stop order.
QBrush gradientBrush(lcad::GradientPreset preset, const QColor& color1, const QColor& color2,
                     const WorldToScreen& toScreen, double scale, const lcad::BoundingBox& box, double angleRadians) {
    const lcad::Point2D mid((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0);
    const double halfSpan = 0.5 * lcad::Point2D(box.max.x - box.min.x, box.max.y - box.min.y).length();
    const lcad::Point2D dir(std::cos(angleRadians), std::sin(angleRadians));

    const bool inverted = preset == lcad::GradientPreset::InvCylinder ||
                          preset == lcad::GradientPreset::InvSpherical ||
                          preset == lcad::GradientPreset::InvHemispherical ||
                          preset == lcad::GradientPreset::InvCurved;
    const QColor& edge = inverted ? color2 : color1;
    const QColor& highlight = inverted ? color1 : color2;

    switch (preset) {
    case lcad::GradientPreset::Cylinder:
    case lcad::GradientPreset::InvCylinder:
    case lcad::GradientPreset::Curved:
    case lcad::GradientPreset::InvCurved: {
        const bool curved = preset == lcad::GradientPreset::Curved || preset == lcad::GradientPreset::InvCurved;
        const double highlightPos = curved ? 0.3 : 0.5; // curved's band sits off-center
        QLinearGradient gradient(toScreen(mid - dir * halfSpan), toScreen(mid + dir * halfSpan));
        gradient.setColorAt(0.0, edge);
        gradient.setColorAt(highlightPos, highlight);
        gradient.setColorAt(1.0, edge);
        return QBrush(gradient);
    }
    case lcad::GradientPreset::Spherical:
    case lcad::GradientPreset::InvSpherical:
    case lcad::GradientPreset::Hemispherical:
    case lcad::GradientPreset::InvHemispherical: {
        const bool hemi =
            preset == lcad::GradientPreset::Hemispherical || preset == lcad::GradientPreset::InvHemispherical;
        const lcad::Point2D center = hemi ? mid - dir * (halfSpan * 0.5) : mid;
        QRadialGradient gradient(toScreen(center), std::max(1.0, halfSpan * scale));
        gradient.setColorAt(0.0, highlight);
        gradient.setColorAt(1.0, edge);
        return QBrush(gradient);
    }
    case lcad::GradientPreset::Linear:
    default: {
        QLinearGradient gradient(toScreen(mid - dir * halfSpan), toScreen(mid + dir * halfSpan));
        gradient.setColorAt(0.0, color1);
        gradient.setColorAt(1.0, color2);
        return QBrush(gradient);
    }
    }
}

} // namespace

void paint(QPainter& painter, const lcad::Entity& entity, const WorldToScreen& toScreen, double scale,
           const QColor& color, double penWidth, lcad::LineType linetype, double ltScale,
           const lcad::Document* document, double annotationScaleOverride) {
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

        if (geo.ext1A.distanceTo(geo.ext1B) > 1e-9) painter.drawLine(toScreen(geo.ext1A), toScreen(geo.ext1B));
        if (geo.ext2A.distanceTo(geo.ext2B) > 1e-9) painter.drawLine(toScreen(geo.ext2A), toScreen(geo.ext2B));

        // Arrowheads: slim filled triangles pointing outward along the given
        // inward direction, sized by the entity's arrow-size style.
        const double arrow = dim.arrowSize();
        auto drawArrow = [&](const lcad::Point2D& tip, const lcad::Point2D& inward) {
            const lcad::Point2D normal(-inward.y, inward.x);
            QPolygonF tri;
            tri << toScreen(tip) << toScreen(tip + inward * arrow + normal * (arrow / 3.0))
                << toScreen(tip + inward * arrow - normal * (arrow / 3.0));
            painter.setBrush(color);
            painter.drawPolygon(tri);
            painter.setBrush(Qt::NoBrush);
        };

        if (geo.angular) {
            // The dimension line is an arc about the measured vertex.
            const QPointF c = toScreen(geo.arcCenter);
            const double r = geo.arcRadius * scale;
            const QRectF bounds(c.x() - r, c.y() - r, 2 * r, 2 * r);
            const double startDeg = qRadiansToDegrees(geo.arcStartAngle);
            const double spanDeg = qRadiansToDegrees(geo.arcEndAngle - geo.arcStartAngle);
            painter.drawArc(bounds, static_cast<int>(startDeg * 16), static_cast<int>(spanDeg * 16));
            // Arrow tips point along the arc's tangents.
            auto tangent = [&](double angle, double sign) {
                return lcad::Point2D(-std::sin(angle), std::cos(angle)) * sign;
            };
            if (geo.arrow1) drawArrow(geo.dimA, tangent(geo.arcStartAngle, 1.0));
            if (geo.arrow2) drawArrow(geo.dimB, tangent(geo.arcEndAngle, -1.0));
        } else {
            painter.drawLine(toScreen(geo.dimA), toScreen(geo.dimB));
            const lcad::Point2D span = geo.dimB - geo.dimA;
            const double spanLen = span.length();
            if (spanLen > 1e-9) {
                const lcad::Point2D dir = span * (1.0 / spanLen);
                if (geo.arrow1) drawArrow(geo.dimA, dir);
                if (geo.arrow2) drawArrow(geo.dimB, dir * -1.0);
            }
        }

        const QString label = QString::fromUtf8(geo.label.c_str());
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
        const QFont font = styledFont(painter, document, text.styleName(),
                                      text.height() * scale *
                                          annotationMultiplier(document, text.styleName(), annotationScaleOverride));
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
    case lcad::EntityType::Leader: {
        const auto& leader = static_cast<const lcad::LeaderEntity&>(entity);
        const auto& pts = leader.points();
        if (pts.size() < 2) break;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            painter.drawLine(toScreen(pts[i]), toScreen(pts[i + 1]));
        }
        // Arrowhead at the first point, aimed back along the first segment.
        const lcad::Point2D span = pts[1] - pts[0];
        const double len = span.length();
        if (len > 1e-9) {
            const lcad::Point2D dir = span * (1.0 / len);
            const lcad::Point2D normal(-dir.y, dir.x);
            const double arrow = leader.arrowSize();
            QPolygonF tri;
            tri << toScreen(pts[0]) << toScreen(pts[0] + dir * arrow + normal * (arrow / 3.0))
                << toScreen(pts[0] + dir * arrow - normal * (arrow / 3.0));
            painter.setBrush(color);
            painter.drawPolygon(tri);
            painter.setBrush(Qt::NoBrush);
        }
        break;
    }
    case lcad::EntityType::MLeader: {
        const auto& mleader = static_cast<const lcad::MLeaderEntity&>(entity);
        for (const auto& leg : mleader.legs()) {
            if (leg.empty()) continue;
            lcad::Point2D prev = leg.front();
            for (std::size_t i = 1; i < leg.size(); ++i) {
                painter.drawLine(toScreen(prev), toScreen(leg[i]));
                prev = leg[i];
            }
            painter.drawLine(toScreen(prev), toScreen(mleader.landing()));

            // Arrowhead at the leg's first point, aimed back along its first segment.
            const lcad::Point2D next = leg.size() > 1 ? leg[1] : mleader.landing();
            const lcad::Point2D span = next - leg.front();
            const double len = span.length();
            if (len > 1e-9) {
                const lcad::Point2D dir = span * (1.0 / len);
                const lcad::Point2D normal(-dir.y, dir.x);
                const double arrow = mleader.arrowSize();
                QPolygonF tri;
                tri << toScreen(leg.front()) << toScreen(leg.front() + dir * arrow + normal * (arrow / 3.0))
                    << toScreen(leg.front() + dir * arrow - normal * (arrow / 3.0));
                painter.setBrush(color);
                painter.drawPolygon(tri);
                painter.setBrush(Qt::NoBrush);
            }
        }
        break;
    }
    case lcad::EntityType::MText: {
        const auto& mtext = static_cast<const lcad::MTextEntity&>(entity);
        const double annoScale = annotationMultiplier(document, mtext.styleName(), annotationScaleOverride);
        const QFont font = styledFont(painter, document, mtext.styleName(), mtext.height() * scale * annoScale);
        painter.save();
        painter.setFont(font);
        painter.translate(toScreen(mtext.position()));
        painter.rotate(-qRadiansToDegrees(mtext.rotation())); // same sign flip as the Text case
        const double advance = mtext.lineAdvance() * scale * annoScale;
        const QFontMetricsF metrics(font);
        double y = metrics.ascent(); // first baseline sits one ascent below the top-left anchor
        for (const std::string& line : mtext.wrappedLines()) {
            painter.drawText(QPointF(0, y), QString::fromStdString(line));
            y += advance;
        }
        painter.restore();
        break;
    }
    case lcad::EntityType::Table: {
        const auto& table = static_cast<const lcad::TableEntity&>(entity);
        const double totalW = table.totalWidth();
        const double totalH = table.totalHeight();
        const lcad::Point2D& pos = table.position();

        // Grid lines: outer border plus each internal row/column boundary.
        double y = pos.y;
        painter.drawLine(toScreen(lcad::Point2D(pos.x, y)), toScreen(lcad::Point2D(pos.x + totalW, y)));
        for (double h : table.rowHeights()) {
            y -= h;
            painter.drawLine(toScreen(lcad::Point2D(pos.x, y)), toScreen(lcad::Point2D(pos.x + totalW, y)));
        }
        double x = pos.x;
        painter.drawLine(toScreen(lcad::Point2D(x, pos.y)), toScreen(lcad::Point2D(x, pos.y - totalH)));
        for (double w : table.colWidths()) {
            x += w;
            painter.drawLine(toScreen(lcad::Point2D(x, pos.y)), toScreen(lcad::Point2D(x, pos.y - totalH)));
        }

        // Cell text, clipped to its cell so long strings don't bleed over
        // neighbors.
        const QFont font = styledFont(painter, document, "Standard", table.textHeight() * scale);
        painter.save();
        painter.setFont(font);
        const double pad = 0.2 * table.textHeight() * scale;
        for (int r = 0; r < table.rows(); ++r) {
            for (int c = 0; c < table.cols(); ++c) {
                const std::string& text = table.cellText(r, c);
                if (text.empty()) continue;
                const lcad::BoundingBox cell = table.cellRect(r, c);
                const QPointF topLeft = toScreen(lcad::Point2D(cell.min.x, cell.max.y));
                const QPointF bottomRight = toScreen(lcad::Point2D(cell.max.x, cell.min.y));
                const QRectF rect(topLeft + QPointF(pad, 0), bottomRight - QPointF(pad, 0));
                painter.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                                 QString::fromStdString(text));
            }
        }
        painter.restore();
        break;
    }
    case lcad::EntityType::Image: {
        const auto& image = static_cast<const lcad::ImageEntity&>(entity);
        // Process-wide pixmap cache keyed by "path#page": entities repaint
        // every frame, loading/rasterizing from disk each time would be far
        // too slow. The page suffix keeps different pages of the same PDF
        // (and plain raster images, whose page is always 0) from colliding.
        static std::unordered_map<std::string, QPixmap> cache;
        const std::string cacheKey = image.path() + "#" + std::to_string(image.pdfPage());
        auto it = cache.find(cacheKey);
        if (it == cache.end()) {
            QPixmap pixmap;
            const QString path = QString::fromStdString(image.path());
            if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
#ifdef LCAD_HAS_PDF
                if (const auto doc = Poppler::Document::load(path)) {
                    if (const auto page = doc->page(image.pdfPage())) {
                        // 150dpi is enough detail for an underlay traced over at typical zoom.
                        pixmap = QPixmap::fromImage(page->renderToImage(150.0, 150.0));
                    }
                }
#endif
            } else {
                pixmap.load(path);
            }
            it = cache.emplace(cacheKey, std::move(pixmap)).first;
        }
        const QPixmap& pixmap = it->second;

        painter.save();
        painter.translate(toScreen(image.position()));
        painter.rotate(-qRadiansToDegrees(image.rotation())); // same sign flip as the Text case
        const double w = image.width() * scale;
        const double h = image.height() * scale;
        const QRectF target(0, -h, w, h); // position is the bottom-left corner, screen y is flipped
        if (!pixmap.isNull()) {
            painter.drawPixmap(target, pixmap, pixmap.rect());
        } else {
            painter.setPen(QPen(color, 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(target);
            painter.drawLine(target.topLeft(), target.bottomRight());
            painter.drawLine(target.topRight(), target.bottomLeft());
        }
        painter.restore();
        break;
    }
    case lcad::EntityType::PointCloud: {
        const auto& cloud = static_cast<const lcad::PointCloudEntity&>(entity);
        for (const lcad::Point2D& p : cloud.points()) painter.drawPoint(toScreen(p));
        break;
    }
    case lcad::EntityType::Hatch: {
        const auto& hatch = static_cast<const lcad::HatchEntity&>(entity);
        if (hatch.isGradient()) {
            QPolygonF poly;
            lcad::BoundingBox box;
            for (const lcad::Point2D& v : hatch.vertices()) {
                poly << toScreen(v);
                box.expand(v);
            }
            const lcad::Color& c2 = *hatch.gradientColor2();
            painter.setBrush(gradientBrush(hatch.gradientPreset(), color, QColor(c2.r, c2.g, c2.b), toScreen, scale,
                                           box, hatch.patternAngle()));
            painter.drawPolygon(poly);
            painter.setBrush(Qt::NoBrush);
        } else if (hatch.pattern() == lcad::HatchPattern::Solid) {
            QPolygonF poly;
            for (const lcad::Point2D& v : hatch.vertices()) poly << toScreen(v);
            painter.setBrush(color);
            painter.drawPolygon(poly);
            painter.setBrush(Qt::NoBrush);
        } else {
            // Boundary outline plus the pattern's clipped line work.
            QPolygonF poly;
            for (const lcad::Point2D& v : hatch.vertices()) poly << toScreen(v);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(poly);
            for (const auto& [a, b] : hatch.patternSegments()) {
                painter.drawLine(toScreen(a), toScreen(b));
            }
        }
        break;
    }
    case lcad::EntityType::Insert: {
        // Blocks render as their transformed children, all in the insert's
        // resolved color (v1 simplification; per-child colors would need
        // ByBlock semantics).
        const auto& insert = static_cast<const lcad::InsertEntity&>(entity);
        for (const auto& child : insert.instantiate()) {
            paint(painter, *child, toScreen, scale, color, penWidth, linetype, ltScale, document);
        }
        break;
    }
    case lcad::EntityType::Point: {
        const auto& point = static_cast<const lcad::PointEntity&>(entity);
        const QPointF s = toScreen(point.position());
        const int mode = document ? document->pointMode() : 3;
        const double half = std::max(2.0, (document ? document->pointSize() : 2.0) * scale / 2.0);
        painter.setPen(QPen(color, penWidth));
        switch (mode & 7) {
        case 0: // dot
            painter.drawPoint(s);
            break;
        case 2: // plus
            painter.drawLine(QPointF(s.x() - half, s.y()), QPointF(s.x() + half, s.y()));
            painter.drawLine(QPointF(s.x(), s.y() - half), QPointF(s.x(), s.y() + half));
            break;
        case 4: // vertical tick
            painter.drawLine(s, QPointF(s.x(), s.y() - half));
            break;
        case 3: // X
        default:
            painter.drawLine(QPointF(s.x() - half, s.y() - half), QPointF(s.x() + half, s.y() + half));
            painter.drawLine(QPointF(s.x() - half, s.y() + half), QPointF(s.x() + half, s.y() - half));
            break;
        }
        if (mode & 32) painter.drawEllipse(s, half, half);
        break;
    }
    case lcad::EntityType::ConstructionLine: {
        const auto& cl = static_cast<const lcad::ConstructionLineEntity&>(entity);
        lcad::Point2D a, b;
        cl.asSegment(a, b);
        painter.drawLine(toScreen(a), toScreen(b));
        break;
    }
    case lcad::EntityType::AttDef: {
        // Standalone attribute definitions display their tag, like AutoCAD.
        const auto& attdef = static_cast<const lcad::AttDefEntity&>(entity);
        QFont font = painter.font();
        font.setPixelSize(std::max(1, static_cast<int>(std::round(attdef.height() * scale))));
        painter.save();
        painter.setFont(font);
        painter.translate(toScreen(attdef.position()));
        painter.rotate(-qRadiansToDegrees(attdef.rotation()));
        painter.drawText(QPointF(0, 0), QString::fromStdString(attdef.tag()));
        painter.restore();
        break;
    }
    }
}

} // namespace EntityPainter
