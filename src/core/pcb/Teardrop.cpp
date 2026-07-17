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

    // Pad-side shoulders sit on the pad's own circle, at +-halfAngle from
    // the track direction (measured from pad center).
    auto onPadCircle = [&](double angleFromDir) {
        const double c = std::cos(angleFromDir), s = std::sin(angleFromDir);
        return padCenter + u * (padRadius * c) + perp * (padRadius * s);
    };

    std::vector<Point2D> poly;
    poly.push_back(edge1);
    poly.push_back(onPadCircle(halfAngle));
    // Hug the pad's curvature between the two shoulders, through the
    // point closest to the track (angle 0).
    const int samples = std::max(0, arcSamples);
    for (int i = 1; i < samples; ++i) {
        const double t = static_cast<double>(i) / samples; // (0,1)
        const double angle = halfAngle - t * 2.0 * halfAngle;
        poly.push_back(onPadCircle(angle));
    }
    poly.push_back(onPadCircle(-halfAngle));
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
