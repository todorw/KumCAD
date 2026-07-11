#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// TABLE: insertion point, then row/column count and cell size. Places an
// empty grid as one undo step; fill it in afterward with TABLEDIT.
class TableCommand : public DrawCommand {
public:
    explicit TableCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("TABLE  Specify insertion point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Insert, Rows, Columns, ColumnWidth, RowHeight };

    lcad::Document& m_document;
    Stage m_stage = Stage::Insert;
    lcad::Point2D m_position;
    int m_rows = 3;
    int m_columns = 3;
    double m_columnWidth = 2.5;
    bool m_finished = false;
};
