#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD POINT: places a point node per click until Enter.
class PointCommand : public DrawCommand {
public:
    explicit PointCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("POINT  Specify a point (Enter to finish):"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// PDMODE/PDSIZE in one walk: point marker style and size (PTYPE-lite).
class PdModeCommand : public DrawCommand {
public:
    explicit PdModeCommand(lcad::Document& document) : m_document(document) {}

    QString start() override {
        return QStringLiteral(
                   "PDMODE  Marker style: 0 dot, 2 plus, 3 X, 4 tick; +32 circles it.\nEnter mode <%1>:")
            .arg(m_document.pointMode());
    }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Mode, Size };

    lcad::Document& m_document;
    Stage m_stage = Stage::Mode;
    bool m_finished = false;
};

// AutoCAD DIVIDE/MEASURE: place point nodes along a picked curve — DIVIDE
// into n equal parts, MEASURE every fixed distance from the start.
class DivideCommand : public DrawCommand {
public:
    DivideCommand(lcad::Document& document, double pickTolerance, bool measure)
        : m_document(document), m_pickTolerance(pickTolerance), m_measure(measure) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    double m_pickTolerance;
    bool m_measure;
    lcad::EntityId m_targetId = 0;
    bool m_finished = false;
};
