#pragma once

#include "core/document/Document.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class DrawingView;

// A simplified Markup Set Manager: KumCAD has no DWF/PDF import, so there's
// no real markup set to review the way AutoCAD's does (redlines made by a
// reviewer in a separate DWF/PDF viewer, imported back for the author to
// address). Instead, this browses the current drawing's own annotations --
// TEXT, MTEXT, LEADER, and MLEADER entities in model space -- as a
// lightweight notes list; double-clicking one zooms to it. A practical
// stand-in for "jump between markups," not a DWF/PDF redline import.
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
