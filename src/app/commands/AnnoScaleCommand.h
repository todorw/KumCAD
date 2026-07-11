#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// A simplified CANNOSCALE: sets the document-wide annotation scale that
// Annotative text styles (see StyleCommand) render at. Takes a single typed
// positive number; Enter keeps the current value. Mirrors LtScaleCommand.
class AnnoScaleCommand : public DrawCommand {
public:
    explicit AnnoScaleCommand(lcad::Document& document) : m_document(document) {}

    QString start() override {
        return QStringLiteral("ANNOSCALE  Enter new annotation scale <%1>:").arg(m_document.annotationScale());
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value <= 0) return QStringLiteral("*Scale must be positive*");
        m_document.setAnnotationScale(value);
        m_finished = true;
        return QStringLiteral("*ANNOSCALE set to %1*").arg(value);
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
