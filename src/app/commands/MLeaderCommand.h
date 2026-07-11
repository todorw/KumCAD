#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <QStringList>

#include <vector>

// MLEADER: arrowhead point, one or more landing points (Enter to stop
// picking), then annotation lines (empty line finishes). Creates a single-leg
// MLeaderEntity plus an MTEXT at the landing, sized from the document's
// current multileader style, as one undo step. Mirrors LeaderCommand.
class MLeaderCommand : public DrawCommand {
public:
    explicit MLeaderCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_points.empty() ? std::nullopt : std::optional<lcad::Point2D>(m_points.back());
    }
    bool wantsTextInput() const override { return m_stage == Stage::Text; }
    std::optional<QString> onText(const QString& text) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override {
        if (!m_finished && m_stage == Stage::Text) {
            return QStringLiteral("Enter annotation line (empty line to finish):");
        }
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Points, Text };

    void commit();

    lcad::Document& m_document;
    Stage m_stage = Stage::Points;
    std::vector<lcad::Point2D> m_points;
    QStringList m_lines;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
