#include "commands/WBlockCommand.h"

#include "core/document/DocumentExtract.h"
#include "core/io/DxfWriter.h"

std::optional<QString> WBlockCommand::onText(const QString& text) {
    m_finished = true;

    std::vector<lcad::EntityId> ids = m_selectedIds;
    if (ids.empty()) { // real AutoCAD's own "Entire Drawing" fallback
        for (const lcad::Entity* e : m_document.entities()) ids.push_back(e->id());
    }

    const lcad::Document extracted = lcad::extractSubset(m_document, ids);

    std::string error;
    if (!lcad::writeDxf(extracted, text.trimmed().toStdString(), &error)) {
        return QStringLiteral("*%1*").arg(QString::fromStdString(error));
    }
    return QStringLiteral("*%1 object(s) written to %2*").arg(ids.size()).arg(text.trimmed());
}
