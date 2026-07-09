#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// Linear/aligned dimension, the AutoCAD DIMLINEAR/DIMALIGNED pair: measures
// between two definition points, drawn as a dimension line (through
// linePoint) with extension lines, arrowheads, and a centered value label.
//
// Aligned: the dimension line is parallel to p1-p2 and measures their true
// distance. Linear: the dimension line is horizontal or vertical (picked by
// where linePoint sits relative to the measured points) and measures the X or
// Y delta. All the derived drawing geometry comes from geometry(), so the
// renderer and hit-testing share one construction.
class DimensionEntity : public Entity {
public:
    struct Geometry {
        Point2D dimA, dimB;      // dimension line endpoints (arrow tips)
        Point2D ext1A, ext1B;    // extension line from p1
        Point2D ext2A, ext2B;    // extension line from p2
        Point2D textPos;         // label anchor (center of the text)
        double textAngle = 0.0;  // label rotation, radians CCW
        double value = 0.0;      // measured distance
    };

    DimensionEntity(EntityId id, LayerId layer, Point2D p1, Point2D p2, Point2D linePoint, bool aligned,
                    double textHeight = 2.5)
        : Entity(id, layer), m_p1(p1), m_p2(p2), m_linePoint(linePoint), m_aligned(aligned),
          m_textHeight(textHeight) {}

    const Point2D& point1() const { return m_p1; }
    const Point2D& point2() const { return m_p2; }
    const Point2D& linePoint() const { return m_linePoint; }
    bool aligned() const { return m_aligned; }
    double textHeight() const { return m_textHeight; }

    Geometry geometry() const;

    EntityType type() const override { return EntityType::Dimension; }
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
    Point2D m_p1;
    Point2D m_p2;
    Point2D m_linePoint;
    bool m_aligned;
    double m_textHeight;
};

} // namespace lcad
