#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style MIRROR: acts on a pre-existing selection. Two points define
// the mirror line, then "Erase source objects?" decides copy vs. move. The
// whole result lands as one undoable step.
class MirrorCommand : public DrawCommand {
public:
    MirrorCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_hasFirst ? std::optional<lcad::Point2D>(m_first) : std::nullopt;
    }
    bool wantsTextInput() const override { return m_hasSecond; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    void commit(bool eraseSource);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    lcad::Point2D m_first;
    lcad::Point2D m_second;
    bool m_hasFirst = false;
    bool m_hasSecond = false;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
