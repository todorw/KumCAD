#pragma once

#include "core/core3d/Assembly.h"

#include <QMainWindow>

class Viewport3D;
class QListWidget;

// A host window for Sprint 5's Assembly: components (each a shape read
// from its own STEP file -- see StepIges.h -- since KumCAD is still
// single-document at the Document3D level) placed by a chain of closed-form
// mates (see Assembly.h for why that's a deliberate, disclosed
// simplification vs. a general nonlinear DOF solver). Mirrors Window3D's
// own "prove the core works end to end from the app, not just from tests"
// standard, including the same unverified-viewport caveat (Viewport3D.h).
class AssemblyWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AssemblyWindow(QWidget* parent = nullptr);

private:
    void addComponentFromStep();
    void addMate();
    void patternComponentAction();
    void solve();
    void checkDof();
    void checkInterferences();
    void exportStep();
    void exportStl();
    void exportPartsList();
    void refreshComponentList();
    void refreshMateList();
    void refreshViewport();

    lcad::Assembly m_assembly;
    Viewport3D* m_viewport = nullptr;
    QListWidget* m_componentList = nullptr;
    QListWidget* m_mateList = nullptr;
};
