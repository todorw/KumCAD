#include "DrawingView.h"

#include "CommandDispatcher.h"
#include "EntityPainter.h"
#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Intersect.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/SnapGeometry.h"
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

std::vector<lcad::Entity*> DrawingView::spaceEntities() const {
    return inLayoutMode() ? m_document.paperEntities(m_layoutIndex) : m_document.entities();
}

lcad::Entity* DrawingView::hitTestEntity(const lcad::Point2D& worldPt) const {
    const double tol = pickToleranceWorld();
    lcad::Entity* best = nullptr;
    double bestDist = tol;
    for (lcad::Entity* e : spaceEntities()) {
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

std::optional<std::pair<lcad::SnapPoint, std::optional<lcad::SnapRef>>> DrawingView::findSnapCandidate(
    const QPointF& screenPos) const {
    constexpr double kSnapPickPx = 10.0;
    std::optional<std::pair<lcad::SnapPoint, std::optional<lcad::SnapRef>>> best;
    double bestDist = kSnapPickPx;

    const auto consider = [&](const lcad::SnapPoint& sp, const std::optional<lcad::SnapRef>& ref) {
        const QPointF s = worldToScreen(sp.point);
        const double dx = s.x() - screenPos.x();
        const double dy = s.y() - screenPos.y();
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= bestDist) {
            bestDist = d;
            best = {sp, ref};
        }
    };

    // Entities close enough to the cursor to matter for the cursor-dependent
    // snap kinds (intersection/perpendicular/tangent/nearest).
    const lcad::Point2D cursor = screenToWorld(screenPos);
    const double tolWorld = kSnapPickPx / m_scale;
    std::vector<lcad::Entity*> nearby;

    for (lcad::Entity* e : spaceEntities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        if (e->distanceTo(cursor) <= tolWorld * 1.5) nearby.push_back(e);
        // Per-kind running index makes the SnapRef durable across edits that
        // move points without restructuring the entity.
        int kindCounts[lcad::kSnapKindCount] = {};
        for (const lcad::SnapPoint& sp : e->snapCandidates()) {
            const int kindIndex = kindCounts[static_cast<int>(sp.kind)]++;
            if (!snapModeEnabled(sp.kind)) continue;
            consider(sp, lcad::SnapRef{e->id(), sp.kind, kindIndex});
        }
    }

    if (snapModeEnabled(lcad::SnapKind::Intersection)) {
        for (std::size_t i = 0; i < nearby.size(); ++i) {
            for (std::size_t j = i + 1; j < nearby.size(); ++j) {
                for (const lcad::Point2D& p : lcad::intersectEntities(*nearby[i], *nearby[j])) {
                    consider({p, lcad::SnapKind::Intersection}, std::nullopt);
                }
            }
        }
    }

    // Perpendicular and tangent are measured from the active command's
    // reference point (e.g. LINE's previous vertex).
    std::optional<lcad::Point2D> anchor;
    if (m_dispatcher && m_dispatcher->hasActiveCommand()) {
        anchor = m_dispatcher->activeDrawCommand()->anchorPoint();
    }
    if (anchor) {
        for (lcad::Entity* e : nearby) {
            if (snapModeEnabled(lcad::SnapKind::Perpendicular)) {
                for (const lcad::Point2D& p : lcad::perpendicularPoints(*e, *anchor)) {
                    consider({p, lcad::SnapKind::Perpendicular}, std::nullopt);
                }
            }
            if (snapModeEnabled(lcad::SnapKind::Tangent)) {
                for (const lcad::Point2D& p : lcad::tangentPoints(*e, *anchor)) {
                    consider({p, lcad::SnapKind::Tangent}, std::nullopt);
                }
            }
        }
    }

    // Nearest is the fallback: only when nothing more specific hit.
    if (!best && snapModeEnabled(lcad::SnapKind::Nearest)) {
        for (lcad::Entity* e : nearby) {
            if (const auto p = lcad::nearestPointOnEntity(*e, cursor)) {
                consider({*p, lcad::SnapKind::Nearest}, std::nullopt);
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
    std::optional<lcad::Point2D> anchor;
    if (m_dispatcher && m_dispatcher->hasActiveCommand()) {
        anchor = m_dispatcher->activeDrawCommand()->anchorPoint();
    }
    return resolvePointWithAnchor(screenPos, anchor);
}

std::optional<lcad::Point2D> DrawingView::snapToPolarRay(const lcad::Point2D& origin, const lcad::Point2D& pt,
                                                         double tolWorld) const {
    const lcad::Point2D d = pt - origin;
    const double dist = d.length();
    if (dist < 1e-9) return std::nullopt;
    const double increment = m_polarIncrementDeg * M_PI / 180.0;
    const double angle = std::atan2(d.y, d.x);
    const double snapped = std::round(angle / increment) * increment;
    const lcad::Point2D dir(std::cos(snapped), std::sin(snapped));
    const double along = d.dot(dir);
    if (along <= 1e-9) return std::nullopt;
    const lcad::Point2D projected = origin + dir * along;
    if (projected.distanceTo(pt) > tolWorld) return std::nullopt;
    return projected;
}

lcad::Point2D DrawingView::resolvePointWithAnchor(const QPointF& screenPos,
                                                  const std::optional<lcad::Point2D>& orthoAnchor) {
    m_currentSnap.reset();
    m_currentSnapRef.reset();
    m_polarGuide.reset();
    m_trackGuide.reset();
    if (m_osnapEnabled) {
        if (auto snap = findSnapCandidate(screenPos)) {
            m_currentSnap = snap->first;
            m_currentSnapRef = snap->second;
            // Hovering an osnap point "acquires" it for object snap tracking.
            if (m_otrackEnabled) m_trackPoint = snap->first.point;
            return snap->first.point;
        }
    }

    lcad::Point2D working = screenToWorld(screenPos);
    const double tolWorld = 10.0 / m_scale;

    if (m_orthoEnabled && orthoAnchor) {
        working = applyOrtho(*orthoAnchor, working);
    } else {
        // Polar tracking off the command's anchor, object snap tracking off
        // the last acquired osnap point. When both rays apply, their
        // intersection wins (the classic "line up with both" case).
        std::optional<lcad::Point2D> polarHit;
        std::optional<lcad::Point2D> trackHit;
        if (m_polarEnabled && orthoAnchor) polarHit = snapToPolarRay(*orthoAnchor, working, tolWorld);
        if (m_otrackEnabled && m_trackPoint && (!orthoAnchor || m_trackPoint->distanceTo(*orthoAnchor) > 1e-9)) {
            trackHit = snapToPolarRay(*m_trackPoint, working, tolWorld);
        }
        if (polarHit && trackHit && orthoAnchor) {
            const lcad::Point2D dirA = *polarHit - *orthoAnchor;
            const lcad::Point2D dirB = *trackHit - *m_trackPoint;
            const double cross = dirA.x * dirB.y - dirA.y * dirB.x;
            if (std::abs(cross) > 1e-9) {
                const lcad::Point2D ab = *m_trackPoint - *orthoAnchor;
                const double t = (ab.x * dirB.y - ab.y * dirB.x) / cross;
                const lcad::Point2D crossing = *orthoAnchor + dirA * t;
                if (crossing.distanceTo(working) <= tolWorld * 2.0) {
                    m_polarGuide = {{*orthoAnchor, crossing}};
                    m_trackGuide = {{*m_trackPoint, crossing}};
                    return crossing;
                }
            }
        }
        if (polarHit) {
            m_polarGuide = {{*orthoAnchor, *polarHit}};
            working = *polarHit;
        } else if (trackHit) {
            m_trackGuide = {{*m_trackPoint, *trackHit}};
            working = *trackHit;
        }
        if (polarHit || trackHit) return working;
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
    if (on) {
        m_polarEnabled = false; // AutoCAD: ortho and polar are exclusive
        m_polarGuide.reset();
    }
    emit modesChanged();
    update();
}

void DrawingView::setGridSnapEnabled(bool on) {
    m_gridSnapEnabled = on;
    emit modesChanged();
    update();
}

void DrawingView::setPolarEnabled(bool on) {
    m_polarEnabled = on;
    if (on) m_orthoEnabled = false; // AutoCAD: polar and ortho are exclusive
    if (!on) m_polarGuide.reset();
    emit modesChanged();
    update();
}

void DrawingView::setOtrackEnabled(bool on) {
    m_otrackEnabled = on;
    if (!on) {
        m_trackPoint.reset();
        m_trackGuide.reset();
    }
    emit modesChanged();
    update();
}

void DrawingView::setActiveLayoutIndex(int index) {
    if (index >= static_cast<int>(m_document.layouts().size())) index = -1;
    m_layoutIndex = index;
    m_selectedViewport = -1;
    m_dragMode = DragMode::None;
    if (index >= 0) {
        // Fit the sheet on screen.
        const lcad::Layout& layout = m_document.layouts()[index];
        m_viewCenter = lcad::Point2D(layout.paperWidth / 2.0, layout.paperHeight / 2.0);
        const double margin = 1.15;
        m_scale = std::min(width() / (layout.paperWidth * margin), height() / (layout.paperHeight * margin));
        if (m_scale <= 0 || !std::isfinite(m_scale)) m_scale = 2.0;
    }
    update();
}

QRectF DrawingView::viewportScreenRect(const lcad::Viewport& vp) const {
    const QPointF tl =
        worldToScreen(lcad::Point2D(vp.paperCenter.x - vp.paperWidth / 2.0, vp.paperCenter.y + vp.paperHeight / 2.0));
    const QPointF br =
        worldToScreen(lcad::Point2D(vp.paperCenter.x + vp.paperWidth / 2.0, vp.paperCenter.y - vp.paperHeight / 2.0));
    return QRectF(tl, br).normalized();
}

int DrawingView::viewportAt(const QPointF& screenPos) const {
    if (m_layoutIndex < 0) return -1;
    const auto& viewports = m_document.layouts()[m_layoutIndex].viewports;
    for (int i = static_cast<int>(viewports.size()) - 1; i >= 0; --i) {
        if (viewportScreenRect(viewports[i]).contains(screenPos)) return i;
    }
    return -1;
}

void DrawingView::drawLayoutMode(QPainter& painter) {
    const lcad::Layout& layout = m_document.layouts()[m_layoutIndex];

    // The sheet, with a drop shadow.
    const QPointF tl = worldToScreen(lcad::Point2D(0, layout.paperHeight));
    const QPointF br = worldToScreen(lcad::Point2D(layout.paperWidth, 0));
    const QRectF paper = QRectF(tl, br).normalized();
    painter.fillRect(paper.translated(5, 5), QColor(0, 0, 0, 110));
    painter.fillRect(paper, Qt::white);
    painter.setPen(QPen(QColor(140, 140, 140), 1));
    painter.drawRect(paper);

    for (std::size_t i = 0; i < layout.viewports.size(); ++i) {
        const lcad::Viewport& vp = layout.viewports[i];
        const QRectF rect = viewportScreenRect(vp);

        painter.save();
        painter.setClipRect(rect.intersected(paper));
        const auto toScreen = [this, &vp](const lcad::Point2D& p) {
            return worldToScreen(vp.paperCenter + (p - vp.modelCenter) * vp.viewScale);
        };
        const double effScale = vp.viewScale * m_scale;
        for (lcad::Entity* e : m_document.entities()) {
            const lcad::Layer* layer = m_document.findLayer(e->layer());
            if (layer && !layer->visible) continue;
            lcad::Color c = layer ? layer->color : lcad::Color{255, 255, 255};
            if (const auto& override = e->colorOverride()) c = *override;
            // Light colors tuned for the dark canvas vanish on paper.
            QColor color(c.r, c.g, c.b);
            if (c.r > 200 && c.g > 200 && c.b > 200) color = Qt::black;
            lcad::LineType linetype = layer ? layer->linetype : lcad::LineType::Continuous;
            if (const auto& lt = e->linetypeOverride()) linetype = *lt;
            EntityPainter::paint(painter, *e, toScreen, effScale, color, 1.0, linetype,
                                 m_document.lineTypeScale(), &m_document);
        }
        painter.restore();

        const bool selected = static_cast<int>(i) == m_selectedViewport;
        painter.setPen(QPen(selected ? QColor(80, 160, 255) : QColor(90, 90, 90), selected ? 2.0 : 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
    }

    // Entities drawn directly on the sheet (title blocks, notes), in paper
    // coordinates, with the same selection/hover treatment as model space.
    for (lcad::Entity* e : m_document.paperEntities(m_layoutIndex)) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        const bool selected = m_selection.count(e->id()) > 0;
        const bool isDragGhostSource = (m_dragMode == DragMode::MoveSelection && selected) ||
                                        (m_dragMode == DragMode::Grip && e->id() == m_gripEntityId);
        if (isDragGhostSource) continue;
        const bool hovered = m_hoverEntityId && *m_hoverEntityId == e->id();
        lcad::Color c = layer ? layer->color : lcad::Color{255, 255, 255};
        if (const auto& override = e->colorOverride()) c = *override;
        QColor color(c.r, c.g, c.b);
        if (c.r > 200 && c.g > 200 && c.b > 200) color = Qt::black; // light colors vanish on paper
        double width = 1.0;
        if (selected) {
            color = kSelectedColor;
            width = 2.0;
        } else if (hovered) {
            color = kHoverColor;
            width = 2.0;
        }
        drawEntity(painter, *e, color, width);
    }
}

void DrawingView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(33, 33, 33));

    if (m_layoutIndex >= 0 && m_layoutIndex < static_cast<int>(m_document.layouts().size())) {
        drawLayoutMode(painter);
        drawDragPreview(painter);
        drawGrips(painter);
        drawPreview(painter); // e.g. MVIEW's rubber-band rectangle
        drawSelectionBox(painter);
        drawTrackingGuides(painter);
        drawSnapMarker(painter);
        if (m_lastMouseWorld) {
            const QPointF c = worldToScreen(*m_lastMouseWorld);
            painter.setPen(QPen(QColor(120, 120, 120), 1));
            painter.drawLine(QPointF(c.x() - 10, c.y()), QPointF(c.x() + 10, c.y()));
            painter.drawLine(QPointF(c.x(), c.y() - 10), QPointF(c.x(), c.y() + 10));
        }
        return;
    }

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
        if (m_lineweightDisplay) {
            double lw = layer ? layer->lineweight : 0.25;
            if (const auto& lwOverride = e->lineweightOverride()) lw = *lwOverride;
            width = std::clamp(lw / 0.25, 1.0, 12.0); // 0.25 mm ~ one pixel
        }
        if (selected) {
            color = kSelectedColor;
            width = 2.0;
        } else if (hovered) {
            color = kHoverColor;
            width = 2.0;
        } else if (const auto& override = e->colorOverride()) {
            color = QColor(override->r, override->g, override->b);
        } else {
            color = layer ? QColor(layer->color.r, layer->color.g, layer->color.b) : QColor(255, 255, 255);
        }
        // Xref geometry renders dimmed so it reads as reference, not native.
        if (!selected && !hovered && e->type() == lcad::EntityType::Insert &&
            static_cast<const lcad::InsertEntity*>(e)->block()->isXref()) {
            color.setAlpha(120);
        }
        drawEntity(painter, *e, color, width);
    }

    drawDragPreview(painter);
    drawGrips(painter);
    drawPreview(painter);
    drawSelectionBox(painter);
    drawTrackingGuides(painter);
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
    lcad::LineType linetype = lcad::LineType::Continuous;
    if (const auto& override = entity.linetypeOverride()) {
        linetype = *override;
    } else if (const lcad::Layer* layer = m_document.findLayer(entity.layer())) {
        linetype = layer->linetype;
    }
    EntityPainter::paint(
        painter, entity, [this](const lcad::Point2D& p) { return worldToScreen(p); }, m_scale, color, penWidth,
        linetype, m_document.lineTypeScale(), &m_document);
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

void DrawingView::drawTrackingGuides(QPainter& painter) {
    if (!m_polarGuide && !m_trackGuide) return;
    painter.setPen(QPen(QColor(0, 255, 120, 140), 1, Qt::DashLine));
    const auto drawGuide = [&](const std::pair<lcad::Point2D, lcad::Point2D>& guide) {
        const QPointF a = worldToScreen(guide.first);
        const QPointF b = worldToScreen(guide.second);
        // Extend past the snapped point so the ray reads as infinite.
        const QPointF d = b - a;
        const double len = std::sqrt(d.x() * d.x() + d.y() * d.y());
        const QPointF ext = len > 1e-9 ? b + d * (60.0 / len) : b;
        painter.drawLine(a, ext);
        painter.drawEllipse(a, 3.0, 3.0); // small ring at the tracked origin
    };
    if (m_polarGuide) drawGuide(*m_polarGuide);
    if (m_trackGuide) drawGuide(*m_trackGuide);
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
    case lcad::SnapKind::Node:
        painter.drawEllipse(s, kHalf * 0.7, kHalf * 0.7);
        painter.drawLine(QPointF(s.x() - kHalf, s.y() - kHalf), QPointF(s.x() + kHalf, s.y() + kHalf));
        painter.drawLine(QPointF(s.x() - kHalf, s.y() + kHalf), QPointF(s.x() + kHalf, s.y() - kHalf));
        break;
    case lcad::SnapKind::Intersection:
        painter.drawLine(QPointF(s.x() - kHalf, s.y() - kHalf), QPointF(s.x() + kHalf, s.y() + kHalf));
        painter.drawLine(QPointF(s.x() - kHalf, s.y() + kHalf), QPointF(s.x() + kHalf, s.y() - kHalf));
        break;
    case lcad::SnapKind::Perpendicular:
        // Right-angle glyph.
        painter.drawLine(QPointF(s.x() - kHalf, s.y() - kHalf), QPointF(s.x() - kHalf, s.y() + kHalf));
        painter.drawLine(QPointF(s.x() - kHalf, s.y() + kHalf), QPointF(s.x() + kHalf, s.y() + kHalf));
        painter.drawLine(QPointF(s.x() - kHalf, s.y()), QPointF(s.x(), s.y()));
        painter.drawLine(QPointF(s.x(), s.y()), QPointF(s.x(), s.y() + kHalf));
        break;
    case lcad::SnapKind::Tangent:
        painter.drawEllipse(s, kHalf * 0.8, kHalf * 0.8);
        painter.drawLine(QPointF(s.x() - kHalf, s.y() - kHalf * 0.8), QPointF(s.x() + kHalf, s.y() - kHalf * 0.8));
        break;
    case lcad::SnapKind::Nearest: {
        // Bowtie.
        QPolygonF bow;
        bow << QPointF(s.x() - kHalf, s.y() - kHalf) << QPointF(s.x() + kHalf, s.y() + kHalf)
            << QPointF(s.x() + kHalf, s.y() - kHalf) << QPointF(s.x() - kHalf, s.y() + kHalf);
        painter.drawPolygon(bow);
        break;
    }
    }
}

void DrawingView::updateSelectionFromBox(const QRectF& screenBox, bool crossing) {
    lcad::BoundingBox worldBox;
    worldBox.expand(screenToWorld(screenBox.topLeft()));
    worldBox.expand(screenToWorld(screenBox.bottomRight()));
    for (lcad::Entity* e : spaceEntities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue; // locked layers can't be selected, same as click-select
        const lcad::BoundingBox eb = e->boundingBox();
        const bool hit = crossing ? worldBox.intersects(eb) : worldBox.containsBox(eb);
        if (hit) m_selection.insert(e->id());
    }
}

void DrawingView::eraseSelection() {
    if (m_selection.empty()) return;
    auto batch = std::make_unique<lcad::BatchCommand>("Erase");
    for (lcad::EntityId id : m_selection) {
        batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
    }
    m_document.commandStack().execute(std::move(batch));
    m_selection.clear();
    emit selectionChanged();
    emit documentEdited();
}

void DrawingView::selectAll() {
    m_selection.clear();
    for (lcad::Entity* e : spaceEntities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        m_selection.insert(e->id());
    }
    emit selectionChanged();
    update();
}

void DrawingView::pruneSelectionForLayerState() {
    bool changed = false;
    for (auto it = m_selection.begin(); it != m_selection.end();) {
        const lcad::Entity* e = m_document.findEntity(*it);
        const lcad::Layer* layer = e ? m_document.findLayer(e->layer()) : nullptr;
        if (!e || (layer && (!layer->visible || layer->locked))) {
            it = m_selection.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        emit selectionChanged();
        update();
    }
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
            const lcad::Point2D picked = resolvePoint(event->position());
            m_dispatcher->handlePointPicked(picked, m_currentSnapRef);
            update();
            return;
        }

        if (inLayoutMode()) {
            // Layout mode: paper entities (grips, then bodies) take priority;
            // a click on empty sheet falls through to viewport select/drag,
            // then box select.
            if (!hitTestGrip(event->position()) && !hitTestEntity(worldPt)) {
                const int hitViewport = viewportAt(event->position());
                m_selectedViewport = hitViewport;
                if (hitViewport >= 0) {
                    const lcad::Viewport& vp = m_document.layouts()[m_layoutIndex].viewports[hitViewport];
                    m_dragMode = DragMode::MoveViewport;
                    m_dragStartScreen = event->position();
                    m_dragCurrentScreen = event->position();
                    m_viewportDragOffset = vp.paperCenter - worldPt;
                    update();
                    return;
                }
            } else {
                m_selectedViewport = -1;
            }
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
                // Selecting a group member selects the whole group.
                if (const auto* members = m_document.groupOf(hit->id())) {
                    for (lcad::EntityId member : *members) {
                        if (m_document.findEntity(member)) m_selection.insert(member);
                    }
                }
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
    if (m_dragMode == DragMode::MoveViewport && inLayoutMode() && m_selectedViewport >= 0) {
        auto& viewports = m_document.layouts()[m_layoutIndex].viewports;
        if (m_selectedViewport < static_cast<int>(viewports.size())) {
            viewports[m_selectedViewport].paperCenter = worldPt + m_viewportDragOffset;
        }
        m_dragCurrentScreen = event->position();
        update();
        return;
    }
    if (m_dragMode == DragMode::MoveSelection || m_dragMode == DragMode::Grip) {
        m_dragCurrentScreen = event->position();
        // Drags honor the drafting aids too: osnap onto other geometry, ortho
        // relative to where the drag started (the grip's old position for grip
        // edits), and grid snap.
        const lcad::Point2D anchor = m_dragMode == DragMode::Grip ? m_gripOldPos : m_dragStartWorld;
        m_dragCurrentWorld = resolvePointWithAnchor(event->position(), anchor);
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

    if (m_dragMode == DragMode::MoveViewport) {
        m_dragMode = DragMode::None;
        const QPointF screenDelta = m_dragCurrentScreen - m_dragStartScreen;
        if (std::abs(screenDelta.x()) >= 3 || std::abs(screenDelta.y()) >= 3) emit documentEdited();
        update();
        return;
    }

    if (m_dragMode == DragMode::MoveSelection) {
        m_dragMode = DragMode::None;
        m_currentSnap.reset();
        const QPointF screenDelta = m_dragCurrentScreen - m_dragStartScreen;
        const double screenDist = std::sqrt(screenDelta.x() * screenDelta.x() + screenDelta.y() * screenDelta.y());
        if (screenDist >= 3.0) {
            const lcad::Point2D delta = m_dragCurrentWorld - m_dragStartWorld;
            std::vector<lcad::EntityId> ids(m_selection.begin(), m_selection.end());
            m_document.commandStack().execute(
                std::make_unique<lcad::TranslateEntitiesCommand>(m_document, std::move(ids), delta));
            emit documentEdited();
        }
        update();
        return;
    }

    if (m_dragMode == DragMode::Grip) {
        m_dragMode = DragMode::None;
        m_currentSnap.reset();
        // Same 3px threshold as a selection drag: a plain click on a grip must
        // not nudge the geometry to the (up to pick-tolerance off) click point.
        const QPointF screenDelta = m_dragCurrentScreen - m_dragStartScreen;
        const double screenDist = std::sqrt(screenDelta.x() * screenDelta.x() + screenDelta.y() * screenDelta.y());
        if (screenDist >= 3.0) {
            m_document.commandStack().execute(std::make_unique<lcad::MoveGripCommand>(
                m_document, m_gripEntityId, m_gripIndex, m_gripOldPos, m_dragCurrentWorld));
            emit documentEdited();
        }
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
        if (inLayoutMode()) {
            auto& viewports = m_document.layouts()[m_layoutIndex].viewports;
            if (m_selectedViewport >= 0 && m_selectedViewport < static_cast<int>(viewports.size())) {
                viewports.erase(viewports.begin() + m_selectedViewport);
                m_selectedViewport = -1;
                emit documentEdited();
            }
            update();
            return;
        }
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
