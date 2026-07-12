#include "commands/QSelectCommand.h"

namespace {

std::optional<lcad::EntityType> entityTypeFromName(const QString& name) {
    const QString upper = name.toUpper();
    if (upper == QLatin1String("LINE")) return lcad::EntityType::Line;
    if (upper == QLatin1String("CIRCLE")) return lcad::EntityType::Circle;
    if (upper == QLatin1String("ARC")) return lcad::EntityType::Arc;
    if (upper == QLatin1String("LWPOLYLINE") || upper == QLatin1String("POLYLINE")) return lcad::EntityType::Polyline;
    if (upper == QLatin1String("ELLIPSE")) return lcad::EntityType::Ellipse;
    if (upper == QLatin1String("SPLINE")) return lcad::EntityType::Spline;
    if (upper == QLatin1String("TEXT")) return lcad::EntityType::Text;
    if (upper == QLatin1String("MTEXT")) return lcad::EntityType::MText;
    if (upper == QLatin1String("DIMENSION")) return lcad::EntityType::Dimension;
    if (upper == QLatin1String("LEADER")) return lcad::EntityType::Leader;
    if (upper == QLatin1String("MULTILEADER") || upper == QLatin1String("MLEADER")) return lcad::EntityType::MLeader;
    if (upper == QLatin1String("HATCH")) return lcad::EntityType::Hatch;
    if (upper == QLatin1String("INSERT") || upper == QLatin1String("BLOCK")) return lcad::EntityType::Insert;
    if (upper == QLatin1String("POINT")) return lcad::EntityType::Point;
    if (upper == QLatin1String("XLINE") || upper == QLatin1String("RAY") ||
        upper == QLatin1String("CONSTRUCTIONLINE")) {
        return lcad::EntityType::ConstructionLine;
    }
    if (upper == QLatin1String("ATTDEF")) return lcad::EntityType::AttDef;
    if (upper == QLatin1String("TABLE") || upper == QLatin1String("ACAD_TABLE")) return lcad::EntityType::Table;
    return std::nullopt;
}

} // namespace

std::optional<QString> QSelectCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_stage == Stage::Type) {
        if (!trimmed.isEmpty() && trimmed != QLatin1String("*")) {
            const auto type = entityTypeFromName(trimmed);
            if (!type) return QStringLiteral("*Unknown type* Object type <*>:");
            m_typeFilter = type;
        }
        m_stage = Stage::Layer;
        return QStringLiteral("Layer name (Enter for any):");
    }

    // Stage::Layer
    std::optional<lcad::LayerId> layerFilter;
    if (!trimmed.isEmpty()) {
        for (const lcad::Layer& l : m_document.layers()) {
            if (QString::fromStdString(l.name).compare(trimmed, Qt::CaseInsensitive) == 0) {
                layerFilter = l.id;
                break;
            }
        }
        if (!layerFilter) {
            return QStringLiteral("*No layer named \"%1\"*\nLayer name (Enter for any):").arg(trimmed);
        }
    }

    m_matches.clear();
    for (const lcad::Entity* e : m_document.entities()) {
        if (m_typeFilter && e->type() != *m_typeFilter) continue;
        if (layerFilter && e->layer() != *layerFilter) continue;
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        m_matches.push_back(e->id());
    }
    m_finished = true;
    return QStringLiteral("*%1 object(s) selected*").arg(m_matches.size());
}
