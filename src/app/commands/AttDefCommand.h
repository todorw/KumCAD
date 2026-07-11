#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style ATTDEF: walks tag, prompt, default value, and text height,
// then places the attribute definition at a picked point. Select it together
// with geometry and run BLOCK; INSERT then prompts for the value.
class AttDefCommand : public DrawCommand {
public:
    explicit AttDefCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("ATTDEF  Enter tag name (no spaces):"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::Position; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Tag, Prompt, Default, Height, Position };

    lcad::Document& m_document;
    Stage m_stage = Stage::Tag;
    QString m_tag;
    QString m_prompt;
    QString m_default;
    double m_height = 2.5;
    bool m_finished = false;
};
