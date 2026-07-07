#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style ROTATE: acts on a pre-existing selection. Specify base point,
// then a point defining the rotation angle -- the angle applied is simply the
// angle of the base->point vector (0 = East), matching AutoCAD's default
// (non-Reference) point-pick behavior.
class RotateCommand : public DrawCommand {
public:
    RotateCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
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
