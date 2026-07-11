#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style XREF (lite): Attach loads an external DXF/DWG as a cached
// reference block and places an insert at a picked point; Reload re-reads an
// attached file; Detach erases the drawing's inserts of it (undoable — the
// cached definition stays until the file is saved without it being used).
// Xref geometry renders dimmed to tell it apart from native entities.
class XrefCommand : public DrawCommand {
public:
    explicit XrefCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::InsertPoint; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Option, AttachPath, InsertPoint, ReloadName, DetachName };

    QString xrefList() const;

    lcad::Document& m_document;
    Stage m_stage = Stage::Option;
    const lcad::BlockDefinition* m_attached = nullptr;
    bool m_finished = false;
};
