#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Commands.h"
#include "core/document/Document.h"

// AutoCAD MATCHPROP: pick a source object, then paint its layer and
// color/linetype/lineweight overrides onto each picked destination (one undo
// step per pick) until Enter.
class MatchPropCommand : public DrawCommand {
public:
    MatchPropCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override { return QStringLiteral("MATCHPROP  Select source object:"); }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        lcad::Entity* hit = pick(pt);
        if (!hit) {
            return m_sourceId == 0 ? QStringLiteral("*Nothing there*\nSelect source object:")
                                   : QStringLiteral("*Nothing there*\nSelect destination object:");
        }
        if (m_sourceId == 0) {
            m_sourceId = hit->id();
            return QStringLiteral("Select destination object (Enter to finish):");
        }
        if (hit->id() == m_sourceId) return QStringLiteral("*That is the source*\nSelect destination object:");
        m_document.commandStack().execute(
            std::make_unique<lcad::MatchPropertiesCommand>(m_document, m_sourceId, std::vector{hit->id()}));
        return QStringLiteral("*Properties matched*\nSelect destination object (Enter to finish):");
    }

    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Entity* pick(const lcad::Point2D& pt) const {
        lcad::Entity* best = nullptr;
        double bestDist = m_pickTolerance;
        for (lcad::Entity* e : m_document.entities()) {
            const lcad::Layer* layer = m_document.findLayer(e->layer());
            if (layer && (!layer->visible || layer->locked)) continue;
            const double d = e->distanceTo(pt);
            if (d <= bestDist) {
                bestDist = d;
                best = e;
            }
        }
        return best;
    }

    lcad::Document& m_document;
    double m_pickTolerance;
    lcad::EntityId m_sourceId = 0;
    bool m_finished = false;
};
