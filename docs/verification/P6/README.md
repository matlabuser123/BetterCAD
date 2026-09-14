# P6 — Sketch solver: verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2, Eigen 5.0.1 (headers,
FetchContent with SHA-256, private to `bettercad_sketch`).

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 212/212 passed |
| release | exit 0 | exit 0 | 0 | 212/212 passed |
| debug-shared | exit 0 | exit 0 | 0 | 212/212 passed |

There are 16 new solver test cases (`tests/sketch/SolverTests.cpp`). The
constraint tests were extended for the new `Fixed` type.

## Constraint equation system

`src/sketch/solver/SolverSystem.*` defines the system:

- **Unknowns x.** Coordinates of point entities that are not `Fixed`, plus
  circle radii, in SI metres.
- **Equations F(x) = 0.** Each enabled constraint contributes its equations,
  and every arc adds the internal equation |end − c| − |start − c| = 0.
  Every residual is a length, so Jacobian rows are O(1):

  | Constraint | Residual(s) |
  |------------|-------------|
  | Coincident | pₐ.x − p_b.x, pₐ.y − p_b.y |
  | Horizontal / Vertical | a.y − b.y / a.x − b.x |
  | Distance | \|b − a\| − d; point–line: signed distance − (±d), keeping the starting side |
  | Radius | r − d (circle), \|start − c\| − d (arc) |
  | Equal | \|l₁\| − \|l₂\|, r₁ − r₂ (circles or arcs) |
  | Parallel | (d₁ × d₂) / \|d₂\| |
  | Perpendicular | (d₁ · d₂) / \|d₂\| |
  | Fixed | removes the point's unknowns |

- **Jacobian.** Derivatives are analytic.
- **Method.** Gauss–Newton with the minimum-norm step, J⁺F via complete
  orthogonal decomposition, so under-constrained geometry moves as little as
  possible. It uses Armijo backtracking and falls back to Levenberg–Marquardt
  damping. Iteration continues until ‖F‖∞ ≤ 10⁻³ × tolerance (tolerance
  1e-10 m).

## Solver diagnostics

| Status | Condition | Geometry written? |
|--------|-----------|-------------------|
| `UNDER_CONSTRAINED` | converged, DOF = n − rank(J) > 0 | yes |
| `FULLY_CONSTRAINED` | converged, DOF = 0, no redundancy | yes |
| `OVER_CONSTRAINED` | converged, but some Jacobian rows depend on earlier rows; `redundant` lists those constraints | no |
| `INCONSISTENT` | stationary point (JᵀF ≈ 0) with residual > tolerance; `conflicting` lists the unsatisfied constraints | no |
| `SOLVER_FAILURE` | not converged and not stationary, degenerate solution, or invalid options | no |

Redundancy is found by sequential Gram–Schmidt over Jacobian rows in
constraint-ID order, so the later of two dependent constraints is reported.
`analyze()` runs the same diagnosis without modifying the sketch.

## Acceptance

### Rectangle

`Solver: rectangle reaches 100 x 50 mm as a closed profile`: four
connected lines start deliberately non-rectangular (bottom about 95 mm and
tilted). With horizontal/vertical constraints, width 100 mm and height
50 mm:

- **Without an anchor:** `UNDER_CONSTRAINED`, DOF 2 (translation), 8
  unknowns, 6 equations.
- **With a fixed corner:** `FULLY_CONSTRAINED`, DOF 0, with corners at
  (0,0), (100,0), (100,50) and (0,50) mm to 1e-8 mm.
- **In both cases:** all four sides are 100/50 mm, the profile is closed
  (each line ends where the next starts), and the maximum residual is within
  tolerance.

`Solver: separate lines are closed into a rectangle by coincident
constraints` builds the same rectangle from four separate lines with
coincident constraints: `FULLY_CONSTRAINED`, 14 unknowns and 14 equations.
Measured with `-s`:

- maximum residual **6.2e-20 m**;
- closing gaps **0 to 4.8e-20 m**;
- width and height **100.0 / 50.0 mm**.

### Conflict

`Solver: conflicting lengths are reported, not applied`: a 150 mm line
with length = 100 mm and length = 200 mm constraints gives:

- status **`INCONSISTENT`**;
- `conflicting` = both constraints;
- maximum residual **50.000 mm** (the least-squares compromise);
- `geometryChanged == false` and endpoints unchanged.

After the 200 mm constraint is disabled, the same sketch solves to 100.0 mm.

### Simple cases

- **Horizontal line:** both ends move by half the offset (min-norm), DOF 3.
- **Vertical line:** same.
- **Fixed distance:** 100 → 80 mm, symmetric.
- **Circle radius:** radius 12.5 mm with the centre unchanged, DOF 2; with
  the centre fixed, `FULLY_CONSTRAINED`.

## Other diagnostics tested

- **Redundancy (`OVER_CONSTRAINED`, geometry unchanged):** the same
  horizontal constraint twice; a Parallel implied by two Horizontals; a
  distance between two fixed points. With a wrong value, that distance is
  `INCONSISTENT` instead.
- **Every constraint type solved and checked:** coincident, parallel,
  perpendicular, point–line distance (both sides), equal lengths, and arc
  radius with equal circle/arc (both arc ends on the circle).
- **Empty and unconstrained sketches:** DOF counted correctly (5 for a point
  plus a circle).
- **Collapsing geometry:** a coincident constraint on a line's own endpoints
  is rejected with `SOLVER_FAILURE`, and the sketch is unchanged.
- **Iteration limit and invalid options:** `SOLVER_FAILURE`.
- **Determinism:** two identical sketches solve to bit-identical geometry;
  re-solving a solved sketch changes nothing; `analyze()` does not modify the
  sketch.
- **Inside a document:** solving via `modifyObject` updates the revision.

## Findings during implementation

- **Release-only uninitialized-value warning.** Only the Release build
  (`-O3`) reported `-Wmaybe-uninitialized` inside Eigen's `maxCoeff()`
  inlined into the solver's damped step. `maxCoeff()` is undefined for an
  empty matrix. The call cannot be empty at run time, but the maximum
  diagonal is now computed explicitly, which is defined for every size. The
  warning was not suppressed.
- **Stale test binary after a failed build.** In the first P6 evidence run,
  CTest ran the stale test binary after that build failure (196 instead of
  212 tests). The logs in this directory are from the rerun after the fix.
  Later evidence runs skip CTest when the build fails.
- **Added `Fixed` constraint.** A `Fixed` constraint type (hold a point) was
  added to the P5 representation. It is needed to make a sketch fully
  constrained.
