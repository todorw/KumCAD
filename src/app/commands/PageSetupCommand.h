#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style PAGESETUP (lite): sets the active layout's sheet — a named
// paper size (A4..A0, Letter) plus orientation, or custom dimensions in mm.
class PageSetupCommand : public DrawCommand {
public:
    PageSetupCommand(lcad::Document& document, int layoutIndex)
        : m_document(document), m_layoutIndex(layoutIndex), m_pendingWidth(layout().paperWidth),
          m_pendingHeight(layout().paperHeight) {}

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
    enum class Stage { Size, Orientation, CustomWidth, CustomHeight };

    lcad::Layout& layout() { return m_document.layouts()[m_layoutIndex]; }
    void commit();

    lcad::Document& m_document;
    int m_layoutIndex;
    double m_pendingWidth;  // not written to the document until commit()
    double m_pendingHeight;
    Stage m_stage = Stage::Size;
    double m_width = 297.0; // portrait-oriented base size, before orientation
    double m_height = 210.0;
    bool m_finished = false;
};
