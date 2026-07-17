#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// WIPEOUT: collects vertices as the user clicks, committing a single
// WipeoutEntity (one undo step) when finished, same shape-picking pattern
// as TRACK/PLINE. Needs at least 3 points to produce anything.
class WipeoutCommand : public DrawCommand {
public:
    explicit WipeoutCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("WIPEOUT  Specify first point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    bool requestFinish() override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_points.empty() ? std::nullopt : std::optional<lcad::Point2D>(m_points.back());
    }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::Point2D> m_points;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    std::optional<QString> m_result;
    bool m_finished = false;
};
