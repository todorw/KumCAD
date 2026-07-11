#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Commands.h"
#include "core/document/Document.h"

#include <utility>
#include <vector>

// LWEIGHT (lite): sets a lineweight override in mm on the current selection
// (0 restores ByLayer). LWDISPLAY toggles whether weights show on screen.
class LweightCommand : public DrawCommand {
public:
    LweightCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override {
        return QStringLiteral("LWEIGHT  Enter lineweight in mm for %1 object(s), 0 = ByLayer:").arg(m_ids.size());
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }

    std::optional<QString> onScalar(double value) override {
        if (value < 0 || value > 5.0) return QStringLiteral("*Lineweight must be 0..5 mm*");
        std::optional<double> weight;
        if (value > 1e-9) weight = value;
        m_document.commandStack().execute(
            std::make_unique<lcad::SetEntityLineweightCommand>(m_document, m_ids, weight));
        m_finished = true;
        return weight ? QStringLiteral("*Lineweight set to %1 mm (LWDISPLAY shows it)*").arg(value)
                      : QStringLiteral("*Lineweight restored to ByLayer*");
    }

    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    bool m_finished = false;
};
