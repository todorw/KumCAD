#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// MLEADERSTYLE with named styles: prompts for a style name first (Enter keeps
// the current style, an existing name restores it, a new name creates one
// seeded from the current values), then walks the style's values (text
// height, arrow size, landing gap). The edited style becomes current; new
// multileaders snapshot it at creation. Mirrors DimStyleCommand.
class MLeaderStyleCommand : public DrawCommand {
public:
    explicit MLeaderStyleCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;

    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Name, TextHeight, ArrowSize, LandingGap };

    lcad::Document& m_document;
    Stage m_stage = Stage::Name;
    std::string m_styleName;
    lcad::MLeaderStyle m_style;
    bool m_finished = false;
};
