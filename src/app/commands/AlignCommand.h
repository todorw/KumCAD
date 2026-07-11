#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style ALIGN (2D): acts on a pre-existing selection. One source/
// destination point pair moves; a second pair adds rotation about the first
// destination and, if requested at the final prompt, scales the selection so
// the source pair spacing matches the destination pair spacing. Applied as a
// single undo step.
class AlignCommand : public DrawCommand {
public:
    AlignCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onOption(const QString& option) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_resultMessage; }
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Source1, Dest1, Source2, Dest2, ScaleQuery };

    QString apply(bool scaleToFit);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    Stage m_stage = Stage::Source1;
    lcad::Point2D m_s1, m_d1, m_s2, m_d2;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    std::optional<QString> m_resultMessage;
    bool m_finished = false;
};
