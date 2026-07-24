#pragma once

#include "commands/DrawCommand.h"
#include "DrawingView.h"

// AutoCAD's SNAP: reports the current on/off state and spacing, or sets
// them. Typing ON/OFF toggles grid-snap (the same flag F9/View > Grid Snap
// drives). Typing a positive number sets an explicit snap spacing --
// independent of GRID's own display spacing, matching real AutoCAD where a
// drawing commonly has a finer/coarser snap than its visible grid. Enter
// with no input keeps the current spacing and finishes.
class SnapCommand : public DrawCommand {
public:
    explicit SnapCommand(DrawingView& view) : m_view(view) {}

    QString start() override {
        return QStringLiteral("SNAP  Specify snap spacing or [ON/OFF] <%1, currently %2>:")
            .arg(m_view.effectiveSnapSpacing())
            .arg(m_view.gridSnapEnabled() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value <= 0) return QStringLiteral("*Spacing must be positive*");
        m_view.setSnapSpacingOverride(value);
        m_view.setGridSnapEnabled(true);
        m_finished = true;
        return QStringLiteral("*Snap spacing set to %1*").arg(value);
    }

    std::optional<QString> onOption(const QString& option) override {
        const QString upper = option.trimmed().toUpper();
        if (upper == QLatin1String("ON")) {
            m_view.setGridSnapEnabled(true);
            m_finished = true;
            return QStringLiteral("*Snap on*");
        }
        if (upper == QLatin1String("OFF")) {
            m_view.setGridSnapEnabled(false);
            m_finished = true;
            return QStringLiteral("*Snap off*");
        }
        return std::nullopt;
    }

    bool requestFinish() override {
        m_finished = true;
        return true; // Enter keeps the current snap state
    }

    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    DrawingView& m_view;
    bool m_finished = false;
};
