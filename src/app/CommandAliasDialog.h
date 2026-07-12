#pragma once

#include <QDialog>

class QListWidget;
class CommandAliases;

// A simplified CUI (Customize User Interface): edits the custom command
// alias table (see CommandAliases) that CommandDispatcher checks before its
// own built-in aliases. Real AutoCAD's CUI also covers toolbars, ribbon
// panels, workspaces, and double-click actions -- this only covers command
// aliases, the one piece with a genuinely useful non-visual editor.
class CommandAliasDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommandAliasDialog(CommandAliases& aliases, QWidget* parent = nullptr);

private slots:
    void onAdd();
    void onEdit();
    void onDelete();

private:
    void refresh();
    // Shows the add/edit form; existingAlias empty means "create new".
    // Returns true if the user saved.
    bool editAlias(const QString& existingAlias);

    CommandAliases& m_aliases;
    QListWidget* m_list;
};
