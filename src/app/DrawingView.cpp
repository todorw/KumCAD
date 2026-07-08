#include "DrawingView.h"

#include "CommandDispatcher.h"
#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"

#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
const QColor kSelectedColor(255, 170, 0);
const QColor kHoverColor(120, 200, 255);
const QColor kDragPreviewColor(0, 220, 180);
const QColor kGripColor(0, 180, 255);
} // namespace

DrawingView::DrawingView(lcad::Document& document, QWidget* parent) : QOpenGLWidget(parent), m_document(document) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

lcad::Point2D DrawingView::screenToWorld(const QPointF& screen) const {
    return lcad::Point2D((screen.x() - width() / 2.0) / m_scale + m_viewCenter.x,
                          (height() / 2.0 - screen.y()) / m_scale + m_viewCenter.y);
}

QPointF DrawingView::worldToScreen(const lcad::Point2D& world) const {
    return QPointF((world.x - m_viewCenter.x) * m_scale + width() / 2.0,
                    height() / 2.0 - (world.y - m_viewCenter.y) * m_scale);
}

void DrawingView::zoomExtents() {
    lcad::BoundingBox box;
    for (lcad::Entity* e : m_document.entities()) box.expand(e->boundingBox());
    if (!box.isValid()) {
        m_viewCenter = lcad::Point2D(0.0, 0.0);
        m_scale = 10.0;
        update();
        return;
    }
    const double w = std::max(box.max.x - box.min.x, 1e-6);
    const double h = std::max(box.max.y - box.min.y, 1e-6);
    m_viewCenter = lcad::Point2D((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0);
    const double margin = 1.2;
    m_scale = std::min(width() / (w * margin), height() / (h * margin));
    update();
}

lcad::Entity* DrawingView::hitTestEntity(const lcad::Point2D& worldPt) const {
    const double tol = pickToleranceWorld();
    lcad::Entity* best = nullptr;
    double bestDist = tol;
    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        const double d = e->distanceTo(worldPt);
        if (d <= bestDist) {
            bestDist = d;
            best = e;
        }
    }
    return best;
}

std::optional<std::pair<lcad::EntityId, std::size_t>> DrawingView::hitTestGrip(const QPointF& screenPt) const {
    constexpr double kGripPickPx = 8.0;
    for (lcad::EntityId id : m_selection) {
        lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        const auto grips = e->gripPoints();
        for (std::size_t i = 0; i < grips.size(); ++i) {
            const QPointF gs = worldToScreen(grips[i]);
            const double dx = gs.x() - screenPt.x();
            const double dy = gs.y() - screenPt.y();
            if (std::sqrt(dx * dx + dy * dy) <= kGripPickPx) return std::make_pair(id, i);
        }
    }
    return std::nullopt;
}

std::optional<lcad::SnapPoint> DrawingView::findSnapCandidate(const QPointF& screenPos) const {
    constexpr double kSnapPickPx = 10.0;
    std::optional<lcad::SnapPoint> best;
    double bestDist = kSnapPickPx;
    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        for (const lcad::SnapPoint& sp : e->snapCandidates()) {
            const QPointF s = worldToScreen(sp.point);
            const double dx = s.x() - screenPos.x();
            const double dy = s.y() - screenPos.y();
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d <= bestDist) {
                bestDist = d;
                best = sp;
            }
        }
    }
    return best;
}

lcad::Point2D DrawingView::applyOrtho(const lcad::Point2D& anchor, const lcad::Point2D& pt) const {
    const double dx = pt.x - anchor.x;
    const double dy = pt.y - anchor.y;
    if (std::abs(dx) >= std::abs(dy)) return lcad::Point2D(pt.x, anchor.y);
    return lcad::Point2D(anchor.x, pt.y);
}

lcad::Point2D DrawingView::snapToGrid(const lcad::Point2D& pt) const {
    const double spacing = gridSpacing();
    return lcad::Point2D(std::round(pt.x / spacing) * spacing, std::round(pt.y / spacing) * spacing);
}

lcad::Point2D DrawingView::resolvePoint(const QPointF& screenPos) {
    m_currentSnap.reset();
    if (m_osnapEnabled) {
        if (auto snap = findSnapCandidate(screenPos)) {
            m_currentSnap = snap;
            return snap->point;
        }
    }

    lcad::Point2D working = screenToWorld(screenPos);

    if (m_orthoEnabled && m_dispatcher && m_dispatcher->hasActiveCommand()) {
        if (auto anchor = m_dispatcher->activeDrawCommand()->anchorPoint()) {
            working = applyOrtho(*anchor, working);
        }
    }

    if (m_gridSnapEnabled) working = snapToGrid(working);

    return working;
}

void DrawingView::setOsnapEnabled(bool on) {
    m_osnapEnabled = on;
    emit modesChanged();
    update();
}

void DrawingView::setOrthoEnabled(bool on) {
    m_orthoEnabled = on;
    emit modesChanged();
    update();
}

void DrawingView::setGridSnapEnabled(bool on) {
    m_gridSnapEnabled = on;
    emit modesChanged();
    update();
}

void DrawingView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(33, 33, 33));

    drawGrid(painter);

    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;

        const bool selected = m_selection.count(e->id()) > 0;
        const bool isDragGhostSource = (m_dragMode == DragMode::MoveSelection && selected) ||
                                        (m_dragMode == DragMode::Grip && e->id() == m_gripEntityId);
        if (isDragGhostSource) continue; // drawn instead by drawDragPreview()

        const bool hovered = m_hoverEntityId && *m_hoverEntityId == e->id();
        QColor color;
        double width = 1.0;
        if (selected) {
            color = kSelectedColor;
            width = 2.0;
        } else if (hovered) {
            color = kHoverColor;
            width = 2.0;
        } else {
            color = layer ? QColor(layer->color.r, layer->color.g, layer->color.b) : QColor(255, 255, 255);
        }
        drawEntity(painter, *e, color, width);
    }

    drawDragPreview(painter);
    drawGrips(painter);
    drawPreview(painter);
    drawSelectionBox(painter);
    drawSnapMarker(painter);

    if (m_lastMouseWorld) {
        const QPointF c = worldToScreen(*m_lastMouseWorld);
        painter.setPen(QPen(QColor(200, 200, 200), 1));
        painter.drawLine(QPointF(c.x() - 10, c.y()), QPointF(c.x() + 10, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - 10), QPointF(c.x(), c.y() + 10));
    }
}

double DrawingView::gridSpacing() const {
    double spacing = 1.0;
    while (spacing * m_scale < 20.0) spacing *= 10.0;
    while (spacing * m_scale > 200.0) spacing /= 10.0;
    return spacing;
}

void DrawingView::drawGrid(QPainter& painter) {
    const double spacing = gridSpacing();

    const lcad::Point2D topLeft = screenToWorld(QPointF(0, 0));
    const lcad::Point2D bottomRight = screenToWorld(QPointF(width(), height()));
    const double minX = std::min(topLeft.x, bottomRight.x);
    const double maxX = std::max(topLeft.x, bottomRight.x);
    const double minY = std::min(topLeft.y, bottomRight.y);
    const double maxY = std::max(topLeft.y, bottomRight.y);

    if ((maxX - minX) / spacing > 2000 || (maxY - minY) / spacing > 2000) return;

    painter.setPen(QPen(QColor(60, 60, 60), 1));
    const double startX = std::floor(minX / spacing) * spacing;
    for (double x = startX; x <= maxX; x += spacing) {
        painter.drawLine(worldToScreen(lcad::Point2D(x, minY)), worldToScreen(lcad::Point2D(x, maxY)));
    }
    const double startY = std::floor(minY / spacing) * spacing;
    for (double y = startY; y <= maxY; y += spacing) {
        painter.drawLine(worldToScreen(lcad::Point2D(minX, y)), worldToScreen(lcad::Point2D(maxX, y)));
    }

    painter.setPen(QPen(QColor(110, 60, 60), 1));
    painter.drawLine(worldToScreen(lcad::Point2D(minX, 0)), worldToScreen(lcad::Point2D(maxX, 0)));
    painter.setPen(QPen(QColor(60, 110, 60), 1));
    painter.drawLine(worldToScreen(lcad::Point2D(0, minY)), worldToScreen(lcad::Point2D(0, maxY)));
}

void DrawingView::drawEntity(QPainter& painter, const lcad::Entity& entity, const QColor& color, double penWidth) {
    painter.setPen(QPen(color, penWidth));

    switch (entity.type()) {
    case lcad::EntityType::Line: {
        const auto& line = static_cast<const lcad::LineEntity&>(entity);
        painter.drawLine(worldToScreen(line.start()), worldToScreen(line.end()));
        break;
    }
    case lcad::EntityType::Circle: {
        const auto& circle = static_cast<const lcad::CircleEntity&>(entity);
        const QPointF c = worldToScreen(circle.center());
        const double r = circle.radius() * m_scale;
        painter.drawEllipse(c, r, r);
        break;
    }
    case lcad::EntityType::Arc: {
        const auto& arc = static_cast<const lcad::ArcEntity&>(entity);
        const QPointF c = worldToScreen(arc.center());
        const double r = arc.radius() * m_scale;
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
        // by worldToScreen() -- so no extra sign flip is needed here.
        const double startDeg = qRadiansToDegrees(arc.startAngle());
        const double spanDeg = qRadiansToDegrees(sweep);
        painter.drawArc(bounds, static_cast<int>(startDeg * 16), static_cast<int>(spanDeg * 16));
        break;
    }
    case lcad::EntityType::Polyline: {
        const auto& pl = static_cast<const lcad::PolylineEntity&>(entity);
        const auto& verts = pl.vertices();
        for (std::size_t i = 0; i + 1 < verts.size(); ++i) {
            painter.drawLine(worldToScreen(verts[i]), worldToScreen(verts[i + 1]));
        }
        if (pl.closed() && verts.size() > 1) painter.drawLine(worldToScreen(verts.back()), worldToScreen(verts.front()));
        break;
    }
    case lcad::EntityType::Ellipse: {
        const auto& ellipse = static_cast<const lcad::EllipseEntity&>(entity);
        const QPointF c = worldToScreen(ellipse.center());
        painter.drawEllipse(c, ellipse.radiusX() * m_scale, ellipse.radiusY() * m_scale);
        break;
    }
    case lcad::EntityType::Text: {
        const auto& text = static_cast<const lcad::TextEntity&>(entity);
        QFont font = painter.font();
        font.setPixelSize(std::max(1, static_cast<int>(std::round(text.height() * m_scale))));
        painter.save();
        painter.setFont(font);
        painter.translate(worldToScreen(text.position()));
        // painter.rotate() is clockwise in raw (Y-down) screen space, which is
        // visually clockwise too since we draw directly in that space with no
        // further flip -- our world angle convention is CCW-positive (visually),
        // so it needs the opposite sign here, same reasoning as the ARC case above.
        painter.rotate(-qRadiansToDegrees(text.rotation()));
        painter.drawText(QPointF(0, 0), QString::fromStdString(text.text()));
        painter.restore();
        break;
    }
    }
}

void DrawingView::drawGrips(QPainter& painter) {
    constexpr double kHalf = 4.0;
    painter.setPen(QPen(QColor(0, 0, 0, 150), 1));
    painter.setBrush(kGripColor);
    for (lcad::EntityId id : m_selection) {
        if (m_dragMode == DragMode::Grip && id == m_gripEntityId) continue; // shown via drag preview instead
        lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        for (const lcad::Point2D& gp : e->gripPoints()) {
            const QPointF s = worldToScreen(gp);
            painter.drawRect(QRectF(s.x() - kHalf, s.y() - kHalf, kHalf * 2, kHalf * 2));
        }
    }
    painter.setBrush(Qt::NoBrush);
}

void DrawingView::drawDragPreview(QPainter& painter) {
    if (m_dragMode == DragMode::MoveSelection) {
        const lcad::Point2D delta = m_dragCurrentWorld - m_dragStartWorld;
        for (lcad::EntityId id : m_selection) {
            lcad::Entity* e = m_document.findEntity(id);
            if (!e) continue;
            std::unique_ptr<lcad::Entity> clone = e->clone();
            clone->translate(delta);
            drawEntity(painter, *clone, kDragPreviewColor, 2.0);
        }
    } else if (m_dragMode == DragMode::Grip) {
        if (lcad::Entity* e = m_document.findEntity(m_gripEntityId)) {
            std::unique_ptr<lcad::Entity> clone = e->clone();
            clone->moveGripPoint(m_gripIndex, m_dragCurrentWorld);
            drawEntity(painter, *clone, kDragPreviewColor, 2.0);
        }
    }
}

void DrawingView::drawPreview(QPainter& painter) {
    if (!m_dispatcher) return;
    DrawCommand* cmd = m_dispatcher->activeDrawCommand();
    if (!cmd) return;
    painter.setPen(QPen(QColor(0, 200, 255), 1, Qt::DashLine));
    for (const auto& seg : cmd->previewSegments()) {
        painter.drawLine(worldToScreen(seg.first), worldToScreen(seg.second));
    }
}

void DrawingView::drawSelectionBox(QPainter& painter) {
    if (m_dragMode != DragMode::BoxSelect) return;
    const bool crossing = m_dragCurrentScreen.x() < m_dragStartScreen.x();
    painter.setPen(QPen(crossing ? QColor(0, 200, 0) : QColor(0, 120, 255), 1, Qt::DashLine));
    painter.setBrush(crossing ? QColor(0, 200, 0, 30) : QColor(0, 120, 255, 30));
    painter.drawRect(QRectF(m_dragStartScreen, m_dragCurrentScreen).normalized());
}

void DrawingView::drawSnapMarker(QPainter& painter) {
    if (!m_currentSnap) return;
    const QPointF s = worldToScreen(m_currentSnap->point);
    constexpr double kHalf = 6.0;
    painter.setPen(QPen(QColor(0, 255, 120), 2));
    painter.setBrush(Qt::NoBrush);
    switch (m_currentSnap->kind) {
    case lcad::SnapKind::Endpoint:
        painter.drawRect(QRectF(s.x() - kHalf, s.y() - kHalf, kHalf * 2, kHalf * 2));
        break;
    case lcad::SnapKind::Midpoint: {
        QPolygonF tri;
        tri << QPointF(s.x(), s.y() - kHalf) << QPointF(s.x() - kHalf, s.y() + kHalf)
            << QPointF(s.x() + kHalf, s.y() + kHalf);
        painter.drawPolygon(tri);
        break;
    }
    case lcad::SnapKind::Center:
        painter.drawEllipse(s, kHalf, kHalf);
        break;
    case lcad::SnapKind::Quadrant: {
        QPolygonF diamond;
        diamond << QPointF(s.x(), s.y() - kHalf) << QPointF(s.x() + kHalf, s.y()) << QPointF(s.x(), s.y() + kHalf)
                << QPointF(s.x() - kHalf, s.y());
        painter.drawPolygon(diamond);
        break;
    }
    }
}

void DrawingView::updateSelectionFromBox(const QRectF& screenBox, bool crossing) {
    lcad::BoundingBox worldBox;
    worldBox.expand(screenToWorld(screenBox.topLeft()));
    worldBox.expand(screenToWorld(screenBox.bottomRight()));
    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue; // locked layers can't be selected, same as click-select
        const lcad::BoundingBox eb = e->boundingBox();
        const bool hit = crossing ? worldBox.intersects(eb) : worldBox.containsBox(eb);
        if (hit) m_selection.insert(e->id());
    }
}

void DrawingView::eraseSelection() {
    for (lcad::EntityId id : m_selection) {
        m_document.commandStack().execute(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
    }
    m_selection.clear();
    emit selectionChanged();
}

void DrawingView::resetViewState() {
    m_selection.clear();
    m_hoverEntityId.reset();
    m_dragMode = DragMode::None;
    zoomExtents();
}

void DrawingView::mousePressEvent(QMouseEvent* event) {
    const lcad::Point2D worldPt = screenToWorld(event->position());

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_dispatcher && m_dispatcher->hasActiveCommand()) {
            m_dispatcher->handlePointPicked(resolvePoint(event->position()));
            update();
            return;
        }

        if (auto grip = hitTestGrip(event->position())) {
            m_dragMode = DragMode::Grip;
            m_gripEntityId = grip->first;
            m_gripIndex = grip->second;
            if (lcad::Entity* e = m_document.findEntity(m_gripEntityId)) {
                const auto grips = e->gripPoints();
                if (m_gripIndex < grips.size()) m_gripOldPos = grips[m_gripIndex];
            }
            m_dragStartScreen = event->position();
            m_dragCurrentScreen = event->position();
            m_dragStartWorld = worldPt;
            m_dragCurrentWorld = worldPt;
            return;
        }

        lcad::Entity* hit = hitTestEntity(worldPt);
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        if (hit) {
            const bool alreadySelected = m_selection.count(hit->id()) > 0;
            if (shift && alreadySelected) {
                m_selection.erase(hit->id());
                emit selectionChanged();
                update();
                return;
            }
            if (!alreadySelected) {
                if (!shift) m_selection.clear();
                m_selection.insert(hit->id());
                emit selectionChanged();
            }
            m_dragMode = DragMode::MoveSelection;
            m_dragStartScreen = event->position();
            m_dragCurrentScreen = event->position();
            m_dragStartWorld = worldPt;
            m_dragCurrentWorld = worldPt;
            update();
            return;
        }

        m_dragMode = DragMode::BoxSelect;
        m_dragStartScreen = event->position();
        m_dragCurrentScreen = event->position();
        return;
    }

    if (event->button() == Qt::RightButton) {
        if (m_dispatcher && m_dispatcher->hasActiveCommand()) {
            m_dispatcher->handleFinishRequested();
            update();
        }
        return;
    }
}

void DrawingView::mouseMoveEvent(QMouseEvent* event) {
    const lcad::Point2D worldPt = screenToWorld(event->position());
    m_lastMouseWorld = worldPt;
    emit mouseWorldMoved(worldPt);

    if (m_panning) {
        const QPointF delta = event->position() - m_lastPanPos;
        m_viewCenter.x -= delta.x() / m_scale;
        m_viewCenter.y += delta.y() / m_scale;
        m_lastPanPos = event->position();
        update();
        return;
    }

    if (m_dragMode == DragMode::BoxSelect) {
        m_dragCurrentScreen = event->position();
        update();
        return;
    }
    if (m_dragMode == DragMode::MoveSelection || m_dragMode == DragMode::Grip) {
        m_dragCurrentScreen = event->position();
        m_dragCurrentWorld = worldPt;
        update();
        return;
    }

    if (m_dispatcher && m_dispatcher->hasActiveCommand()) {
        const lcad::Point2D resolved = resolvePoint(event->position());
        m_lastMouseWorld = resolved;
        emit mouseWorldMoved(resolved);
        m_dispatcher->handleMouseMoved(resolved);
        update();
        return;
    }

    m_currentSnap.reset(); // no active command: no snap marker while idle/hovering
    lcad::Entity* hit = hitTestEntity(worldPt);
    m_hoverEntityId = hit ? std::optional<lcad::EntityId>(hit->id()) : std::nullopt;
    update();
}

void DrawingView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    if (m_dragMode == DragMode::BoxSelect) {
        m_dragMode = DragMode::None;
        const QPointF delta = m_dragCurrentScreen - m_dragStartScreen;
        const bool addToSelection = event->modifiers() & Qt::ShiftModifier;

        if (std::abs(delta.x()) < 3 && std::abs(delta.y()) < 3) {
            if (!addToSelection) m_selection.clear();
        } else {
            const bool crossing = m_dragCurrentScreen.x() < m_dragStartScreen.x();
            if (!addToSelection) m_selection.clear();
            updateSelectionFromBox(QRectF(m_dragStartScreen, m_dragCurrentScreen).normalized(), crossing);
        }
        emit selectionChanged();
        update();
        return;
    }

    if (m_dragMode == DragMode::MoveSelection) {
        m_dragMode = DragMode::None;
        const QPointF screenDelta = m_dragCurrentScreen - m_dragStartScreen;
        const double screenDist = std::sqrt(screenDelta.x() * screenDelta.x() + screenDelta.y() * screenDelta.y());
        if (screenDist >= 3.0) {
            const lcad::Point2D delta = m_dragCurrentWorld - m_dragStartWorld;
            std::vector<lcad::EntityId> ids(m_selection.begin(), m_selection.end());
            m_document.commandStack().execute(
                std::make_unique<lcad::TranslateEntitiesCommand>(m_document, std::move(ids), delta));
        }
        update();
        return;
    }

    if (m_dragMode == DragMode::Grip) {
        m_dragMode = DragMode::None;
        m_document.commandStack().execute(std::make_unique<lcad::MoveGripCommand>(
            m_document, m_gripEntityId, m_gripIndex, m_gripOldPos, m_dragCurrentWorld));
        update();
        return;
    }
}

void DrawingView::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const lcad::Point2D before = screenToWorld(event->position());
    m_scale = std::clamp(m_scale * factor, 0.01, 100000.0);
    const lcad::Point2D after = screenToWorld(event->position());
    m_viewCenter.x += (before.x - after.x);
    m_viewCenter.y += (before.y - after.y);
    update();
}

void DrawingView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        eraseSelection();
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        m_dragMode = DragMode::None; // cancel any in-progress drag without committing
        if (m_dispatcher) m_dispatcher->handleEscape();
        m_selection.clear();
        emit selectionChanged();
        update();
        return;
    }
    // F3/F8/F9 (object snap/ortho/grid snap) are handled as window-level QAction
    // shortcuts in MainWindow instead of here, so they still work when the
    // command-line input has keyboard focus rather than the canvas.
    QOpenGLWidget::keyPressEvent(event);
}
