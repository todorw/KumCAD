#include "commands/DataExtractionCommand.h"

#include "core/document/DataExtraction.h"

std::optional<QString> DataExtractionCommand::onPoint(const lcad::Point2D& pt) {
    m_finished = true;
    const lcad::DataExtractionResult result = lcad::extractBlockData(m_document);
    lcad::buildDataExtractionTable(m_document, result, pt);
    return QStringLiteral("*Data extraction placed (%1 row(s), %2 attribute column(s))*")
        .arg(result.rows.size())
        .arg(result.attributeColumns.size());
}
