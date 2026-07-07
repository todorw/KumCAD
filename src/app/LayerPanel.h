#pragma once

#include "core/document/Document.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;

// Dockable layer manager: lists Document's layers with a visibility checkbox
// and color swatch, lets the user add layers and pick the current one (where
// new entities get drawn), mirroring AutoCAD's Layer Properties palette.
class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(lcad::Document& document, QWidget* parent = nullptr);

    void refresh();

signals:
    // Emitted whenever something that affects rendering changed (visibility,
    // a new layer) so the canvas can repaint.
    void layersChanged();

private slots:
    void onAddLayer();
    void onItemChanged(QListWidgetItem* item);
    void onCurrentRowChanged(int row);

private:
    lcad::Document& m_document;
    QListWidget* m_list;
    bool m_updating = false;
};
