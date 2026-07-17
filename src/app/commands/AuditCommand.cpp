#include "commands/AuditCommand.h"

#include "core/document/Audit.h"

std::optional<QString> AuditCommand::onText(const QString& text) {
    m_finished = true;
    const QString option = text.trimmed().toUpper();
    const bool fix = option == QLatin1String("Y") || option == QLatin1String("YES");

    const auto result = lcad::runAudit(m_document, fix);
    if (result.issues.empty()) {
        m_result = QStringLiteral("*AUDIT: no errors found*");
        return m_result;
    }

    QString msg = fix ? QStringLiteral("*AUDIT: %1 error(s) found, %2 fixed*\n")
                            .arg(result.issues.size())
                            .arg(result.fixedCount)
                      : QStringLiteral("*AUDIT: %1 error(s) found (not fixed -- rerun with Yes to fix)*\n")
                            .arg(result.issues.size());
    for (const lcad::AuditIssue& issue : result.issues) {
        msg += QStringLiteral("  Entity %1: %2%3\n")
                  .arg(static_cast<qulonglong>(issue.entityId))
                  .arg(QString::fromStdString(issue.description))
                  .arg(issue.fixed ? QStringLiteral(" (fixed)") : QString());
    }
    m_result = msg.trimmed();
    return m_result;
}
