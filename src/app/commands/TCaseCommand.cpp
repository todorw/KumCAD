#include "commands/TCaseCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/MText.h"
#include "core/geometry/Text.h"

namespace {

QString applyCase(const QString& text, const QString& option) {
    if (option == QLatin1String("LOWER")) return text.toLower();
    if (option == QLatin1String("TITLE")) {
        QString result = text.toLower();
        bool startOfWord = true;
        for (QChar& c : result) {
            if (c.isLetter()) {
                if (startOfWord) c = c.toUpper();
                startOfWord = false;
            } else {
                startOfWord = true;
            }
        }
        return result;
    }
    return text.toUpper(); // default: Upper
}

} // namespace

std::optional<QString> TCaseCommand::onText(const QString& text) {
    QString option = text.trimmed().toUpper();
    if (option.isEmpty() || option == QLatin1String("U")) option = QStringLiteral("UPPER");
    else if (option == QLatin1String("L")) option = QStringLiteral("LOWER");
    else if (option == QLatin1String("T")) option = QStringLiteral("TITLE");
    else if (option != QLatin1String("UPPER") && option != QLatin1String("LOWER") && option != QLatin1String("TITLE")) {
        return QStringLiteral("*Invalid option, expected Upper/Lower/Title*");
    }
    apply(option);
    m_finished = true;
    return m_result;
}

bool TCaseCommand::requestFinish() {
    apply(QStringLiteral("UPPER")); // the "<Upper>" default
    m_finished = true;
    return true;
}

void TCaseCommand::apply(const QString& option) {
    auto batch = std::make_unique<lcad::BatchCommand>("TCase");
    int changed = 0;
    for (lcad::EntityId id : m_ids) {
        lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;

        std::unique_ptr<lcad::Entity> clone = e->clone();
        if (e->type() == lcad::EntityType::Text) {
            auto& t = static_cast<lcad::TextEntity&>(*clone);
            t.setText(applyCase(QString::fromStdString(t.text()), option).toStdString());
        } else if (e->type() == lcad::EntityType::MText) {
            auto& t = static_cast<lcad::MTextEntity&>(*clone);
            t.setText(applyCase(QString::fromStdString(t.text()), option).toStdString());
        } else {
            continue;
        }
        batch->add(std::make_unique<lcad::ReplaceEntityCommand>(m_document, id, std::move(clone)));
        ++changed;
    }
    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
    m_result = QStringLiteral("*%1 text object(s) changed*").arg(changed);
}
