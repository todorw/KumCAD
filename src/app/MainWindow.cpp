#include "MainWindow.h"

#include "CommandDispatcher.h"
#include "CommandLine.h"
#include "DrawingView.h"
#include "IconFactory.h"
#include "LayerPanel.h"

#include <QAction>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("KumCAD"));
    resize(1280, 800);

    m_view = new DrawingView(m_document, this);
    setCentralWidget(m_view);

    m_commandLine = new CommandLine(this);
    m_dispatcher = new CommandDispatcher(m_document, *m_commandLine, this);
    m_view->setDispatcher(m_dispatcher);
    m_dispatcher->setView(m_view);

    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::previewChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_view, &DrawingView::mouseWorldMoved, this, &MainWindow::updateCoordLabel);

    setupDocks();
    setupMenusAndToolbar();

    m_coordLabel = new QLabel(QStringLiteral("0.000, 0.000"), this);
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->showMessage(QStringLiteral("Ready"));

    m_commandLine->appendLine(QStringLiteral(
        "KumCAD — type a command (LINE, CIRCLE, ARC, PLINE, MOVE, COPY, ROTATE, SCALE, ERASE, UNDO, REDO) and press Enter."));
    m_commandLine->appendLine(QStringLiteral("Command:"));
    m_commandLine->input()->setFocus();
}

void MainWindow::setupDocks() {
    auto* commandDock = new QDockWidget(QStringLiteral("Command Line"), this);
    commandDock->setObjectName(QStringLiteral("CommandLineDock"));
    commandDock->setWidget(m_commandLine);
    commandDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, commandDock);

    m_layerPanel = new LayerPanel(m_document, this);
    connect(m_layerPanel, &LayerPanel::layersChanged, m_view, QOverload<>::of(&QWidget::update));

    auto* layerDock = new QDockWidget(QStringLiteral("Layers"), this);
    layerDock->setObjectName(QStringLiteral("LayerDock"));
    layerDock->setWidget(m_layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, layerDock);
}

void MainWindow::setupMenusAndToolbar() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, m_dispatcher, &CommandDispatcher::undo);
    editMenu->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, m_dispatcher, &CommandDispatcher::redo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("&Erase Selected"), QKeySequence::Delete, m_view, &DrawingView::eraseSelection);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("Zoom &Extents"), this, [this]() { m_view->zoomExtents(); });

    QToolBar* toolbar = addToolBar(QStringLiteral("Draw"));
    toolbar->setIconSize(QSize(22, 22));

    auto* selectAction = toolbar->addAction(IconFactory::selectIcon(), QStringLiteral("Select"));
    selectAction->setToolTip(QStringLiteral("Select (Esc) — click or drag entities; drag a grip to reshape"));
    connect(selectAction, &QAction::triggered, m_dispatcher, &CommandDispatcher::handleEscape);

    toolbar->addSeparator();

    auto addCommandAction = [this, toolbar](const QIcon& icon, const QString& label, const QString& command) {
        QAction* action = toolbar->addAction(icon, label);
        connect(action, &QAction::triggered, this, [this, command]() { m_dispatcher->handleCommandText(command); });
    };
    addCommandAction(IconFactory::lineIcon(), QStringLiteral("Line"), QStringLiteral("LINE"));
    addCommandAction(IconFactory::circleIcon(), QStringLiteral("Circle"), QStringLiteral("CIRCLE"));
    addCommandAction(IconFactory::arcIcon(), QStringLiteral("Arc"), QStringLiteral("ARC"));
    addCommandAction(IconFactory::polylineIcon(), QStringLiteral("Polyline"), QStringLiteral("PLINE"));

    toolbar->addSeparator();
    addCommandAction(IconFactory::moveIcon(), QStringLiteral("Move"), QStringLiteral("MOVE"));
    addCommandAction(IconFactory::copyIcon(), QStringLiteral("Copy"), QStringLiteral("COPY"));
    addCommandAction(IconFactory::rotateIcon(), QStringLiteral("Rotate"), QStringLiteral("ROTATE"));
    addCommandAction(IconFactory::scaleIcon(), QStringLiteral("Scale"), QStringLiteral("SCALE"));

    toolbar->addSeparator();
    auto* eraseAction = toolbar->addAction(IconFactory::eraseIcon(), QStringLiteral("Erase"));
    eraseAction->setToolTip(QStringLiteral("Erase selected entities (Delete)"));
    connect(eraseAction, &QAction::triggered, m_view, &DrawingView::eraseSelection);
}

void MainWindow::updateCoordLabel(const lcad::Point2D& pt) {
    if (m_coordLabel) m_coordLabel->setText(QStringLiteral("%1, %2").arg(pt.x, 0, 'f', 3).arg(pt.y, 0, 'f', 3));
}
