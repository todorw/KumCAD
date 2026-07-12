#pragma once

#include "core/document/Document.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;

// A simplified Design Center: browse a DXF/DWG file's block definitions and
// layers, and copy either into the current document by double-clicking --
// content reuse across drawings without opening the source file. Block name
// collisions get AutoCAD's own "Name(2)" treatment; a colliding layer name
// is just reused (its color/linetype aren't overwritten). AutoCAD's real
// Design Center also browses dimension/text/table styles and hatch
// patterns, and supports drag-and-drop; neither is implemented here.
class DesignCenterPanel : public QWidget {
    Q_OBJECT
public:
    explicit DesignCenterPanel(lcad::Document& document, QWidget* parent = nullptr);

signals:
    // Emitted after a block or layer is copied in, so the canvas/panels can refresh.
    void documentImported();

private slots:
    void onBrowse();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void refreshList();
    void importBlock(const std::string& name);
    void importLayer(const std::string& name);

    lcad::Document& m_document;
    lcad::Document m_source; // scratch copy of the browsed file
    QString m_sourcePath;
    QListWidget* m_list;
};
