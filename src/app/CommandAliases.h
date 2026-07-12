#pragma once

#include <QMap>
#include <QString>

#include <optional>

// User-customizable command aliases (a simplified CUI: real AutoCAD's
// alias table lives in acad.pgp and is edited through the CUI editor).
// Persisted as plain text under the platform config directory, one
// "ALIAS,*COMMAND" line per entry (matching acad.pgp's own syntax; ';'
// starts a comment). Checked by CommandDispatcher before its own built-in
// alias table, so a custom alias adds a new shortcut rather than being able
// to redefine what a built-in name/alias does.
class CommandAliases {
public:
    // Loads from disk; leaves the table empty if the file doesn't exist yet.
    void load();

    // nullopt if alias isn't a known custom alias.
    std::optional<QString> resolve(const QString& alias) const;

    // Adds or overwrites, and saves immediately. canonicalCommand should be
    // a real KumCAD command name (e.g. "MOVE"), not another alias.
    void set(const QString& alias, const QString& canonicalCommand);
    bool remove(const QString& alias); // also saves immediately; false if alias wasn't present

    const QMap<QString, QString>& all() const { return m_aliases; }

private:
    static QString filePath();
    void save() const;

    QMap<QString, QString> m_aliases; // alias (upper) -> canonical command (upper)
};
