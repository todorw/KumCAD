#include "commands/PcbCommands.h"

#include "core/document/Commands.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/io/KiCadPcb.h"
#include "core/io/KiCadSch.h"
#include "core/pcb/Autorouter.h"
#include "core/pcb/CopperPour.h"
#include "core/pcb/FootprintGenerator.h"
#include "core/pcb/GerberWriter.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/SpecctraWriter.h"

#include <QStringList>

#include <algorithm>
#include <fstream>
#include <sstream>

QString TrackCommand::start() {
    return QStringLiteral("TRACK  Specify first point:");
}

std::optional<QString> TrackCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_points.empty() && (pt - m_points.back()).length() < 1e-12) {
        return QStringLiteral("Specify next point or [Enter to finish]:");
    }
    m_points.push_back(pt);
    return QStringLiteral("Specify next point or [Enter to finish]:");
}

void TrackCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> TrackCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) segs.emplace_back(m_points[i], m_points[i + 1]);
    if (!m_points.empty() && m_hasPreview) segs.emplace_back(m_points.back(), m_previewPoint);
    return segs;
}

bool TrackCommand::requestFinish() {
    m_finished = true;
    if (m_points.size() < 2) return false;
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::TrackEntity>(id, m_document.currentLayer(), m_points);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    return true;
}

std::optional<QString> ViaCommand::onPoint(const lcad::Point2D& pt) {
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::ViaEntity>(m_document.reserveEntityId(), m_document.currentLayer(), pt)));
    return QStringLiteral("Specify a point (Enter to finish):");
}

std::optional<QString> RatsnestCommand::onText(const QString& text) {
    m_finished = true;
    const std::string path = text.trimmed().toStdString();
    std::ifstream in(path, std::ios::binary);
    if (!in) return QStringLiteral("*Could not open %1*").arg(text.trimmed());

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::vector<lcad::ImportedNet> nets = lcad::parseNetlist(buffer.str());
    const std::vector<lcad::RatsnestLine> lines = lcad::computeRatsnest(m_document, nets);

    double totalLength = 0.0;
    for (const auto& line : lines) totalLength += line.a.distanceTo(line.b);
    return QStringLiteral("*Ratsnest: %1 net(s) read, %2 unrouted connection(s), %3 units total*")
        .arg(nets.size())
        .arg(lines.size())
        .arg(totalLength, 0, 'f', 2);
}

std::optional<QString> CopperPourCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Pick) return std::nullopt;

    const lcad::PolylineEntity* best = nullptr;
    double bestDist = m_pickTolerance;
    for (const lcad::Entity* e : m_document.entities()) {
        if (e->type() != lcad::EntityType::Polyline) continue;
        const auto* pl = static_cast<const lcad::PolylineEntity*>(e);
        if (!pl->closed()) continue;
        const double d = pl->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = pl;
        }
    }
    if (!best) return QStringLiteral("*No closed polyline there*\nSelect a closed polyline boundary:");

    m_boundary = best->flattenedVertices();
    m_stage = Stage::NetlistPath;
    return QStringLiteral("Enter netlist file path:");
}

std::optional<QString> CopperPourCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::NetlistPath: {
        std::ifstream in(text.trimmed().toStdString(), std::ios::binary);
        if (!in) return QStringLiteral("*Could not open %1*\nEnter netlist file path:").arg(text.trimmed());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        m_nets = lcad::parseNetlist(buffer.str());
        m_stage = Stage::NetName;
        return QStringLiteral("Enter net name to pour:");
    }
    case Stage::NetName: {
        const std::string name = text.trimmed().toStdString();
        const auto netIt = std::find_if(m_nets.begin(), m_nets.end(), [&](const lcad::ImportedNet& n) { return n.name == name; });
        if (netIt == m_nets.end()) return QStringLiteral("*No net named \"%1\" in that netlist*").arg(text.trimmed());

        for (const lcad::ImportedNetPin& pin : netIt->pins) {
            for (const lcad::Entity* e : m_document.entities()) {
                if (e->type() != lcad::EntityType::Insert) continue;
                const auto* insert = static_cast<const lcad::InsertEntity*>(e);
                if (!insert->block() || !insert->block()->isFootprint()) continue;
                const std::string* refdes = insert->attributeValue("REFDES");
                if (!refdes || *refdes != pin.refDes) continue;
                for (const auto& padWorld : insert->padWorldPositions()) {
                    if (padWorld.pad->number == pin.pinNumber) m_ownNetPositions.push_back(padWorld.position);
                }
            }
        }
        m_stage = Stage::GridSize;
        return QStringLiteral("Grid size <0.5>:");
    }
    case Stage::GridSize: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid grid size*");
            m_gridSize = value;
        }
        m_stage = Stage::Clearance;
        return QStringLiteral("Clearance <0.2>:");
    }
    case Stage::Clearance: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            m_clearance = text.trimmed().toDouble(&ok);
            if (!ok || m_clearance < 0.0) return QStringLiteral("*Invalid clearance*");
        }
        m_stage = Stage::ThermalRelief;
        return QStringLiteral("Thermal relief on own-net pads? [Yes/No] <No>:");
    }
    case Stage::ThermalRelief: {
        const QString option = text.trimmed().toUpper();
        if (option.isEmpty() || option == QLatin1String("N") || option == QLatin1String("NO")) {
            m_thermalRelief.enabled = false;
        } else if (option == QLatin1String("Y") || option == QLatin1String("YES")) {
            m_thermalRelief.enabled = true;
        } else {
            return QStringLiteral("*Invalid option, expected Yes/No*");
        }
        if (!m_thermalRelief.enabled) {
            m_finished = true;
        } else {
            m_stage = Stage::AntipadRadius;
            return QStringLiteral("Antipad radius <0.6>:");
        }
        break;
    }
    case Stage::AntipadRadius: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid antipad radius*");
            m_thermalRelief.antipadRadius = value;
        }
        m_finished = true;
        break;
    }
    default:
        return std::nullopt;
    }

    const auto ids = lcad::buildCopperPourWithClearance(m_document, m_document.currentLayer(), m_boundary,
                                                         m_ownNetPositions, m_gridSize, m_clearance, m_thermalRelief);
    if (ids.empty()) return QStringLiteral("*Pour produced no copper -- check boundary/grid size*");
    return QStringLiteral("*Copper pour: %1 piece(s)*").arg(ids.size());
}

std::optional<QString> ViaStitchCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Pick) return std::nullopt;

    const lcad::PolylineEntity* best = nullptr;
    double bestDist = m_pickTolerance;
    for (const lcad::Entity* e : m_document.entities()) {
        if (e->type() != lcad::EntityType::Polyline) continue;
        const auto* pl = static_cast<const lcad::PolylineEntity*>(e);
        if (!pl->closed()) continue;
        const double d = pl->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = pl;
        }
    }
    if (!best) return QStringLiteral("*No closed polyline there*\nSelect a closed polyline boundary:");

    m_boundary = best->flattenedVertices();
    m_stage = Stage::Spacing;
    return QStringLiteral("Via spacing <2.0>:");
}

std::optional<QString> ViaStitchCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::Spacing: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid spacing*");
            m_spacing = value;
        }
        m_stage = Stage::Inset;
        return QStringLiteral("Inset from boundary <1.0>:");
    }
    case Stage::Inset: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value < 0.0) return QStringLiteral("*Invalid inset*");
            m_inset = value;
        }
        m_stage = Stage::Diameter;
        return QStringLiteral("Via diameter <0.6>:");
    }
    case Stage::Diameter: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid diameter*");
            m_diameter = value;
        }
        m_stage = Stage::Drill;
        return QStringLiteral("Drill diameter <0.3>:");
    }
    case Stage::Drill: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0 || value >= m_diameter) return QStringLiteral("*Invalid drill diameter*");
            m_drillDiameter = value;
        }
        m_finished = true;
        break;
    }
    default:
        return std::nullopt;
    }

    const auto ids = lcad::stitchVias(m_document, m_document.currentLayer(), m_boundary, m_spacing, m_inset,
                                      m_diameter, m_drillDiameter);
    if (ids.empty()) return QStringLiteral("*No vias placed -- check boundary/spacing*");
    return QStringLiteral("*Via stitching: %1 via(s) placed*").arg(ids.size());
}

std::optional<QString> PanelizeCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Pick) return std::nullopt;

    const lcad::PolylineEntity* best = nullptr;
    double bestDist = m_pickTolerance;
    for (const lcad::Entity* e : m_document.entities()) {
        if (e->type() != lcad::EntityType::Polyline) continue;
        const auto* pl = static_cast<const lcad::PolylineEntity*>(e);
        if (!pl->closed()) continue;
        const double d = pl->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = pl;
        }
    }
    if (!best) return QStringLiteral("*No closed polyline there*\nSelect a closed polyline board boundary:");

    m_boundary = best->flattenedVertices();
    m_stage = Stage::Columns;
    return QStringLiteral("Columns <2>:");
}

std::optional<QString> PanelizeCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::Columns: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const int value = text.trimmed().toInt(&ok);
            if (!ok || value < 1) return QStringLiteral("*Invalid column count*");
            m_params.columns = value;
        }
        m_stage = Stage::Rows;
        return QStringLiteral("Rows <1>:");
    }
    case Stage::Rows: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const int value = text.trimmed().toInt(&ok);
            if (!ok || value < 1) return QStringLiteral("*Invalid row count*");
            m_params.rows = value;
        }
        m_stage = Stage::Gap;
        return QStringLiteral("Gap between boards <2.0>:");
    }
    case Stage::Gap: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value < 0.0) return QStringLiteral("*Invalid gap*");
            m_params.gap = value;
        }
        m_stage = Stage::Separator;
        return QStringLiteral("Separator [Vscore/Mousebites/None] <Vscore>:");
    }
    case Stage::Separator: {
        const QString option = text.trimmed().toUpper();
        if (option.isEmpty() || option == QLatin1String("VSCORE") || option == QLatin1String("V")) {
            m_params.separator = lcad::PanelSeparator::VScore;
        } else if (option == QLatin1String("MOUSEBITES") || option == QLatin1String("M")) {
            m_params.separator = lcad::PanelSeparator::MouseBites;
        } else if (option == QLatin1String("NONE") || option == QLatin1String("N")) {
            m_params.separator = lcad::PanelSeparator::None;
        } else {
            return QStringLiteral("*Invalid option, expected Vscore/Mousebites/None*");
        }
        if (m_params.separator != lcad::PanelSeparator::MouseBites) {
            m_finished = true;
        } else {
            m_stage = Stage::MouseBiteDiameter;
            return QStringLiteral("Mouse-bite hole diameter <0.5>:");
        }
        break;
    }
    case Stage::MouseBiteDiameter: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid hole diameter*");
            m_params.mouseBiteHoleDiameter = value;
        }
        m_stage = Stage::MouseBiteSpacing;
        return QStringLiteral("Mouse-bite hole spacing <1.0>:");
    }
    case Stage::MouseBiteSpacing: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid hole spacing*");
            m_params.mouseBiteSpacing = value;
        }
        m_finished = true;
        break;
    }
    default:
        return std::nullopt;
    }

    const auto ids = lcad::panelizeBoard(m_document, m_boundary, m_params);
    if (ids.empty()) return QStringLiteral("*Panelization produced nothing -- check boundary*");
    return QStringLiteral("*Panelized: %1 x %2 (%3 entities added)*").arg(m_params.columns).arg(m_params.rows).arg(ids.size());
}

std::optional<QString> AutorouteCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::NetlistPath: {
        std::ifstream in(text.trimmed().toStdString(), std::ios::binary);
        if (!in) return QStringLiteral("*Could not open %1*\nEnter netlist file path:").arg(text.trimmed());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        m_nets = lcad::parseNetlist(buffer.str());
        m_params.layer = m_document.currentLayer();
        m_stage = Stage::GridSize;
        return QStringLiteral("Grid size <0.5>:");
    }
    case Stage::GridSize: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid grid size*");
            m_params.gridSize = value;
        }
        m_stage = Stage::TrackWidth;
        return QStringLiteral("Track width <0.25>:");
    }
    case Stage::TrackWidth: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value <= 0.0) return QStringLiteral("*Invalid track width*");
            m_params.trackWidth = value;
        }
        m_stage = Stage::Clearance;
        return QStringLiteral("Clearance <0.2>:");
    }
    case Stage::Clearance: {
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (!ok || value < 0.0) return QStringLiteral("*Invalid clearance*");
            m_params.clearance = value;
        }
        m_stage = Stage::Layers;
        return QStringLiteral("Layer stackup for via insertion, e.g. F.Cu,B.Cu (Enter for single-layer "
                              "on the current layer):");
    }
    case Stage::Layers: {
        const QString trimmed = text.trimmed();
        if (!trimmed.isEmpty()) {
            std::vector<lcad::LayerId> layers;
            for (const QString& name : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const std::string layerName = name.trimmed().toStdString();
                bool found = false;
                for (const lcad::Layer& layer : m_document.layers()) {
                    if (layer.name == layerName) {
                        layers.push_back(layer.id);
                        found = true;
                        break;
                    }
                }
                if (!found) return QStringLiteral("*Layer \"%1\" not found*").arg(name.trimmed());
            }
            if (layers.size() < 2) {
                return QStringLiteral("*Enter at least 2 layer names for multi-layer routing, or leave "
                                      "blank for single-layer*");
            }
            m_params.stackup.layers = layers;
        }
        m_stage = Stage::NetClasses;
        return QStringLiteral("Net class overrides <none> (name:trackWidth:clearance:net1,net2,...; "
                              "multiple separated by ;):");
    }
    case Stage::NetClasses: {
        m_finished = true;
        const QString trimmed = text.trimmed();
        if (!trimmed.isEmpty()) {
            for (const QString& entry : trimmed.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const QStringList fields = entry.split(QLatin1Char(':'));
                if (fields.size() != 4) return QStringLiteral("*Invalid net class \"%1\" -- expected "
                                                              "name:trackWidth:clearance:net1,net2,...*")
                                                 .arg(entry);
                bool widthOk = false, clearanceOk = false;
                const double trackWidth = fields[1].trimmed().toDouble(&widthOk);
                const double clearance = fields[2].trimmed().toDouble(&clearanceOk);
                if (!widthOk || !clearanceOk || trackWidth <= 0.0 || clearance < 0.0) {
                    return QStringLiteral("*Invalid trackWidth/clearance in net class \"%1\"*").arg(entry);
                }
                lcad::NetClass netClass;
                netClass.name = fields[0].trimmed().toStdString();
                netClass.trackWidth = trackWidth;
                netClass.clearance = clearance;
                for (const QString& netName : fields[3].split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    netClass.netNames.push_back(netName.trimmed().toStdString());
                }
                m_netClasses.push_back(netClass);
            }
        }

        const lcad::AutorouteResult result = lcad::autoroute(m_document, m_nets, m_params, m_netClasses);
        if (result.failedCount == 0) {
            return QStringLiteral("*Autoroute: %1 connection(s) routed*").arg(result.routedCount);
        }
        QString failed;
        for (const std::string& name : result.failedNetNames) {
            if (!failed.isEmpty()) failed += QStringLiteral(", ");
            failed += QString::fromStdString(name);
        }
        return QStringLiteral("*Autoroute: %1 routed, %2 failed (%3)*")
            .arg(result.routedCount)
            .arg(result.failedCount)
            .arg(failed);
    }
    default:
        return std::nullopt;
    }
}

std::optional<QString> DsnExportCommand::onText(const QString& text) {
    if (m_stage == Stage::NetlistPath) {
        std::ifstream in(text.trimmed().toStdString(), std::ios::binary);
        if (!in) return QStringLiteral("*Could not open %1*\nEnter netlist file path:").arg(text.trimmed());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        m_nets = lcad::parseNetlist(buffer.str());
        m_stage = Stage::OutputPath;
        return QStringLiteral("Enter output .dsn file path:");
    }

    m_finished = true;
    std::string error;
    if (!lcad::writeSpecctraDsn(m_document, m_nets, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Specctra DSN written to %1 (%2 net(s))*").arg(text.trimmed()).arg(m_nets.size());
}

std::optional<QString> GerberExportCommand::onText(const QString& text) {
    if (m_stage == Stage::LayerName) {
        const std::string name = text.trimmed().toStdString();
        bool found = false;
        for (const lcad::Layer& layer : m_document.layers()) {
            if (layer.name == name) {
                m_layer = layer.id;
                found = true;
                break;
            }
        }
        if (!found) return QStringLiteral("*Layer \"%1\" not found*\nEnter layer name:").arg(text.trimmed());
        m_stage = Stage::Path;
        return QStringLiteral("Enter output file path:");
    }

    if (m_stage == Stage::Path) {
        m_outputPath = text.trimmed().toStdString();
        m_stage = Stage::NetlistPath;
        return QStringLiteral("Netlist file path for %TO.N% net attributes (Enter to skip):");
    }

    m_finished = true;
    std::vector<lcad::ImportedNet> nets;
    const std::string netlistPath = text.trimmed().toStdString();
    if (!netlistPath.empty()) {
        std::ifstream in(netlistPath, std::ios::binary);
        if (!in) return QStringLiteral("*Could not open %1*").arg(text.trimmed());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        nets = lcad::parseNetlist(buffer.str());
    }

    std::string error;
    if (!lcad::writeGerberLayer(m_document, m_layer, m_outputPath, &error, nets)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Gerber layer written to %1*").arg(QString::fromStdString(m_outputPath));
}

std::optional<QString> DrillExportCommand::onText(const QString& text) {
    m_finished = true;
    std::string error;
    if (!lcad::writeExcellonDrill(m_document, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Drill file written to %1*").arg(text.trimmed());
}

std::optional<QString> PickAndPlaceCommand::onText(const QString& text) {
    m_finished = true;
    std::string error;
    if (!lcad::writePickAndPlace(m_document, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Pick-and-place file written to %1*").arg(text.trimmed());
}

std::optional<QString> FootprintGenCommand::onText(const QString& text) {
    if (m_stage == Stage::Name) {
        const std::string name = text.trimmed().toStdString();
        if (name.empty()) return QStringLiteral("*Enter a name*");
        m_name = name;
        m_stage = Stage::FamilyAndParams;
        return QStringLiteral("Family + params <QFP:44:0.8:10:10:0.4:1.5 | SOIC:8:1.27:5:4:0.6:1.5 | "
                              "HEADER:4:1:2.54>:");
    }

    m_finished = true;
    const QStringList fields = text.trimmed().split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (fields.isEmpty()) return QStringLiteral("*Invalid format*");
    const QString family = fields[0].toUpper();

    bool ok = true;
    if (family == QLatin1String("QFP") || family == QLatin1String("SOIC")) {
        if (fields.size() != 7) return QStringLiteral("*Expected family:pinCount:pitch:bodyWidth:bodyLength:"
                                                       "padWidth:padLength*");
        lcad::GullWingParams params;
        params.sideCount = family == QLatin1String("QFP") ? 4 : 2;
        params.pinCount = fields[1].toInt(&ok);
        if (ok) params.pitch = fields[2].toDouble(&ok);
        if (ok) params.bodyWidth = fields[3].toDouble(&ok);
        if (ok) params.bodyLength = fields[4].toDouble(&ok);
        if (ok) params.padWidth = fields[5].toDouble(&ok);
        if (ok) params.padLength = fields[6].toDouble(&ok);
        if (!ok || !lcad::generateGullWingFootprint(m_document, m_name, params)) {
            return QStringLiteral("*Could not generate that footprint -- check the parameters and that \"%1\" "
                                  "isn't already a registered block*")
                .arg(QString::fromStdString(m_name));
        }
        return QStringLiteral("*Footprint \"%1\" generated (%2 pads)*")
            .arg(QString::fromStdString(m_name))
            .arg(params.pinCount);
    }
    if (family == QLatin1String("HEADER")) {
        if (fields.size() != 4) return QStringLiteral("*Expected HEADER:pinCount:rowCount:pitch*");
        lcad::PinHeaderParams params;
        params.pinCount = fields[1].toInt(&ok);
        if (ok) params.rowCount = fields[2].toInt(&ok);
        if (ok) params.pitch = fields[3].toDouble(&ok);
        if (!ok || !lcad::generatePinHeaderFootprint(m_document, m_name, params)) {
            return QStringLiteral("*Could not generate that footprint -- check the parameters and that \"%1\" "
                                  "isn't already a registered block*")
                .arg(QString::fromStdString(m_name));
        }
        return QStringLiteral("*Footprint \"%1\" generated (%2 pads)*")
            .arg(QString::fromStdString(m_name))
            .arg(params.pinCount * params.rowCount);
    }
    return QStringLiteral("*Unknown family \"%1\" -- expected QFP, SOIC, or HEADER*").arg(family);
}

std::optional<QString> KiCadSchExportCommand::onText(const QString& text) {
    m_finished = true;
    std::string error;
    if (!lcad::writeKiCadSch(m_document, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Schematic written to %1*").arg(text.trimmed());
}

std::optional<QString> KiCadSchImportCommand::onText(const QString& text) {
    m_finished = true;
    const std::size_t before = m_document.entities().size();
    std::string error;
    if (!lcad::readKiCadSch(m_document, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    const std::size_t added = m_document.entities().size() - before;
    return QStringLiteral("*Schematic imported from %1 (%2 entit%3 added)*")
        .arg(text.trimmed())
        .arg(added)
        .arg(added == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
}

std::optional<QString> KiCadPcbExportCommand::onText(const QString& text) {
    if (m_stage == Stage::Path) {
        m_outputPath = text.trimmed().toStdString();
        m_stage = Stage::NetlistPath;
        return QStringLiteral("Netlist file path for real net names (Enter to skip):");
    }

    m_finished = true;
    std::vector<lcad::ImportedNet> nets;
    const std::string netlistPath = text.trimmed().toStdString();
    if (!netlistPath.empty()) {
        std::ifstream in(netlistPath, std::ios::binary);
        if (!in) return QStringLiteral("*Could not open %1*").arg(text.trimmed());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        nets = lcad::parseNetlist(buffer.str());
    }
    std::string error;
    if (!lcad::writeKiCadPcb(m_document, nets, m_outputPath, &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Board written to %1*").arg(QString::fromStdString(m_outputPath));
}

std::optional<QString> KiCadPcbImportCommand::onText(const QString& text) {
    m_finished = true;
    const std::size_t before = m_document.entities().size();
    std::string error;
    if (!lcad::readKiCadPcb(m_document, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    const std::size_t added = m_document.entities().size() - before;
    return QStringLiteral("*Board imported from %1 (%2 entit%3 added)*")
        .arg(text.trimmed())
        .arg(added)
        .arg(added == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
}
