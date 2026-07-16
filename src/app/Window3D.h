#pragma once

#include "core/core3d/Document3D.h"

#include <QMainWindow>

class Viewport3D;
class QListWidget;

// A minimal host window for Viewport3D plus a real (if basic) feature-tree
// panel over Document3D -- Phase 2 Sprint 1 (primitives/booleans/undo). The
// full feature-tree UI (parameter editing dialogs, drag-reorder, etc.) is
// later-sprint polish; this is "prove the core works end to end from the
// app, not just from tests."
class Window3D : public QMainWindow {
    Q_OBJECT
public:
    explicit Window3D(QWidget* parent = nullptr);

private:
    void addPrimitive(lcad::FeatureType type);
    void applyBoolean(lcad::FeatureType type);
    void undo();
    void redo();
    void refreshFeatureList();
    void refreshViewport();

    lcad::Document3D m_document;
    Viewport3D* m_viewport = nullptr;
    QListWidget* m_featureList = nullptr;
    double m_nextOffsetX = 0.0;
};
