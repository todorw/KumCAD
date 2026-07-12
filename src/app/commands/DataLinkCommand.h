#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// A simplified DATALINK: imports a CSV file (see core/io/Csv.h) into a new
// TABLE entity, one cell per field. AutoCAD's real Data Link Manager keeps a
// live link back to the Excel/CSV source and can refresh in place; this
// snapshots the file once at import time (matching how XREF's own snapshot
// works when the source isn't reachable).
class DataLinkCommand : public DrawCommand {
public:
    explicit DataLinkCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("DATALINK  Enter CSV file path:"); }
    bool wantsTextInput() const override { return m_stage == Stage::Path; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Path, Position };

    lcad::Document& m_document;
    Stage m_stage = Stage::Path;
    std::vector<std::vector<std::string>> m_rows;
    bool m_finished = false;
};
