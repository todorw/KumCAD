#include "CommandAliases.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

QString CommandAliases::filePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/aliases.pgp");
}

void CommandAliases::load() {
    m_aliases.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))) continue;
        const int comma = line.indexOf(QLatin1Char(','));
        if (comma < 0) continue;
        const QString alias = line.left(comma).trimmed().toUpper();
        QString command = line.mid(comma + 1).trimmed().toUpper();
        if (command.startsWith(QLatin1Char('*'))) command.remove(0, 1); // acad.pgp's "*COMMAND" convention
        if (alias.isEmpty() || command.isEmpty()) continue;
        m_aliases[alias] = command;
    }
}

void CommandAliases::save() const {
    QFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;

    QTextStream out(&file);
    out << "; KumCAD custom command aliases (like AutoCAD's acad.pgp)\n";
    out << "; format: ALIAS,*COMMAND\n";
    for (auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it) {
        out << it.key() << ",*" << it.value() << "\n";
    }
}

std::optional<QString> CommandAliases::resolve(const QString& alias) const {
    const auto it = m_aliases.constFind(alias.toUpper());
    if (it == m_aliases.constEnd()) return std::nullopt;
    return it.value();
}

void CommandAliases::set(const QString& alias, const QString& canonicalCommand) {
    m_aliases[alias.toUpper()] = canonicalCommand.toUpper();
    save();
}

bool CommandAliases::remove(const QString& alias) {
    const bool removed = m_aliases.remove(alias.toUpper()) > 0;
    if (removed) save();
    return removed;
}
