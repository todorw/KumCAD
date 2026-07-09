#include "MainWindow.h"

#include "CommandDispatcher.h"
#include "CommandLine.h"
#include "DrawingView.h"
#include "IconFactory.h"
#include "LayerPanel.h"
#include "PropertiesPanel.h"
#include "core/io/DxfReader.h"
#include "core/io/DxfWriter.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    resize(1280, 800);

    m_view = new DrawingView(m_document, this);
    setCentralWidget(m_view);

    m_commandLine = new CommandLine(this);
    m_dispatcher = new CommandDispatcher(m_document, *m_commandLine, this);
    m_view->setDispatcher(m_dispatcher);
    m_dispatcher->setView(m_view);

    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::previewChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::documentChanged, this, &MainWindow::markDirty);
    connect(m_view, &DrawingView::documentEdited, this, &MainWindow::markDirty);
    connect(m_view, &DrawingView::mouseWorldMoved, this, &MainWindow::updateCoordLabel);
    connect(m_view, &DrawingView::modesChanged, this, &MainWindow::updateModeLabels);

    setupDocks();
    setupMenusAndToolbar();

    m_coordLabel = new QLabel(QStringLiteral("0.000, 0.000"), this);
    m_gridLabel = new QLabel(QStringLiteral("GRID"), this);
    m_orthoLabel = new QLabel(QStringLiteral("ORTHO"), this);
    m_osnapLabel = new QLabel(QStringLiteral("OSNAP"), this);
    for (QLabel* label : {m_gridLabel, m_orthoLabel, m_osnapLabel}) {
        label->setToolTip(QStringLiteral("F9 Grid Snap / F8 Ortho / F3 Object Snap"));
    }
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_gridLabel);
    statusBar()->addPermanentWidget(m_orthoLabel);
    statusBar()->addPermanentWidget(m_osnapLabel);
    statusBar()->showMessage(QStringLiteral("Ready"));
    updateModeLabels();

    m_commandLine->appendLine(QStringLiteral(
        "KumCAD — type a command (LINE, CIRCLE, ARC, PLINE, RECTANG, ELLIPSE, TEXT, DIMLINEAR, DIMALIGNED, MOVE, COPY, "
        "ROTATE, SCALE, MIRROR, OFFSET, TRIM, EXTEND, FILLET, ERASE, DIST, AREA, ZOOM, UNDO, REDO) and press Enter."));
    m_commandLine->appendLine(QStringLiteral("F3 Object Snap / F8 Ortho / F9 Grid Snap toggle the drafting aids below."));
    m_commandLine->appendLine(QStringLiteral("Command:"));
    m_commandLine->input()->setFocus();

    updateWindowTitle();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardUnsavedChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::setupDocks() {
    auto* commandDock = new QDockWidget(QStringLiteral("Command Line"), this);
    commandDock->setObjectName(QStringLiteral("CommandLineDock"));
    commandDock->setWidget(m_commandLine);
    commandDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, commandDock);

    m_layerPanel = new LayerPanel(m_document, this);
    connect(m_layerPanel, &LayerPanel::layersChanged, m_view, &DrawingView::pruneSelectionForLayerState);
    connect(m_layerPanel, &LayerPanel::layersChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_layerPanel, &LayerPanel::layersChanged, this, &MainWindow::markDirty);

    auto* layerDock = new QDockWidget(QStringLiteral("Layers"), this);
    layerDock->setObjectName(QStringLiteral("LayerDock"));
    layerDock->setWidget(m_layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, layerDock);

    m_propertiesPanel = new PropertiesPanel(m_document, *m_view, this);
    connect(m_view, &DrawingView::selectionChanged, m_propertiesPanel, &PropertiesPanel::refresh);
    connect(m_view, &DrawingView::documentEdited, m_propertiesPanel, &PropertiesPanel::refresh);
    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_propertiesPanel, &PropertiesPanel::refresh);
    connect(m_layerPanel, &LayerPanel::layersChanged, m_propertiesPanel, &PropertiesPanel::refresh);
    connect(m_propertiesPanel, &PropertiesPanel::documentChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_propertiesPanel, &PropertiesPanel::documentChanged, this, &MainWindow::markDirty);

    auto* propertiesDock = new QDockWidget(QStringLiteral("Properties"), this);
    propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
    propertiesDock->setWidget(m_propertiesPanel);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);
    tabifyDockWidget(layerDock, propertiesDock);
    layerDock->raise();
}

void MainWindow::setupMenusAndToolbar() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&New"), QKeySequence::New, this, &MainWindow::newDocument);
    fileMenu->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this, &MainWindow::openDocument);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Save"), QKeySequence::Save, this, [this]() { saveDocument(); });
    fileMenu->addAction(QStringLiteral("Save &As..."), QKeySequence::SaveAs, this, [this]() { saveDocumentAs(); });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, m_dispatcher, &CommandDispatcher::undo);
    editMenu->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, m_dispatcher, &CommandDispatcher::redo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Select &All"), QKeySequence::SelectAll, m_view, &DrawingView::selectAll);
    editMenu->addAction(QStringLiteral("&Erase Selected"), QKeySequence::Delete, m_view, &DrawingView::eraseSelection);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("Zoom &Extents"), this, [this]() { m_view->zoomExtents(); });
    viewMenu->addSeparator();
    auto* osnapAction = viewMenu->addAction(QStringLiteral("&Object Snap"), QKeySequence(Qt::Key_F3), this,
                                             [this]() { m_view->setOsnapEnabled(!m_view->osnapEnabled()); });
    osnapAction->setCheckable(true);
    auto* orthoAction = viewMenu->addAction(QStringLiteral("&Ortho Mode"), QKeySequence(Qt::Key_F8), this,
                                             [this]() { m_view->setOrthoEnabled(!m_view->orthoEnabled()); });
    orthoAction->setCheckable(true);
    auto* gridSnapAction = viewMenu->addAction(QStringLiteral("&Grid Snap"), QKeySequence(Qt::Key_F9), this,
                                                [this]() { m_view->setGridSnapEnabled(!m_view->gridSnapEnabled()); });
    gridSnapAction->setCheckable(true);
    connect(m_view, &DrawingView::modesChanged, this, [this, osnapAction, orthoAction, gridSnapAction]() {
        osnapAction->setChecked(m_view->osnapEnabled());
        orthoAction->setChecked(m_view->orthoEnabled());
        gridSnapAction->setChecked(m_view->gridSnapEnabled());
    });
    osnapAction->setChecked(m_view->osnapEnabled());
    orthoAction->setChecked(m_view->orthoEnabled());
    gridSnapAction->setChecked(m_view->gridSnapEnabled());

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
    addCommandAction(IconFactory::ellipseIcon(), QStringLiteral("Ellipse"), QStringLiteral("ELLIPSE"));
    addCommandAction(IconFactory::rectangleIcon(), QStringLiteral("Rectangle"), QStringLiteral("RECTANG"));
    addCommandAction(IconFactory::textIcon(), QStringLiteral("Text"), QStringLiteral("TEXT"));

    toolbar->addSeparator();
    addCommandAction(IconFactory::moveIcon(), QStringLiteral("Move"), QStringLiteral("MOVE"));
    addCommandAction(IconFactory::copyIcon(), QStringLiteral("Copy"), QStringLiteral("COPY"));
    addCommandAction(IconFactory::rotateIcon(), QStringLiteral("Rotate"), QStringLiteral("ROTATE"));
    addCommandAction(IconFactory::scaleIcon(), QStringLiteral("Scale"), QStringLiteral("SCALE"));
    addCommandAction(IconFactory::mirrorIcon(), QStringLiteral("Mirror"), QStringLiteral("MIRROR"));
    addCommandAction(IconFactory::offsetIcon(), QStringLiteral("Offset"), QStringLiteral("OFFSET"));
    addCommandAction(IconFactory::trimIcon(), QStringLiteral("Trim"), QStringLiteral("TRIM"));
    addCommandAction(IconFactory::extendIcon(), QStringLiteral("Extend"), QStringLiteral("EXTEND"));
    addCommandAction(IconFactory::filletIcon(), QStringLiteral("Fillet"), QStringLiteral("FILLET"));

    toolbar->addSeparator();
    addCommandAction(IconFactory::dimensionIcon(), QStringLiteral("Dimension"), QStringLiteral("DIMLINEAR"));

    toolbar->addSeparator();
    auto* eraseAction = toolbar->addAction(IconFactory::eraseIcon(), QStringLiteral("Erase"));
    eraseAction->setToolTip(QStringLiteral("Erase selected entities (Delete)"));
    connect(eraseAction, &QAction::triggered, m_view, &DrawingView::eraseSelection);
}

void MainWindow::updateCoordLabel(const lcad::Point2D& pt) {
    if (m_coordLabel) m_coordLabel->setText(QStringLiteral("%1, %2").arg(pt.x, 0, 'f', 3).arg(pt.y, 0, 'f', 3));
}

void MainWindow::updateModeLabels() {
    if (!m_osnapLabel || !m_orthoLabel || !m_gridLabel) return;
    auto style = [](bool on) {
        return on ? QStringLiteral("color: #7CFC9A; font-weight: bold;") : QStringLiteral("color: #888;");
    };
    m_osnapLabel->setStyleSheet(style(m_view->osnapEnabled()));
    m_orthoLabel->setStyleSheet(style(m_view->orthoEnabled()));
    m_gridLabel->setStyleSheet(style(m_view->gridSnapEnabled()));
}

void MainWindow::markDirty() {
    if (m_dirty) return;
    m_dirty = true;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    const QString name = m_currentFilePath.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(QStringLiteral("%1%2 — KumCAD").arg(name, m_dirty ? QStringLiteral("*") : QString()));
}

bool MainWindow::confirmDiscardUnsavedChanges() {
    if (!m_dirty) return true;

    const auto choice = QMessageBox::question(
        this, QStringLiteral("Unsaved Changes"), QStringLiteral("This drawing has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;
    return saveDocument();
}

void MainWindow::newDocument() {
    if (!confirmDiscardUnsavedChanges()) return;

    m_document = lcad::Document();
    m_view->resetViewState();
    m_layerPanel->refresh();
    m_propertiesPanel->refresh();
    m_currentFilePath.clear();
    m_dirty = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("New drawing"), 3000);
}

void MainWindow::openDocument() {
    if (!confirmDiscardUnsavedChanges()) return;

    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Open Drawing"), QString(), QStringLiteral("DXF Files (*.dxf)"));
    if (path.isEmpty()) return;

    std::string error;
    if (!lcad::readDxf(m_document, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Open Failed"), QString::fromStdString(error));
        return;
    }

    m_view->resetViewState();
    m_layerPanel->refresh();
    m_propertiesPanel->refresh();
    m_currentFilePath = path;
    m_dirty = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(QFileInfo(path).fileName()), 3000);
}

bool MainWindow::saveDocument() {
    if (m_currentFilePath.isEmpty()) return saveDocumentAs();

    std::string error;
    if (!lcad::writeDxf(m_document, m_currentFilePath.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Save Failed"), QString::fromStdString(error));
        return false;
    }
    m_dirty = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(m_currentFilePath).fileName()), 3000);
    return true;
}

bool MainWindow::saveDocumentAs() {
    QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Save Drawing As"), QString(), QStringLiteral("DXF Files (*.dxf)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)) path += QStringLiteral(".dxf");

    std::string error;
    if (!lcad::writeDxf(m_document, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Save Failed"), QString::fromStdString(error));
        return false;
    }
    m_currentFilePath = path;
    m_dirty = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(m_currentFilePath).fileName()), 3000);
    return true;
}
