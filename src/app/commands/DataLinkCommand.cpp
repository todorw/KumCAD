#include "commands/DataLinkCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Table.h"
#include "core/io/Csv.h"

#include <algorithm>

std::optional<QString> DataLinkCommand::onText(const QString& text) {
    if (m_stage != Stage::Path) return std::nullopt;
    const QString path = text.trimmed();
    if (path.isEmpty()) {
        m_finished = true;
        return QStringLiteral("*Cancelled*");
    }
    std::string error;
    const auto rows = lcad::readCsv(path.toStdString(), &error);
    if (!rows || rows->empty()) {
        const QString reason = rows ? QStringLiteral("the file is empty") : QString::fromStdString(error);
        return QStringLiteral("*Could not read \"%1\": %2*\nEnter CSV file path:").arg(path, reason);
    }
    m_rows = *rows;
    m_stage = Stage::Position;
    return QStringLiteral("*%1 row(s), %2 column(s)*\nSpecify insertion point:")
        .arg(m_rows.size())
        .arg(m_rows.front().size());
}

std::optional<QString> DataLinkCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Position || m_rows.empty()) return std::nullopt;

    const int rows = static_cast<int>(m_rows.size());
    const int cols = static_cast<int>(m_rows.front().size());
    const double textHeight = m_document.dimStyle().textHeight;
    const double charWidth = 0.6 * textHeight;
    const double padding = 1.0 * textHeight;

    std::vector<double> colWidths(cols, 2.0 * textHeight);
    for (int c = 0; c < cols; ++c) {
        std::size_t longest = 0;
        for (const auto& row : m_rows) longest = std::max(longest, row[c].size());
        colWidths[c] = std::max(colWidths[c], longest * charWidth + padding);
    }
    const std::vector<double> rowHeights(rows, 1.6 * textHeight);

    std::vector<std::string> cells;
    cells.reserve(static_cast<std::size_t>(rows) * cols);
    for (const auto& row : m_rows) cells.insert(cells.end(), row.begin(), row.end());

    auto table = std::make_unique<lcad::TableEntity>(m_document.reserveEntityId(), m_document.currentLayer(), pt,
                                                      rowHeights, colWidths, cells, textHeight);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(table)));
    m_finished = true;
    return QStringLiteral("*Table imported (%1 x %2)*").arg(rows).arg(cols);
}
