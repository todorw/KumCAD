#pragma once

#include "core/core3d/Document3D.h"

#include <QMainWindow>

class Viewport3D;
class QListWidget;

// A host window for Viewport3D plus a real feature-tree panel over
// Document3D -- Phase 2 Sprints 1 (primitives/booleans/undo/param editing)
// and 2 (sketch editor, launched from here). Drag-reorder and a richer
// tree view are later-sprint polish; this is "prove the core works end to
// end from the app, not just from tests."
class Window3D : public QMainWindow {
    Q_OBJECT
public:
    explicit Window3D(QWidget* parent = nullptr);

private:
    void addPrimitive(lcad::FeatureType type);
    void applyBoolean(lcad::FeatureType type);
    void editSelectedFeature();
    void openSketchEditor();
    void addSketchFeature();
    void undo();
    void redo();
    void refreshFeatureList();
    void refreshViewport();

    lcad::Document3D m_document;
    Viewport3D* m_viewport = nullptr;
    QListWidget* m_featureList = nullptr;
    double m_nextOffsetX = 0.0;
};
