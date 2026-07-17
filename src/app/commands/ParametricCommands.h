#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/document/DocumentConstraints.h"

#include <cmath>

namespace lcad {

// DCRADIUS: dimensional constraint pinning a selected circle's own radius
// to a typed value -- distinct from DIMRADIUS (a plain, one-off Radius
// annotation entity): this actually re-solves the circle (and anything
// else already tied to it) via core/document/DocumentConstraints.h.
class DcRadiusCommand : public DrawCommand {
public:
    DcRadiusCommand(Document& document, EntityId circleId) : m_document(document), m_circleId(circleId) {}

    QString start() override { return QStringLiteral("DCRADIUS  Radius value:"); }
    std::optional<QString> onPoint(const Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override {
        m_finished = true;
        bool ok = false;
        const double value = text.trimmed().toDouble(&ok);
        if (!ok || value <= 0.0) return QStringLiteral("*Invalid radius*");

        DocumentConstraint constraint;
        constraint.type = SketchConstraintType::Radius;
        constraint.geomA = m_circleId;
        constraint.value = value;
        const DocumentConstraintResult result = solveDocumentConstraints(m_document, {constraint});
        return result.converged
                 ? QStringLiteral("*Radius constraint applied*")
                 : QStringLiteral("*Solved with residual %1 -- may not be fully satisfied*").arg(result.finalResidualNorm);
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    Document& m_document;
    EntityId m_circleId;
    bool m_finished = false;
};

// DCANGULAR: dimensional constraint pinning the angle between two
// already-selected lines to a typed value (degrees).
class DcAngularCommand : public DrawCommand {
public:
    DcAngularCommand(Document& document, EntityId lineAId, EntityId lineBId)
        : m_document(document), m_lineAId(lineAId), m_lineBId(lineBId) {}

    QString start() override { return QStringLiteral("DCANGULAR  Angle value (degrees):"); }
    std::optional<QString> onPoint(const Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override {
        m_finished = true;
        bool ok = false;
        const double degrees = text.trimmed().toDouble(&ok);
        if (!ok) return QStringLiteral("*Invalid angle*");

        DocumentConstraint constraint;
        constraint.type = SketchConstraintType::Angle;
        constraint.geomA = m_lineAId;
        constraint.geomB = m_lineBId;
        constraint.value = degrees * M_PI / 180.0;
        const DocumentConstraintResult result = solveDocumentConstraints(m_document, {constraint});
        return result.converged
                 ? QStringLiteral("*Angle constraint applied*")
                 : QStringLiteral("*Solved with residual %1 -- may not be fully satisfied*").arg(result.finalResidualNorm);
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    Document& m_document;
    EntityId m_lineAId, m_lineBId;
    bool m_finished = false;
};

} // namespace lcad
