#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style LAYOUT: New (fresh A4 sheet), Copy (duplicates the active
// layout's sheet size and viewports), Rename, and Delete (refused for the
// last layout). MainWindow rebuilds the space tabs when the command ends.
// Layout management isn't undoable, matching the viewport operations.
class LayoutCommand : public DrawCommand {
public:
    LayoutCommand(lcad::Document& document, int activeLayoutIndex)
        : m_document(document), m_activeIndex(activeLayoutIndex) {}

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
    enum class Stage { Option, NewName, CopyName, DeleteName, RenameOld, RenameNew };

    QString layoutList() const;
    int findLayout(const QString& name) const;
    QString uniqueName(const QString& base) const;

    lcad::Document& m_document;
    int m_activeIndex;
    Stage m_stage = Stage::Option;
    int m_renameIndex = -1;
    bool m_finished = false;
};
