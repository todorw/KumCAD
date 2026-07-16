#pragma once

#include <QMainWindow>

class Viewport3D;

// A minimal host window for Viewport3D -- Phase 2 (3D modeling core)
// foundations only. Shows a test box so there's something to look at once
// a real display makes the viewport actually usable; later 3D sprints
// replace this with the real feature-tree/primitive-command UI.
class Window3D : public QMainWindow {
    Q_OBJECT
public:
    explicit Window3D(QWidget* parent = nullptr);

private:
    Viewport3D* m_viewport = nullptr;
};
