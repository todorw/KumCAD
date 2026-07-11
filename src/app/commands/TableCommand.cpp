#include "commands/TableCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Table.h"

std::optional<QString> TableCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Insert) return std::nullopt;
    m_position = pt;
    m_stage = Stage::Rows;
    return QStringLiteral("Enter number of rows <3>:");
}

std::optional<QString> TableCommand::onScalar(double value) {
    switch (m_stage) {
    case Stage::Rows:
        if (value < 1 || value > 100) return QStringLiteral("*Rows must be 1-100*");
        m_rows = static_cast<int>(value);
        m_stage = Stage::Columns;
        return QStringLiteral("Enter number of columns <3>:");
    case Stage::Columns:
        if (value < 1 || value > 50) return QStringLiteral("*Columns must be 1-50*");
        m_columns = static_cast<int>(value);
        m_stage = Stage::ColumnWidth;
        return QStringLiteral("Specify column width <2.5>:");
    case Stage::ColumnWidth:
        if (value <= 1e-6) return QStringLiteral("*Width must be positive*");
        m_columnWidth = value;
        m_stage = Stage::RowHeight;
        return QStringLiteral("Specify row height <1>:");
    case Stage::RowHeight: {
        if (value <= 1e-6) return QStringLiteral("*Height must be positive*");
        const double textHeight = m_document.dimStyle().textHeight;
        auto table = std::make_unique<lcad::TableEntity>(
            m_document.reserveEntityId(), m_document.currentLayer(), m_position,
            std::vector<double>(m_rows, value), std::vector<double>(m_columns, m_columnWidth),
            std::vector<std::string>(static_cast<std::size_t>(m_rows) * m_columns), textHeight);
        m_document.commandStack().execute(
            std::make_unique<lcad::AddEntityCommand>(m_document, std::move(table)));
        m_finished = true;
        return QStringLiteral("*Table placed (%1 x %2)*").arg(m_rows).arg(m_columns);
    }
    default:
        return std::nullopt;
    }
}
