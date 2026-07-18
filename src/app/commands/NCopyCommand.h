#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// NCOPY (Express Tools): copies ONE nested entity out of a placed block
// (INSERT) into model space as an ordinary top-level entity, without
// exploding the whole block instance. This app's selection system only
// hit-tests top-level entities (an INSERT is one atomic pick, not its
// individual children), so -- unlike real AutoCAD's click-into-a-block
// picking -- the nested entity is chosen by a typed index into a listed
// report, the same "typed index instead of interactive sub-pick"
// convention SketchFeatureDialog's Fillet/Chamfer/Shell edge/face
// selection already established this session.
class NCopyCommand : public DrawCommand {
public:
    NCopyCommand(lcad::Document& document, lcad::EntityId insertId)
        : m_document(document), m_insertId(insertId) {}

    QString start() override;
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::EntityId m_insertId;
    std::optional<QString> m_result;
    bool m_finished = false;
};
