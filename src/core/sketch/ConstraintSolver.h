#pragma once

#include "core/sketch/SketchGeometry.h"

namespace lcad {

struct SolveResult {
    bool converged = false;
    double finalResidualNorm = 0.0;
    int iterations = 0;
};

// Solves sketch's constraints in place via Levenberg-Marquardt with a
// numerically-differentiated Jacobian -- no analytical derivatives, no
// external linear-algebra dependency (see LinearSolve.h for the small
// from-scratch dense solver backing it).
//
// Disclosed limitation (real, not swept under the rug): an over-
// constrained sketch converges to a least-squares compromise (or fails to
// converge) rather than reporting exactly WHICH constraints conflict --
// that needs real symbolic/numeric rank analysis of the constraint
// Jacobian (a much deeper undertaking, the kind FreeCAD's own solver or a
// commercial one does), not something this pass adds. What IS added
// below is a bounded, honest count-based diagnostic (free variables vs.
// constraint equations) -- it tells you whether a sketch has room left to
// move or more equations than it has freedom for, not which specific
// constraint is the redundant one.
SolveResult solveSketch(Sketch& sketch, int maxIterations = 100, double tolerance = 1e-9);

struct DofReport {
    int totalDof = 0;              // 2*(free points) + (free circle radii) + (free arc radii)
    int constraintEquations = 0;   // user constraints (1 scalar equation each, except Midpoint/Symmetric which are 2 -- see SketchConstraintType's own comment) + 2*(arc count) for the always-on arc radius-consistency equations
    int remainingDof = 0;          // max(0, totalDof - constraintEquations) -- 0 means "no freedom left, at least on a naive count"
    // A necessary, not sufficient, over-constraint signal: constraintEquations
    // > totalDof means the system has strictly more equations than unknowns,
    // so it CANNOT have an exact solution in general (though a specific,
    // non-independent set of equations occasionally still could -- that
    // subtlety is exactly what full rank analysis would resolve and this
    // doesn't attempt to).
    bool likelyOverConstrained = false;
};

// A pure count, computed without running the solver -- see DofReport's own
// field comments for exactly what each number does and doesn't prove.
DofReport analyzeDof(const Sketch& sketch);

// One user constraint flagged as redundant by analyzeRedundancy, in the
// order constraints were added to the sketch.
struct RedundantConstraint {
    int constraintIndex = -1;
    // true if this constraint's own residual is non-zero at the sketch's
    // CURRENT configuration (it contradicts the constraints already
    // satisfied before it -- e.g. two different Distance values pinned to
    // the same two points); false if the residual is ~zero (a harmless
    // duplicate, already implied by earlier constraints -- e.g. the same
    // Distance added twice). This classification is evaluated at whatever
    // configuration the sketch is currently in, so it's most meaningful
    // right after solveSketch has converged.
    bool conflicting = false;
};

// The real rank-based redundancy/DOF analysis analyzeDof's own comment
// says a naive count can't give: builds the same numerically-differentiated
// constraint Jacobian solveSketch itself uses, at the sketch's CURRENT
// point/radius values, and finds its actual numeric rank (via
// LinearSolve.h's independentRowFlags) instead of just counting equations.
// A constraint's row(s) being linearly dependent on the rows before it is
// exactly what "this constraint adds nothing new" means -- so this reports
// specific constraint indices, not just a bulk count, closing the gap
// analyzeDof's own doc comment calls out as unresolved.
//
// Still a real, disclosed limitation: rank is evaluated at ONE
// configuration (a linearization), not proven for every configuration the
// sketch could ever take, and which specific constraint gets blamed for a
// dependency is order-dependent (whichever one is processed last among a
// mutually-dependent group) -- the same caveat any Jacobian-rank-based
// diagnostic carries, real CAD sketchers included.
struct RedundancyReport {
    int rank = 0;               // numeric rank of the constraint Jacobian at the sketch's current configuration
    int totalEquations = 0;     // total residual-row count (matches DofReport::constraintEquations)
    int trueRemainingDof = 0;   // totalDof - rank: the sketch's ACTUAL remaining freedom (not analyzeDof's naive estimate)
    bool overConstrained = false; // rank < totalEquations: at least one row is linearly dependent on the others
    std::vector<RedundantConstraint> redundant;
};
RedundancyReport analyzeRedundancy(const Sketch& sketch);

} // namespace lcad
