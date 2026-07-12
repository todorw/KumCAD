#include "commands/ClipboardPasteCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Image.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <algorithm>

std::optional<QString> ClipboardPasteCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Position) return std::nullopt;
    m_position = pt;
    m_stage = Stage::Width;
    return QStringLiteral("Specify width <10>:");
}

std::optional<QString> ClipboardPasteCommand::onScalar(double value) {
    if (m_stage != Stage::Width) return std::nullopt;
    if (value <= 0) return QStringLiteral("*Width must be positive*");

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/pasted");
    QDir().mkpath(dir);
    const QString path =
        dir + QStringLiteral("/paste_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    if (!m_image.save(path, "PNG")) {
        m_finished = true;
        m_result = QStringLiteral("*Could not save the pasted image*");
        return m_result;
    }

    const double aspectRatio = static_cast<double>(m_image.height()) / std::max(1, m_image.width());
    auto image = std::make_unique<lcad::ImageEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                      path.toStdString(), m_position, value, value * aspectRatio);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(image)));
    m_finished = true;
    m_result = QStringLiteral("*Pasted*");
    return m_result;
}

bool ClipboardPasteCommand::requestFinish() {
    if (m_stage == Stage::Width) {
        onScalar(10.0); // the "<10>" default
        return true;
    }
    m_finished = true;
    return false;
}
