#pragma once

#include "core/document/Document.h"

#include <QDialog>

class QComboBox;
class QListWidget;

// AutoCAD's Plot Style Table editor, simplified (see core/document/PlotStyle.h):
// a mode selector (Named/STB vs Color-dependent/CTB) plus create/edit/delete
// of the active table's rows. Named styles override plotted color/lineweight/
// linetype/screening and are assigned per-layer via the Layer panel's context
// menu ("Plot Style" submenu); CTB rows are keyed by the displayed color's
// ACI and need no assignment at all, matching real AutoCAD's .ctb behavior.
class PlotStyleDialog : public QDialog {
    Q_OBJECT
public:
    explicit PlotStyleDialog(lcad::Document& document, QWidget* parent = nullptr);

private slots:
    void onNew();
    void onEdit();
    void onDelete();
    void onModeChanged(int index);

private:
    void refresh();
    bool colorDependent() const;
    // Shows the New/Edit editor for style (by name; empty means "create new").
    // Returns true if the user saved changes.
    bool editStyle(const QString& existingName);
    // Same, for a CTB row (aci <= 0 means "create new").
    bool editCtbEntry(int aci);

    lcad::Document& m_document;
    QComboBox* m_modeCombo;
    QListWidget* m_list;
};
