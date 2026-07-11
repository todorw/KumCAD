#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Commands.h"
#include "core/document/Document.h"

// VPSCALE: sets the selected viewport's scale, in paper units (mm) per model
// unit -- e.g. 1 for 1:1, 0.01 for 1:100. Layout-mode only, with a viewport
// selected.
class VpScaleCommand : public DrawCommand {
public:
    VpScaleCommand(lcad::Document& document, int layoutIndex, int viewportIndex)
        : m_document(document), m_layoutIndex(layoutIndex), m_viewportIndex(viewportIndex) {}

    QString start() override {
        return QStringLiteral("VPSCALE  Enter viewport scale (paper mm per model unit) <%1>:")
            .arg(currentScale());
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value <= 0) return QStringLiteral("*Scale must be positive*");
        if (m_layoutIndex >= 0 && m_layoutIndex < static_cast<int>(m_document.layouts().size())) {
            std::vector<lcad::Layout> layouts = m_document.layouts();
            auto& viewports = layouts[static_cast<std::size_t>(m_layoutIndex)].viewports;
            if (m_viewportIndex >= 0 && m_viewportIndex < static_cast<int>(viewports.size())) {
                viewports[static_cast<std::size_t>(m_viewportIndex)].viewScale = value;
                m_document.commandStack().execute(
                    std::make_unique<lcad::SetLayoutsCommand>(m_document, std::move(layouts)));
            }
        }
        m_finished = true;
        return QStringLiteral("*Viewport scale set to %1*").arg(value);
    }

    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    double currentScale() const {
        if (m_layoutIndex >= 0 && m_layoutIndex < static_cast<int>(m_document.layouts().size())) {
            const auto& viewports = m_document.layouts()[m_layoutIndex].viewports;
            if (m_viewportIndex >= 0 && m_viewportIndex < static_cast<int>(viewports.size())) {
                return viewports[m_viewportIndex].viewScale;
            }
        }
        return 1.0;
    }

    lcad::Document& m_document;
    int m_layoutIndex;
    int m_viewportIndex;
    bool m_finished = false;
};
