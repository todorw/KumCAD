#pragma once

#include "commands/DrawCommand.h"
#include "DrawingView.h"

// AutoCAD's POLARANG: sets the polar tracking increment angle in degrees.
// Takes a single typed number in (0.5, 90]; Enter keeps the current value.
class PolarAngCommand : public DrawCommand {
public:
    explicit PolarAngCommand(DrawingView& view) : m_view(view) {}

    QString start() override {
        return QStringLiteral("POLARANG  Enter polar angle increment in degrees <%1>:")
            .arg(m_view.polarIncrementDeg());
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value <= 0.5 || value > 90.0) return QStringLiteral("*Angle must be in (0.5, 90] degrees*");
        m_view.setPolarIncrementDeg(value);
        m_finished = true;
        return QStringLiteral("*POLARANG set to %1*").arg(value);
    }

    bool requestFinish() override {
        m_finished = true;
        return true;
    }

    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    DrawingView& m_view;
    bool m_finished = false;
};
