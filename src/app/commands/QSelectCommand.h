#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// A simplified QSELECT: filters the current space's entities by type (typed
// as a DXF-style name, e.g. LINE/CIRCLE/TEXT/INSERT, or * for any) and
// optionally by layer name, then selects every match. AutoCAD's QSELECT
// dialog supports far more properties/operators (color, linetype, any
// numeric property with a comparison operator); this covers its most common
// use (grab everything of a type, optionally on a layer).
class QSelectCommand : public DrawCommand {
public:
    explicit QSelectCommand(lcad::Document& document) : m_document(document) {}

    QString start() override {
        return QStringLiteral("QSELECT  Object type <*> (e.g. LINE, CIRCLE, TEXT, INSERT; * for any):");
    }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }
    std::optional<std::vector<lcad::EntityId>> resultSelection() const override {
        return m_finished ? std::optional(m_matches) : std::nullopt;
    }

private:
    enum class Stage { Type, Layer };

    lcad::Document& m_document;
    Stage m_stage = Stage::Type;
    std::optional<lcad::EntityType> m_typeFilter; // nullopt: any type
    std::vector<lcad::EntityId> m_matches;
    bool m_finished = false;
};
