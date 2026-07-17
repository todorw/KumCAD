#include "commands/ExpressToolCommands.h"

#include "core/document/Commands.h"
#include "core/document/ExpressTools.h"
#include "core/geometry/MText.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"

// ---- TCOUNT ----

std::optional<QString> TCountCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_stage == 0) {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const int v = trimmed.toInt(&ok);
            if (!ok) return QStringLiteral("*Enter a whole number*");
            m_start = v;
        }
        m_stage = 1;
        return QStringLiteral("Increment <1>:");
    }
    if (m_stage == 1) {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const int v = trimmed.toInt(&ok);
            if (!ok) return QStringLiteral("*Enter a whole number*");
            m_increment = v;
        }
        m_stage = 2;
        return QStringLiteral("Placement [Prefix/Suffix/Replace] <Prefix>:");
    }
    apply(trimmed.toUpper());
    m_finished = true;
    return m_result;
}

bool TCountCommand::requestFinish() {
    // A finish gesture accepts every remaining default (empty Enters go
    // through onText instead, which advances stage by stage).
    apply(QStringLiteral("PREFIX"));
    m_finished = true;
    return true;
}

void TCountCommand::apply(const QString& placementOption) {
    lcad::TCountPlacement placement = lcad::TCountPlacement::Prefix;
    if (placementOption.startsWith(QLatin1Char('S'))) placement = lcad::TCountPlacement::Suffix;
    else if (placementOption.startsWith(QLatin1Char('R'))) placement = lcad::TCountPlacement::Replace;

    // Collect the text-bearing entities, keeping ids zipped with items.
    std::vector<lcad::EntityId> textIds;
    std::vector<lcad::TCountItem> items;
    for (lcad::EntityId id : m_ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        if (e->type() == lcad::EntityType::Text) {
            const auto& t = static_cast<const lcad::TextEntity&>(*e);
            textIds.push_back(id);
            items.push_back({t.position(), t.text()});
        } else if (e->type() == lcad::EntityType::MText) {
            const auto& t = static_cast<const lcad::MTextEntity&>(*e);
            textIds.push_back(id);
            items.push_back({t.position(), t.text()});
        }
    }
    if (items.empty()) {
        m_result = QStringLiteral("*No text objects in the selection*");
        return;
    }

    const std::vector<std::string> numbered = lcad::applyTcount(items, m_start, m_increment, placement);
    auto batch = std::make_unique<lcad::BatchCommand>("TCount");
    for (std::size_t i = 0; i < textIds.size(); ++i) {
        lcad::Entity* e = m_document.findEntity(textIds[i]);
        std::unique_ptr<lcad::Entity> clone = e->clone();
        if (clone->type() == lcad::EntityType::Text) {
            static_cast<lcad::TextEntity&>(*clone).setText(numbered[i]);
        } else {
            static_cast<lcad::MTextEntity&>(*clone).setText(numbered[i]);
        }
        batch->add(std::make_unique<lcad::ReplaceEntityCommand>(m_document, textIds[i], std::move(clone)));
    }
    m_document.commandStack().execute(std::move(batch));
    m_result = QStringLiteral("*%1 text object(s) numbered*").arg(items.size());
}

// ---- BREAKLINE ----

std::optional<QString> BreaklineCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == 0) {
        m_a = pt;
        m_stage = 1;
        return QStringLiteral("Second point:");
    }
    if (m_stage == 1) {
        m_b = pt;
        m_stage = 2;
        const double defaultSize = m_a.distanceTo(m_b) * 0.05;
        return QStringLiteral("Symbol size <%1>:").arg(defaultSize, 0, 'g', 4);
    }
    return std::nullopt;
}

std::optional<QString> BreaklineCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    double size = m_a.distanceTo(m_b) * 0.05;
    if (!trimmed.isEmpty()) {
        bool ok = false;
        const double v = trimmed.toDouble(&ok);
        if (!ok || v <= 0.0) return QStringLiteral("*Enter a positive size*");
        size = v;
    }
    build(size);
    m_finished = true;
    return m_result;
}

void BreaklineCommand::build(double size) {
    const std::vector<lcad::Point2D> points = lcad::breaklinePoints(m_a, m_b, size);
    if (points.empty()) {
        m_result = QStringLiteral("*Symbol size too large for that segment*");
        return;
    }
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::PolylineEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                           points, false)));
    m_result = QStringLiteral("*Breakline created*");
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> BreaklineCommand::previewSegments() const {
    if (m_stage == 1 && m_preview) return {{m_a, *m_preview}};
    return {};
}

// ---- LAYMRG ----

std::optional<QString> LayMrgCommand::onText(const QString& text) {
    const QString name = text.trimmed();
    if (name.isEmpty()) return QStringLiteral("*Enter a layer name*");
    if (m_stage == 0) {
        if (!lcad::findLayerByName(m_document, name.toStdString())) {
            return QStringLiteral("*No layer named \"%1\"*").arg(name);
        }
        m_sourceName = name;
        m_stage = 1;
        return QStringLiteral("Target layer name:");
    }

    const lcad::Layer* source = lcad::findLayerByName(m_document, m_sourceName.toStdString());
    const lcad::Layer* target = lcad::findLayerByName(m_document, name.toStdString());
    m_finished = true;
    if (!source || !target) {
        m_result = QStringLiteral("*No layer named \"%1\"*").arg(name);
        return m_result;
    }
    if (source->id == target->id) {
        m_result = QStringLiteral("*Source and target are the same layer*");
        return m_result;
    }
    if (source->id == 0) {
        m_result = QStringLiteral("*Layer \"0\" cannot be merged away*");
        return m_result;
    }

    const std::vector<lcad::EntityId> ids = lcad::entityIdsOnLayer(m_document, source->id);
    const lcad::LayerId sourceId = source->id;
    if (!ids.empty()) {
        m_document.commandStack().execute(
            std::make_unique<lcad::SetEntityLayerCommand>(m_document, ids, target->id));
    }
    m_document.deleteLayer(sourceId);
    m_result = QStringLiteral("*%1 object(s) moved to \"%2\"; layer \"%3\" deleted*")
                   .arg(ids.size())
                   .arg(name)
                   .arg(m_sourceName);
    return m_result;
}

// ---- LAYDEL ----

std::optional<QString> LayDelCommand::onText(const QString& text) {
    const QString name = text.trimmed();
    if (name.isEmpty()) return QStringLiteral("*Enter a layer name*");
    m_finished = true;
    const lcad::Layer* layer = lcad::findLayerByName(m_document, name.toStdString());
    if (!layer) {
        m_result = QStringLiteral("*No layer named \"%1\"*").arg(name);
        return m_result;
    }
    if (layer->id == 0) {
        m_result = QStringLiteral("*Layer \"0\" cannot be deleted*");
        return m_result;
    }
    const std::vector<lcad::EntityId> ids = lcad::entityIdsOnLayer(m_document, layer->id);
    const lcad::LayerId layerId = layer->id;
    if (!ids.empty()) {
        auto batch = std::make_unique<lcad::BatchCommand>("LayDel");
        for (lcad::EntityId id : ids) {
            batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
        }
        m_document.commandStack().execute(std::move(batch));
    }
    m_document.deleteLayer(layerId);
    m_result = QStringLiteral("*Layer \"%1\" and %2 object(s) deleted*").arg(name).arg(ids.size());
    return m_result;
}

// ---- COPYTOLAYER ----

std::optional<QString> CopyToLayerCommand::onText(const QString& text) {
    const QString name = text.trimmed();
    if (name.isEmpty()) return QStringLiteral("*Enter a layer name*");
    m_finished = true;

    lcad::LayerId targetId;
    if (const lcad::Layer* existing = lcad::findLayerByName(m_document, name.toStdString())) {
        targetId = existing->id;
    } else {
        targetId = m_document.addLayer(name.toStdString(), lcad::Color{255, 255, 255});
    }

    auto batch = std::make_unique<lcad::BatchCommand>("CopyToLayer");
    int copied = 0;
    for (lcad::EntityId id : m_ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        std::unique_ptr<lcad::Entity> clone = e->clone();
        clone->setId(m_document.reserveEntityId());
        clone->setLayer(targetId);
        batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(clone)));
        ++copied;
    }
    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
    m_result = QStringLiteral("*%1 object(s) copied to \"%2\"*").arg(copied).arg(name);
    return m_result;
}
