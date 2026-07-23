#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style BOUNDARY/BPOLY: pick internal points, derive each one's
// enclosing boundary from whatever Line/Polyline/Arc/Circle entities
// surround it (the same HatchBoundary.h detection HATCH/GRADIENT already
// use), then ask which object type to create from the picked boundaries --
// a closed Polyline (the default) or a Region -- matching real AutoCAD's
// own BOUNDARY dialog's "Object type" choice. One undo step. Enter with at
// least one boundary picked accepts the Polyline default.
class BoundaryCommand : public DrawCommand {
public:
    explicit BoundaryCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onOption(const QString& option) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override {
        if (m_finished) return m_result;
        if (m_stage == Stage::ObjectType) {
            return QStringLiteral("Object type [Polyline/Region] <Polyline>:");
        }
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BoundaryPick, ObjectType };
    enum class ObjectType { Polyline, Region };

    void commit();

    lcad::Document& m_document;
    std::vector<std::vector<lcad::Point2D>> m_pickedBoundaries;
    Stage m_stage = Stage::BoundaryPick;
    ObjectType m_objectType = ObjectType::Polyline;
    std::optional<QString> m_result;
    bool m_finished = false;
};
