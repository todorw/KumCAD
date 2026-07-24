#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style DIMEDIT on a pre-selected set of dimensions: [New] types
// replacement text (real AutoCAD's own "<>" placeholder substitutes the
// real auto-measured value -- see DimensionEntity::textOverride's own
// comment), [Rotate] types a text angle in degrees -- applied to every
// selected dimension in one undo step. Real, disclosed scope: Home and
// Oblique (extension-line skew, not modeled by Geometry at all) aren't
// implemented.
class DimEditCommand : public DrawCommand {
public:
    DimEditCommand(lcad::Document& document, std::vector<lcad::EntityId> dimensionIds)
        : m_document(document), m_dimensionIds(std::move(dimensionIds)) {}

    QString start() override {
        return QStringLiteral("DIMEDIT  Enter type of dimension editing [New/Rotate] <New>:");
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
    enum class Stage { Mode, NewText, RotateAngle };
    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_dimensionIds;
    Stage m_stage = Stage::Mode;
    bool m_finished = false;
};

// AutoCAD-style DIMTEDIT on a single pre-selected dimension: a point
// click repositions its text (moves the same linePoint grip that's
// already draggable interactively -- DIMTEDIT is real AutoCAD's own
// dedicated command for exactly that, not new geometry), or a typed
// number sets its text rotation angle in degrees.
class DimTeditCommand : public DrawCommand {
public:
    DimTeditCommand(lcad::Document& document, lcad::EntityId dimensionId)
        : m_document(document), m_dimensionId(dimensionId) {}

    QString start() override {
        return QStringLiteral("DIMTEDIT  Specify new location for dimension text, or enter an angle:");
    }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::EntityId m_dimensionId;
    bool m_finished = false;
};
