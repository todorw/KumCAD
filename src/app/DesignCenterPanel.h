#pragma once

#include "core/document/Document.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;

// A simplified Design Center: browse a DXF/DWG file's block definitions,
// layers, dimension styles, text styles, and multileader styles, and copy
// any of them into the current document by double-clicking -- content reuse
// across drawings without opening the source file. Block name collisions
// get AutoCAD's own "Name(2)" treatment; a colliding layer/style name is
// just left as-is in the target (matching the layer behavior this already
// had) rather than silently overwritten. Two things AutoCAD's real Design
// Center also browses have no equivalent here to import: hatch patterns are
// a fixed built-in set shared by every KumCAD document rather than a
// per-drawing custom .pat collection, and there's no table-style concept at
// all (TableEntity's row/column sizing and text height are per-instance).
// Drag-and-drop isn't implemented either -- double-click only.
class DesignCenterPanel : public QWidget {
    Q_OBJECT
public:
    explicit DesignCenterPanel(lcad::Document& document, QWidget* parent = nullptr);

signals:
    // Emitted after something is copied in, so the canvas/panels can refresh.
    void documentImported();

private slots:
    void onBrowse();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void refreshList();
    void importBlock(const std::string& name);
    void importLayer(const std::string& name);
    void importDimStyle(const std::string& name);
    void importTextStyle(const std::string& name);
    void importMLeaderStyle(const std::string& name);

    lcad::Document& m_document;
    lcad::Document m_source; // scratch copy of the browsed file
    QString m_sourcePath;
    QListWidget* m_list;
};
