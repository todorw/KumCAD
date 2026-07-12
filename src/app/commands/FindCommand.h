#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD FIND (simplified): searches TEXT/MTEXT content for a
// case-insensitive substring; an empty replacement at the second prompt
// means find-only (matches become the new selection, nothing edited),
// otherwise every match is replaced (also case-insensitive) as one undo
// step. Other FIND targets (attributes, dimension text, table cells,
// hyperlinks) aren't searched.
class FindCommand : public DrawCommand {
public:
    explicit FindCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("FIND  Enter text to find:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }
    std::optional<std::vector<lcad::EntityId>> resultSelection() const override {
        return m_finished ? std::optional(m_matches) : std::nullopt;
    }

private:
    enum class Stage { Find, Replace };

    void runSearch();

    lcad::Document& m_document;
    Stage m_stage = Stage::Find;
    QString m_findText;
    QString m_replaceText;
    bool m_doReplace = false;
    std::vector<lcad::EntityId> m_matches;
    bool m_finished = false;
};
