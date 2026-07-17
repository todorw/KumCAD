#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QList>
#include <QMainWindow>
#include <QString>

class DrawingView;
class CommandLine;
class CommandDispatcher;
class LayerPanel;
class PropertiesPanel;
class ToolPalette;
class SheetSetPanel;
class DesignCenterPanel;
class MarkupSetPanel;
class QLabel;
class QMenu;
class QPrinter;
class QTabBar;

// Multi-document support (see [[kumcad-6060-75-push]] parity notes: real
// AutoCAD's MDI is a single frame with a file-tab strip; every panel here
// (LayerPanel, PropertiesPanel, ...) is instead wired directly to one
// lcad::Document& for the object's whole lifetime, so retargeting them at
// runtime would be a much larger refactor than this feature warrants).
// This is the disclosed, honest alternative real editors also ship:
// multiple independent top-level MainWindows, each a full document, with a
// Window menu tying them together (list/activate/tile/cascade) -- New
// Window and Open in New Window (File menu) create them; closing the last
// one quits the app exactly as the single-window build always did.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Every live MainWindow, in creation order. The original stack-allocated
    // instance in main() is included (its destructor deregisters it when
    // main() returns, same as every heap one does on close).
    static const QList<MainWindow*>& openWindows() { return s_windows; }

    // Sheet Set Manager: loads path like loadFromPath(), then switches to
    // the layout named layoutName if it has one (Model space if empty or
    // not found). Confirms discarding unsaved changes first, like Open.
    void openSheet(const QString& path, const QString& layoutName);

    // Electrical Panel Layout: a genuinely distinct entry point from plain
    // schematic capture (WelcomeScreen::Choice::ElectricalPanel), not just
    // the same window reused silently -- pre-registers the electrical/
    // panel symbol library (see core/electrical/ElectricalLibrary.h),
    // starts a first named sheet, and tags the window title so it reads
    // as "Panel Layout" rather than a generic drawing. There's no ERC/DRC
    // step in this mode (panel wiring has no copper/net-topology rules to
    // check the way PCB layout does) -- that's an intentional omission,
    // not an oversight.
    void setupElectricalPanelMode();

    // Same pattern as setupElectricalPanelMode above -- a genuinely
    // distinct, discoverable entry point (see WelcomeScreen's "Other"
    // submenu) rather than leaving these domains reachable only by
    // typing an exact, undocumented-in-the-UI command name. Every 2D
    // window already has the relevant symbol library pre-registered
    // (registerPidSymbols is called unconditionally in the constructor),
    // so there's nothing to set up beyond the mode label and a status-
    // bar hint pointing at the actual commands.
    void setupPidMode();
    void setupCivilMode();
    void setupCamMode();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupDocks();
    void setupMenusAndToolbar();
    void updateCoordLabel(const lcad::Point2D& pt);
    void updateModeLabels();

    void markDirty();
    void updateWindowTitle();

    // Rebuilds the Model/layout tabs when the document's layouts changed
    // (LAYOUT command, Open/New). Keeps the active tab when it survives.
    void syncSpaceTabs();

    // Returns false if the user cancelled (caller should abort New/Open/close).
    bool confirmDiscardUnsavedChanges();

    void newDocument();
    void openDocument();
    // File > New Window / Open in New Window: create another top-level
    // MainWindow (WA_DeleteOnClose, unlike the primary instance) rather
    // than reusing this one -- see the class comment.
    void newWindow();
    void openInNewWindow();
    void rebuildWindowMenu();
    // Loads path (DXF or DWG) into m_document and refreshes the UI, the
    // part of openDocument() after the file picker -- reused by
    // openSheet() (Sheet Set Manager) so it can load a specific known file
    // without prompting. Returns false (with a warning dialog already
    // shown) on failure.
    bool loadFromPath(const QString& path);
    bool saveDocument();
    bool saveDocumentAs();

    void printDocument();
    void exportPdf();
    // eTransmit: packages the current DXF plus every xref/image/point-cloud
    // file it references into one zip. Requires the drawing to already be
    // saved (so there's a base path to resolve relative dependencies
    // against and a file to package in the first place).
    void eTransmit();
    // Fit-to-page rendering onto paper (white background, near-white colors
    // mapped to black): model extents in model space, or the active layout's
    // sheet with its viewports when a layout tab is current.
    void renderDrawing(QPrinter& printer);

    lcad::Document m_document;
    DrawingView* m_view = nullptr;
    CommandLine* m_commandLine = nullptr;
    CommandDispatcher* m_dispatcher = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    ToolPalette* m_toolPalette = nullptr;
    SheetSetPanel* m_sheetSetPanel = nullptr;
    DesignCenterPanel* m_designCenterPanel = nullptr;
    MarkupSetPanel* m_markupSetPanel = nullptr;
    QTabBar* m_spaceTabs = nullptr;
    QMenu* m_windowMenu = nullptr;

    static QList<MainWindow*> s_windows;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_osnapLabel = nullptr;
    QLabel* m_orthoLabel = nullptr;
    QLabel* m_gridLabel = nullptr;
    QLabel* m_polarLabel = nullptr;
    QLabel* m_otrackLabel = nullptr;
    QLabel* m_dynLabel = nullptr;

    QString m_currentFilePath;
    bool m_dirty = false;
    QString m_modeLabel; // e.g. "Panel Layout" -- appended to the window title when set
};
