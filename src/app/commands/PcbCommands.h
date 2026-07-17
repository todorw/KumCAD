#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/pcb/Ratsnest.h"

#include <vector>

// TRACK: collects vertices as the user clicks, committing a single
// TrackEntity (one undo step) when finished, same shape as WIRE but with a
// copper width.
class TrackCommand : public DrawCommand {
public:
    explicit TrackCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    bool requestFinish() override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_points.empty() ? std::nullopt : std::optional<lcad::Point2D>(m_points.back());
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::Point2D> m_points;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};

// VIA: places a via per click until Enter.
class ViaCommand : public DrawCommand {
public:
    explicit ViaCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("VIA  Specify a point (Enter to finish):"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// RATSNEST: reads a netlist file path (typically exported by NETLIST from a
// schematic document -- see core/pcb/Ratsnest.h for why this is file-based
// rather than a live cross-document link) and reports the unrouted
// connections it implies for the pads already placed in this board
// document.
class RatsnestCommand : public DrawCommand {
public:
    explicit RatsnestCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("RATSNEST  Enter netlist file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// GERBER: layer name, then output file path -- writes that layer's copper
// as Gerber (see core/pcb/GerberWriter.h).
class GerberExportCommand : public DrawCommand {
public:
    explicit GerberExportCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("GERBER  Enter layer name:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { LayerName, Path };
    lcad::Document& m_document;
    Stage m_stage = Stage::LayerName;
    lcad::LayerId m_layer = 0;
    bool m_finished = false;
};

// DSNEXPORT: netlist file path, then output file path -- writes a
// Specctra DSN file (see core/pcb/SpecctraWriter.h) for external
// autorouters (FreeRouting etc.) instead of an in-house autorouter.
class DsnExportCommand : public DrawCommand {
public:
    explicit DsnExportCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("DSNEXPORT  Enter netlist file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { NetlistPath, OutputPath };
    lcad::Document& m_document;
    Stage m_stage = Stage::NetlistPath;
    std::vector<lcad::ImportedNet> m_nets;
    bool m_finished = false;
};

// DRILL: output file path -- writes an Excellon drill file for the whole
// board (see core/pcb/GerberWriter.h).
class DrillExportCommand : public DrawCommand {
public:
    explicit DrillExportCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("DRILL  Enter output file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// PNP: output file path -- writes a pick-and-place CSV (see
// core/pcb/GerberWriter.h).
class PickAndPlaceCommand : public DrawCommand {
public:
    explicit PickAndPlaceCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("PNP  Enter output file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};
