#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// REVCLOUD (Object mode): converts a selected Polyline or Circle into a
// revision cloud (see core/geometry/PolylineOps.h's revisionCloud) --
// AutoCAD's own "Object" REVCLOUD option, not its freehand-drag mode.
class RevcloudCommand : public DrawCommand {
public:
    RevcloudCommand(lcad::Document& document, lcad::EntityId targetId)
        : m_document(document), m_targetId(targetId) {}

    QString start() override;
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::EntityId m_targetId;
    std::optional<QString> m_result;
    bool m_finished = false;
};
