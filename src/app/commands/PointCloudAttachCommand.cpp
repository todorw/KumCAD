#include "commands/PointCloudAttachCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/PointCloud.h"
#include "core/io/PointCloudFile.h"

std::optional<QString> PointCloudAttachCommand::onText(const QString& text) {
    const QString path = text.trimmed();
    if (path.isEmpty()) {
        m_finished = true;
        return QStringLiteral("*Cancelled*");
    }

    std::vector<lcad::Point2D> points = lcad::readPointCloudXyz(path.toStdString());
    if (points.empty()) {
        return QStringLiteral("*Could not read any points from \"%1\"*\nEnter XYZ file path:").arg(path);
    }

    auto cloud = std::make_unique<lcad::PointCloudEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                           path.toStdString(), std::move(points));
    const std::size_t count = cloud->points().size();
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(cloud)));
    m_finished = true;
    return QStringLiteral("*%1 point(s) attached*").arg(count);
}
