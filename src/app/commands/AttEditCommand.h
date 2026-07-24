#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD's ATTEDIT/EATTEDIT: edits an already-placed INSERT's own
// attribute VALUES (the "go back and fix the DWG NO. on the title block"
// workflow) -- InsertEntity::setAttribute() was previously only ever
// called at insertion time (InsertCommand), with no path to revisit it
// afterward. Lists the target's current tag=value pairs, then repeatedly
// prompts for a tag to edit (case-insensitive match) and its new value
// (Enter keeps the current value unchanged, matching real AutoCAD's own
// "Enter new value <current>:" default-on-blank convention) until Enter
// at the tag prompt finishes.
class AttEditCommand : public DrawCommand {
public:
    AttEditCommand(lcad::Document& document, lcad::EntityId targetId)
        : m_document(document), m_targetId(targetId) {}

    QString start() override;
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    QString listing() const;

    lcad::Document& m_document;
    lcad::EntityId m_targetId;
    std::string m_pendingTag;
    bool m_awaitingValue = false;
    bool m_finished = false;
};
