#include "commands/LengthTuneCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Track.h"
#include "core/pcb/LengthTuning.h"

QString LengthTuneCommand::start() {
    const lcad::Entity* e = m_document.findEntity(m_trackId);
    const double current = (e && e->type() == lcad::EntityType::Track)
                               ? lcad::pathLength(static_cast<const lcad::TrackEntity&>(*e).vertices())
                               : 0.0;
    return QStringLiteral("LENGTHTUNE  Current length %1 -- target length:").arg(current, 0, 'g', 5);
}

std::optional<QString> LengthTuneCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_stage == 0) {
        bool ok = false;
        const double v = trimmed.toDouble(&ok);
        if (!ok || v <= 0.0) return QStringLiteral("*Enter a positive target length*");
        m_targetLength = v;
        m_stage = 1;
        return QStringLiteral("Meander amplitude <0.5>:");
    }
    if (m_stage == 1) {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v <= 0.0) return QStringLiteral("*Enter a positive amplitude*");
            m_amplitude = v;
        }
        m_stage = 2;
        return QStringLiteral("Meander pitch <%1>:").arg(m_amplitude * 2.0, 0, 'g', 3);
    }

    double pitch = m_amplitude * 2.0;
    if (!trimmed.isEmpty()) {
        bool ok = false;
        const double v = trimmed.toDouble(&ok);
        if (!ok || v <= 0.0) return QStringLiteral("*Enter a positive pitch*");
        pitch = v;
    }
    apply(pitch);
    m_finished = true;
    return m_result;
}

void LengthTuneCommand::apply(double pitch) {
    const lcad::Entity* e = m_document.findEntity(m_trackId);
    if (!e || e->type() != lcad::EntityType::Track) {
        m_result = QStringLiteral("*Track no longer exists*");
        return;
    }
    const auto& track = static_cast<const lcad::TrackEntity&>(*e);

    const lcad::TuneResult tuned = lcad::tuneTrackLength(track.vertices(), m_targetLength, m_amplitude, pitch);
    auto replacement = std::make_unique<lcad::TrackEntity>(m_trackId, track.layer(), tuned.path, track.width());
    m_document.commandStack().execute(std::make_unique<lcad::ReplaceEntityCommand>(m_document, m_trackId, std::move(replacement)));

    m_result = tuned.metTarget
                  ? QStringLiteral("*Length tuned: %1 -> %2*").arg(tuned.originalLength, 0, 'g', 5).arg(tuned.achievedLength, 0, 'g', 5)
                  : QStringLiteral("*Not enough room for the full target: %1 -> %2 (wanted %3)*")
                        .arg(tuned.originalLength, 0, 'g', 5)
                        .arg(tuned.achievedLength, 0, 'g', 5)
                        .arg(m_targetLength, 0, 'g', 5);
}
