#include "BlockEditorWindow.h"

#include "CommandDispatcher.h"
#include "CommandLine.h"
#include "DrawingView.h"
#include "core/document/BlockEdit.h"

#include <QCloseEvent>
#include <QDockWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QToolBar>

BlockEditorWindow::BlockEditorWindow(lcad::Document& parentDoc, std::string blockName, QWidget* parent)
    : QMainWindow(parent), m_parentDoc(parentDoc), m_blockName(std::move(blockName)),
      m_blockDoc(lcad::extractBlockForEditing(parentDoc, m_blockName)) {
    resize(1000, 700);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Block Editor \xE2\x80\x94 %1").arg(QString::fromStdString(m_blockName)));

    m_view = new DrawingView(m_blockDoc, this);
    setCentralWidget(m_view);

    m_commandLine = new CommandLine(this);
    m_dispatcher = new CommandDispatcher(m_blockDoc, *m_commandLine, this);
    m_view->setDispatcher(m_dispatcher);
    m_dispatcher->setView(m_view);

    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::previewChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::documentChanged, this, [this]() { m_dirty = true; });
    connect(m_view, &DrawingView::documentEdited, this, [this]() {
        m_dirty = true;
        m_view->update();
    });

    auto* commandDock = new QDockWidget(QStringLiteral("Command Line"), this);
    commandDock->setObjectName(QStringLiteral("BlockEditorCommandLineDock"));
    commandDock->setWidget(m_commandLine);
    commandDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, commandDock);

    auto* toolbar = addToolBar(QStringLiteral("Block Editor"));
    toolbar->addAction(QStringLiteral("Save Block Definition"), this, &BlockEditorWindow::saveBlock);
    toolbar->addAction(QStringLiteral("Close"), this, &QWidget::close);

    m_commandLine->appendLine(
        QStringLiteral("BEDIT \xE2\x80\x94 editing block \"%1\". Draw/edit like any drawing; \"Save Block "
                       "Definition\" writes changes back to every INSERT of it.")
            .arg(QString::fromStdString(m_blockName)));
    m_commandLine->appendLine(QStringLiteral("Command:"));
    m_commandLine->input()->setFocus();
}

void BlockEditorWindow::saveBlock() {
    lcad::BlockDefinition* block = m_parentDoc.findBlock(m_blockName);
    if (!block) {
        m_commandLine->appendLine(QStringLiteral("*Block \"%1\" no longer exists in the parent drawing*")
                                       .arg(QString::fromStdString(m_blockName)));
        return;
    }
    lcad::applyBlockEdit(*block, m_blockDoc);
    m_dirty = false;
    m_commandLine->appendLine(QStringLiteral("*Block \"%1\" saved -- every INSERT of it now reflects this edit*")
                                   .arg(QString::fromStdString(m_blockName)));
    emit blockSaved();
}

void BlockEditorWindow::closeEvent(QCloseEvent* event) {
    if (!m_dirty) {
        event->accept();
        return;
    }
    const auto choice =
        QMessageBox::question(this, QStringLiteral("Unsaved Block Changes"),
                              QStringLiteral("Block \"%1\" has unsaved changes. Save before closing?")
                                  .arg(QString::fromStdString(m_blockName)),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        event->ignore();
        return;
    }
    if (choice == QMessageBox::Save) saveBlock();
    event->accept();
}
