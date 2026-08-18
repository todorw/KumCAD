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
    // Parallel and Perpendicular are orientation-only mates (SolidWorks'/
    // FreeCAD's own "Parallel"/"Perpendicular" mate types): unlike every
    // mate type above, they don't pin any point at all, so componentB's
    // CURRENT world position is left untouched -- only its rotation is
    // adjusted, pivoting about its own reference point (ax/ay/az-side
    // fields are unused for these two; only the direction fields matter).
    // A real mate-stack solver would leave the remaining translational (and,
    // for Parallel, one rotational) DOF genuinely free; since this
    // closed-form solver has no way to represent "still free," preserving
    // whatever placement componentB already has is the most useful,
    // disclosed choice.
    Parallel,      // componentB's reference direction becomes parallel to componentA's (same sense)
    Perpendicular, // componentB's reference direction becomes perpendicular to componentA's, rotated
                   // the minimal amount from its own current direction (picks an arbitrary
                   // perpendicular when componentB's current direction is already exactly
                   // parallel to componentA's, since then any perpendicular direction is equally
                   // valid and there's no "closest" one to prefer)
    // A cylindrical face (componentB's reference axis, radius `value`)
    // resting tangent to a planar face (componentA's reference point +
    // normal) -- the common "pin/shaft against a flat face" real case.
    // Rotates componentB's reference axis to the closest-to-current
    // direction perpendicular to componentA's plane (identical
    // minimal-rotation rule as Perpendicular), the same "closed-form, only
    // touch what the mate actually constrains" philosophy: componentB's
    // position within the plane is left wherever it already was, only the
    // perpendicular distance from the plane is corrected to exactly `value`.
    Tangent,
    // Cylinder-cylinder tangency -- componentA/componentB's reference axes
    // (point + direction, same convention as Concentric) end up PARALLEL
    // (same closed-form rotation as Concentric), with `value`/`value2` as
    // each cylinder's own radius (radiusA/radiusB, matching
    // ConstraintSolver.h's TangentCircleCircle/InternalTangentCircleCircle
    // pair this mirrors from 2D sketching into 3D). External: the two
    // axes end up radiusA+radiusB apart (two shafts sitting side by side,
    // touching). Internal: |radiusA-radiusB| apart (a shaft resting
    // inside a bore, touching one side) -- same real distinction sketch
    // tangency already draws between two circles touching externally vs.
    // one nested inside the other. Only the perpendicular offset from
    // componentA's axis is corrected; componentB's position ALONG the
    // shared axis, and which radial side of componentA's axis it sits on,
    // are both left wherever they already were (closest-to-current radial
    // direction, the same minimal-change convention Perpendicular/Tangent
    // already use for rotation, applied here to a translation instead).
    AxisTangentExternal,
    AxisTangentInternal,
    // FreeCAD Assembly workbench's own "Fixed" joint: rigidly welds
    // componentB to componentA with ZERO relative DOF -- mechanically
    // identical to Concentric (point coincidence, reference directions
    // parallel/same-sense) PLUS an explicit `value`-degree roll around
    // the shared axis (reusing Angle's own extra-rotation step).
    // Concentric alone leaves one rotational DOF (spin around the
    // shared axis) unconstrained by construction, same as Coincident;
    // the explicit roll is what actually pins that last DOF down to a
    // single, fully-determined relative pose.
    Fixed,
    // FreeCAD Assembly workbench's own "Slider" joint: the classic
    // 1-translational-DOF prismatic joint. Unlike every mate type
    // above, Slider touches ONLY componentB's position, never its
    // orientation -- componentB's reference point ends up exactly
    // `value` along componentA's reference axis from componentA's own
    // reference point (matching Tangent's own slide-along-axis
    // mechanic, just without Tangent's preceding rotation step); its
    // off-axis position and its rotation are both left exactly as they
    // already were, the same "only touch what this mate actually
    // constrains" philosophy Parallel/Perpendicular already use for
    // orientation instead of position.
    Slider,
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

    double value = 0.0;  // Distance offset, Angle degrees, Tangent's cylinder radius, or AxisTangent*'s radiusA
    double value2 = 0.0; // AxisTangent*'s radiusB only (unused by every other mate type, same
                          // "second scalar only some types need" precedent as SketchConstraint's
                          // own value/value2 pair for Fix)
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

// Linear or polar duplication of an existing component -- there was no way
// to place several identical instances (e.g. "8 bolts around a bolt
// circle") without importing/mating each one by hand, one at a time.
enum class ComponentPatternKind { Linear, Polar };
struct ComponentPatternParams {
    ComponentPatternKind kind = ComponentPatternKind::Linear;
    int count = 2; // TOTAL instances including the original (unpatterned) one
    // Linear: direction (need not be unit) + spacing between consecutive copies.
    double dirX = 1.0, dirY = 0.0, dirZ = 0.0;
    double spacing = 10.0;
    // Polar: axis (need not be unit) through origin, spread evenly across
    // totalAngleDegrees using totalAngleDegrees/count as the per-copy step
    // -- deliberately NOT Document3D's own PolarPattern feature convention
    // (totalAngle/(count-1), which reaches the exact end angle but puts a
    // full-360-degree pattern's last copy exactly on top of the source).
    // The dominant real use case for patterning a whole COMPONENT (a bolt
    // circle, wheel studs) is a full 360-degree spread of evenly-spaced,
    // non-overlapping instances, matching real AutoCAD's own ARRAYPOLAR
    // default ("Fill angle 360" / "Item count N" spaces items N ways, not
    // N-1) -- worth a real, disclosed, deliberate divergence from this
    // codebase's own solid-feature convention rather than inheriting a
    // gotcha that matters far more here than it does for a fused solid
    // copy landing harmlessly on top of itself.
    double axisX = 0.0, axisY = 0.0, axisZ = 1.0;
    double originX = 0.0, originY = 0.0, originZ = 0.0;
    double totalAngleDegrees = 360.0;
};

// Adds count-1 new components to assembly, each a copy of
// assembly.components()[sourceIndex] (same shape and name, suffixed
// " (Ni)") placed at the pattern transform for that copy applied ON TOP
// OF the source's own CURRENT world placement -- so patterning an already-
// mated component carries its real placement into every copy, not just
// its local origin. Each copy is fixed (Assembly::solve() never moves a
// pattern copy independently -- it's placed directly, the same way a
// solid LinearPattern/PolarPattern feature's own copies aren't separately
// re-solved either). Returns the indices of the newly added components
// (NOT including the original source, still at sourceIndex). Returns
// empty and adds nothing if sourceIndex is out of range, count < 2, or
// the pattern direction/axis is degenerate (zero length).
std::vector<int> patternComponent(Assembly& assembly, int sourceIndex, const ComponentPatternParams& params);

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

// One real BOM/parts-list row: a distinct component name and how many
// placed components share it (an assembly commonly has several instances
// of the same imported part -- e.g. 4 identical bolts -- and a real parts
// list counts them once each, not once per instance).
struct PartsListEntry {
    std::string name;
    int quantity = 0;
};

// Groups assembly's components by name (exact match, case-sensitive; an
// empty name groups with every other empty-named component, since
// there's nothing else to distinguish them by), in first-appearance
// order -- the same "encounter order, not sorted" convention a real BOM
// table usually keeps rather than an arbitrary alphabetical resort.
std::vector<PartsListEntry> buildPartsList(const Assembly& assembly);

// Every component's own shape transformed by its CURRENT world placement
// (call Assembly::solve() first if mates haven't been solved yet) --
// parallel to assembly.components(), with a null entry wherever that
// component's own shape is null. Shared by detectInterferences below and
// by STEP/IGES export of a whole assembled product (see StepIges.h's
// vector-of-shapes overloads).
std::vector<TopoDS_Shape> assemblyPlacedShapes(const Assembly& assembly);

// One pair of components whose placed (world-transformed) shapes overlap
// by a non-trivial volume.
struct InterferencePair {
    int componentA = -1;
    int componentB = -1;
    double interferenceVolume = 0.0;
};

// Pairwise solid-solid intersection check across every placed component,
// at its CURRENT world placement (call Assembly::solve() first if mates
// haven't been solved yet) -- an O(n^2) real boolean common + volume
// check per pair (BRepAlgoAPI_Common + BRepGProp), fine for the small
// (tens-of-parts) assemblies this codebase's own Assembly window targets,
// not attempted at real large-assembly scale. A pair whose common volume
// is below a small epsilon (touching but not overlapping, or numerically
// negligible) is not reported.
std::vector<InterferencePair> detectInterferences(const Assembly& assembly);

} // namespace lcad
