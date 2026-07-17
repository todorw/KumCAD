#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AUDIT: prompts "Fix any errors detected? [Yes/No] <No>", matching real
// AutoCAD's own AUDIT prompt, then reports every issue found (and, if
// fixing, what was fixed).
class AuditCommand : public DrawCommand {
public:
    explicit AuditCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("AUDIT  Fix any errors detected? [Yes/No] <No>:"); }
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
