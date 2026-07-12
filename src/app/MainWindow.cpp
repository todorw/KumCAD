#include "MainWindow.h"

#include "CommandDispatcher.h"
#include "CommandLine.h"
#include "DrawingView.h"
#include "EntityPainter.h"
#include "IconFactory.h"
#include "LayerPanel.h"
#include "PropertiesPanel.h"
#include "ToolPalette.h"
#include "SheetSetPanel.h"
#include "DesignCenterPanel.h"
#include "MarkupSetPanel.h"
#include "core/geometry/Image.h"
#include "core/geometry/PointCloud.h"
#include "core/io/DwgReader.h"
#include "core/io/DwgWriter.h"
#include "core/io/DxfReader.h"
#include "core/io/DxfWriter.h"
#include "core/io/Xref.h"
#include "core/io/Zip.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QSet>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>
#include <QToolBar>

#include <algorithm>
#include <fstream>
#include <sstream>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    resize(1280, 800);

    m_view = new DrawingView(m_document, this);

    // Model/layout tabs under the canvas, like AutoCAD's space tabs.
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_view, 1);
    m_spaceTabs = new QTabBar(central);
    m_spaceTabs->setExpanding(false);
    m_spaceTabs->addTab(QStringLiteral("Model"));
    for (const lcad::Layout& layout : m_document.layouts()) {
        m_spaceTabs->addTab(QString::fromStdString(layout.name));
    }
    connect(m_spaceTabs, &QTabBar::currentChanged, this, [this](int index) {
        m_view->setActiveLayoutIndex(index - 1);
        m_document.setActiveSpace(index - 1); // new entities go to this space
        statusBar()->showMessage(index == 0 ? QStringLiteral("Model space")
                                            : QStringLiteral("Paper space — draw directly on the sheet; MVIEW places "
                                                             "a viewport, VPSCALE sets its scale, PAGESETUP sizes "
                                                             "the sheet"),
                                 5000);
    });
    centralLayout->addWidget(m_spaceTabs);
    setCentralWidget(central);

    m_commandLine = new CommandLine(this);
    m_dispatcher = new CommandDispatcher(m_document, *m_commandLine, this);
    m_view->setDispatcher(m_dispatcher);
    m_dispatcher->setView(m_view);

    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::previewChanged, m_view, QOverload<>::of(&QWidget::update));
    connect(m_dispatcher, &CommandDispatcher::documentChanged, this, &MainWindow::markDirty);
    connect(m_dispatcher, &CommandDispatcher::documentChanged, this, &MainWindow::syncSpaceTabs);
    connect(m_view, &DrawingView::documentEdited, this, &MainWindow::markDirty);
    connect(m_view, &DrawingView::mouseWorldMoved, this, &MainWindow::updateCoordLabel);
    connect(m_view, &DrawingView::modesChanged, this, &MainWindow::updateModeLabels);

    setupDocks();
    setupMenusAndToolbar();

    m_coordLabel = new QLabel(QStringLiteral("0.000, 0.000"), this);
    m_gridLabel = new QLabel(QStringLiteral("GRID"), this);
    m_orthoLabel = new QLabel(QStringLiteral("ORTHO"), this);
    m_polarLabel = new QLabel(QStringLiteral("POLAR"), this);
    m_osnapLabel = new QLabel(QStringLiteral("OSNAP"), this);
    m_otrackLabel = new QLabel(QStringLiteral("OTRACK"), this);
    m_dynLabel = new QLabel(QStringLiteral("DYN"), this);
    for (QLabel* label : {m_gridLabel, m_orthoLabel, m_polarLabel, m_osnapLabel, m_otrackLabel, m_dynLabel}) {
        label->setToolTip(QStringLiteral(
            "F9 Grid Snap / F8 Ortho / F10 Polar / F3 Object Snap / F11 Snap Tracking / F12 Dynamic Input"));
    }
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_gridLabel);
    statusBar()->addPermanentWidget(m_orthoLabel);
    statusBar()->addPermanentWidget(m_polarLabel);
    statusBar()->addPermanentWidget(m_osnapLabel);
    statusBar()->addPermanentWidget(m_otrackLabel);
    statusBar()->addPermanentWidget(m_dynLabel);
    statusBar()->showMessage(QStringLiteral("Ready"));
    updateModeLabels();

    m_commandLine->appendLine(QStringLiteral(
        "KumCAD — type a command and press Enter. Draw: LINE, CIRCLE, ARC, PLINE, SPLINE, ELLIPSE, RECTANG, POINT, "
        "XLINE, RAY, TEXT, MTEXT, HATCH, LEADER. Modify: MOVE, COPY, ROTATE, SCALE, MIRROR, OFFSET, TRIM, EXTEND, "
        "FILLET, STRETCH, LENGTHEN, BREAK, ALIGN, PEDIT, ARRAY, EXPLODE, ERASE, MATCHPROP."));
    m_commandLine->appendLine(QStringLiteral(
        "More: DIMLINEAR/ALIGNED/RADIUS/DIAMETER/ANGULAR, DIMSTYLE, STYLE, BLOCK, INSERT, ATTDEF, XREF, GROUP, "
        "DIVIDE, MEASURE, LAYOUT, PAGESETUP, MVIEW, LWEIGHT, LWDISPLAY, PURGE, OSNAP, POLARANG, DIST, AREA, ID."));
    m_commandLine->appendLine(QStringLiteral(
        "Also: TABLE, TABLEDIT, MLEADER, MLEADERSTYLE, GRADIENT, ANNOSCALE, BPARAMETER, QSELECT, FIND, QUICKCALC "
        "(CAL), ACTRECORD/ACTSTOP/PLAY. Type AutoLISP directly, e.g. (+ 1 2) or (load \"script.lsp\")."));
    m_commandLine->appendLine(QStringLiteral(
        "F3 Object Snap / F8 Ortho / F9 Grid Snap / F10 Polar / F11 Snap Tracking / F12 Dynamic Input toggle the "
        "drafting aids below."));
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

    m_toolPalette = new ToolPalette(m_document, *m_dispatcher, this);
    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_toolPalette, &ToolPalette::refresh);

    auto* toolPaletteDock = new QDockWidget(QStringLiteral("Tool Palette"), this);
    toolPaletteDock->setObjectName(QStringLiteral("ToolPaletteDock"));
    toolPaletteDock->setWidget(m_toolPalette);
    addDockWidget(Qt::RightDockWidgetArea, toolPaletteDock);
    tabifyDockWidget(propertiesDock, toolPaletteDock);

    m_sheetSetPanel = new SheetSetPanel(this);
    connect(m_sheetSetPanel, &SheetSetPanel::sheetOpenRequested, this, &MainWindow::openSheet);

    auto* sheetSetDock = new QDockWidget(QStringLiteral("Sheet Set Manager"), this);
    sheetSetDock->setObjectName(QStringLiteral("SheetSetDock"));
    sheetSetDock->setWidget(m_sheetSetPanel);
    addDockWidget(Qt::RightDockWidgetArea, sheetSetDock);
    tabifyDockWidget(toolPaletteDock, sheetSetDock);

    m_designCenterPanel = new DesignCenterPanel(m_document, this);
    connect(m_designCenterPanel, &DesignCenterPanel::documentImported, this, [this]() {
        m_layerPanel->refresh();
        m_toolPalette->refresh();
        markDirty();
        m_view->update();
    });

    auto* designCenterDock = new QDockWidget(QStringLiteral("Design Center"), this);
    designCenterDock->setObjectName(QStringLiteral("DesignCenterDock"));
    designCenterDock->setWidget(m_designCenterPanel);
    addDockWidget(Qt::RightDockWidgetArea, designCenterDock);
    tabifyDockWidget(sheetSetDock, designCenterDock);

    m_markupSetPanel = new MarkupSetPanel(m_document, *m_view, this);
    connect(m_dispatcher, &CommandDispatcher::documentChanged, m_markupSetPanel, &MarkupSetPanel::refresh);
    connect(m_view, &DrawingView::documentEdited, m_markupSetPanel, &MarkupSetPanel::refresh);

    auto* markupSetDock = new QDockWidget(QStringLiteral("Markup Set"), this);
    markupSetDock->setObjectName(QStringLiteral("MarkupSetDock"));
    markupSetDock->setWidget(m_markupSetPanel);
    addDockWidget(Qt::RightDockWidgetArea, markupSetDock);
    tabifyDockWidget(designCenterDock, markupSetDock);

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
    fileMenu->addAction(QStringLiteral("&Print..."), QKeySequence::Print, this, &MainWindow::printDocument);
    fileMenu->addAction(QStringLiteral("Export P&DF..."), this, &MainWindow::exportPdf);
    fileMenu->addAction(QStringLiteral("eTransmit..."), this, &MainWindow::eTransmit);
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
    auto* polarAction = viewMenu->addAction(QStringLiteral("&Polar Tracking"), QKeySequence(Qt::Key_F10), this,
                                             [this]() { m_view->setPolarEnabled(!m_view->polarEnabled()); });
    polarAction->setCheckable(true);
    auto* otrackAction =
        viewMenu->addAction(QStringLiteral("Object Snap &Tracking"), QKeySequence(Qt::Key_F11), this,
                            [this]() { m_view->setOtrackEnabled(!m_view->otrackEnabled()); });
    otrackAction->setCheckable(true);
    auto* dynAction = viewMenu->addAction(QStringLiteral("D&ynamic Input"), QKeySequence(Qt::Key_F12), this,
                                          [this]() { m_view->setDynamicInputEnabled(!m_view->dynamicInputEnabled()); });
    dynAction->setCheckable(true);
    connect(m_view, &DrawingView::modesChanged, this,
            [this, osnapAction, orthoAction, gridSnapAction, polarAction, otrackAction, dynAction]() {
                osnapAction->setChecked(m_view->osnapEnabled());
                orthoAction->setChecked(m_view->orthoEnabled());
                gridSnapAction->setChecked(m_view->gridSnapEnabled());
                polarAction->setChecked(m_view->polarEnabled());
                otrackAction->setChecked(m_view->otrackEnabled());
                dynAction->setChecked(m_view->dynamicInputEnabled());
            });
    osnapAction->setChecked(m_view->osnapEnabled());
    orthoAction->setChecked(m_view->orthoEnabled());
    gridSnapAction->setChecked(m_view->gridSnapEnabled());
    polarAction->setChecked(m_view->polarEnabled());
    otrackAction->setChecked(m_view->otrackEnabled());
    dynAction->setChecked(m_view->dynamicInputEnabled());

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
    addCommandAction(IconFactory::hatchIcon(), QStringLiteral("Hatch"), QStringLiteral("HATCH"));
    addCommandAction(IconFactory::blockIcon(), QStringLiteral("Block"), QStringLiteral("BLOCK"));

    toolbar->addSeparator();
    auto* eraseAction = toolbar->addAction(IconFactory::eraseIcon(), QStringLiteral("Erase"));
    eraseAction->setToolTip(QStringLiteral("Erase selected entities (Delete)"));
    connect(eraseAction, &QAction::triggered, m_view, &DrawingView::eraseSelection);
}

void MainWindow::updateCoordLabel(const lcad::Point2D& pt) {
    if (m_coordLabel) m_coordLabel->setText(QStringLiteral("%1, %2").arg(pt.x, 0, 'f', 3).arg(pt.y, 0, 'f', 3));
}

void MainWindow::updateModeLabels() {
    if (!m_osnapLabel || !m_orthoLabel || !m_gridLabel || !m_polarLabel || !m_otrackLabel || !m_dynLabel) return;
    auto style = [](bool on) {
        return on ? QStringLiteral("color: #7CFC9A; font-weight: bold;") : QStringLiteral("color: #888;");
    };
    m_osnapLabel->setStyleSheet(style(m_view->osnapEnabled()));
    m_orthoLabel->setStyleSheet(style(m_view->orthoEnabled()));
    m_gridLabel->setStyleSheet(style(m_view->gridSnapEnabled()));
    m_polarLabel->setStyleSheet(style(m_view->polarEnabled()));
    m_otrackLabel->setStyleSheet(style(m_view->otrackEnabled()));
    m_dynLabel->setStyleSheet(style(m_view->dynamicInputEnabled()));
}

void MainWindow::syncSpaceTabs() {
    const auto& layouts = m_document.layouts();
    bool matches = m_spaceTabs->count() == static_cast<int>(layouts.size()) + 1;
    if (matches) {
        for (std::size_t i = 0; i < layouts.size(); ++i) {
            if (m_spaceTabs->tabText(static_cast<int>(i) + 1) != QString::fromStdString(layouts[i].name)) {
                matches = false;
                break;
            }
        }
    }
    if (matches) return;

    const QSignalBlocker blocker(m_spaceTabs);
    while (m_spaceTabs->count() > 1) m_spaceTabs->removeTab(m_spaceTabs->count() - 1);
    for (const lcad::Layout& layout : layouts) {
        m_spaceTabs->addTab(QString::fromStdString(layout.name));
    }
    // The previously active layout may be gone; fall back to Model.
    int index = m_spaceTabs->currentIndex();
    if (index > static_cast<int>(layouts.size())) index = 0;
    m_spaceTabs->setCurrentIndex(index);
    m_view->setActiveLayoutIndex(index - 1);
    m_document.setActiveSpace(index - 1);
}

void MainWindow::markDirty() {
    // Every mutation path (dispatcher commands, canvas edits, panel changes)
    // funnels through here, making it the one hook where associative
    // dimensions get re-resolved before the scheduled repaint runs.
    m_document.reassociateDimensions();
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
    syncSpaceTabs();
    m_currentFilePath.clear();
    m_dirty = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("New drawing"), 3000);
}

void MainWindow::openDocument() {
    if (!confirmDiscardUnsavedChanges()) return;

    const QString filter = lcad::dwgSupportAvailable()
                               ? QStringLiteral("Drawings (*.dxf *.dwg);;DXF Files (*.dxf);;DWG Files (*.dwg)")
                               : QStringLiteral("DXF Files (*.dxf)");
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Drawing"), QString(), filter);
    if (path.isEmpty()) return;

    loadFromPath(path);
}

bool MainWindow::loadFromPath(const QString& path) {
    const bool isDwg = path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive);
    std::string error;
    const bool ok = isDwg ? lcad::readDwg(m_document, path.toStdString(), &error)
                          : lcad::readDxf(m_document, path.toStdString(), &error);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Open Failed"), QString::fromStdString(error));
        return false;
    }

    const int refreshedXrefs = lcad::reloadAllXrefs(m_document, QFileInfo(path).absolutePath().toStdString());
    if (refreshedXrefs > 0) {
        statusBar()->showMessage(QStringLiteral("Refreshed %1 xref(s) from disk").arg(refreshedXrefs), 4000);
    }

    m_view->resetViewState();
    m_layerPanel->refresh();
    m_propertiesPanel->refresh();
    syncSpaceTabs();
    // A DWG can only be saved back as DXF, so don't adopt its path as the
    // save target -- Ctrl+S falls through to Save As.
    m_currentFilePath = isDwg ? QString() : path;
    m_dirty = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

void MainWindow::openSheet(const QString& path, const QString& layoutName) {
    if (!confirmDiscardUnsavedChanges()) return;
    if (!loadFromPath(path)) return;

    if (!layoutName.isEmpty()) {
        for (int i = 1; i < m_spaceTabs->count(); ++i) {
            if (m_spaceTabs->tabText(i) == layoutName) {
                m_spaceTabs->setCurrentIndex(i);
                return;
            }
        }
        statusBar()->showMessage(
            QStringLiteral("Sheet's layout \"%1\" not found -- showing Model space").arg(layoutName), 4000);
    }
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

void MainWindow::printDocument() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(QStringLiteral("Print Drawing"));
    if (dialog.exec() != QDialog::Accepted) return;
    renderDrawing(printer);
    statusBar()->showMessage(QStringLiteral("Printed"), 3000);
}

void MainWindow::exportPdf() {
    QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Export PDF"), QString(), QStringLiteral("PDF Files (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    renderDrawing(printer);
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::eTransmit() {
    if (m_currentFilePath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("eTransmit"), QStringLiteral("Save the drawing first."));
        return;
    }

    auto readFile = [](const QString& path) -> std::optional<std::string> {
        std::ifstream in(path.toStdString(), std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream oss;
        oss << in.rdbuf();
        return oss.str();
    };

    // Gather every xref/image/point-cloud path the drawing references,
    // resolving relative ones against its own folder like reloadAllXrefs.
    const QString baseDir = QFileInfo(m_currentFilePath).absolutePath();
    const auto resolve = [&baseDir](const QString& path) {
        QFileInfo info(path);
        return info.isRelative() ? QDir(baseDir).filePath(path) : path;
    };

    QStringList dependencyPaths;
    for (const auto& block : m_document.blocks()) {
        if (block->isXref()) dependencyPaths << resolve(QString::fromStdString(block->xrefPath));
    }
    auto collectFrom = [&](const std::vector<lcad::Entity*>& entities) {
        for (const lcad::Entity* e : entities) {
            if (e->type() == lcad::EntityType::Image) {
                dependencyPaths << resolve(QString::fromStdString(static_cast<const lcad::ImageEntity*>(e)->path()));
            } else if (e->type() == lcad::EntityType::PointCloud) {
                dependencyPaths
                    << resolve(QString::fromStdString(static_cast<const lcad::PointCloudEntity*>(e)->path()));
            }
        }
    };
    collectFrom(m_document.entities());
    for (std::size_t i = 0; i < m_document.layouts().size(); ++i) {
        collectFrom(m_document.paperEntities(static_cast<int>(i)));
    }

    const QString suggested = QFileInfo(m_currentFilePath).completeBaseName() + QStringLiteral("_transmit.zip");
    const QString zipPath =
        QFileDialog::getSaveFileName(this, QStringLiteral("eTransmit"), suggested, QStringLiteral("Zip Files (*.zip)"));
    if (zipPath.isEmpty()) return;

    std::vector<std::pair<std::string, std::string>> entries;
    QSet<QString> usedNames;
    int missing = 0;
    const auto addEntry = [&](const QString& sourcePath) {
        const QString name = QFileInfo(sourcePath).fileName();
        if (name.isEmpty() || usedNames.contains(name)) return; // silently skips same-named duplicates
        usedNames.insert(name);
        if (const auto content = readFile(sourcePath)) {
            entries.emplace_back(name.toStdString(), *content);
        } else {
            ++missing;
        }
    };
    addEntry(m_currentFilePath);
    for (const QString& dep : dependencyPaths) addEntry(dep);

    if (!lcad::writeZip(zipPath.toStdString(), entries)) {
        QMessageBox::warning(this, QStringLiteral("eTransmit"), QStringLiteral("Could not write the zip file."));
        return;
    }

    QString message = QStringLiteral("Packaged %1 file(s) into %2").arg(entries.size()).arg(QFileInfo(zipPath).fileName());
    if (missing > 0) message += QStringLiteral(" (%1 dependency file(s) not found and skipped)").arg(missing);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::renderLayout(QPrinter& printer, const lcad::Layout& layout) {
    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect viewport = painter.viewport();
    painter.fillRect(viewport, Qt::white);

    // Fit the sheet to the page (the sheet itself prints without a border).
    const double margin = 1.02;
    const double scale =
        std::min(viewport.width() / (layout.paperWidth * margin), viewport.height() / (layout.paperHeight * margin));
    const QPointF pageCenter = QRectF(viewport).center();
    const double cx = layout.paperWidth / 2.0;
    const double cy = layout.paperHeight / 2.0;
    const auto paperToPage = [scale, cx, cy, pageCenter](const lcad::Point2D& p) {
        return QPointF((p.x - cx) * scale + pageCenter.x(), pageCenter.y() - (p.y - cy) * scale);
    };

    const double penWidth = std::max(1.0, printer.resolution() / 72.0);
    for (const lcad::Viewport& vp : layout.viewports) {
        const QPointF tl = paperToPage(
            lcad::Point2D(vp.paperCenter.x - vp.paperWidth / 2.0, vp.paperCenter.y + vp.paperHeight / 2.0));
        const QPointF br = paperToPage(
            lcad::Point2D(vp.paperCenter.x + vp.paperWidth / 2.0, vp.paperCenter.y - vp.paperHeight / 2.0));
        painter.save();
        painter.setClipRect(QRectF(tl, br).normalized());
        const auto toScreen = [&](const lcad::Point2D& p) {
            return paperToPage(vp.paperCenter + (p - vp.modelCenter) * vp.viewScale);
        };
        const double effScale = vp.viewScale * scale;
        for (const lcad::Entity* e : m_document.entities()) {
            const lcad::Layer* layer = m_document.findLayer(e->layer());
            if (layer && !layer->visible) continue;
            lcad::Color c = layer ? layer->color : lcad::Color{255, 255, 255};
            if (const auto& override = e->colorOverride()) c = *override;
            QColor color(c.r, c.g, c.b);
            if (c.r > 200 && c.g > 200 && c.b > 200) color = Qt::black;
            lcad::LineType linetype = layer ? layer->linetype : lcad::LineType::Continuous;
            if (const auto& lt = e->linetypeOverride()) linetype = *lt;
            EntityPainter::paint(painter, *e, toScreen, effScale, color, penWidth, linetype,
                                 m_document.lineTypeScale(), &m_document);
        }
        painter.restore();
    }

    // Entities drawn directly on the sheet (title blocks, notes).
    const int layoutIndex = static_cast<int>(&layout - m_document.layouts().data());
    for (const lcad::Entity* e : m_document.paperEntities(layoutIndex)) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        lcad::Color c = layer ? layer->color : lcad::Color{255, 255, 255};
        if (const auto& override = e->colorOverride()) c = *override;
        QColor color(c.r, c.g, c.b);
        if (c.r > 200 && c.g > 200 && c.b > 200) color = Qt::black;
        lcad::LineType linetype = layer ? layer->linetype : lcad::LineType::Continuous;
        if (const auto& lt = e->linetypeOverride()) linetype = *lt;
        EntityPainter::paint(painter, *e, paperToPage, scale, color, penWidth, linetype,
                             m_document.lineTypeScale(), &m_document);
    }
}

void MainWindow::renderDrawing(QPrinter& printer) {
    if (m_view->inLayoutMode()) {
        const int index = m_view->activeLayoutIndex();
        if (index >= 0 && index < static_cast<int>(m_document.layouts().size())) {
            renderLayout(printer, m_document.layouts()[index]);
            return;
        }
    }

    lcad::BoundingBox box;
    const auto entities = m_document.entities();
    for (const lcad::Entity* e : entities) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        box.expand(e->boundingBox());
    }

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect viewport = painter.viewport();
    painter.fillRect(viewport, Qt::white);

    if (!box.isValid()) {
        painter.drawText(viewport, Qt::AlignCenter, QStringLiteral("(empty drawing)"));
        return;
    }

    const double w = std::max(box.max.x - box.min.x, 1e-6);
    const double h = std::max(box.max.y - box.min.y, 1e-6);
    const double margin = 1.1;
    const double scale = std::min(viewport.width() / (w * margin), viewport.height() / (h * margin));
    const double cx = (box.min.x + box.max.x) / 2.0;
    const double cy = (box.min.y + box.max.y) / 2.0;
    const QPointF pageCenter = QRectF(viewport).center();
    const auto toScreen = [scale, cx, cy, pageCenter](const lcad::Point2D& p) {
        return QPointF((p.x - cx) * scale + pageCenter.x(), pageCenter.y() - (p.y - cy) * scale);
    };

    // Roughly one printer's point of line width regardless of resolution.
    const double penWidth = std::max(1.0, printer.resolution() / 72.0);

    for (const lcad::Entity* e : entities) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && !layer->visible) continue;

        lcad::Color c = layer ? layer->color : lcad::Color{255, 255, 255};
        if (const auto& override = e->colorOverride()) c = *override;
        // Colors that read well on the dark canvas vanish on paper.
        QColor color(c.r, c.g, c.b);
        if (c.r > 200 && c.g > 200 && c.b > 200) color = Qt::black;

        lcad::LineType linetype = layer ? layer->linetype : lcad::LineType::Continuous;
        if (const auto& ltOverride = e->linetypeOverride()) linetype = *ltOverride;

        EntityPainter::paint(painter, *e, toScreen, scale, color, penWidth, linetype, m_document.lineTypeScale(),
                             &m_document);
    }
}

bool MainWindow::saveDocumentAs() {
    const QString filter = lcad::dwgWriteSupportAvailable()
                               ? QStringLiteral("DXF Files (*.dxf);;DWG Files (*.dwg)")
                               : QStringLiteral("DXF Files (*.dxf)");
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Drawing As"), QString(), filter);
    if (path.isEmpty()) return false;

    if (path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)) {
        // DWG is an export: LibreDWG's writer is lossier than our DXF, so
        // the DXF path (and dirty state) stay as they were.
        std::string error;
        int skipped = 0;
        if (!lcad::writeDwg(m_document, path.toStdString(), &error, &skipped)) {
            QMessageBox::warning(this, QStringLiteral("DWG Export Failed"), QString::fromStdString(error));
            return false;
        }
        const QString note = skipped > 0
                                 ? QStringLiteral("Exported %1 (%2 entities skipped — DXF keeps everything)")
                                       .arg(QFileInfo(path).fileName())
                                       .arg(skipped)
                                 : QStringLiteral("Exported %1").arg(QFileInfo(path).fileName());
        statusBar()->showMessage(note, 6000);
        return true;
    }

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
