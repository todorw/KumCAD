#pragma once

#include "core/document/Document.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class DrawingView;

// A simplified Markup Set Manager: real redline import needs a markup layer
// laid over the sheet in a separate viewer, which KumCAD can now approximate
// for PDFs (see ImageAttachCommand/PDFATTACH -- attach the reviewer's marked-
// up PDF as an underlay, then annotate over it in KumCAD directly), but
// there's still no DWF import and no actual redline/comment-thread format to
// parse either way. Instead, this browses the current drawing's own
// annotations -- TEXT, MTEXT, LEADER, and MLEADER entities in model space --
// as a lightweight notes list; double-clicking one zooms to it. A practical
// stand-in for "jump between markups," not a real markup-set review flow.
class MarkupSetPanel : public QWidget {
    Q_OBJECT
public:
    MarkupSetPanel(lcad::Document& document, DrawingView& view, QWidget* parent = nullptr);

    void refresh();

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    lcad::Document& m_document;
    DrawingView& m_view;
    QListWidget* m_list;
};
