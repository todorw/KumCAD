#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <string>
#include <vector>

namespace lcad {

// A placed component in an assembly -- typically a shape imported from its
// own STEP file (see StepIges.h) or a saved .kcad3d, since KumCAD is still
// single-document at the Document3D level (matching the same "file-based,
// not live-linked" simplification Phase 1's schematic/PCB netlist import
// made). shape is in the component's own local frame; placement carries it
// into assembly-world space.
struct AssemblyComponent {
    std::string name;
    TopoDS_Shape shape;
    gp_Trsf placement;   // world placement; identity until fixed or solved
    bool fixed = false;  // fixed components are never moved by Assembly::solve()
};

// Which pair of reference elements a mate aligns: a point+direction on
// each component's own local frame, resolvable either by typing numbers
// in directly or by a real face pick (see Pick3D.h's pickFace and
// AssemblyWindow's "Pick Face on A/B..." -- there's still no LIVE
// interactive viewport picking, i.e. no mouse-to-ray conversion wired
// into Viewport3D's mouse events yet, since that specific piece is the
// still-unverified-in-this-environment territory; the ray itself is
// typed in, same as Window3D's "List Edges..." precedent for Fillet/
// Chamfer, but the actual face geometry it resolves to is real, picked
// geometry, not a hand-computed guess).
//
// Rather than a general nonlinear multi-body DOF solver (what FreeCAD's
// Assembly workbench or a real mechanical CAD mate stack actually builds --
// the "second-highest risk" item called out when this phase was planned),
// each mate is solved in closed form: it places componentB directly
// relative to componentA's current placement, so a chain of mates solves
// like Document3D's own feature tree does -- one forward pass in list
// order, with "append order is topological order" as the standing
// assumption (componentA must already have its final placement, either
// because it's `fixed` or because an earlier mate already placed it as its
// own componentB).
enum class MateType {
    Coincident, // point-to-point coincidence, reference directions anti-parallel
                // (the common "face-to-face" mate convention)
    Concentric, // point-to-point coincidence, reference directions parallel
                // (the common "axis-to-axis" mate convention)
    Distance,   // like Coincident, offset along the shared direction by `value`
    Angle,      // like Concentric, plus an extra rotation of `value` degrees
                // around the shared axis after aligning
};

struct Mate {
    MateType type = MateType::Coincident;
    int componentA = -1;
    int componentB = -1;

    // Reference point + direction on componentA, in ITS OWN local frame.
    double ax = 0.0, ay = 0.0, az = 0.0;
    double adx = 0.0, ady = 0.0, adz = 1.0;

    // Reference point + direction on componentB, in ITS OWN local frame.
    double bx = 0.0, by = 0.0, bz = 0.0;
    double bdx = 0.0, bdy = 0.0, bdz = 1.0;

    double value = 0.0; // Distance offset, or Angle degrees
};

class Assembly {
public:
    int addComponent(AssemblyComponent component);
    void addMate(Mate mate);

    // Solves every mate in list order (see MateType's comment for the
    // solving model). Out-of-range component indices are skipped.
    void solve();

    const std::vector<AssemblyComponent>& components() const { return m_components; }
    std::vector<AssemblyComponent>& components() { return m_components; }
    const std::vector<Mate>& mates() const { return m_mates; }

private:
    std::vector<AssemblyComponent> m_components;
    std::vector<Mate> m_mates;
};

// A bounded diagnostic matching the closed-form solving model above (not
// a general DOF/redundancy analysis over a constraint graph, which this
// solver was deliberately never built as -- see MateType's own comment).
struct AssemblyDofReport {
    // Neither fixed nor ever the componentB of a mate -- stays at its
    // default identity placement, effectively floating/unplaced.
    std::vector<int> unplacedComponentIndices;
    // The componentB of more than one mate: in this solver's forward-pass
    // model, a later mate targeting the same componentB silently
    // overwrites the placement an earlier mate gave it (see Assembly::
    // solve()'s own "one forward pass" model) rather than being detected
    // as a conflict -- this surfaces that real footgun instead of letting
    // it pass silently.
    std::vector<int> multiplyMatedComponentIndices;
};

AssemblyDofReport analyzeAssemblyDof(const Assembly& assembly);

} // namespace lcad
