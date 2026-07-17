#pragma once

#include "core/core3d/Feature3D.h"
#include "core/document/Command.h"
#include "core/sketch/SketchGeometry.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

// The 3D-modeling document: an ordered feature tree. Boolean features may
// only reference earlier features (inputA/inputB < their own index), so
// the list order is always a valid topological order -- no separate
// dependency graph structure is needed, just "recompute forward from the
// earliest changed index."
class Document3D {
public:
    // Appends feature, recomputes it, and returns its index.
    int addFeature(Feature3D feature);

    // Replaces the feature at index, then recomputes it and every feature
    // that (directly or transitively) depends on it, in list order. Does
    // nothing if index is out of range.
    void updateFeature(int index, Feature3D feature);

    // Removes the feature at index. Fails (returns false, no change) if
    // any other feature's inputA/inputB still references it -- delete
    // dependents first.
    bool removeFeature(int index);

    // The mirror of removeFeature: re-inserts feature at index, shifting
    // every existing feature's inputA/inputB (and its own recomputed
    // shape) that referenced index or later up by one, then recomputes
    // index onward. Exists so RemoveFeature3DCommand's undo can restore a
    // removed feature to its exact original position rather than
    // appending it at the end (which would silently renumber everything
    // between the two, breaking any dependency indices in between).
    void insertFeatureAt(int index, Feature3D feature);

    const std::vector<Feature3D>& features() const { return m_features; }
    const Feature3D* findFeature(int index) const;

    // The last successfully computed shape for a feature, or a null shape
    // if it failed (e.g. a boolean op on shapes that don't overlap the way
    // expected) or index is invalid.
    const TopoDS_Shape& shapeAt(int index) const;
    bool isValid(int index) const;

    CommandStack& commandStack() { return m_commandStack; }

    // Sketches finished in the sketch editor (Phase 2 Sprint 2), kept here
    // so a later sketch-based feature (Pad/Pocket/Revolve -- Sprint 3) has
    // something concrete to reference by index. Not undoable yet (no
    // Command wrapper) and not itself a dependency the feature-tree
    // recompute engine understands -- that wiring is Sprint 3's job.
    int addSketch(Sketch sketch);
    const std::vector<Sketch>& sketches() const { return m_sketches; }

    // Shapes with no parametric recipe of their own -- read from an
    // external STEP/IGES file (StepIges.h) or restored from a .kcad3d's
    // embedded BRep blob (Persistence3D.h). A FeatureType::Imported
    // feature's importIndex points into this list; recompute just copies
    // the shape verbatim.
    int addImportedShape(TopoDS_Shape shape);
    const std::vector<TopoDS_Shape>& importedShapes() const { return m_importedShapes; }

    // Named document variables (FreeCAD's own expression-engine variables,
    // simplified: no spreadsheet, just a flat name->number table), which
    // Feature3D::expressions entries can reference by name. Not undoable
    // yet (matching addSketch's own "not itself a dependency the recompute
    // engine understands" disclosure) -- setVariable recomputes every
    // feature so the change is felt immediately.
    void setVariable(const std::string& name, double value);
    bool removeVariable(const std::string& name);
    const std::unordered_map<std::string, double>& variables() const { return m_variables; }

private:
    std::vector<Feature3D> m_features;
    std::vector<TopoDS_Shape> m_shapes;
    std::vector<bool> m_valid;
    CommandStack m_commandStack;
    std::vector<Sketch> m_sketches;
    std::vector<TopoDS_Shape> m_importedShapes;
    std::unordered_map<std::string, double> m_variables;

    void recomputeOne(int index);
    // Overwrites f's double fields that have an entry in f.expressions,
    // evaluating each against m_variables. See Feature3D::expressions'
    // own comment for the "leave the previous value on failure" contract.
    void applyExpressions(Feature3D& f) const;
};

} // namespace lcad
