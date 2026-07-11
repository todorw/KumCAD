#include "commands/LengthenCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/ModifyOps.h"

namespace {

lcad::Entity* pickLengthenTarget(lcad::Document& document, const lcad::Point2D& pt, double tolerance) {
    lcad::Entity* best = nullptr;
    double bestDist = tolerance;
    for (lcad::Entity* e : document.entities()) {
        const lcad::Layer* layer = document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        const lcad::EntityType t = e->type();
        if (t != lcad::EntityType::Line && t != lcad::EntityType::Arc) continue;
        const double d = e->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = e;
        }
    }
    return best;
}

} // namespace

QString LengthenCommand::start() {
    return QStringLiteral("LENGTHEN  ") + prompt();
}

QString LengthenCommand::prompt() const {
    switch (m_mode) {
    case Mode::None:
        return QStringLiteral("Select object to report length, or set a mode [DElta/Percent/Total]:");
    case Mode::Delta:
        return QStringLiteral("Select object to change (delta %1) or [DElta/Percent/Total/Enter to finish]:")
            .arg(m_value);
    case Mode::Percent:
        return QStringLiteral("Select object to change (%1%%) or [DElta/Percent/Total/Enter to finish]:").arg(m_value);
    case Mode::Total:
        return QStringLiteral("Select object to change (total %1) or [DElta/Percent/Total/Enter to finish]:")
            .arg(m_value);
    }
    return {};
}

std::optional<QString> LengthenCommand::onOption(const QString& option) {
    const QString opt = option.toUpper();
    if (opt == QLatin1String("DE") || opt == QLatin1String("DELTA")) {
        m_mode = Mode::Delta;
        m_awaitingValue = true;
        return QStringLiteral("Enter delta length:");
    }
    if (opt == QLatin1String("P") || opt == QLatin1String("PERCENT")) {
        m_mode = Mode::Percent;
        m_awaitingValue = true;
        return QStringLiteral("Enter percentage of original length <100>:");
    }
    if (opt == QLatin1String("T") || opt == QLatin1String("TOTAL")) {
        m_mode = Mode::Total;
        m_awaitingValue = true;
        return QStringLiteral("Enter total length:");
    }
    return std::nullopt;
}

std::optional<QString> LengthenCommand::onScalar(double value) {
    if (!m_awaitingValue) return std::nullopt;
    if (m_mode == Mode::Percent && value <= 0.0) return QStringLiteral("*Percentage must be positive*\n") + prompt();
    if (m_mode == Mode::Total && value <= 0.0) return QStringLiteral("*Total length must be positive*\n") + prompt();
    m_value = value;
    m_awaitingValue = false;
    return prompt();
}

std::optional<QString> LengthenCommand::onPoint(const lcad::Point2D& pt) {
    lcad::Entity* target = pickLengthenTarget(m_document, pt, m_pickTolerance);
    if (!target) return QStringLiteral("*No line or arc there*\n") + prompt();

    const auto length = lcad::curveLength(*target);
    if (!length) return QStringLiteral("*Cannot measure that entity*\n") + prompt();

    if (m_mode == Mode::None) {
        return QStringLiteral("Current length: %1\n%2").arg(*length, 0, 'f', 4).arg(prompt());
    }

    double deltaLen = 0.0;
    switch (m_mode) {
    case Mode::Delta:
        deltaLen = m_value;
        break;
    case Mode::Percent:
        deltaLen = *length * (m_value / 100.0) - *length;
        break;
    case Mode::Total:
        deltaLen = m_value - *length;
        break;
    case Mode::None:
        break;
    }

    auto lengthened = lcad::lengthenedClone(*target, pt, deltaLen);
    if (!lengthened) return QStringLiteral("*Change would make the entity degenerate*\n") + prompt();

    m_document.commandStack().execute(
        std::make_unique<lcad::ReplaceEntityCommand>(m_document, target->id(), std::move(lengthened)));
    return QStringLiteral("*Length changed by %1*\n%2").arg(deltaLen, 0, 'f', 4).arg(prompt());
}
