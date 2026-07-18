#include "core/pcb/Teardrop.h"

#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <algorithm>
#include <cmath>

namespace lcad {

std::vector<Point2D> buildTeardrop(const Point2D& padCenter, double padRadius, const Point2D& dir, double trackWidth,
                                   double length, double halfAngleDegrees, int arcSamples) {
    if (padRadius <= 0.0 || trackWidth <= 0.0 || length <= padRadius) return {};
    const double dirLen = dir.length();
    if (dirLen < 1e-9) return {};

    constexpr double kPi = 3.14159265358979323846;
    const Point2D u = dir * (1.0 / dirLen); // unit direction, pad -> track
    const Point2D perp(-u.y, u.x);
    const double halfAngle = halfAngleDegrees * kPi / 180.0;

    const Point2D trackPoint = padCenter + u * length;
    const Point2D edge1 = trackPoint + perp * (trackWidth / 2.0);
    const Point2D edge2 = trackPoint - perp * (trackWidth / 2.0);

    // True tangent-line construction: the shoulder is where a straight
    // line from edge1/edge2 is tangent to the pad's own circle, not a
    // fixed angle around it. In the right triangle (pad center C, tangent
    // point T, edge point P) the right angle sits at T (CT is a radius,
    // perpendicular to the tangent line PT), so the angle at C between CP
    // and CT is acos(padRadius / |CP|) -- standard tangent-line-from-an-
    // external-point geometry. edge1's own angle from the track direction
    // (atan2(trackWidth/2, length)) plus that offset gives the shoulder's
    // own angle around the pad; edge2's shoulder is the mirror image.
    const double halfTrackWidth = trackWidth / 2.0;
    const double distToEdge = std::sqrt(length * length + halfTrackWidth * halfTrackWidth);
    double shoulderAngle = halfAngle;
    if (distToEdge > padRadius + 1e-9) {
        const double angleToEdge = std::atan2(halfTrackWidth, length);
        const double tangentOffset = std::acos(std::clamp(padRadius / distToEdge, -1.0, 1.0));
        shoulderAngle = angleToEdge + tangentOffset;
    }
    // A degenerate ratio (a very wide track on a small pad) can still push
    // this past a half-turn, which would self-intersect -- halfAngleDegrees
    // becomes a real cap in that case rather than the shoulder's own
    // primary source, the one place it still matters now.
    shoulderAngle = std::min(shoulderAngle, kPi - 1e-3);

    // Pad-side shoulders sit on the pad's own circle, at +-shoulderAngle
    // from the track direction (measured from pad center).
    auto onPadCircle = [&](double angleFromDir) {
        const double c = std::cos(angleFromDir), s = std::sin(angleFromDir);
        return padCenter + u * (padRadius * c) + perp * (padRadius * s);
    };

    std::vector<Point2D> poly;
    poly.push_back(edge1);
    poly.push_back(onPadCircle(shoulderAngle));
    // Hug the pad's curvature between the two shoulders, through the
    // point closest to the track (angle 0).
    const int samples = std::max(0, arcSamples);
    for (int i = 1; i < samples; ++i) {
        const double t = static_cast<double>(i) / samples; // (0,1)
        const double angle = shoulderAngle - t * 2.0 * shoulderAngle;
        poly.push_back(onPadCircle(angle));
    }
    poly.push_back(onPadCircle(-shoulderAngle));
    poly.push_back(edge2);
    poly.push_back(edge1); // close explicitly

    return poly;
}

std::vector<EntityId> addTeardropsToDocument(Document& doc, LayerId layer, const TeardropParams& params) {
    std::vector<EntityId> created;

    struct Anchor {
        Point2D position;
        double radius;
    };
    std::vector<Anchor> anchors;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Via) {
            const auto* via = static_cast<const ViaEntity*>(e);
            anchors.push_back({via->position(), via->diameter() / 2.0});
        } else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (!insert->block() || !insert->block()->isFootprint()) continue;
            for (const auto& padWorld : insert->padWorldPositions()) {
                anchors.push_back({padWorld.position, std::max(padWorld.pad->width, padWorld.pad->height) / 2.0});
            }
        }
    }
    if (anchors.empty()) return created;

    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Track) continue;
        const auto* track = static_cast<const TrackEntity*>(e);
        const auto& verts = track->vertices();
        if (verts.size() < 2) continue;

        // Check both ends of the track independently.
        for (bool atFront : {true, false}) {
            const Point2D& endpoint = atFront ? verts.front() : verts.back();
            const Point2D& next = atFront ? verts[1] : verts[verts.size() - 2];
            if (endpoint.distanceTo(next) < 1e-9) continue; // degenerate zero-length leading segment

            const Anchor* match = nullptr;
            for (const Anchor& anchor : anchors) {
                if (endpoint.distanceTo(anchor.position) <= params.tolerance) {
                    match = &anchor;
                    break;
                }
            }
            if (!match) continue;

            const Point2D dir = next - endpoint; // pad -> track direction (away from the pad, into the track)
            const double length = params.lengthFactor * match->radius;
            const auto poly = buildTeardrop(match->position, match->radius, dir, track->width(), length,
                                            params.halfAngleDegrees, params.arcSamples);
            if (poly.empty()) continue;

            const EntityId id = doc.reserveEntityId();
            doc.addEntity(std::make_unique<HatchEntity>(id, layer, poly));
            created.push_back(id);
        }
    }
    return created;
}

} // namespace lcad
