#include "IconFactory.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <functional>

namespace IconFactory {

namespace {

constexpr int kSize = 32;
constexpr QColor kStroke(224, 224, 224);

QIcon build(const std::function<void(QPainter&)>& draw) {
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(kStroke, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    draw(painter);
    return QIcon(pixmap);
}

void dot(QPainter& painter, QPointF center) {
    painter.setBrush(kStroke);
    painter.drawEllipse(center, 1.6, 1.6);
    painter.setBrush(Qt::NoBrush);
}

} // namespace

QIcon selectIcon() {
    return build([](QPainter& painter) {
        QPolygonF arrow;
        arrow << QPointF(8, 6) << QPointF(8, 25) << QPointF(13, 20) << QPointF(16.5, 27) << QPointF(19, 25.5)
              << QPointF(15.5, 18.5) << QPointF(22, 18) << QPointF(8, 6);
        painter.setBrush(kStroke);
        painter.drawPolygon(arrow);
    });
}

QIcon lineIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(7, 25), QPointF(25, 7));
        dot(painter, QPointF(7, 25));
        dot(painter, QPointF(25, 7));
    });
}

QIcon circleIcon() {
    return build([](QPainter& painter) {
        painter.drawEllipse(QPointF(16, 16), 10, 10);
        dot(painter, QPointF(16, 16));
    });
}

QIcon arcIcon() {
    return build([](QPainter& painter) {
        QPainterPath path;
        path.arcMoveTo(6, 6, 20, 20, 0);
        path.arcTo(6, 6, 20, 20, 0, 90);
        painter.drawPath(path);
        dot(painter, QPointF(26, 16));
        dot(painter, QPointF(16, 6));
    });
}

QIcon polylineIcon() {
    return build([](QPainter& painter) {
        QPolygonF poly;
        poly << QPointF(6, 24) << QPointF(13, 11) << QPointF(19, 19) << QPointF(26, 8);
        painter.drawPolyline(poly);
        for (const QPointF& p : poly) dot(painter, p);
    });
}

QIcon ellipseIcon() {
    return build([](QPainter& painter) {
        painter.save();
        painter.translate(16, 16);
        painter.rotate(20);
        painter.drawEllipse(QPointF(0, 0), 11, 6.5);
        painter.restore();
    });
}

QIcon textIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(7, 8), QPointF(25, 8));
        painter.drawLine(QPointF(16, 8), QPointF(16, 24));
        painter.drawLine(QPointF(11, 24), QPointF(21, 24));
    });
}

QIcon rectangleIcon() {
    return build([](QPainter& painter) {
        painter.drawRect(QRectF(6, 9, 20, 14));
        dot(painter, QPointF(6, 23));
        dot(painter, QPointF(26, 9));
    });
}

QIcon moveIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(16, 5), QPointF(16, 27));
        painter.drawLine(QPointF(5, 16), QPointF(27, 16));
        auto arrow = [&](QPointF tip, QPointF a, QPointF b) {
            painter.drawLine(tip, a);
            painter.drawLine(tip, b);
        };
        arrow(QPointF(16, 5), QPointF(12, 9), QPointF(20, 9));
        arrow(QPointF(16, 27), QPointF(12, 23), QPointF(20, 23));
        arrow(QPointF(5, 16), QPointF(9, 12), QPointF(9, 20));
        arrow(QPointF(27, 16), QPointF(23, 12), QPointF(23, 20));
    });
}

QIcon copyIcon() {
    return build([](QPainter& painter) {
        painter.drawRect(QRectF(6, 10, 14, 14));
        painter.drawLine(QPointF(10, 10), QPointF(10, 6));
        painter.drawLine(QPointF(10, 6), QPointF(24, 6));
        painter.drawLine(QPointF(24, 6), QPointF(24, 20));
        painter.drawLine(QPointF(24, 20), QPointF(20, 20));
    });
}

QIcon rotateIcon() {
    return build([](QPainter& painter) {
        QPainterPath path;
        path.arcMoveTo(6, 6, 20, 20, -30);
        path.arcTo(6, 6, 20, 20, -30, 260);
        painter.drawPath(path);
        // Arrowhead at the open end of the sweep.
        QPolygonF head;
        head << QPointF(23.5, 8.5) << QPointF(27.5, 8.0) << QPointF(25.5, 12.0);
        painter.setBrush(kStroke);
        painter.drawPolygon(head);
    });
}

QIcon scaleIcon() {
    return build([](QPainter& painter) {
        painter.drawRect(QRectF(6, 6, 12, 12));
        painter.drawLine(QPointF(20, 20), QPointF(27, 27));
        QPolygonF head;
        head << QPointF(27, 21) << QPointF(27, 27) << QPointF(21, 27);
        painter.setBrush(kStroke);
        painter.drawPolygon(head);
    });
}

QIcon mirrorIcon() {
    return build([](QPainter& painter) {
        // Dashed mirror axis with a solid triangle and its reflection.
        QPen dashed = painter.pen();
        dashed.setStyle(Qt::DashLine);
        painter.save();
        painter.setPen(dashed);
        painter.drawLine(QPointF(16, 4), QPointF(16, 28));
        painter.restore();
        QPolygonF left;
        left << QPointF(12, 10) << QPointF(5, 22) << QPointF(12, 22) << QPointF(12, 10);
        painter.setBrush(kStroke);
        painter.drawPolygon(left);
        painter.setBrush(Qt::NoBrush);
        QPolygonF right;
        right << QPointF(20, 10) << QPointF(27, 22) << QPointF(20, 22) << QPointF(20, 10);
        painter.drawPolygon(right);
    });
}

QIcon offsetIcon() {
    return build([](QPainter& painter) {
        painter.drawEllipse(QPointF(14, 18), 8, 8);
        QPen thin = painter.pen();
        thin.setWidthF(1.4);
        painter.save();
        painter.setPen(thin);
        painter.drawEllipse(QPointF(14, 18), 12.5, 12.5);
        painter.restore();
    });
}

QIcon trimIcon() {
    return build([](QPainter& painter) {
        // Cutting edge with the trimmed-away part dashed.
        painter.drawLine(QPointF(16, 4), QPointF(16, 28));
        painter.drawLine(QPointF(4, 16), QPointF(16, 16));
        QPen dashed = painter.pen();
        dashed.setStyle(Qt::DashLine);
        painter.save();
        painter.setPen(dashed);
        painter.drawLine(QPointF(16, 16), QPointF(28, 16));
        painter.restore();
    });
}

QIcon extendIcon() {
    return build([](QPainter& painter) {
        // Line growing (dashed) toward a boundary edge.
        painter.drawLine(QPointF(26, 4), QPointF(26, 28));
        painter.drawLine(QPointF(4, 16), QPointF(14, 16));
        QPen dashed = painter.pen();
        dashed.setStyle(Qt::DashLine);
        painter.save();
        painter.setPen(dashed);
        painter.drawLine(QPointF(14, 16), QPointF(25, 16));
        painter.restore();
        QPolygonF head;
        head << QPointF(25, 16) << QPointF(20, 13) << QPointF(20, 19);
        painter.setBrush(kStroke);
        painter.drawPolygon(head);
    });
}

QIcon filletIcon() {
    return build([](QPainter& painter) {
        QPainterPath path;
        path.moveTo(6, 6);
        path.lineTo(6, 16);
        path.arcTo(QRectF(6, 6, 20, 20), 180, 90); // rounded corner
        path.lineTo(26, 26);
        painter.drawPath(path);
    });
}

QIcon dimensionIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(6, 6), QPointF(6, 26));
        painter.drawLine(QPointF(26, 6), QPointF(26, 26));
        painter.drawLine(QPointF(6, 16), QPointF(26, 16));
        painter.setBrush(kStroke);
        QPolygonF left;
        left << QPointF(6, 16) << QPointF(11, 13.5) << QPointF(11, 18.5);
        painter.drawPolygon(left);
        QPolygonF right;
        right << QPointF(26, 16) << QPointF(21, 13.5) << QPointF(21, 18.5);
        painter.drawPolygon(right);
    });
}

QIcon hatchIcon() {
    return build([](QPainter& painter) {
        painter.drawRect(QRectF(6, 6, 20, 20));
        QPen thin = painter.pen();
        thin.setWidthF(1.4);
        painter.save();
        painter.setPen(thin);
        painter.setClipRect(QRectF(6, 6, 20, 20));
        for (int i = -2; i < 6; ++i) {
            const double base = 6.0 + i * 6.0;
            painter.drawLine(QPointF(base, 26), QPointF(base + 20, 6));
        }
        painter.restore();
    });
}

QIcon blockIcon() {
    return build([](QPainter& painter) {
        // Grouped shapes with an insertion-point marker.
        painter.drawRect(QRectF(6, 10, 12, 12));
        painter.drawEllipse(QPointF(21, 21), 5, 5);
        dot(painter, QPointF(6, 22));
    });
}

QIcon eraseIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(9, 9), QPointF(23, 23));
        painter.drawLine(QPointF(23, 9), QPointF(9, 23));
    });
}

QIcon box3DIcon() {
    // The same isometric-cube construction appIcon() uses, scaled to
    // toolbar size and drawn as a plain stroked wireframe instead of the
    // app icon's shaded/filled faces -- echoes the app's own mark at a
    // small size rather than inventing an unrelated box glyph.
    return build([](QPainter& painter) {
        const QPointF center(16, 16.6);
        const QPointF T(center.x(), center.y() - 9.8);
        const QPointF Lh(center.x() - 7, center.y() - 2.8);
        const QPointF Rh(center.x() + 7, center.y() - 2.8);
        const QPointF Fc(center.x(), center.y() + 1.4);
        const QPointF Ll(center.x() - 7, center.y() + 7);
        const QPointF Rl(center.x() + 7, center.y() + 7);
        const QPointF Bt(center.x(), center.y() + 12.6);
        painter.drawPolygon(QPolygonF({T, Lh, Fc, Rh}));
        painter.drawPolygon(QPolygonF({Lh, Fc, Bt, Ll}));
        painter.drawPolygon(QPolygonF({Rh, Fc, Bt, Rl}));
    });
}

QIcon cylinder3DIcon() {
    return build([](QPainter& painter) {
        constexpr double rx = 9, ry = 3.2;
        const QPointF top(16, 9);
        const QPointF bottom(16, 24);
        painter.drawEllipse(top, rx, ry);
        painter.drawLine(QPointF(top.x() - rx, top.y()), QPointF(bottom.x() - rx, bottom.y()));
        painter.drawLine(QPointF(top.x() + rx, top.y()), QPointF(bottom.x() + rx, bottom.y()));
        QPainterPath frontArc;
        frontArc.moveTo(bottom.x() - rx, bottom.y());
        frontArc.arcTo(bottom.x() - rx, bottom.y() - ry, rx * 2, ry * 2, 180, 180);
        painter.drawPath(frontArc);
    });
}

QIcon sphere3DIcon() {
    return build([](QPainter& painter) {
        painter.drawEllipse(QPointF(16, 16), 10, 10);
        painter.save();
        QPen thin = painter.pen();
        thin.setWidthF(1.3);
        painter.setPen(thin);
        painter.translate(16, 16);
        painter.rotate(-15);
        painter.drawArc(QRectF(-10, -3.5, 20, 7), 0, 180 * 16);
        painter.restore();
    });
}

QIcon cone3DIcon() {
    return build([](QPainter& painter) {
        constexpr double rx = 9, ry = 3.2;
        const QPointF apex(16, 6);
        const QPointF baseCenter(16, 24);
        painter.drawEllipse(baseCenter, rx, ry);
        painter.drawLine(apex, QPointF(baseCenter.x() - rx, baseCenter.y()));
        painter.drawLine(apex, QPointF(baseCenter.x() + rx, baseCenter.y()));
    });
}

QIcon torus3DIcon() {
    return build([](QPainter& painter) {
        painter.drawEllipse(QPointF(16, 16), 10, 6);
        painter.drawEllipse(QPointF(16, 16), 4, 2.4);
    });
}

QIcon wedge3DIcon() {
    return build([](QPainter& painter) {
        QPolygonF poly;
        poly << QPointF(6, 24) << QPointF(26, 24) << QPointF(26, 8) << QPointF(6, 18);
        painter.drawPolygon(poly);
    });
}

QIcon union3DIcon() {
    return build([](QPainter& painter) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(kStroke);
        painter.drawEllipse(QPointF(12, 16), 8, 8);
        painter.drawEllipse(QPointF(20, 16), 8, 8);
    });
}

QIcon cut3DIcon() {
    // A solid (A minus B), plus B's own outline so the "subtracted from"
    // shape is still visible -- the standard boolean-cut icon convention.
    return build([](QPainter& painter) {
        QPainterPath a, b;
        a.addEllipse(QPointF(12, 16), 8, 8);
        b.addEllipse(QPointF(20, 16), 8, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(kStroke);
        painter.drawPath(a.subtracted(b));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(kStroke, 1.3));
        painter.drawEllipse(QPointF(20, 16), 8, 8);
    });
}

QIcon intersect3DIcon() {
    return build([](QPainter& painter) {
        QPainterPath a, b;
        a.addEllipse(QPointF(12, 16), 8, 8);
        b.addEllipse(QPointF(20, 16), 8, 8);
        painter.setPen(QPen(kStroke, 1.3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(12, 16), 8, 8);
        painter.drawEllipse(QPointF(20, 16), 8, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(kStroke);
        painter.drawPath(a.intersected(b));
    });
}

namespace {

QIcon buildLarge(const std::function<void(QPainter&)>& draw) {
    constexpr int size = 56;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(kStroke, 2.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    draw(painter);
    return QIcon(pixmap);
}

} // namespace

QIcon mode2DIcon() {
    return buildLarge([](QPainter& painter) {
        painter.drawRect(QRectF(11, 11, 34, 34));
        painter.drawLine(QPointF(11, 34), QPointF(34, 11));
        dot(painter, QPointF(11, 34));
        dot(painter, QPointF(34, 11));
    });
}

QIcon mode3DIcon() {
    return buildLarge([](QPainter& painter) {
        // Wireframe cube in a simple isometric projection.
        const QPointF ftl(13, 20), ftr(33, 20), fbl(13, 40), fbr(33, 40);
        const QPointF btl(21, 11), btr(41, 11), bbl(21, 31), bbr(41, 31);
        painter.drawLine(ftl, ftr);
        painter.drawLine(ftr, fbr);
        painter.drawLine(fbr, fbl);
        painter.drawLine(fbl, ftl);
        painter.drawLine(btl, btr);
        painter.drawLine(btr, bbr);
        painter.drawLine(bbr, bbl);
        painter.drawLine(bbl, btl);
        painter.drawLine(ftl, btl);
        painter.drawLine(ftr, btr);
        painter.drawLine(fbr, bbr);
        painter.drawLine(fbl, bbl);
    });
}

QIcon modePcbIcon() {
    return buildLarge([](QPainter& painter) {
        painter.drawRect(QRectF(15, 15, 26, 26));
        // Pin stubs on all four sides, like a chip package.
        auto pin = [&](QPointF a, QPointF b) { painter.drawLine(a, b); };
        pin(QPointF(21, 15), QPointF(21, 9));
        pin(QPointF(29, 15), QPointF(29, 9));
        pin(QPointF(21, 41), QPointF(21, 47));
        pin(QPointF(29, 41), QPointF(29, 47));
        pin(QPointF(15, 21), QPointF(9, 21));
        pin(QPointF(15, 29), QPointF(9, 29));
        pin(QPointF(41, 21), QPointF(47, 21));
        pin(QPointF(41, 29), QPointF(47, 29));
        dot(painter, QPointF(20, 20));
    });
}

QIcon modeElectricalIcon() {
    return buildLarge([](QPainter& painter) {
        // A relay coil (rectangle) with two contact lines below it, like a
        // simplified IEC 60617 relay/contactor symbol.
        painter.drawRect(QRectF(15, 11, 26, 14));
        painter.drawLine(QPointF(20, 25), QPointF(20, 32));
        painter.drawLine(QPointF(20, 32), QPointF(28, 40));
        painter.drawLine(QPointF(20, 40), QPointF(28, 32));
        painter.drawLine(QPointF(28, 32), QPointF(28, 47));
        dot(painter, QPointF(20, 32));
        dot(painter, QPointF(28, 40));
    });
}

QIcon modeOtherIcon() {
    return buildLarge([](QPainter& painter) {
        painter.setBrush(kStroke);
        dot(painter, QPointF(15, 28));
        dot(painter, QPointF(28, 28));
        dot(painter, QPointF(41, 28));
        painter.setBrush(Qt::NoBrush);
    });
}

QIcon appIcon() {
    // Rendered at 256 and let Qt downscale for smaller uses (taskbar,
    // window icon, welcome screen) -- crisper than the old 64px canvas at
    // any of those sizes.
    constexpr int size = 256;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // A richer two-tone diagonal gradient rather than a flat fill --
    // reads as a real app icon, not a placeholder swatch.
    QLinearGradient bg(0, 0, size, size);
    bg.setColorAt(0.0, QColor(52, 128, 224));
    bg.setColorAt(1.0, QColor(24, 68, 140));
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    const double margin = size * 0.03125; // 8px at 256
    const double radius = size * 0.1875;  // 48px at 256
    painter.drawRoundedRect(QRectF(margin, margin, size - 2 * margin, size - 2 * margin), radius, radius);

    // The mark: an isometric cube, evoking 3D modeling (KumCAD's own
    // expansion past pure 2D drafting) while staying legible as a plain
    // geometric shape at small sizes -- unlike the old compass/
    // straightedge motif, which read as a generic "circle and a line"
    // once scaled down to a taskbar icon.
    // Standard isometric cube construction: a top vertex, two shoulders
    // (top face's left/right corners), a front-center vertex (where all
    // three faces meet), two lower corners, and a bottom vertex.
    const QPointF center(size / 2.0, size / 2.0 + size * 0.02);
    const double edgeX = size * 0.195;    // horizontal half-width of a face
    const double edgeYDown = size * 0.195; // vertical drop of a face's diagonal edge

    const QPointF T(center.x(), center.y() - edgeYDown * 1.4);
    const QPointF Lh(center.x() - edgeX, center.y() - edgeYDown * 0.4);
    const QPointF Rh(center.x() + edgeX, center.y() - edgeYDown * 0.4);
    const QPointF Fc(center.x(), center.y() + edgeYDown * 0.2);
    const QPointF Ll(center.x() - edgeX, center.y() + edgeYDown * 1.0);
    const QPointF Rl(center.x() + edgeX, center.y() + edgeYDown * 1.0);
    const QPointF Bt(center.x(), center.y() + edgeYDown * 1.8);

    const QPolygonF topFace({T, Lh, Fc, Rh});
    const QPolygonF leftFace({Lh, Fc, Bt, Ll});
    const QPolygonF rightFace({Rh, Fc, Bt, Rl});

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 235)); // top: lightest, catching the light
    painter.drawPolygon(topFace);
    painter.setBrush(QColor(255, 255, 255, 140)); // left: mid tone
    painter.drawPolygon(leftFace);
    painter.setBrush(QColor(255, 255, 255, 80)); // right: shadowed
    painter.drawPolygon(rightFace);

    QPen edgePen(QColor(255, 255, 255, 235), size * 0.016);
    edgePen.setCapStyle(Qt::RoundCap);
    edgePen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(edgePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(topFace);
    painter.drawPolygon(leftFace);
    painter.drawPolygon(rightFace);

    return QIcon(pixmap);
}

} // namespace IconFactory
