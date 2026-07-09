#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// OFFSET, adapted to KumCAD's select-first workflow: acts on the current
// selection. Specify a distance (typed, or as two picked points), then pick a
// point on the side to offset toward. Lines, circles, and arcs get parallel
// copies as one undoable step; other entity types are skipped with a note.
class OffsetCommand : public DrawCommand {
public:
    OffsetCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_hasDistBase ? std::optional<lcad::Point2D>(m_distBase) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    QString commit(const lcad::Point2D& sidePoint);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    double m_distance = 0.0;
    bool m_hasDistance = false;
    lcad::Point2D m_distBase; // first pick of a picked-two-points distance
    bool m_hasDistBase = false;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
