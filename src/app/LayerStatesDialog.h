#pragma once

#include "core/document/Document.h"

#include <QDialog>

class QListWidget;

// AutoCAD's Layer States Manager: save the current layers'
// visibility/lock/color/linetype/lineweight as a named snapshot, and restore
// one later (undoable -- restoring goes through RestoreLayerStateCommand).
class LayerStatesDialog : public QDialog {
    Q_OBJECT
public:
    explicit LayerStatesDialog(lcad::Document& document, QWidget* parent = nullptr);

signals:
    // Emitted after a restore actually changes the document, so the caller
    // can refresh its own layer list and repaint the canvas.
    void layerStateApplied();

private slots:
    void onSaveNew();
    void onRestore();
    void onDelete();

private:
    void refresh();

    lcad::Document& m_document;
    QListWidget* m_list;
};
