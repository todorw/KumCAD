#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style WBLOCK: writes the current selection (or the whole
// document, if nothing is selected -- real AutoCAD's own "Entire
// Drawing" default) out as a new, standalone DXF file. Extracts a
// fully self-contained Document first (see core/document/
// DocumentExtract.h: every layer/block dependency comes along, not just
// what the selection directly touches) so the written file opens
// correctly on its own, the same real promise a genuine wblock'd file
// makes.
class WBlockCommand : public DrawCommand {
public:
    WBlockCommand(lcad::Document& document, std::vector<lcad::EntityId> selectedIds)
        : m_document(document), m_selectedIds(std::move(selectedIds)) {}

    QString start() override { return QStringLiteral("WBLOCK  Enter output file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_selectedIds;
    bool m_finished = false;
};
