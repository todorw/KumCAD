#pragma once

#include <vector>

namespace lcad {

// Solves the dense n x n system a*x = b via Gaussian elimination with
// partial pivoting. Returns false (leaving x untouched) if a is singular
// to numerical precision. A small from-scratch solver -- no external
// linear-algebra library (Eigen etc.) is wired into this codebase, and a
// sketch's variable count is small enough (tens, not thousands) that a
// plain dense O(n^3) elimination is plenty fast.
bool solveLinearSystem(std::vector<std::vector<double>> a, std::vector<double> b, std::vector<double>& x);

// Consumes rows in the given order, row-reducing each against the
// independent rows seen so far (no pivot-swapping, since callers care about
// which ORIGINAL row first became dependent, not about solving anything).
// Returns, per original row, whether it was linearly independent of every
// row before it at the time it was processed -- the same order-dependent
// "this one added nothing new" diagnostic a real constraint solver reports
// when it flags a specific redundant/conflicting constraint, rather than
// just a bulk rank number. The rank of the whole set is the count of true
// entries. tol gates both the elimination pivots and the final near-zero
// check, in the same units as the matrix entries.
std::vector<bool> independentRowFlags(std::vector<std::vector<double>> rows, double tol = 1e-7);

} // namespace lcad
