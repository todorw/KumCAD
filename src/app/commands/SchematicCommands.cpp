#include "commands/SchematicCommands.h"

#include "core/document/Commands.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/electrical/WireList.h"
#include "core/schematic/Bom.h"
#include "core/schematic/Netlist.h"
#include "core/schematic/Sheets.h"

std::optional<QString> JunctionCommand::onPoint(const lcad::Point2D& pt) {
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document,
        std::make_unique<lcad::JunctionEntity>(m_document.reserveEntityId(), m_document.currentLayer(), pt)));
    return QStringLiteral("Specify a point (Enter to finish):");
}

std::optional<QString> NoConnectCommand::onPoint(const lcad::Point2D& pt) {
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document,
        std::make_unique<lcad::NoConnectEntity>(m_document.reserveEntityId(), m_document.currentLayer(), pt)));
    return QStringLiteral("Specify a point (Enter to finish):");
}

std::optional<QString> NetLabelCommand::onPoint(const lcad::Point2D& pt) {
    if (m_havePosition) return std::nullopt; // waiting for text, not a point
    m_position = pt;
    m_havePosition = true;
    return QStringLiteral("Enter net name:");
}

std::optional<QString> NetLabelCommand::onText(const QString& text) {
    m_finished = true;
    const std::string name = text.trimmed().toStdString();
    if (name.empty()) return std::nullopt; // Enter with nothing: cancel like TEXT
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::NetLabelEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                            m_position, name)));
    return QStringLiteral("*Net label \"%1\" placed*").arg(text.trimmed());
}

std::optional<QString> PinAddCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == Stage::Position) {
        m_position = pt;
        m_stage = Stage::StubEnd;
        return QStringLiteral("Specify pin stub end point:");
    }
    if (m_stage == Stage::StubEnd) {
        m_block->pins.push_back(lcad::Pin{m_pinName, m_pinNumber, m_electricalType, m_position, pt});
        m_finished = true;
        return QStringLiteral("*Pin \"%1\" (%2) added to block \"%3\"*")
            .arg(QString::fromStdString(m_pinName), QString::fromStdString(m_pinNumber),
                 QString::fromStdString(m_block->name));
    }
    return std::nullopt;
}

std::optional<QString> PinAddCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    switch (m_stage) {
    case Stage::BlockName: {
        m_block = m_document.findBlock(trimmed.toStdString());
        if (!m_block) return QStringLiteral("*Block \"%1\" not found*\nEnter block name:").arg(trimmed);
        m_stage = Stage::PinName;
        return QStringLiteral("Enter pin name:");
    }
    case Stage::PinName:
        if (trimmed.isEmpty()) return QStringLiteral("*Pin name can't be empty*\nEnter pin name:");
        m_pinName = trimmed.toStdString();
        m_stage = Stage::PinNumber;
        return QStringLiteral("Enter pin number:");
    case Stage::PinNumber:
        if (trimmed.isEmpty()) return QStringLiteral("*Pin number can't be empty*\nEnter pin number:");
        m_pinNumber = trimmed.toStdString();
        m_stage = Stage::ElectricalType;
        return QStringLiteral(
            "Electrical type [Input/Output/Bidirectional/Tristate/Passive/Power/OpenCollector/NotConnected] "
            "<Passive>:");
    case Stage::ElectricalType: {
        const QString t = trimmed.toUpper();
        if (t.isEmpty() || t == QLatin1String("PASSIVE")) m_electricalType = lcad::PinElectricalType::Passive;
        else if (t == QLatin1String("INPUT")) m_electricalType = lcad::PinElectricalType::Input;
        else if (t == QLatin1String("OUTPUT")) m_electricalType = lcad::PinElectricalType::Output;
        else if (t == QLatin1String("BIDIRECTIONAL")) m_electricalType = lcad::PinElectricalType::Bidirectional;
        else if (t == QLatin1String("TRISTATE")) m_electricalType = lcad::PinElectricalType::TriState;
        else if (t == QLatin1String("POWER")) m_electricalType = lcad::PinElectricalType::Power;
        else if (t == QLatin1String("OPENCOLLECTOR")) m_electricalType = lcad::PinElectricalType::OpenCollector;
        else if (t == QLatin1String("NOTCONNECTED")) m_electricalType = lcad::PinElectricalType::NotConnected;
        else return QStringLiteral("*Unrecognized type*\nElectrical type <Passive>:");
        m_stage = Stage::Position;
        return QStringLiteral("Specify pin position:");
    }
    default:
        return std::nullopt;
    }
}

std::optional<QString> WireListCommand::onPoint(const lcad::Point2D& pt) {
    m_finished = true;
    const std::vector<lcad::Net> nets = lcad::computeNets(m_document);
    lcad::buildWireListTable(m_document, nets, pt);
    return QStringLiteral("*Wire list placed (%1 net(s))*").arg(nets.size());
}

std::optional<QString> BomCommand::onPoint(const lcad::Point2D& pt) {
    m_position = pt;
    m_havePosition = true;
    return QStringLiteral("BOM  Include DNP (Do-Not-Populate) parts? [Yes/No] <No>:");
}

std::optional<QString> BomCommand::onText(const QString& text) {
    m_finished = true;
    const QString trimmed = text.trimmed();
    const bool includeDnp = trimmed.compare(QStringLiteral("Y"), Qt::CaseInsensitive) == 0 ||
                            trimmed.compare(QStringLiteral("Yes"), Qt::CaseInsensitive) == 0;
    const std::vector<lcad::BomRow> rows = lcad::generateBom(m_document, includeDnp);
    lcad::buildBomTable(m_document, rows, m_position);
    int totalParts = 0;
    int dnpParts = 0;
    for (const lcad::BomRow& row : rows) {
        totalParts += row.quantity;
        if (row.dnp) dnpParts += row.quantity;
    }
    return QStringLiteral("*BOM placed (%1 row(s), %2 part(s) total, %3 DNP)*")
        .arg(rows.size())
        .arg(totalParts)
        .arg(dnpParts);
}

std::optional<QString> NetlistExportCommand::onText(const QString& text) {
    m_finished = true;
    const std::string path = text.trimmed().toStdString();
    if (path.empty()) return std::nullopt;

    const std::vector<lcad::Net> nets = lcad::computeNets(m_document);
    std::string error;
    if (!lcad::writeNetlist(m_document, nets, path, &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*Netlist written to %1 (%2 nets)*").arg(text.trimmed()).arg(nets.size());
}

std::optional<QString> SheetNewCommand::onText(const QString& text) {
    m_finished = true;
    const QString name = text.trimmed();
    if (name.isEmpty()) return std::nullopt;

    lcad::createSheet(m_document, name.toStdString());
    lcad::goToSheet(m_document, name.toStdString());
    return QStringLiteral("*Sheet \"%1\" created and active*").arg(name);
}

std::optional<QString> SheetGoToCommand::onText(const QString& text) {
    m_finished = true;
    const QString name = text.trimmed();
    if (name.isEmpty()) return std::nullopt;

    if (!lcad::goToSheet(m_document, name.toStdString())) {
        return QStringLiteral("*No sheet named \"%1\"*").arg(name);
    }
    return QStringLiteral("*Sheet \"%1\" is now active*").arg(name);
}
