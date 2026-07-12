#include "core/geometry/Insert.h"

#include "core/geometry/AttDef.h"
#include "core/geometry/ModifyOps.h"
#include "core/geometry/Text.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lcad {

void InsertEntity::setAttribute(const std::string& tag, const std::string& value) {
    for (auto& [existingTag, existingValue] : m_attributes) {
        if (existingTag == tag) {
            existingValue = value;
            return;
        }
    }
    m_attributes.emplace_back(tag, value);
}

const std::string* InsertEntity::attributeValue(const std::string& tag) const {
    for (const auto& [existingTag, value] : m_attributes) {
        if (existingTag == tag) return &value;
    }
    return nullptr;
}

std::vector<std::unique_ptr<Entity>> InsertEntity::instantiate() const {
    std::vector<std::unique_ptr<Entity>> result;
    if (!m_block) return result;
    result.reserve(m_block->entities.size());

    // Child ids assigned to at least one visibility state; ids outside this
    // set are always shown, matching AutoCAD's "unassigned == visible in
    // every state" default.
    std::unordered_set<EntityId> stateControlled;
    if (m_block->dynamicVisibility) {
        for (const auto& [state, ids] : m_block->dynamicVisibility->visibleIds) {
            (void)state;
            for (EntityId id : ids) stateControlled.insert(id);
        }
    }

    for (const auto& child : m_block->entities) {
        if (m_block->dynamicVisibility && stateControlled.count(child->id())) {
            // An instance that has never had its state set explicitly
            // starts on the block's first state, not "no state" (which
            // would hide every state-controlled child).
            const std::string& state = (m_visibilityState.empty() && !m_block->dynamicVisibility->states.empty())
                                            ? m_block->dynamicVisibility->states.front()
                                            : m_visibilityState;
            const auto it = m_block->dynamicVisibility->visibleIds.find(state);
            const bool visible =
                it != m_block->dynamicVisibility->visibleIds.end() &&
                std::find(it->second.begin(), it->second.end(), child->id()) != it->second.end();
            if (!visible) continue;
        }

        std::unique_ptr<Entity> copy;
        if (child->type() == EntityType::AttDef) {
            // Attribute definitions become the insert's value (or the
            // default) as plain text, like a resolved ATTRIB.
            const auto& attdef = static_cast<const AttDefEntity&>(*child);
            const std::string* value = attributeValue(attdef.tag());
            const std::string& shown = value ? *value : attdef.defaultValue();
            if (shown.empty()) continue;
            copy = std::make_unique<TextEntity>(child->id(), child->layer(), attdef.position(), shown,
                                                attdef.height(), attdef.rotation());
        } else {
            copy = child->clone();
        }
        if (m_block->dynamicParam && std::abs(m_dynamicStretch) > 1e-9) {
            const auto& dp = *m_block->dynamicParam;
            const Point2D axis = dp.endPoint - dp.basePoint;
            const double len = axis.length();
            if (len > 1e-9) {
                BoundingBox frame;
                frame.expand(dp.frameMin);
                frame.expand(dp.frameMax);
                copy = stretchedClone(*copy, frame, axis * (m_dynamicStretch / len));
            }
        }
        if (m_block->dynamicFlip && m_dynamicFlipped) {
            copy->mirror(m_block->dynamicFlip->basePoint, m_block->dynamicFlip->endPoint);
        }
        if (m_block->dynamicRotation && std::abs(m_dynamicRotationAngle) > 1e-9) {
            copy->rotate(m_block->dynamicRotation->basePoint, m_dynamicRotationAngle);
        }
        if (m_block->dynamicLookup) {
            for (const auto& [label, factor] : m_block->dynamicLookup->presets) {
                if (label == m_lookupValue) {
                    copy->scale(Point2D(0, 0), factor);
                    break;
                }
            }
        }

        const bool arrayed = m_block->dynamicArray && m_dynamicArrayCount > 1;
        const int copies = arrayed ? m_dynamicArrayCount : 1;
        for (int i = 0; i < copies; ++i) {
            std::unique_ptr<Entity> inst = copy->clone();
            if (i > 0) inst->translate(m_block->dynamicArray->direction * (m_block->dynamicArray->spacing * i));
            inst->scale(Point2D(0, 0), m_scale);
            inst->rotate(Point2D(0, 0), m_rotation);
            inst->translate(m_position);
            result.push_back(std::move(inst));
        }
    }
    return result;
}

BoundingBox InsertEntity::boundingBox() const {
    BoundingBox box;
    for (const auto& e : instantiate()) box.expand(e->boundingBox());
    if (!box.isValid()) box.expand(m_position); // empty block: at least the insertion point
    return box;
}

double InsertEntity::distanceTo(const Point2D& pt) const {
    double best = pt.distanceTo(m_position);
    for (const auto& e : instantiate()) best = std::min(best, e->distanceTo(pt));
    return best;
}

void InsertEntity::translate(const Point2D& delta) {
    m_position = m_position + delta;
}

void InsertEntity::rotate(const Point2D& center, double angleRadians) {
    m_position = rotateAround(m_position, center, angleRadians);
    m_rotation += angleRadians;
}

void InsertEntity::scale(const Point2D& center, double factor) {
    m_position = scaleAround(m_position, center, factor);
    m_scale *= factor;
}

void InsertEntity::mirror(const Point2D& a, const Point2D& b) {
    m_position = mirrorAcross(m_position, a, b);
    const double phi = std::atan2(b.y - a.y, b.x - a.x);
    m_rotation = 2.0 * phi - m_rotation;
}

namespace {
Point2D insertGripToWorld(const Point2D& local, double scale, double rotation, const Point2D& position) {
    Point2D p = scaleAround(local, Point2D(0, 0), scale);
    p = rotateAround(p, Point2D(0, 0), rotation);
    return p + position;
}
} // namespace

std::vector<Point2D> InsertEntity::gripPoints() const {
    std::vector<Point2D> pts{m_position};
    if (!m_block) return pts;
    if (m_block->dynamicParam) {
        const auto& dp = *m_block->dynamicParam;
        const Point2D axis = dp.endPoint - dp.basePoint;
        const double len = axis.length();
        const Point2D local = len > 1e-9 ? dp.endPoint + axis * (m_dynamicStretch / len) : dp.endPoint;
        pts.push_back(insertGripToWorld(local, m_scale, m_rotation, m_position));
    }
    if (m_block->dynamicFlip) {
        // The grip sits off the flip line's midpoint, on whichever side
        // reflects the current flipped state, so dragging it across the
        // line toggles (see moveGripPoint's matching cross-product test).
        const auto& fp = *m_block->dynamicFlip;
        const Point2D mid = (fp.basePoint + fp.endPoint) * 0.5;
        const Point2D axis = fp.endPoint - fp.basePoint;
        const double len = axis.length();
        Point2D normal = len > 1e-9 ? Point2D(-axis.y, axis.x) * (1.0 / len) : Point2D(0, 1);
        if (m_dynamicFlipped) normal = normal * -1.0;
        pts.push_back(insertGripToWorld(mid + normal * std::max(1.0, len * 0.15), m_scale, m_rotation, m_position));
    }
    if (m_block->dynamicRotation) {
        const auto& rp = *m_block->dynamicRotation;
        const Point2D local =
            rp.basePoint + Point2D(std::cos(m_dynamicRotationAngle), std::sin(m_dynamicRotationAngle)) * rp.defaultRadius;
        pts.push_back(insertGripToWorld(local, m_scale, m_rotation, m_position));
    }
    if (m_block->dynamicArray) {
        const auto& ap = *m_block->dynamicArray;
        const Point2D local = ap.basePoint + ap.direction * (ap.spacing * m_dynamicArrayCount);
        pts.push_back(insertGripToWorld(local, m_scale, m_rotation, m_position));
    }
    return pts;
}

void InsertEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_position = newPos;
        return;
    }
    if (!m_block) return;

    Point2D local = newPos - m_position;
    local = rotateAround(local, Point2D(0, 0), -m_rotation);
    if (std::abs(m_scale) > 1e-9) local = local * (1.0 / m_scale);

    std::size_t i = 1;
    if (m_block->dynamicParam) {
        if (index == i) {
            const auto& dp = *m_block->dynamicParam;
            const Point2D axis = dp.endPoint - dp.basePoint;
            const double len = axis.length();
            if (len > 1e-9) m_dynamicStretch = (local - dp.endPoint).dot(axis * (1.0 / len));
            return;
        }
        ++i;
    }
    if (m_block->dynamicFlip) {
        if (index == i) {
            const auto& fp = *m_block->dynamicFlip;
            const Point2D axis = fp.endPoint - fp.basePoint;
            const double cross = axis.x * (local.y - fp.basePoint.y) - axis.y * (local.x - fp.basePoint.x);
            m_dynamicFlipped = cross < 0;
            return;
        }
        ++i;
    }
    if (m_block->dynamicRotation) {
        if (index == i) {
            const auto& rp = *m_block->dynamicRotation;
            m_dynamicRotationAngle = std::atan2(local.y - rp.basePoint.y, local.x - rp.basePoint.x);
            return;
        }
        ++i;
    }
    if (m_block->dynamicArray) {
        if (index == i) {
            const auto& ap = *m_block->dynamicArray;
            const double len = ap.direction.length();
            if (len > 1e-9 && ap.spacing > 1e-9) {
                const double proj = (local - ap.basePoint).dot(ap.direction * (1.0 / len));
                m_dynamicArrayCount = std::max(ap.minCount, static_cast<int>(std::lround(proj / ap.spacing)));
            }
            return;
        }
        ++i;
    }
}

std::vector<SnapPoint> InsertEntity::snapCandidates() const {
    return {{m_position, SnapKind::Endpoint}};
}

std::unique_ptr<Entity> InsertEntity::clone() const {
    return std::make_unique<InsertEntity>(*this);
}

} // namespace lcad
