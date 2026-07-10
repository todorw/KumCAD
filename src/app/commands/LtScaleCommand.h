#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD's LTSCALE: sets the global linetype pattern scale. Takes a single
// typed positive number; Enter keeps the current value.
class LtScaleCommand : public DrawCommand {
public:
    explicit LtScaleCommand(lcad::Document& document) : m_document(document) {}

    QString start() override {
        return QStringLiteral("LTSCALE  Enter new linetype scale <%1>:").arg(m_document.lineTypeScale());
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value <= 0) return QStringLiteral("*Scale must be positive*");
        m_document.setLineTypeScale(value);
        m_finished = true;
        return QStringLiteral("*LTSCALE set to %1*").arg(value);
    }

    bool requestFinish() override {
        m_finished = true;
        return true; // Enter keeps the current scale
    }

    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};
