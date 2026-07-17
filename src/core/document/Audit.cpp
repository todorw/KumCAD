#include "core/document/Audit.h"

#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

namespace lcad {

AuditResult runAudit(Document& doc, bool fix) {
    AuditResult result;

    std::vector<EntityId> toDelete;
    for (Entity* e : doc.entities()) {
        bool entityFixed = false;

        if (!doc.findLayer(e->layer())) {
            AuditIssue issue;
            issue.entityId = e->id();
            issue.description = "references a nonexistent layer";
            if (fix) {
                e->setLayer(0);
                issue.fixed = true;
                entityFixed = true;
            }
            result.issues.push_back(issue);
        }

        std::string degenerateReason;
        if (e->type() == EntityType::Line) {
            const auto& line = static_cast<const LineEntity&>(*e);
            if (line.start().distanceTo(line.end()) < 1e-9) degenerateReason = "zero-length line";
        } else if (e->type() == EntityType::Circle) {
            const auto& circle = static_cast<const CircleEntity&>(*e);
            if (circle.radius() <= 1e-9) degenerateReason = "zero/negative-radius circle";
        } else if (e->type() == EntityType::Arc) {
            const auto& arc = static_cast<const ArcEntity&>(*e);
            if (arc.radius() <= 1e-9) degenerateReason = "zero/negative-radius arc";
        } else if (e->type() == EntityType::Polyline) {
            const auto& pl = static_cast<const PolylineEntity&>(*e);
            if (pl.vertices().size() < 2) degenerateReason = "polyline with fewer than 2 vertices";
        }

        if (!degenerateReason.empty()) {
            AuditIssue issue;
            issue.entityId = e->id();
            issue.description = degenerateReason;
            if (fix) {
                toDelete.push_back(e->id());
                issue.fixed = true;
            }
            result.issues.push_back(issue);
        }

        if (entityFixed) ++result.fixedCount;
    }

    for (EntityId id : toDelete) {
        doc.removeEntity(id);
        ++result.fixedCount;
    }

    return result;
}

} // namespace lcad
