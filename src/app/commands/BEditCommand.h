#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// BEDIT: prompts for a block name, then opens a BlockEditorWindow scoped to
// that block's own entities -- AutoCAD's in-place block editor (see
// BlockEditorWindow.h). Matches CommandDispatcher's own existing
// window-opening precedent (PCB3D's Board3DWindow, Window3D's
// AssemblyWindow): the new window is its own independent top-level window,
// not owned/modal to this one.
class BEditCommand : public DrawCommand {
public:
    explicit BEditCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BEDIT  Enter block name:"); }
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
    bool m_finished = false;
};
