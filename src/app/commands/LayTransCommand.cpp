#include "commands/LayTransCommand.h"

#include "core/document/LayerTranslator.h"

std::optional<QString> LayTransCommand::onText(const QString& text) {
    m_finished = true;
    const QString path = text.trimmed();
    if (path.isEmpty()) {
        m_result = QStringLiteral("*Cancelled*");
        return m_result;
    }

    std::vector<lcad::LayerTranslation> mappings;
    std::string error;
    if (!lcad::loadLayerTranslationFile(path.toStdString(), mappings, &error)) {
        m_result = QStringLiteral("*Could not read mapping file: %1*").arg(QString::fromStdString(error));
        return m_result;
    }
    if (mappings.empty()) {
        m_result = QStringLiteral("*No valid mappings in that file*");
        return m_result;
    }

    const auto result = lcad::applyLayerTranslations(m_document, mappings);
    QString msg = QStringLiteral("*LAYTRANS: %1 renamed, %2 merged*").arg(result.renamed).arg(result.merged);
    if (!result.notFound.empty()) {
        QString names;
        for (const std::string& n : result.notFound) {
            if (!names.isEmpty()) names += QStringLiteral(", ");
            names += QString::fromStdString(n);
        }
        msg += QStringLiteral(" (not found: %1)").arg(names);
    }
    m_result = msg;
    return m_result;
}
