#include "commands/PcbCommands.h"

#include "core/document/Commands.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
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
        m_finished = true;
        double clearance = 0.2;
        if (!text.trimmed().isEmpty()) {
            bool ok = false;
            clearance = text.trimmed().toDouble(&ok);
            if (!ok || clearance < 0.0) return QStringLiteral("*Invalid clearance*");
        }
        const auto ids = lcad::buildCopperPourWithClearance(m_document, m_document.currentLayer(), m_boundary,
                                                             m_ownNetPositions, m_gridSize, clearance);
        if (ids.empty()) return QStringLiteral("*Pour produced no copper -- check boundary/grid size*");
        return QStringLiteral("*Copper pour: %1 piece(s)*").arg(ids.size());
    }
    default:
        return std::nullopt;
    }
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

    m_finished = true;
    std::string error;
    if (!lcad::writeGerberLayer(m_document, m_layer, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Gerber layer written to %1*").arg(text.trimmed());
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
