#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// Interactive Express Tools / layer tools (see core/document/ExpressTools.h
// for the testable logic each of these wraps). The one-shot selection-based
// tools (BURST, TXT2MTXT, TORIENT, LAYISO/LAYOFF/...) live directly in
// CommandDispatcher like EXPLODE/OVERKILL; these are the ones that need a
// prompt sequence.

// TCOUNT: numbers the selected TEXT/MTEXT entities in reading order.
class TCountCommand : public DrawCommand {
public:
    TCountCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override { return QStringLiteral("TCOUNT  Start number <1>:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    void apply(const QString& placementOption);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    int m_stage = 0; // 0 = start number, 1 = increment, 2 = placement
    int m_start = 1;
    int m_increment = 1;
    std::optional<QString> m_result;
    bool m_finished = false;
};

// BREAKLINE: a polyline from two picked points with the zigzag break symbol
// at the midpoint.
class BreaklineCommand : public DrawCommand {
public:
    explicit BreaklineCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("BREAKLINE  First point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage == 2; }
    std::optional<QString> onText(const QString& text) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    void onPreviewPoint(const lcad::Point2D& pt) override { m_preview = pt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    void build(double size);

    lcad::Document& m_document;
    int m_stage = 0; // 0 = first point, 1 = second point, 2 = size
    lcad::Point2D m_a;
    lcad::Point2D m_b;
    std::optional<lcad::Point2D> m_preview;
    std::optional<QString> m_result;
    bool m_finished = false;
};

// LAYMRG: moves everything from one layer onto another, then deletes the
// source layer. The entity moves are undoable; the layer-record deletion is
// not (matching PURGE).
class LayMrgCommand : public DrawCommand {
public:
    explicit LayMrgCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("LAYMRG  Source layer name:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    int m_stage = 0;
    QString m_sourceName;
    std::optional<QString> m_result;
    bool m_finished = false;
};

// LAYDEL: deletes a layer and everything on it (entity deletion undoable,
// layer record not -- same convention as LAYMRG above).
class LayDelCommand : public DrawCommand {
public:
    explicit LayDelCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("LAYDEL  Layer name to delete:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::optional<QString> m_result;
    bool m_finished = false;
};

// COPYTOLAYER: copies the selection onto another layer (created on demand),
// originals untouched.
class CopyToLayerCommand : public DrawCommand {
public:
    CopyToLayerCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)) {}

    QString start() override { return QStringLiteral("COPYTOLAYER  Target layer name:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D&) override { return std::nullopt; }
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    std::optional<QString> m_result;
    bool m_finished = false;
};
