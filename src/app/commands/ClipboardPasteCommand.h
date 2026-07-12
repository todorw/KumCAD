#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <QImage>

// AutoCAD's OLE embedding (PASTESPEC/pasting an Excel range, a Word
// selection, etc. as a live linked object), reduced to its one genuinely
// cross-platform-implementable piece: pasting a raster image from the
// clipboard as an underlay. Real OLE embedding relies on Windows COM/OLE
// automation to keep a live link back to the source application (Excel,
// Word) and re-render through it -- there is no non-Windows equivalent to
// build against, so that half is simply out of scope, not approximated.
// This command saves the clipboard's image to a small per-user cache
// directory (so the path stays valid across save/reload, matching how
// IMAGEATTACH itself works) and places it exactly like an attached image.
class ClipboardPasteCommand : public DrawCommand {
public:
    ClipboardPasteCommand(lcad::Document& document, QImage image)
        : m_document(document), m_image(std::move(image)) {}

    QString start() override { return QStringLiteral("PASTECLIP  Specify insertion point (bottom-left):"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Position, Width };

    lcad::Document& m_document;
    QImage m_image;
    Stage m_stage = Stage::Position;
    lcad::Point2D m_position;
    std::optional<QString> m_result;
    bool m_finished = false;
};
