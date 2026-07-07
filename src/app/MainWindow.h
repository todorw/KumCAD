#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QMainWindow>
#include <QString>

class DrawingView;
class CommandLine;
class CommandDispatcher;
class LayerPanel;
class QLabel;

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

    void markDirty();
    void updateWindowTitle();

    // Returns false if the user cancelled (caller should abort New/Open/close).
    bool confirmDiscardUnsavedChanges();

    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();

    lcad::Document m_document;
    DrawingView* m_view = nullptr;
    CommandLine* m_commandLine = nullptr;
    CommandDispatcher* m_dispatcher = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    QLabel* m_coordLabel = nullptr;

    QString m_currentFilePath;
    bool m_dirty = false;
};
