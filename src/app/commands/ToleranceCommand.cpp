#include "commands/ToleranceCommand.h"

#include "core/document/Commands.h"

#include <QStringList>

namespace {

std::optional<lcad::GeoCharacteristic> parseCharacteristic(const QString& token) {
    const QString t = token.trimmed().toUpper();
    using GC = lcad::GeoCharacteristic;
    if (t == QLatin1String("STRAIGHTNESS") || t == QLatin1String("STR")) return GC::Straightness;
    if (t == QLatin1String("FLATNESS") || t == QLatin1String("FLT")) return GC::Flatness;
    if (t == QLatin1String("CIRCULARITY") || t == QLatin1String("CIR")) return GC::Circularity;
    if (t == QLatin1String("CYLINDRICITY") || t == QLatin1String("CYL")) return GC::Cylindricity;
    if (t == QLatin1String("PROFILELINE") || t == QLatin1String("PRL")) return GC::ProfileLine;
    if (t == QLatin1String("PROFILESURFACE") || t == QLatin1String("PRS")) return GC::ProfileSurface;
    if (t == QLatin1String("ANGULARITY") || t == QLatin1String("ANG")) return GC::Angularity;
    if (t == QLatin1String("PERPENDICULARITY") || t == QLatin1String("PER")) return GC::Perpendicularity;
    if (t == QLatin1String("PARALLELISM") || t == QLatin1String("PAR")) return GC::Parallelism;
    if (t == QLatin1String("POSITION") || t == QLatin1String("POS")) return GC::Position;
    if (t == QLatin1String("CONCENTRICITY") || t == QLatin1String("CON")) return GC::Concentricity;
    if (t == QLatin1String("SYMMETRY") || t == QLatin1String("SYM")) return GC::Symmetry;
    if (t == QLatin1String("CIRCULARRUNOUT") || t == QLatin1String("CRO")) return GC::CircularRunout;
    if (t == QLatin1String("TOTALRUNOUT") || t == QLatin1String("TRO")) return GC::TotalRunout;
    return std::nullopt;
}

std::optional<lcad::MaterialCondition> parseModifier(const QString& token) {
    const QString t = token.trimmed().toUpper();
    if (t == QLatin1String("NONE")) return lcad::MaterialCondition::None;
    if (t == QLatin1String("M")) return lcad::MaterialCondition::MMC;
    if (t == QLatin1String("L")) return lcad::MaterialCondition::LMC;
    return std::nullopt;
}

} // namespace

QString ToleranceCommand::start() { return QStringLiteral("TOLERANCE  Specify insertion point:"); }

std::optional<QString> ToleranceCommand::onPoint(const lcad::Point2D& pt) {
    if (m_havePosition) return std::nullopt; // already placed -- ignore further clicks
    m_position = pt;
    m_havePosition = true;
    return QStringLiteral("Row 1 <characteristic:value:modifier:diameter:datums, e.g. POSITION:0.05:M:DIA:A,B,C, "
                          "modifier/diameter/datums may be NONE/NODIA/NONE> (Enter to finish rows):");
}

std::optional<QString> ToleranceCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (m_rows.empty()) return QStringLiteral("*At least one row is required*");
        const auto id = m_document.reserveEntityId();
        auto entity = std::make_unique<lcad::ToleranceEntity>(id, m_document.currentLayer(), m_position, m_rows);
        m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
        m_finished = true;
        return QStringLiteral("*Tolerance frame added (%1 row(s))*").arg(m_rows.size());
    }

    const QStringList fields = trimmed.split(QLatin1Char(':'));
    if (fields.size() != 5) {
        return QStringLiteral("*Expected characteristic:value:modifier:diameter:datums*");
    }
    const auto characteristic = parseCharacteristic(fields[0]);
    if (!characteristic) return QStringLiteral("*Unrecognized characteristic \"%1\"*").arg(fields[0]);
    bool ok = false;
    const double value = fields[1].trimmed().toDouble(&ok);
    if (!ok || value < 0.0) return QStringLiteral("*Invalid tolerance value*");
    const auto modifier = parseModifier(fields[2]);
    if (!modifier) return QStringLiteral("*Modifier must be M, L, or NONE*");
    const QString diaToken = fields[3].trimmed().toUpper();
    if (diaToken != QLatin1String("DIA") && diaToken != QLatin1String("NODIA")) {
        return QStringLiteral("*Diameter field must be DIA or NODIA*");
    }

    lcad::ToleranceRow row;
    row.characteristic = *characteristic;
    row.toleranceValue = value;
    row.toleranceModifier = *modifier;
    row.diameterSymbol = diaToken == QLatin1String("DIA");
    const QString datumsToken = fields[4].trimmed();
    if (datumsToken.toUpper() != QLatin1String("NONE")) {
        for (const QString& letter : datumsToken.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            lcad::ToleranceDatumRef datum;
            datum.letter = letter.trimmed().toStdString();
            row.datums.push_back(datum);
        }
    }
    m_rows.push_back(row);
    return QStringLiteral("*Row %1 added: %2* Next row, or Enter to finish:")
        .arg(m_rows.size())
        .arg(QString::fromStdString(lcad::ToleranceEntity::rowText(row)));
}
