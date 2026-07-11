#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style STYLE (text styles): prompts for a style name (Enter keeps
// the current one, a new name creates a style), then walks font family,
// fixed height (0 = prompt per TEXT), width factor, and oblique angle. The
// edited style becomes current; existing entities using it re-render with
// the new look.
class StyleCommand : public DrawCommand {
public:
    explicit StyleCommand(lcad::Document& document) : m_document(document) {}

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
    enum class Stage { Name, Font, Height, WidthFactor, Oblique, Annotative };

    lcad::Document& m_document;
    Stage m_stage = Stage::Name;
    lcad::TextStyle m_style;
    bool m_finished = false;
};
