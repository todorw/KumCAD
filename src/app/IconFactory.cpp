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

QIcon eraseIcon() {
    return build([](QPainter& painter) {
        painter.drawLine(QPointF(9, 9), QPointF(23, 23));
        painter.drawLine(QPointF(23, 9), QPointF(9, 23));
    });
}

} // namespace IconFactory
