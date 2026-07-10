#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <optional>
#include <vector>

// AutoCAD-style PLINE: collects vertices as the user clicks, committing a
// single PolylineEntity (one undo step) when finished via Enter/right-click
// or Escape. Needs at least two points to produce anything.
//
// The Arc option switches to tangent-continuous arc segments, AutoCAD's PLINE
// Arc default: each picked point ends an arc that starts tangent to the
// previous segment. Line switches back to straight segments.
class PolylineCommand : public DrawCommand {
public:
    explicit PolylineCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onOption(const QString& option) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    bool requestFinish() override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_points.empty() ? std::nullopt : std::optional<lcad::Point2D>(m_points.back());
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    // Bulge for a new segment from the current last point to `to`, tangent to
    // the previous segment (0 in line mode or with no direction history yet).
    double bulgeForNextSegment(const lcad::Point2D& to) const;
    void commit(bool closed);
    QString prompt() const;

    lcad::Document& m_document;
    std::vector<lcad::Point2D> m_points;
    std::vector<double> m_bulges; // parallel to m_points (bulge of the segment leaving each vertex)
    bool m_arcMode = false;
    std::optional<double> m_tangent; // direction (radians) at the end of the last segment
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
