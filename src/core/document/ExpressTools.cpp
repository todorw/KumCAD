#include "core/document/ExpressTools.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lcad {

namespace {

// Shared "reading order" comparison: top-to-bottom (y descending), then
// left-to-right (x ascending), with a small tolerance so texts on the same
// visual line don't reorder by sub-millimeter y jitter.
bool readingOrderLess(const Point2D& a, const Point2D& b) {
    constexpr double kLineTol = 1e-6;
    if (std::abs(a.y - b.y) > kLineTol) return a.y > b.y;
    return a.x < b.x;
}

} // namespace

std::optional<CombinedText> combineTextEntities(const std::vector<const TextEntity*>& texts) {
    std::vector<const TextEntity*> sorted;
    for (const TextEntity* t : texts) {
        if (t) sorted.push_back(t);
    }
    if (sorted.empty()) return std::nullopt;
    std::stable_sort(sorted.begin(), sorted.end(), [](const TextEntity* a, const TextEntity* b) {
        return readingOrderLess(a->position(), b->position());
    });

    CombinedText result;
    result.position = sorted.front()->position();
    result.height = sorted.front()->height();
    result.rotation = sorted.front()->rotation();
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) result.text += '\n';
        result.text += sorted[i]->text();
    }
    return result;
}

std::vector<std::string> applyTcount(const std::vector<TCountItem>& items, int start, int increment,
                                     TCountPlacement placement) {
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&items](std::size_t a, std::size_t b) {
        return readingOrderLess(items[a].position, items[b].position);
    });

    std::vector<std::string> result(items.size());
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const std::size_t index = order[rank];
        const std::string number = std::to_string(start + static_cast<int>(rank) * increment);
        switch (placement) {
        case TCountPlacement::Prefix: result[index] = number + " " + items[index].text; break;
        case TCountPlacement::Suffix: result[index] = items[index].text + " " + number; break;
        case TCountPlacement::Replace: result[index] = number; break;
        }
    }
    return result;
}

double torientRotation(double rotationRadians) {
    constexpr double kPi = 3.14159265358979323846;
    double r = std::fmod(rotationRadians, 2.0 * kPi);
    if (r < 0) r += 2.0 * kPi; // [0, 2pi)
    // Right-reading means the baseline points into the right half-plane:
    // rotation in (-pi/2, pi/2]. Anything pointing left/down flips by pi.
    if (r > kPi / 2.0 && r <= 3.0 * kPi / 2.0) r -= kPi;
    else if (r > 3.0 * kPi / 2.0) r -= 2.0 * kPi;
    return r;
}

std::vector<Point2D> breaklinePoints(const Point2D& a, const Point2D& b, double size) {
    const Point2D d = b - a;
    const double len = d.length();
    if (len < 1e-9 || size <= 0.0 || len < 2.0 * size) return {};
    const Point2D dir = d * (1.0 / len);
    const Point2D perp(-dir.y, dir.x);
    const Point2D mid = a + d * 0.5;

    // Standard breakline symbol: straight run, zigzag through the midpoint,
    // straight run. The zigzag spans size along the line and +/-size across.
    std::vector<Point2D> points;
    points.reserve(6);
    points.push_back(a);
    points.push_back(mid - dir * size);
    points.push_back(mid - dir * (size * 0.25) + perp * size);
    points.push_back(mid + dir * (size * 0.25) - perp * size);
    points.push_back(mid + dir * size);
    points.push_back(b);
    return points;
}

std::vector<EntityId> entityIdsOnLayer(const Document& doc, LayerId layer) {
    std::vector<EntityId> ids;
    for (const Entity* e : doc.entities()) {
        if (e->layer() == layer) ids.push_back(e->id());
    }
    for (int i = 0; i < static_cast<int>(doc.layouts().size()); ++i) {
        for (const Entity* e : doc.paperEntities(i)) {
            if (e->layer() == layer) ids.push_back(e->id());
        }
    }
    return ids;
}

const Layer* findLayerByName(const Document& doc, const std::string& name) {
    for (const Layer& layer : doc.layers()) {
        if (layer.name == name) return &layer;
    }
    return nullptr;
}

} // namespace lcad
