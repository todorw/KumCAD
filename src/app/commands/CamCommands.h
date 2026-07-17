#pragma once

#include "commands/DrawCommand.h"
#include "core/cam/Toolpath.h"
#include "core/document/Document.h"

// GCODE: select a closed polyline profile, then tool diameter, cut side
// (OUTSIDE/INSIDE/ONLINE), feed rate, plunge rate, cut depth, safe height,
// and an output file path -- writes G-code for it (see core/cam/
// Toolpath.h, core/cam/GCodeWriter.h). Pressing Enter with no input at
// each of the last four prompts keeps ToolpathParams's own default for
// that field, shown in the prompt. Only PolylineEntity profiles are
// supported (a HATCH's traced boundary or a drawn PLINE/RECTANG) -- not
// circles or other closed curves directly.
class GCodeExportCommand : public DrawCommand {
public:
    GCodeExportCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override { return QStringLiteral("GCODE  Select a closed polyline profile:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::Pick; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Pick, ToolDiameter, CutSide, FeedRate, PlungeRate, CutDepth, SafeHeight, Path };
    lcad::Document& m_document;
    double m_pickTolerance;
    Stage m_stage = Stage::Pick;
    lcad::EntityId m_profileId = 0;
    lcad::ToolpathParams m_params; // toolDiameter/side/feedRate/plungeRate/cutDepth/safeHeight, built up prompt by prompt
    bool m_finished = false;
};
