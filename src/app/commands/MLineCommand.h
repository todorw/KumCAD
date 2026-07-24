#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <optional>
#include <vector>

// AutoCAD-style MLINE: collects centerline vertices the same way PLINE
// does (MLINE's own segments are always straight, matching real MLINE --
// no Arc option to mirror), committing a single MLineEntity when finished.
// Uses a fixed 2-element "STANDARD"-style pair (offsets +0.5/-0.5, matching
// real AutoCAD's own default MLSTYLE) rather than a full named-style
// table -- MLineEntity itself embeds its element list directly instead of
// indirecting through a separate style object (see MLine.h's own comment
// on that simplification); Scale and Justification (Top/Zero/Bottom, real
// AutoCAD's own three) still work exactly as they do for a real style.
class MLineCommand : public DrawCommand {
public:
    explicit MLineCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onOption(const QString& option) override;
    std::optional<QString> onScalar(double value) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    bool requestFinish() override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_points.empty() ? std::nullopt : std::optional<lcad::Point2D>(m_points.back());
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Points, AwaitingScale, AwaitingJustification };
    enum class Justification { Top, Zero, Bottom };

    QString prompt() const;
    void commit(bool closed);

    lcad::Document& m_document;
    std::vector<lcad::Point2D> m_points;
    Stage m_stage = Stage::Points;
    double m_scale = 1.0;
    Justification m_justification = Justification::Zero;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
