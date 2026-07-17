#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <vector>

namespace lcad {

// Linear-static structural FEM (Phase 3.6 -- the plan's own "heaviest
// lift"). No external FEM solver (CalculiX, the one FreeCAD itself
// vendors) is available on this machine, so this is a real, from-scratch,
// deliberately small-scale solver -- the same "write it, don't vendor it,
// and disclose the limits honestly" call this codebase made for its
// sketch constraint solver instead of vendoring PlaneGCS.

struct FemMesh {
    std::vector<std::array<double, 3>> nodes;
    std::vector<std::array<int, 4>> tets; // 4 node indices per tetrahedron
};

// A voxel mesh: shape's bounding box is subdivided into a grid of cubes
// (divisions cells along its longest axis, proportionally fewer along the
// others), each cube split into 6 tetrahedra, keeping only tets whose
// centroid classifies as inside shape (BRepClass3d_SolidClassifier). The
// mesh boundary is therefore a "staircase" approximation of the true
// surface, not a conforming boundary-fitted mesh -- a real, disclosed
// limitation, since no external meshing library is available to vendor
// instead. Keep divisions small (single digits): the resulting dense
// linear system is solved via core/sketch/LinearSolve.h's O(n^3) Gaussian
// elimination, reused as-is rather than writing a second linear solver --
// this is emphatically not sized for industrial mesh counts.
FemMesh buildVoxelMesh(const TopoDS_Shape& shape, int divisions);

struct FemMaterial {
    double youngsModulus = 200000.0; // consistent force/length^2 units with the loads applied
    double poissonsRatio = 0.3;
    double density = 1.0; // mass/length^3, consistent with the other units -- only used by solveModal
};

// Every node with X <= fixedXMax is fully fixed (zero displacement) -- a
// plane-based clamp (the classic cantilever-beam setup), not general per-
// node/per-face selection, since there's no face-picking in the still-
// unverified 3D viewport (the same scope cut Sprint 3's Fillet/Chamfer and
// Sprint 5's Assembly mates made).
struct FemBoundaryCondition {
    double fixedXMax = 0.0;
};

// A point load (force vector) applied at the single mesh node closest to
// point. Multiple loads can approximate a distributed load by spreading
// the total force across several nearby nodes.
struct FemLoad {
    std::array<double, 3> point{0.0, 0.0, 0.0};
    std::array<double, 3> forceVector{0.0, 0.0, 0.0};
};

struct FemResult {
    bool solved = false;
    std::vector<std::array<double, 3>> displacements; // parallel to mesh.nodes
    std::vector<double> vonMisesStress;                // parallel to mesh.tets
};

// A uniform body force (e.g. density*gravity) applied to every element,
// distributed as volume/4 * forcePerVolume to each of its 4 nodes -- the
// exact consistent nodal load for a uniform body force integrated over a
// linear tetrahedron's shape functions (each of which integrates to
// exactly 1/4 of the total over the element). Returns one FemLoad per
// (node, contributing tet) pair; solveLinearStatic already sums every
// load that resolves to the same nearest node, so nodes shared by
// several tets correctly accumulate their share from each.
std::vector<FemLoad> distributedBodyForce(const FemMesh& mesh, const std::array<double, 3>& forcePerVolume);

// A uniform pressure (force per area, acting along direction -- need not
// be a unit vector, it's normalized internally) applied over every
// boundary triangular face of the mesh (a face belonging to exactly one
// tet) whose centroid falls within the axis-aligned box
// [boxMin,boxMax], split evenly across that face's 3 nodes -- the exact
// consistent nodal load for a uniform traction over a linear triangle.
// A box selection rather than real face-picking, the same simplification
// FemBoundaryCondition's own fixedXMax plane already makes and for the
// same reason (no face-picking in the still-unverified 3D viewport).
std::vector<FemLoad> distributedPressureLoad(const FemMesh& mesh, const std::array<double, 3>& boxMin,
                                             const std::array<double, 3>& boxMax, double pressure,
                                             const std::array<double, 3>& direction);

// Assembles constant-strain-tetrahedron element stiffnesses (shape
// function gradients are found by solving, per element, the same kind of
// small dense linear system LinearSolve.h already provides -- rather than
// hand-deriving cofactor-expansion gradient formulas, which is exactly the
// kind of algebra-error risk the sketch constraint solver's own
// numerical-Jacobian choice was made to avoid), applies boundaryCondition
// and loads, and solves K*u = F. Returns solved=false if mesh has no
// tets, or the system is singular (e.g. nothing is actually fixed).
FemResult solveLinearStatic(const FemMesh& mesh, const FemMaterial& material,
                            const FemBoundaryCondition& boundaryCondition, const std::vector<FemLoad>& loads);

struct FemModalResult {
    bool solved = false;
    double angularFrequencySquared = 0.0; // omega^2 of the fundamental mode, in consistent force/length/mass units -- not Hz
    std::vector<std::array<double, 3>> modeShape; // parallel to mesh.nodes, mass-normalized (arbitrary overall sign/scale)
};

// Solves for the structure's fundamental (lowest-frequency) vibration
// mode via inverse power iteration on the generalized eigenproblem
// K*phi = omega^2*M*phi -- a real, standard, from-scratch numerical
// method (the natural next step from solveLinearStatic's own K*u=F,
// reusing its exact same stiffness assembly and LinearSolve.h dense
// solver), not a full multi-mode solver: "basic" means exactly one mode,
// the fundamental one, since finding several needs a substantially
// heavier algorithm (subspace iteration/Lanczos with deflation) this
// codebase isn't going to hand-roll on top of an O(n^3) dense solve.
//
// M is a lumped (diagonal) mass matrix built the same way
// distributedBodyForce distributes a body force -- each tet's
// density*volume split evenly across its own 4 nodes -- so M^-1 is
// trivial and no second dense factorization is needed for it.
// boundaryCondition is applied exactly as in solveLinearStatic (an
// identity row/column in K for each fixed DOF); those DOFs are also
// zeroed out of M, which removes them from the eigenproblem entirely
// rather than producing spurious zero-displacement "modes" -- iteration
// naturally keeps them at exactly 0 since a fixed DOF's every inverse-
// iteration right-hand-side entry is mass[dof]*x[dof] = 0.
//
// Returns solved=false under the same conditions solveLinearStatic does
// (no tets, degenerate element, or singular K).
FemModalResult solveModal(const FemMesh& mesh, const FemMaterial& material,
                          const FemBoundaryCondition& boundaryCondition, int maxIterations = 150);

// One small shape (a compound of 4 triangular faces, not a true watertight
// solid -- display-only, so there's no need to risk OCCT's shell-sewing/
// solid-classification edge cases for something never fed back into a
// boolean op) per tet, at its DEFORMED position (node position +
// displacement*displacementScale, so the shapes visibly bend/stretch
// rather than sitting exactly where the undeformed mesh was), with a
// parallel RGB color in [0,1] per element -- a blue-green-red heatmap of
// that element's von Mises stress relative to the mesh's own maximum, not
// an absolute engineering color scale.
struct FemVisualization {
    std::vector<TopoDS_Shape> elementShapes;
    std::vector<std::array<double, 3>> elementColors;
};

FemVisualization buildFemVisualization(const FemMesh& mesh, const FemResult& result, double displacementScale = 1.0);

} // namespace lcad
