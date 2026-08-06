#include "core/document/DocumentConstraints.h"

#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/PointEnt.h"
#include "core/sketch/ConstraintSolver.h"

#include <map>
#include <optional>

namespace lcad {

namespace {

// Which Sketch construct (if any) an entity maps to. kind tells
// getPoint()/writeBack() which case applies; index is that construct's
// own Sketch index (a line/circle index for Line/Circle, unused for
// Point since a bare point has no line/circle of its own).
enum class EntityKind { Unsupported, Line, Circle, Point, Arc };

struct EntityMapping {
    EntityKind kind = EntityKind::Unsupported;
    int sketchIndex = -1;    // Sketch line/circle/arc index (Line/Circle/Arc only)
    int pointIndex[3] = {-1, -1, -1}; // Sketch point index per DocumentPointRef::pointIndex
                                      // (0/1 for Line; 0 for Circle; 0=start/1=end/2=center for Arc)
};

class Builder {
public:
    Builder(Document& doc, double snapTolerance) : m_doc(doc), m_snapTolerance(snapTolerance) {}

    // Registers entityId's own point(s) into the Sketch (idempotent --
    // safe to call more than once for the same entity), returning its
    // mapping (kind == Unsupported if entityId isn't a Line/Circle/Point,
    // or doesn't exist at all).
    EntityMapping& ensureEntity(EntityId entityId) {
        auto it = m_mappings.find(entityId);
        if (it != m_mappings.end()) return it->second;

        EntityMapping mapping;
        const Entity* entity = m_doc.findEntity(entityId);
        if (entity) {
            if (entity->type() == EntityType::Line) {
                const auto* line = static_cast<const LineEntity*>(entity);
                mapping.kind = EntityKind::Line;
                mapping.pointIndex[0] = getOrAddPoint(line->start());
                mapping.pointIndex[1] = getOrAddPoint(line->end());
                mapping.sketchIndex = m_sketch.addLine(mapping.pointIndex[0], mapping.pointIndex[1]);
            } else if (entity->type() == EntityType::Circle) {
                const auto* circle = static_cast<const CircleEntity*>(entity);
                mapping.kind = EntityKind::Circle;
                mapping.pointIndex[0] = getOrAddPoint(circle->center());
                mapping.sketchIndex = m_sketch.addCircle(mapping.pointIndex[0], circle->radius());
            } else if (entity->type() == EntityType::Point) {
                const auto* point = static_cast<const PointEntity*>(entity);
                mapping.kind = EntityKind::Point;
                mapping.pointIndex[0] = getOrAddPoint(point->position());
            } else if (entity->type() == EntityType::Arc) {
                const auto* arc = static_cast<const ArcEntity*>(entity);
                mapping.kind = EntityKind::Arc;
                mapping.pointIndex[0] = getOrAddPoint(arc->startPoint());
                mapping.pointIndex[1] = getOrAddPoint(arc->endPoint());
                mapping.pointIndex[2] = getOrAddPoint(arc->center());
                mapping.sketchIndex = m_sketch.addArc(mapping.pointIndex[2], mapping.pointIndex[0],
                                                      mapping.pointIndex[1], arc->radius(), /*ccw=*/true);
            }
        }
        return m_mappings.emplace(entityId, mapping).first->second;
    }

    int pointIndexFor(const DocumentPointRef& ref) {
        const EntityMapping& mapping = ensureEntity(ref.entityId);
        if (mapping.kind == EntityKind::Unsupported) return -1;
        if (ref.pointIndex < 0 || ref.pointIndex > 2) return -1;
        return mapping.pointIndex[ref.pointIndex];
    }

    Sketch& sketch() { return m_sketch; }

    void writeBack() {
        for (const auto& [entityId, mapping] : m_mappings) {
            Entity* entity = m_doc.findEntity(entityId);
            if (!entity) continue;
            if (mapping.kind == EntityKind::Line) {
                entity->moveGripPoint(0, m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[0])]);
                entity->moveGripPoint(1, m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[1])]);
            } else if (mapping.kind == EntityKind::Circle) {
                const Point2D center = m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[0])];
                const double radius = m_sketch.circles()[static_cast<std::size_t>(mapping.sketchIndex)].radius;
                entity->moveGripPoint(0, center);
                entity->moveGripPoint(1, center + Point2D(radius, 0.0));
            } else if (mapping.kind == EntityKind::Point) {
                entity->moveGripPoint(0, m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[0])]);
            } else if (mapping.kind == EntityKind::Arc) {
                // Center (grip 2) first: ArcEntity::moveGripPoint(2, ...) translates the
                // whole arc, keeping startAngle/endAngle unchanged, so start/end (grips 0/1)
                // -- which recompute their angle via atan2 against the CURRENT center --
                // must be set afterward, against the arc's final center position.
                const Point2D center = m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[2])];
                const Point2D start = m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[0])];
                const Point2D end = m_sketch.points()[static_cast<std::size_t>(mapping.pointIndex[1])];
                const double radius = m_sketch.arcs()[static_cast<std::size_t>(mapping.sketchIndex)].radius;
                entity->moveGripPoint(2, center);
                static_cast<ArcEntity*>(entity)->setRadius(radius);
                entity->moveGripPoint(0, start);
                entity->moveGripPoint(1, end);
            }
        }
    }

private:
    int getOrAddPoint(const Point2D& p) {
        for (std::size_t i = 0; i < m_knownPositions.size(); ++i) {
            if (m_knownPositions[i].distanceTo(p) <= m_snapTolerance) return static_cast<int>(i);
        }
        m_knownPositions.push_back(p);
        return m_sketch.addPoint(p, false);
    }

    Document& m_doc;
    double m_snapTolerance;
    Sketch m_sketch;
    std::vector<Point2D> m_knownPositions;
    std::map<EntityId, EntityMapping> m_mappings;
};

// Which EntityKind a constraint type's geomA/geomB slot expects, straight
// from SketchConstraintType's own per-type doc comments (Horizontal's
// "geomA (line)", Radius's "geomA (circle)", ArcRadius's "geomA (arc)",
// etc.) -- nullopt means that type doesn't use that slot at all (Distance/
// DistanceX/DistanceY/Fix are entirely point-based) or that slot isn't
// used by this type at all (e.g. Radius's geomB). ConstraintSolver.cpp
// indexes geomA/geomB straight into sketch.lines()/circles()/arcs() by
// constraint type with no kind check of its own, so a mismatched pairing
// (e.g. a Horizontal constraint's geomA resolving to a Circle or Arc,
// whose sketchIndex is really an index into a DIFFERENT array) would index
// the wrong array there -- checked here instead, where the mapping's real
// EntityKind is still available, so it's rejected the same way any other
// unsupported reference is (silently skipped).
struct ExpectedGeomKinds {
    std::optional<EntityKind> a, b;
};

ExpectedGeomKinds expectedGeomKinds(SketchConstraintType type) {
    switch (type) {
        case SketchConstraintType::Horizontal:
        case SketchConstraintType::Vertical:
        case SketchConstraintType::PointOnLine:
        case SketchConstraintType::Midpoint:
        case SketchConstraintType::Symmetric:
            return {EntityKind::Line, std::nullopt};
        case SketchConstraintType::Parallel:
        case SketchConstraintType::Perpendicular:
        case SketchConstraintType::Equal:
        case SketchConstraintType::Angle:
            return {EntityKind::Line, EntityKind::Line};
        case SketchConstraintType::Radius:
        case SketchConstraintType::Diameter:
        case SketchConstraintType::PointOnCircle:
            return {EntityKind::Circle, std::nullopt};
        case SketchConstraintType::EqualCircleRadius:
        case SketchConstraintType::TangentCircleCircle:
        case SketchConstraintType::InternalTangentCircleCircle:
            return {EntityKind::Circle, EntityKind::Circle};
        case SketchConstraintType::ArcRadius:
            return {EntityKind::Arc, std::nullopt};
        case SketchConstraintType::EqualArcRadius:
            return {EntityKind::Arc, EntityKind::Arc};
        case SketchConstraintType::Tangent:
            return {EntityKind::Line, EntityKind::Circle};
        default:
            return {std::nullopt, std::nullopt}; // Distance/DistanceX/DistanceY/Fix: point-based only
    }
}

} // namespace

DocumentConstraintResult solveDocumentConstraints(Document& doc, const std::vector<DocumentConstraint>& constraints,
                                                  double snapTolerance) {
    DocumentConstraintResult result;
    Builder builder(doc, snapTolerance);

    for (const DocumentConstraint& c : constraints) {
        SketchConstraint sc;
        sc.type = c.type;
        sc.value = c.value;

        const ExpectedGeomKinds expectedKinds = expectedGeomKinds(c.type);

        bool ok = true;
        if (c.geomA != 0) {
            const EntityMapping& mapping = builder.ensureEntity(c.geomA);
            if (mapping.kind == EntityKind::Unsupported) ok = false;
            else if (expectedKinds.a && mapping.kind != *expectedKinds.a) ok = false;
            else sc.geomA = mapping.sketchIndex;
        }
        if (ok && c.geomB != 0) {
            const EntityMapping& mapping = builder.ensureEntity(c.geomB);
            if (mapping.kind == EntityKind::Unsupported) ok = false;
            else if (expectedKinds.b && mapping.kind != *expectedKinds.b) ok = false;
            else sc.geomB = mapping.sketchIndex;
        }
        if (ok && c.pointA.entityId != 0) {
            sc.pointA = builder.pointIndexFor(c.pointA);
            if (sc.pointA < 0) ok = false;
        }
        if (ok && c.pointB.entityId != 0) {
            sc.pointB = builder.pointIndexFor(c.pointB);
            if (sc.pointB < 0) ok = false;
        }
        if (!ok) continue; // unsupported/missing entity, or its kind doesn't match this constraint type -- skip

        builder.sketch().addConstraint(sc);
    }

    const SolveResult solved = solveSketch(builder.sketch());
    result.converged = solved.converged;
    result.finalResidualNorm = solved.finalResidualNorm;
    builder.writeBack();
    return result;
}

} // namespace lcad
