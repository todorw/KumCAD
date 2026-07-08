#include "CommandDispatcher.h"

#include "CommandLine.h"
#include "DrawingView.h"
#include "commands/ArcCommand.h"
#include "commands/CircleCommand.h"
#include "commands/CopyCommand.h"
#include "commands/EllipseCommand.h"
#include "commands/LineCommand.h"
#include "commands/MoveCommand.h"
#include "commands/PolylineCommand.h"
#include "commands/RotateCommand.h"
#include "commands/ScaleCommand.h"
#include "commands/TextCommand.h"

#include <QStringList>

CommandDispatcher::CommandDispatcher(lcad::Document& document, CommandLine& commandLine, QObject* parent)
    : QObject(parent), m_document(document), m_commandLine(commandLine) {
    connect(&m_commandLine, &CommandLine::commandEntered, this, &CommandDispatcher::handleCommandText);
}

void CommandDispatcher::startCommand(std::unique_ptr<DrawCommand> command, const QString& name) {
    m_activeCommand = std::move(command);
    m_commandLine.appendLine(name);
    m_commandLine.appendLine(m_activeCommand->start());
}

void CommandDispatcher::finishCommand() {
    m_activeCommand.reset();
    m_commandLine.appendLine(QStringLiteral("Command:"));
    emit documentChanged();
    emit previewChanged();
}

void CommandDispatcher::handleCommandText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_activeCommand) {
        if (m_activeCommand->wantsTextInput()) {
            // Free-text stage (e.g. TEXT's "Enter text:"): route everything here,
            // including an empty Enter, rather than trying to parse it as a point/number.
            const std::optional<QString> prompt = m_activeCommand->onText(trimmed);
            if (m_activeCommand->isFinished()) {
                finishCommand();
                return;
            }
            if (prompt) {
                m_commandLine.appendLine(*prompt);
                emit documentChanged();
                return;
            }
            m_commandLine.appendLine(QStringLiteral("*Invalid input*"));
            return;
        }
        if (trimmed.isEmpty()) {
            handleFinishRequested();
            return;
        }
        lcad::Point2D pt;
        if (tryParsePoint(trimmed, pt)) {
            handlePointPicked(pt);
            return;
        }
        bool isNumber = false;
        const double scalar = trimmed.toDouble(&isNumber);
        if (isNumber) {
            const std::optional<QString> prompt = m_activeCommand->onScalar(scalar);
            if (m_activeCommand->isFinished()) {
                finishCommand();
                return;
            }
            if (prompt) {
                m_commandLine.appendLine(*prompt);
                emit documentChanged();
                return;
            }
        }
        m_commandLine.appendLine(QStringLiteral("*Invalid input, expected x,y or a number*"));
        return;
    }

    const QString cmd = trimmed.toUpper();
    if (cmd == QLatin1String("LINE") || cmd == QLatin1String("L")) {
        startCommand(std::make_unique<LineCommand>(m_document), QStringLiteral("LINE"));
    } else if (cmd == QLatin1String("CIRCLE") || cmd == QLatin1String("C")) {
        startCommand(std::make_unique<CircleCommand>(m_document), QStringLiteral("CIRCLE"));
    } else if (cmd == QLatin1String("ARC") || cmd == QLatin1String("A")) {
        startCommand(std::make_unique<ArcCommand>(m_document), QStringLiteral("ARC"));
    } else if (cmd == QLatin1String("PLINE") || cmd == QLatin1String("PL")) {
        startCommand(std::make_unique<PolylineCommand>(m_document), QStringLiteral("PLINE"));
    } else if (cmd == QLatin1String("ELLIPSE") || cmd == QLatin1String("EL")) {
        startCommand(std::make_unique<EllipseCommand>(m_document), QStringLiteral("ELLIPSE"));
    } else if (cmd == QLatin1String("TEXT") || cmd == QLatin1String("DT")) {
        startCommand(std::make_unique<TextCommand>(m_document), QStringLiteral("TEXT"));
    } else if (cmd == QLatin1String("MOVE") || cmd == QLatin1String("M")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<MoveCommand>(m_document, ids), QStringLiteral("MOVE"));
    } else if (cmd == QLatin1String("COPY") || cmd == QLatin1String("CO") || cmd == QLatin1String("CP")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<CopyCommand>(m_document, ids), QStringLiteral("COPY"));
    } else if (cmd == QLatin1String("ROTATE") || cmd == QLatin1String("RO")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<RotateCommand>(m_document, ids), QStringLiteral("ROTATE"));
    } else if (cmd == QLatin1String("SCALE") || cmd == QLatin1String("SC")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<ScaleCommand>(m_document, ids), QStringLiteral("SCALE"));
    } else if (cmd == QLatin1String("ERASE") || cmd == QLatin1String("E")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) {
            m_view->eraseSelection();
            m_commandLine.appendLine(QStringLiteral("*%1 erased*").arg(ids.size()));
            emit documentChanged();
        }
    } else if (cmd == QLatin1String("UNDO") || cmd == QLatin1String("U")) {
        undo();
    } else if (cmd == QLatin1String("REDO")) {
        redo();
    } else if (!cmd.isEmpty()) {
        m_commandLine.appendLine(QStringLiteral("*Unknown command \"%1\"*").arg(trimmed));
    }
}

void CommandDispatcher::handlePointPicked(const lcad::Point2D& pt) {
    if (!m_activeCommand) return;
    const std::optional<QString> prompt = m_activeCommand->onPoint(pt);
    if (m_activeCommand->isFinished()) {
        finishCommand();
        return;
    }
    if (prompt) m_commandLine.appendLine(*prompt);
    emit documentChanged();
}

void CommandDispatcher::handleMouseMoved(const lcad::Point2D& pt) {
    if (!m_activeCommand) return;
    m_activeCommand->onPreviewPoint(pt);
    emit previewChanged();
}

void CommandDispatcher::handleFinishRequested() {
    if (!m_activeCommand) return;
    m_activeCommand->requestFinish();
    finishCommand();
}

void CommandDispatcher::handleEscape() {
    if (!m_activeCommand) return;
    m_activeCommand->cancel();
    finishCommand();
}

void CommandDispatcher::undo() {
    m_document.commandStack().undo();
    m_commandLine.appendLine(QStringLiteral("*Undo*"));
    emit documentChanged();
}

void CommandDispatcher::redo() {
    m_document.commandStack().redo();
    m_commandLine.appendLine(QStringLiteral("*Redo*"));
    emit documentChanged();
}

std::vector<lcad::EntityId> CommandDispatcher::selectionForModify() const {
    if (!m_view || !m_view->hasSelection()) {
        m_commandLine.appendLine(QStringLiteral("*Select objects first, then run this command*"));
        return {};
    }
    return m_view->selectedIds();
}

bool CommandDispatcher::tryParsePoint(const QString& text, lcad::Point2D& out) {
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 2) return false;
    bool okX = false;
    bool okY = false;
    const double x = parts[0].trimmed().toDouble(&okX);
    const double y = parts[1].trimmed().toDouble(&okY);
    if (!okX || !okY) return false;
    out = lcad::Point2D(x, y);
    return true;
}
