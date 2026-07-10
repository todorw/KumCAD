#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// DIMLINEAR / DIMALIGNED: first extension line origin, second origin, then
// the dimension line location. Aligned measures the true point-to-point
// distance; linear measures the X or Y delta (picked by drag direction).
class DimCommand : public DrawCommand {
public:
    DimCommand(lcad::Document& document, bool aligned) : m_document(document), m_aligned(aligned) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onSnapContext(const std::optional<lcad::SnapRef>& ref) override { m_pendingSnap = ref; }
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        if (m_stage == Stage::SecondPoint) return m_p1;
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { FirstPoint, SecondPoint, LinePosition };

    lcad::Document& m_document;
    bool m_aligned;
    Stage m_stage = Stage::FirstPoint;
    lcad::Point2D m_p1;
    lcad::Point2D m_p2;
    // Osnap references behind each picked origin (set when the user snapped
    // onto real geometry); stored on the entity to make it associative.
    std::optional<lcad::SnapRef> m_pendingSnap;
    std::optional<lcad::SnapRef> m_ref1;
    std::optional<lcad::SnapRef> m_ref2;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
