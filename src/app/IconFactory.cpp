#include "IconFactory.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

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

QIcon eraseIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(9, 9), QPointF(23, 23));
        painter.drawLine(QPointF(23, 9), QPointF(9, 23));
    });
}

} // namespace IconFactory
