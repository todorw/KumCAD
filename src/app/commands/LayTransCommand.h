#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// LAYTRANS: prompts for a translation mapping file path (see
// core/document/LayerTranslator.h), applies every mapping, and reports
// how many layers were renamed/merged, plus any oldName not found.
class LayTransCommand : public DrawCommand {
public:
    explicit LayTransCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("LAYTRANS  Enter translation mapping file path:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::optional<QString> m_result;
    bool m_finished = false;
};
