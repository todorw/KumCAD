#pragma once

#include "core/document/Document.h"

#include <QWidget>

class DrawingView;
class QComboBox;
class QFormLayout;
class QLabel;

// Dockable properties inspector: shows the selected entity's type and
// geometry (read-only -- see task notes for why) plus an editable Layer
// dropdown that reassigns the selection's layer as a single undoable step.
// Mirrors (a small slice of) AutoCAD's Properties palette.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(lcad::Document& document, DrawingView& view, QWidget* parent = nullptr);

signals:
    // Emitted after a layer reassignment, so the canvas repaints and the
    // window's dirty flag gets set, same as LayerPanel::layersChanged().
    void documentChanged();

public slots:
    void refresh();

private slots:
    void onLayerComboChanged(int index);

private:
    void addRow(const QString& label, const QString& value);

    lcad::Document& m_document;
    DrawingView& m_view;
    QLabel* m_summaryLabel;
    QComboBox* m_layerCombo;
    QFormLayout* m_fieldsForm;
    bool m_updating = false;
};
