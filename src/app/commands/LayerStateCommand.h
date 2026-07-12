#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD LAYERSTATE (Layer States Manager), command-line form: pick an
// action (Save/Restore/Delete/?), then a state name. The Layer panel's
// "Layer States..." dialog covers the same operations with a picker UI;
// this exists for scripting/AutoLISP parity, matching how other manager-ish
// features (DATALINK, GEOGRAPHICLOCATION) got a command-line entry point.
class LayerStateCommand : public DrawCommand {
public:
    explicit LayerStateCommand(lcad::Document& document) : m_document(document) {}

    QString start() override {
        return QStringLiteral("LAYERSTATE  Enter option [Save/Restore/Delete/?]:");
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Action, Name };
    enum class Action { Save, Restore, Delete };

    lcad::Document& m_document;
    Stage m_stage = Stage::Action;
    Action m_action = Action::Save;
    bool m_finished = false;
};
