#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style DATAEXTRACTION/EATTEXT: a single insertion-point prompt
// that immediately extracts every attributed block instance in the drawing
// (auto column discovery, one row per instance -- see
// core/document/DataExtraction.h) and drops the report as a TABLE entity,
// the same "just place it" flow BomCommand already uses rather than a
// multi-page wizard dialog (real EATTEXT's block/attribute picker is a much
// larger project than this pass warrants).
class DataExtractionCommand : public DrawCommand {
public:
    explicit DataExtractionCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("DATAEXTRACTION  Specify insertion point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};
