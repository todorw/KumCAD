#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QMainWindow>
#include <QString>

class DrawingView;
class CommandLine;
class CommandDispatcher;
class LayerPanel;
class PropertiesPanel;
class QLabel;
class QPrinter;
class QTabBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupDocks();
    void setupMenusAndToolbar();
    void updateCoordLabel(const lcad::Point2D& pt);
    void updateModeLabels();

    void markDirty();
    void updateWindowTitle();

    // Returns false if the user cancelled (caller should abort New/Open/close).
    bool confirmDiscardUnsavedChanges();

    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();

    void printDocument();
    void exportPdf();
    // Fit-to-page rendering onto paper (white background, near-white colors
    // mapped to black): model extents in model space, or the active layout's
    // sheet with its viewports when a layout tab is current.
    void renderDrawing(QPrinter& printer);
    void renderLayout(QPrinter& printer, const lcad::Layout& layout);

    lcad::Document m_document;
    DrawingView* m_view = nullptr;
    CommandLine* m_commandLine = nullptr;
    CommandDispatcher* m_dispatcher = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    QTabBar* m_spaceTabs = nullptr;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_osnapLabel = nullptr;
    QLabel* m_orthoLabel = nullptr;
    QLabel* m_gridLabel = nullptr;
    QLabel* m_polarLabel = nullptr;
    QLabel* m_otrackLabel = nullptr;

    QString m_currentFilePath;
    bool m_dirty = false;
};
