#include "commands/HatchBoundary.h"

#include "core/geometry/Arc.h"
#include "core/geometry/BoundaryTrace.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <cmath>

namespace {

using Seg = std::pair<lcad::Point2D, lcad::Point2D>;

void addChain(std::vector<Seg>& segs, const std::vector<lcad::Point2D>& pts, bool closed) {
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) segs.emplace_back(pts[i], pts[i + 1]);
    if (closed && pts.size() > 2) segs.emplace_back(pts.back(), pts.front());
}

void tessellateArc(std::vector<Seg>& segs, const lcad::ArcEntity& arc) {
    double sweep = arc.endAngle() - arc.startAngle();
    while (sweep <= 0.0) sweep += 2.0 * M_PI;
    const int n = std::max(8, static_cast<int>(std::ceil(sweep / (2.0 * M_PI) * 64.0)));
    lcad::Point2D prev = arc.startPoint();
    for (int i = 1; i <= n; ++i) {
        const double t = arc.startAngle() + sweep * i / n;
        const lcad::Point2D p(arc.center().x + arc.radius() * std::cos(t), arc.center().y + arc.radius() * std::sin(t));
        segs.emplace_back(prev, p);
        prev = p;
    }
}

void tessellateCircle(std::vector<Seg>& segs, const lcad::CircleEntity& circle) {
    constexpr int n = 64;
    lcad::Point2D prev(circle.center().x + circle.radius(), circle.center().y);
    for (int i = 1; i <= n; ++i) {
        const double t = 2.0 * M_PI * i / n;
        const lcad::Point2D p(circle.center().x + circle.radius() * std::cos(t),
                              circle.center().y + circle.radius() * std::sin(t));
        segs.emplace_back(prev, p);
        prev = p;
    }
}

} // namespace

std::optional<std::vector<lcad::Point2D>> pickHatchBoundary(lcad::Document& document, const lcad::Point2D& pt) {
    std::vector<Seg> segs;
    for (const lcad::Entity* e : document.entities()) {
        const lcad::Layer* layer = document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        switch (e->type()) {
        case lcad::EntityType::Line: {
            const auto& line = static_cast<const lcad::LineEntity&>(*e);
            segs.emplace_back(line.start(), line.end());
            break;
        }
        case lcad::EntityType::Polyline: {
            const auto& pl = static_cast<const lcad::PolylineEntity&>(*e);
            addChain(segs, pl.flattenedVertices(), pl.closed());
            break;
        }
        case lcad::EntityType::Arc:
            tessellateArc(segs, static_cast<const lcad::ArcEntity&>(*e));
            break;
        case lcad::EntityType::Circle:
            tessellateCircle(segs, static_cast<const lcad::CircleEntity&>(*e));
            break;
        default:
            break;
        }
    }
    return lcad::traceBoundary(segs, pt);
}
