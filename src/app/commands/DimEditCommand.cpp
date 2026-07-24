#include "commands/DimEditCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Dimension.h"

#include <cmath>

std::optional<QString> DimEditCommand::onText(const QString& text) {
    if (m_stage == Stage::Mode) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("New"), Qt::CaseInsensitive) == 0 ||
            trimmed.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) {
            m_stage = Stage::NewText;
            return QStringLiteral("Enter new dimension text <>:");
        }
        if (trimmed.compare(QStringLiteral("Rotate"), Qt::CaseInsensitive) == 0 ||
            trimmed.compare(QStringLiteral("R"), Qt::CaseInsensitive) == 0) {
            m_stage = Stage::RotateAngle;
            return QStringLiteral("Specify angle for dimension text:");
        }
        return QStringLiteral("*Enter New or Rotate*");
    }

    double rotationRadians = 0.0;
    if (m_stage == Stage::RotateAngle) {
        bool ok = false;
        rotationRadians = text.trimmed().toDouble(&ok) * M_PI / 180.0;
        if (!ok) return QStringLiteral("*Invalid angle*");
    }

    m_finished = true;
    auto batch = std::make_unique<lcad::BatchCommand>("Dimedit");
    int applied = 0;
    for (lcad::EntityId id : m_dimensionIds) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (!e || e->type() != lcad::EntityType::Dimension) continue;
        auto clone = e->clone();
        auto* dim = static_cast<lcad::DimensionEntity*>(clone.get());
        if (m_stage == Stage::NewText) {
            dim->setTextOverride(text.trimmed().toStdString());
        } else {
            dim->setTextRotationOverride(rotationRadians);
        }
        batch->add(std::make_unique<lcad::ReplaceEntityCommand>(m_document, id, std::move(clone)));
        ++applied;
    }
    if (applied > 0) m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*%1 dimension(s) updated*").arg(applied);
}

std::optional<QString> DimTeditCommand::onPoint(const lcad::Point2D& pt) {
    m_finished = true;
    const lcad::Entity* e = m_document.findEntity(m_dimensionId);
    if (!e || e->type() != lcad::EntityType::Dimension) return QStringLiteral("*Dimension no longer exists*");
    const auto& dim = static_cast<const lcad::DimensionEntity&>(*e);
    m_document.commandStack().execute(
        std::make_unique<lcad::MoveGripCommand>(m_document, m_dimensionId, 2, dim.linePoint(), pt));
    return QStringLiteral("*Text repositioned*");
}

std::optional<QString> DimTeditCommand::onScalar(double value) {
    m_finished = true;
    const lcad::Entity* e = m_document.findEntity(m_dimensionId);
    if (!e || e->type() != lcad::EntityType::Dimension) return QStringLiteral("*Dimension no longer exists*");
    auto clone = e->clone();
    static_cast<lcad::DimensionEntity*>(clone.get())->setTextRotationOverride(value * M_PI / 180.0);
    m_document.commandStack().execute(
        std::make_unique<lcad::ReplaceEntityCommand>(m_document, m_dimensionId, std::move(clone)));
    return QStringLiteral("*Text rotated*");
}
