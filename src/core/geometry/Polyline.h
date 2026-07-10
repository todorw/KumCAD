#pragma once

#include "core/geometry/Entity.h"

#include <functional>
#include <optional>
#include <vector>

namespace lcad {

// Circular-arc form of one bulged polyline segment. The arc runs from the
// segment's start vertex through a signed CCW sweep (negative = clockwise),
// matching the DXF bulge convention: bulge = tan(includedAngle / 4), negative
// when the arc goes clockwise from start to end.
struct BulgeArc {
    Point2D center;
    double radius = 0.0;
    double startAngle = 0.0; // angle of the start vertex, radians CCW from +X
    double sweep = 0.0;      // signed included angle, radians (positive CCW)
};

// Arc parameters for the segment a->b with the given bulge, or nullopt when
// the segment is straight (|bulge| ~ 0) or degenerate (a ~ b).
std::optional<BulgeArc> bulgeToArc(const Point2D& a, const Point2D& b, double bulge);

class PolylineEntity : public Entity {
public:
    PolylineEntity(EntityId id, LayerId layer, std::vector<Point2D> vertices, bool closed = false)
        : Entity(id, layer), m_vertices(std::move(vertices)), m_bulges(m_vertices.size(), 0.0), m_closed(closed) {}

    PolylineEntity(EntityId id, LayerId layer, std::vector<Point2D> vertices, std::vector<double> bulges, bool closed)
        : Entity(id, layer), m_vertices(std::move(vertices)), m_bulges(std::move(bulges)), m_closed(closed) {
        m_bulges.resize(m_vertices.size(), 0.0);
    }

    const std::vector<Point2D>& vertices() const { return m_vertices; }
    bool closed() const { return m_closed; }

    // Per-vertex bulge for the segment leaving that vertex (the last one only
    // matters when the polyline is closed), following DXF LWPOLYLINE semantics.
    const std::vector<double>& bulges() const { return m_bulges; }
    double bulgeAt(std::size_t index) const { return index < m_bulges.size() ? m_bulges[index] : 0.0; }
    void setBulge(std::size_t index, double bulge) {
        if (index < m_bulges.size()) m_bulges[index] = bulge;
    }
    bool hasArcs() const;

    // Calls fn(start, end, bulge) for every segment, including the closing one.
    void forEachSegment(const std::function<void(const Point2D&, const Point2D&, double)>& fn) const;

    // The polyline with arc segments approximated by short chords -- for
    // consumers that only understand straight segments (hatching, area,
    // intersection fallbacks). Straight-only polylines return the vertices as-is.
    std::vector<Point2D> flattenedVertices() const;

    EntityType type() const override { return EntityType::Polyline; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    void mirror(const Point2D& a, const Point2D& b) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::vector<SnapPoint> snapCandidates() const override;
    std::unique_ptr<Entity> clone() const override;

private:
    std::vector<Point2D> m_vertices;
    std::vector<double> m_bulges; // parallel to m_vertices
    bool m_closed;
};

} // namespace lcad
