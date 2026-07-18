#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/pcb/Autorouter.h"
#include "core/pcb/CopperPour.h"
#include "core/pcb/Panelization.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/ViaStitching.h"

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

// GERBER: layer name, then output file path, then an optional netlist
// file path (Enter to skip) -- writes that layer's copper as Gerber (see
// core/pcb/GerberWriter.h). Providing a netlist resolves each footprint
// pad's own net and tags its flash with a real %TO.N% object attribute;
// skipping it writes the file exactly as before (no %TO.N% at all).
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
    enum class Stage { LayerName, Path, NetlistPath };
    lcad::Document& m_document;
    Stage m_stage = Stage::LayerName;
    std::string m_outputPath;
    lcad::LayerId m_layer = 0;
    bool m_finished = false;
};

// COPPERPOUR: select a closed polyline boundary, then a netlist file path
// and a net name (resolves that net's own placed pads as clearance-exempt
// positions the same way RATSNEST does), then grid size and clearance --
// builds an auto-clearance copper pour (see core/pcb/CopperPour.h) on the
// current layer.
class CopperPourCommand : public DrawCommand {
public:
    CopperPourCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override { return QStringLiteral("COPPERPOUR  Select a closed polyline boundary:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::Pick; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Pick, NetlistPath, NetName, GridSize, Clearance, ThermalRelief, AntipadRadius };
    lcad::Document& m_document;
    double m_pickTolerance;
    Stage m_stage = Stage::Pick;
    std::vector<lcad::Point2D> m_boundary;
    std::vector<lcad::ImportedNet> m_nets;
    std::vector<lcad::Point2D> m_ownNetPositions;
    double m_gridSize = 0.5;
    double m_clearance = 0.2;
    lcad::ThermalReliefParams m_thermalRelief;
    bool m_finished = false;
};

// VIASTITCH: select a closed polyline boundary (the same pour-boundary
// picking CopperPourCommand uses above), then via spacing, inset, pad
// diameter, and drill diameter -- stitches ground/net-tie vias along it
// on the current layer (see core/pcb/ViaStitching.h).
class ViaStitchCommand : public DrawCommand {
public:
    ViaStitchCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override { return QStringLiteral("VIASTITCH  Select a closed polyline boundary:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::Pick; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Pick, Spacing, Inset, Diameter, Drill };
    lcad::Document& m_document;
    double m_pickTolerance;
    Stage m_stage = Stage::Pick;
    std::vector<lcad::Point2D> m_boundary;
    double m_spacing = 2.0;
    double m_inset = 1.0;
    double m_diameter = 0.6;
    double m_drillDiameter = 0.3;
    bool m_finished = false;
};

// PANELIZE: select a closed polyline boundary (the single board's own
// outline, same picking convention COPPERPOUR/VIASTITCH use above), then
// columns, rows, gap, and separator style -- gangs the board up into an
// NxM panel with V-score or mouse-bite separators (see
// core/pcb/Panelization.h).
class PanelizeCommand : public DrawCommand {
public:
    PanelizeCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override { return QStringLiteral("PANELIZE  Select a closed polyline board boundary:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::Pick; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Pick, Columns, Rows, Gap, Separator, MouseBiteDiameter, MouseBiteSpacing };
    lcad::Document& m_document;
    double m_pickTolerance;
    Stage m_stage = Stage::Pick;
    std::vector<lcad::Point2D> m_boundary;
    lcad::PanelizeParams m_params;
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

// AUTOROUTE: netlist file path, then grid size / track width / clearance
// (each with a default) -- runs the in-house Lee/maze grid autorouter (see
// core/pcb/Autorouter.h) on the current layer for every net in that
// netlist, adding one TrackEntity per successfully routed connection.
class AutorouteCommand : public DrawCommand {
public:
    explicit AutorouteCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("AUTOROUTE  Enter netlist file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { NetlistPath, GridSize, TrackWidth, Clearance, Layers, NetClasses };
    lcad::Document& m_document;
    Stage m_stage = Stage::NetlistPath;
    std::vector<lcad::ImportedNet> m_nets;
    lcad::AutorouteParams m_params;
    std::vector<lcad::NetClass> m_netClasses;
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

// FOOTPRINTGEN: footprint name, then a family + parameters line (see
// core/pcb/FootprintGenerator.h) -- generates a real IPC-style parametric
// footprint instead of a one-off hardcoded one. Format:
//   QFP:pinCount:pitch:bodyWidth:bodyLength:padWidth:padLength
//   SOIC:pinCount:pitch:bodyWidth:bodyLength:padWidth:padLength
//   HEADER:pinCount:rowCount:pitch
class FootprintGenCommand : public DrawCommand {
public:
    explicit FootprintGenCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("FOOTPRINTGEN  Enter new footprint name:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Name, FamilyAndParams };
    lcad::Document& m_document;
    Stage m_stage = Stage::Name;
    std::string m_name;
    bool m_finished = false;
};
