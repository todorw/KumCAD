#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style COPY: acts on a pre-existing selection. Specify base point,
// then repeatedly specify second points -- each one drops a copy of the
// selection offset by that vector (its own undo step), until Enter/right-click
// finishes the command. The originals are left in place and selected.
class CopyCommand : public DrawCommand {
public:
    CopyCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    bool requestFinish() override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    lcad::Point2D m_base;
    bool m_hasBase = false;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
