#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <string>

// The BPARAMETER/BFLIP/BROTATION/BARRAY/BVISIBILITY/BLOOKUP family: each
// adds one dynamic-block parameter kind (see Block.h) to an existing block,
// AutoCAD's Block Editor simplified into single typed commands instead of a
// separate editing mode. None are undoable, like other block/layer
// management commands (BLOCK, PURGE).

// BPARAMETER: a linear stretch parameter (see DynamicLinearParameter).
// Prompts for the block name, then the parameter's base and end points, then
// the two corners of the stretch frame (a crossing window: child vertices
// inside it move with the end-point grip, like STRETCH).
class BlockParamCommand : public DrawCommand {
public:
    explicit BlockParamCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BPARAMETER  Enter block name to make dynamic:"); }
    bool wantsTextInput() const override { return m_stage == Stage::BlockName; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, BasePoint, EndPoint, FrameCorner1, FrameCorner2 };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    lcad::Point2D m_basePoint;
    lcad::Point2D m_endPoint;
    lcad::Point2D m_frameCorner1;
    bool m_finished = false;
};

// BFLIP: adds a flip parameter (DynamicFlipParameter) -- block name, then
// the flip line's base point and endpoint.
class BlockFlipCommand : public DrawCommand {
public:
    explicit BlockFlipCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BFLIP  Enter block name to add a flip parameter:"); }
    bool wantsTextInput() const override { return m_stage == Stage::BlockName; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, BasePoint, EndPoint };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    lcad::Point2D m_basePoint;
    bool m_finished = false;
};

// BROTATION: adds a rotation parameter (DynamicRotationParameter) -- block
// name, the rotation base point, then a point setting the grip's default
// radius.
class BlockRotationCommand : public DrawCommand {
public:
    explicit BlockRotationCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BROTATION  Enter block name to add a rotation parameter:"); }
    bool wantsTextInput() const override { return m_stage == Stage::BlockName; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, BasePoint, RadiusPoint };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    lcad::Point2D m_basePoint;
    bool m_finished = false;
};

// BARRAY: adds an array parameter (DynamicArrayParameter) -- block name,
// base point, then a second point setting the item direction and spacing.
class BlockArrayCommand : public DrawCommand {
public:
    explicit BlockArrayCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BARRAY  Enter block name to add an array parameter:"); }
    bool wantsTextInput() const override { return m_stage == Stage::BlockName; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, BasePoint, SecondPoint };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    lcad::Point2D m_basePoint;
    bool m_finished = false;
};

// BVISIBILITY: adds a visibility parameter (DynamicVisibilityParameter).
// Since KumCAD has no block-editor edit-in-place mode to click a block's own
// children, states are built entirely from typed input: the block's
// children are listed once (index: type), then each state is a name
// followed by a comma-separated list of visible indices (or "all"), looped
// until an empty state name ends it.
class BlockVisibilityCommand : public DrawCommand {
public:
    explicit BlockVisibilityCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BVISIBILITY  Enter block name to add a visibility parameter:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, StateName, EntityIndices };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    lcad::DynamicVisibilityParameter m_draft;
    std::string m_pendingStateName;
    bool m_finished = false;
};

// BLOOKUP: adds a lookup parameter (DynamicLookupParameter) -- a named list
// of scale-factor presets, entered as repeated (name, scale factor) pairs
// until an empty name ends the loop.
class BlockLookupCommand : public DrawCommand {
public:
    explicit BlockLookupCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BLOOKUP  Enter block name to add a lookup parameter:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, ValueName, ScaleFactor };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    lcad::DynamicLookupParameter m_draft;
    std::string m_pendingValueName;
    bool m_finished = false;
};
