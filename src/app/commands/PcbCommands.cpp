#include "commands/PcbCommands.h"

#include "core/document/Commands.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/pcb/GerberWriter.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/SpecctraWriter.h"

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
