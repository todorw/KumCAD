#pragma once

#include "core/geometry/Entity.h"

#include <string>
#include <vector>

namespace lcad {

// AutoCAD POINTCLOUD (simplified): a large flat set of points from a scan
// file, kept as one entity so an attach is one undo step and one draw call
// rather than thousands of individual entities. Z is discarded -- KumCAD is
// a 2D system throughout, so a cloud shows as its plan-view projection.
//
// distanceTo() (used for click-picking) tests the bounding box rather than
// every individual point: with realistic cloud sizes (tens of thousands of
// points), per-point hit-testing on every mouse-move would be needlessly
// slow, and "click anywhere in the cloud's extent" is a reasonable way to
// select a reference object you're not meant to edit point-by-point anyway.
// There's a single grip (the bounding box center) that moves the whole
// cloud, matching ImageEntity/InsertEntity.
class PointCloudEntity : public Entity {
public:
    PointCloudEntity(EntityId id, LayerId layer, std::string path, std::vector<Point2D> points)
        : Entity(id, layer), m_path(std::move(path)), m_points(std::move(points)) {}

    const std::string& path() const { return m_path; }
    const std::vector<Point2D>& points() const { return m_points; }

    EntityType type() const override { return EntityType::PointCloud; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    void mirror(const Point2D& a, const Point2D& b) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    // Too many points to usefully offer as individual osnap candidates.
    std::vector<SnapPoint> snapCandidates() const override { return {}; }
    std::unique_ptr<Entity> clone() const override;

private:
    std::string m_path;
    std::vector<Point2D> m_points;
};

} // namespace lcad
