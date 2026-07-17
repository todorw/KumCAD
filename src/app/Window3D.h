#pragma once

#include "core/core3d/Bim.h"
#include "core/core3d/Document3D.h"
#include "core/core3d/SheetMetal.h"

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
    // Reports the selected feature's own edge indices (see Pick3D.h's
    // pickEdge numbering) with a world-space midpoint each, so a user can
    // work out which index to type into SketchFeatureDialog's Fillet/
    // Chamfer Edge Indices field without live interactive picking.
    void listSelectedFeatureEdges();
    // Same idea as listSelectedFeatureEdges, for FeatureType::Shell's
    // faceIndices (see Pick3D.h's pickFace numbering).
    void listSelectedFeatureFaces();
    void openSketchEditor();
    void addSketchFeature();
    void undo();
    void redo();
    void refreshFeatureList();
    void refreshViewport();

    // Sprint 4: STEP/IGES interchange + native .kcad3d persistence.
    void importStepFile();
    void importIgesFile();
    void exportStepFile();
    void exportIgesFile();
    void saveKcad3dFile();
    void openKcad3dFile();

    // Sprint 5: opens a separate top-level Assembly window.
    void openAssemblyWindow();

    // Phase 3.1: projects Front/Top/Right/Iso views of the document's tip
    // shapes into a new 2D drawing.
    void generateDrawingViews();

    // Phase 3.2: builds a sheet-metal strip (see SheetMetal.h) and adds it
    // as an Imported feature (it has no parametric recipe Feature3D
    // understands, same as a STEP import), and separately exports its flat
    // pattern to a new 2D drawing.
    void addSheetMetalPart();
    void exportFlatPattern();
    // Adds a real face-based flange (see SheetMetal.h's buildFaceFlange)
    // onto the currently-selected feature's shape.
    void addFaceFlange();

    // Phase 3.3: a BIM model lives alongside the feature tree rather than
    // inside it (walls/openings/slabs aren't Feature3D types) -- its
    // shapes are drawn into the same viewport and folded into the same
    // "Generate Drawing Views" export, but are otherwise a separate model.
    void addBimWall();
    void addBimOpening();
    void addBimSlab();
    // Structural members (columns/beams) and room/space boundaries --
    // same "lives alongside the feature tree, not inside it" model as
    // walls/openings/slabs already use.
    void addBimColumn();
    void addBimBeam();
    void addBimSpace();
    void importIfcLite();
    void exportIfcLite();
    void exportOpeningSchedule();
    void exportRoomSchedule();

    // Phase 3.4: builds a pipe run (see Piping.h) and adds it as an
    // Imported feature, same reuse pattern as addSheetMetalPart().
    void addPipeRun();

    // Phase 3.5: slice-based multi-level roughing G-code over the
    // document's own tip shapes (see Cam3D.h).
    void generate3DCamToolpath();

    // Phase 3.6: linear-static FEM over the document's own tip shapes (see
    // Fem.h). Reports summary results (max displacement, max von Mises
    // stress) via a message box, and displays a blue-to-red von Mises
    // stress heatmap (deformation exaggerated for visibility) in the
    // viewport, replacing the feature tree's own shapes until the next
    // edit -- same unverified-in-this-environment caveat as the rest of
    // Viewport3D.
    void runFemAnalysis();
    // Same target-shape resolution as runFemAnalysis, but solves for the
    // fundamental vibration mode (see Fem.h's solveModal) instead of a
    // static load case -- reports the frequency and displays the mode
    // shape the same deformed/exaggerated way, reusing
    // buildFemVisualization by wrapping the mode shape into a plain
    // FemResult (uniform color, since a mode shape has no stress of its
    // own to heatmap).
    void runFemModalAnalysis();
    // Steady-state heat conduction (see Fem.h's solveThermalSteadyState),
    // reusing buildFemVisualization for a cold-to-hot heatmap by wrapping
    // per-tet average temperature into a plain FemResult (no deformation).
    void runFemThermalAnalysis();

    lcad::Document3D m_document;
    Viewport3D* m_viewport = nullptr;
    QListWidget* m_featureList = nullptr;
    double m_nextOffsetX = 0.0;

    lcad::SheetMetalPart m_lastSheetMetalPart;
    bool m_hasSheetMetalPart = false;

    lcad::BimModel m_bimModel;
};
