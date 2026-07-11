#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// TABLEDIT: pick a table cell, type its new text (empty keeps the cell
// unchanged), repeat until Enter -- one undo step per cell edited.
class TableEditCommand : public DrawCommand {
public:
    explicit TableEditCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("TABLEDIT  Select table cell:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage == Stage::Text; }
    std::optional<QString> onText(const QString& text) override;
    bool requestFinish() override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Select, Text };

    lcad::Document& m_document;
    Stage m_stage = Stage::Select;
    lcad::EntityId m_tableId = 0;
    int m_row = 0;
    int m_col = 0;
    bool m_finished = false;
};
