#include "commands/BlockParamCommand.h"

#include "core/geometry/Entity.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

std::optional<QString> BlockParamCommand::onText(const QString& text) {
    if (m_stage != Stage::BlockName) return std::nullopt;
    const std::string name = text.trimmed().toStdString();
    m_block = m_document.findBlock(name);
    if (!m_block) {
        return QStringLiteral("*Block \"%1\" not found*\nEnter block name to make dynamic:")
            .arg(QString::fromStdString(name));
    }
    m_stage = Stage::BasePoint;
    return QStringLiteral("Specify parameter base point:");
}

std::optional<QString> BlockParamCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::BasePoint:
        m_basePoint = pt;
        m_stage = Stage::EndPoint;
        return QStringLiteral("Specify parameter endpoint:");
    case Stage::EndPoint:
        if (pt.distanceTo(m_basePoint) < 1e-9) return QStringLiteral("*Endpoint can't match the base point*");
        m_endPoint = pt;
        m_stage = Stage::FrameCorner1;
        return QStringLiteral("Specify first corner of stretch frame:");
    case Stage::FrameCorner1:
        m_frameCorner1 = pt;
        m_stage = Stage::FrameCorner2;
        return QStringLiteral("Specify opposite corner of stretch frame:");
    case Stage::FrameCorner2: {
        lcad::DynamicLinearParameter dp;
        dp.basePoint = m_basePoint;
        dp.endPoint = m_endPoint;
        dp.frameMin = lcad::Point2D(std::min(m_frameCorner1.x, pt.x), std::min(m_frameCorner1.y, pt.y));
        dp.frameMax = lcad::Point2D(std::max(m_frameCorner1.x, pt.x), std::max(m_frameCorner1.y, pt.y));
        m_block->dynamicParam = dp;
        m_finished = true;
        return QStringLiteral("*Block \"%1\" is now dynamic (linear stretch parameter)*")
            .arg(QString::fromStdString(m_block->name));
    }
    default:
        return std::nullopt;
    }
}

std::optional<QString> BlockFlipCommand::onText(const QString& text) {
    if (m_stage != Stage::BlockName) return std::nullopt;
    const std::string name = text.trimmed().toStdString();
    m_block = m_document.findBlock(name);
    if (!m_block) {
        return QStringLiteral("*Block \"%1\" not found*\nEnter block name to add a flip parameter:")
            .arg(QString::fromStdString(name));
    }
    m_stage = Stage::BasePoint;
    return QStringLiteral("Specify flip line base point:");
}

std::optional<QString> BlockFlipCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::BasePoint:
        m_basePoint = pt;
        m_stage = Stage::EndPoint;
        return QStringLiteral("Specify flip line endpoint:");
    case Stage::EndPoint: {
        if (pt.distanceTo(m_basePoint) < 1e-9) return QStringLiteral("*Endpoint can't match the base point*");
        m_block->dynamicFlip = lcad::DynamicFlipParameter{m_basePoint, pt};
        m_finished = true;
        return QStringLiteral("*Block \"%1\" now has a flip parameter*").arg(QString::fromStdString(m_block->name));
    }
    default:
        return std::nullopt;
    }
}

std::optional<QString> BlockRotationCommand::onText(const QString& text) {
    if (m_stage != Stage::BlockName) return std::nullopt;
    const std::string name = text.trimmed().toStdString();
    m_block = m_document.findBlock(name);
    if (!m_block) {
        return QStringLiteral("*Block \"%1\" not found*\nEnter block name to add a rotation parameter:")
            .arg(QString::fromStdString(name));
    }
    m_stage = Stage::BasePoint;
    return QStringLiteral("Specify rotation base point:");
}

std::optional<QString> BlockRotationCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::BasePoint:
        m_basePoint = pt;
        m_stage = Stage::RadiusPoint;
        return QStringLiteral("Specify default radius point:");
    case Stage::RadiusPoint: {
        const double radius = pt.distanceTo(m_basePoint);
        if (radius < 1e-9) return QStringLiteral("*Radius point can't match the base point*");
        m_block->dynamicRotation = lcad::DynamicRotationParameter{m_basePoint, radius};
        m_finished = true;
        return QStringLiteral("*Block \"%1\" now has a rotation parameter*")
            .arg(QString::fromStdString(m_block->name));
    }
    default:
        return std::nullopt;
    }
}

std::optional<QString> BlockArrayCommand::onText(const QString& text) {
    if (m_stage != Stage::BlockName) return std::nullopt;
    const std::string name = text.trimmed().toStdString();
    m_block = m_document.findBlock(name);
    if (!m_block) {
        return QStringLiteral("*Block \"%1\" not found*\nEnter block name to add an array parameter:")
            .arg(QString::fromStdString(name));
    }
    m_stage = Stage::BasePoint;
    return QStringLiteral("Specify array base point:");
}

std::optional<QString> BlockArrayCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::BasePoint:
        m_basePoint = pt;
        m_stage = Stage::SecondPoint;
        return QStringLiteral("Specify second point (sets item direction and spacing):");
    case Stage::SecondPoint: {
        const double spacing = pt.distanceTo(m_basePoint);
        if (spacing < 1e-9) return QStringLiteral("*Second point can't match the base point*");
        const lcad::Point2D direction = (pt - m_basePoint) * (1.0 / spacing);
        m_block->dynamicArray = lcad::DynamicArrayParameter{m_basePoint, direction, spacing, 1};
        m_finished = true;
        return QStringLiteral("*Block \"%1\" now has an array parameter*")
            .arg(QString::fromStdString(m_block->name));
    }
    default:
        return std::nullopt;
    }
}

namespace {
QString shortEntityTypeName(lcad::EntityType type) {
    switch (type) {
    case lcad::EntityType::Line: return QStringLiteral("Line");
    case lcad::EntityType::Circle: return QStringLiteral("Circle");
    case lcad::EntityType::Arc: return QStringLiteral("Arc");
    case lcad::EntityType::Polyline: return QStringLiteral("Polyline");
    case lcad::EntityType::Ellipse: return QStringLiteral("Ellipse");
    case lcad::EntityType::Spline: return QStringLiteral("Spline");
    case lcad::EntityType::Text: return QStringLiteral("Text");
    case lcad::EntityType::MText: return QStringLiteral("MText");
    case lcad::EntityType::Dimension: return QStringLiteral("Dimension");
    case lcad::EntityType::Leader: return QStringLiteral("Leader");
    case lcad::EntityType::MLeader: return QStringLiteral("MLeader");
    case lcad::EntityType::Hatch: return QStringLiteral("Hatch");
    case lcad::EntityType::Insert: return QStringLiteral("Insert");
    case lcad::EntityType::Point: return QStringLiteral("Point");
    case lcad::EntityType::ConstructionLine: return QStringLiteral("ConstructionLine");
    case lcad::EntityType::AttDef: return QStringLiteral("AttDef");
    case lcad::EntityType::Table: return QStringLiteral("Table");
    case lcad::EntityType::Image: return QStringLiteral("Image");
    case lcad::EntityType::PointCloud: return QStringLiteral("PointCloud");
    }
    return QStringLiteral("Entity");
}
} // namespace

std::optional<QString> BlockVisibilityCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::BlockName: {
        const std::string name = text.trimmed().toStdString();
        m_block = m_document.findBlock(name);
        if (!m_block) {
            return QStringLiteral("*Block \"%1\" not found*\nEnter block name to add a visibility parameter:")
                .arg(QString::fromStdString(name));
        }
        QStringList listing;
        for (std::size_t i = 0; i < m_block->entities.size(); ++i) {
            listing << QStringLiteral("%1:%2").arg(i).arg(shortEntityTypeName(m_block->entities[i]->type()));
        }
        m_stage = Stage::StateName;
        return QStringLiteral("Children: %1\nEnter visibility state name (Enter to finish):")
            .arg(listing.isEmpty() ? QStringLiteral("(none)") : listing.join(QStringLiteral(" ")));
    }
    case Stage::StateName: {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            if (m_draft.states.empty()) {
                m_finished = true;
                return QStringLiteral("*Cancelled: need at least one visibility state*");
            }
            m_block->dynamicVisibility = m_draft;
            m_finished = true;
            return QStringLiteral("*Block \"%1\" now has a visibility parameter with %2 state(s)*")
                .arg(QString::fromStdString(m_block->name))
                .arg(m_draft.states.size());
        }
        m_pendingStateName = trimmed.toStdString();
        m_stage = Stage::EntityIndices;
        return QStringLiteral("Enter visible entity indices for \"%1\" (comma-separated, or 'all'):").arg(trimmed);
    }
    case Stage::EntityIndices: {
        std::vector<lcad::EntityId> ids;
        const QString trimmed = text.trimmed();
        if (trimmed.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
            for (const auto& child : m_block->entities) ids.push_back(child->id());
        } else {
            for (const QString& token : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                bool ok = false;
                const int idx = token.trimmed().toInt(&ok);
                if (ok && idx >= 0 && static_cast<std::size_t>(idx) < m_block->entities.size()) {
                    ids.push_back(m_block->entities[static_cast<std::size_t>(idx)]->id());
                }
            }
        }
        m_draft.states.push_back(m_pendingStateName);
        m_draft.visibleIds[m_pendingStateName] = std::move(ids);
        m_stage = Stage::StateName;
        return QStringLiteral("Enter visibility state name (Enter to finish):");
    }
    }
    return std::nullopt;
}

std::optional<QString> BlockLookupCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::BlockName: {
        const std::string name = text.trimmed().toStdString();
        m_block = m_document.findBlock(name);
        if (!m_block) {
            return QStringLiteral("*Block \"%1\" not found*\nEnter block name to add a lookup parameter:")
                .arg(QString::fromStdString(name));
        }
        m_stage = Stage::ValueName;
        return QStringLiteral("Enter lookup value name (Enter to finish):");
    }
    case Stage::ValueName: {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            if (m_draft.presets.empty()) {
                m_finished = true;
                return QStringLiteral("*Cancelled: need at least one lookup value*");
            }
            m_block->dynamicLookup = m_draft;
            m_finished = true;
            return QStringLiteral("*Block \"%1\" now has a lookup parameter with %2 value(s)*")
                .arg(QString::fromStdString(m_block->name))
                .arg(m_draft.presets.size());
        }
        m_pendingValueName = trimmed.toStdString();
        m_stage = Stage::ScaleFactor;
        return QStringLiteral("Enter scale factor for \"%1\":").arg(trimmed);
    }
    case Stage::ScaleFactor: {
        bool ok = false;
        const double factor = text.trimmed().toDouble(&ok);
        if (!ok || factor <= 0.0) return QStringLiteral("*Enter a positive number for the scale factor*");
        m_draft.presets.emplace_back(m_pendingValueName, factor);
        m_stage = Stage::ValueName;
        return QStringLiteral("Enter lookup value name (Enter to finish):");
    }
    }
    return std::nullopt;
}
