#pragma once

#include "core/document/Document.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class CommandDispatcher;

// A simplified tool palette: lists the document's block definitions.
// Clicking one starts INSERT pre-filled with that block's name, leaving the
// user to click the insertion point on the canvas as usual -- quick reuse of
// existing blocks without typing INSERT + the name each time. AutoCAD's real
// Tool Palettes also support drag-and-drop placement and non-block tools
// (hatches, commands); neither is implemented here.
class ToolPalette : public QWidget {
    Q_OBJECT
public:
    ToolPalette(lcad::Document& document, CommandDispatcher& dispatcher, QWidget* parent = nullptr);

    void refresh();

private slots:
    void onItemClicked(QListWidgetItem* item);

private:
    lcad::Document& m_document;
    CommandDispatcher& m_dispatcher;
    QListWidget* m_list;
};
