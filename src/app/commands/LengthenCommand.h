#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style LENGTHEN for lines and arcs. Picking an object with no mode
// set reports its length; DElta/Percent/Total (typed as DE/P/T) set how the
// following numeric value changes each picked object, applied to the end
// nearer the pick. Each pick is one undo step.
class LengthenCommand : public DrawCommand {
public:
    LengthenCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    std::optional<QString> onOption(const QString& option) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Mode { None, Delta, Percent, Total };

    QString prompt() const;

    lcad::Document& m_document;
    double m_pickTolerance;
    Mode m_mode = Mode::None;
    bool m_awaitingValue = false;
    double m_value = 0.0;
    bool m_finished = false;
};
