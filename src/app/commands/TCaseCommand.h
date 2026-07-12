#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD Express Tools' TCASE: changes the case of every TEXT/MTEXT entity
// in the current selection (other entity types in the selection are left
// alone). Real TCASE also offers Sentence case and tOGGLE cASE; this covers
// the three most commonly reached-for options.
class TCaseCommand : public DrawCommand {
public:
    TCaseCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override { return QStringLiteral("TCASE  Enter case option [Upper/Lower/Title] <Upper>:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    void apply(const QString& option);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    std::optional<QString> m_result;
    bool m_finished = false;
};
