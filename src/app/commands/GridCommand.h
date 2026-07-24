#pragma once

#include "commands/DrawCommand.h"
#include "DrawingView.h"

// AutoCAD's GRID: reports the current on/off state and spacing, or sets
// them. Typing ON/OFF toggles grid display (same effect as F9's menu
// sibling, the View > Grid Snap toggle, but GRID's own on/off is display-only
// -- it does NOT touch snap, matching real AutoCAD where GRID and SNAP are
// independent). Typing a positive number sets an explicit spacing,
// overriding the view's normal auto zoom-based spacing; Enter with no input
// keeps the current spacing and finishes.
class GridCommand : public DrawCommand {
public:
    explicit GridCommand(DrawingView& view) : m_view(view) {}

    QString start() override {
        return QStringLiteral("GRID  Specify grid spacing or [ON/OFF] <%1, currently %2>:")
            .arg(m_view.effectiveGridSpacing())
            .arg(m_view.gridVisible() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value <= 0) return QStringLiteral("*Spacing must be positive*");
        m_view.setGridSpacingOverride(value);
        m_view.setGridVisible(true);
        m_finished = true;
        return QStringLiteral("*Grid spacing set to %1*").arg(value);
    }

    std::optional<QString> onOption(const QString& option) override {
        const QString upper = option.trimmed().toUpper();
        if (upper == QLatin1String("ON")) {
            m_view.setGridVisible(true);
            m_finished = true;
            return QStringLiteral("*Grid on*");
        }
        if (upper == QLatin1String("OFF")) {
            m_view.setGridVisible(false);
            m_finished = true;
            return QStringLiteral("*Grid off*");
        }
        return std::nullopt;
    }

    bool requestFinish() override {
        m_finished = true;
        return true; // Enter keeps the current grid state
    }

    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    DrawingView& m_view;
    bool m_finished = false;
};
