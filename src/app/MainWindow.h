#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QMainWindow>

class DrawingView;
class CommandLine;
class CommandDispatcher;
class LayerPanel;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupDocks();
    void setupMenusAndToolbar();
    void updateCoordLabel(const lcad::Point2D& pt);

    lcad::Document m_document;
    DrawingView* m_view = nullptr;
    CommandLine* m_commandLine = nullptr;
    CommandDispatcher* m_dispatcher = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    QLabel* m_coordLabel = nullptr;
};
