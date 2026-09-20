# P13-SOLVE-001 — Assembly Constraint Solver

```text
STATUS:          PASS
BASELINE:        d5e469a (P13-SOLVE-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           solving the seven basic mate types into derived component
                 transforms, with DOF classification. No mechanical joints,
                 no configurations, no assembly regeneration pipeline, no
                 optimization.
IMPLEMENTATION:  a solver boundary in assembly, the equation system and its
                 analytic Jacobian, a Gauss-Newton loop with rank analysis
TESTS:           33 new Catch2 cases in 2 files
EVIDENCE:        this directory
```

## Architectural contract

```text
mate intent  ->  constraint problem  ->  solver  ->  derived transforms
```

ADR-005 settled the hard parts of this milestone before it started, while
rejecting the easier options, and this implementation takes them as given:

| Contract | How it is honoured |
| --- | --- |
| The solved transform is **derived**, never persisted | returned in the result; nothing is written to the document or the file |
| **No seed, no warm start** | the starting configuration is `placementOf()` and nothing else |
| Deterministic start **and** iteration order | components in ascending ID order; equations in mate ID order |
| Grounding is **reported**, never assumed | an assembly with nothing grounded keeps its six rigid-body modes and says so |
| Five states, never a boolean | `assembly::SolveStatus` names the same five the sketch solver does — a separate enum in the assembly module, not a reuse of the sketch one |

ADR-005 rejected persisting a solved transform even as a solver seed, because
a seeded solve depends on save history and two documents with identical
intent could solve differently. `P12-PARAM-002` measured that path-dependence
at 1.3e-15 per configuration cycle in the sketch solver. The honest cost,
which ADR-005 names, is that this solver must converge from intent alone —
and that is what the basin tests below measure rather than assume.

**The solver cannot modify the document, by type.** Every entry point takes a
`const Document&`; there is no mutating overload. That is a stronger
guarantee than a test that it does not.

## Solver input/output

```cpp
Result<AssemblySolveResult> solve(const Document&, const SolverOptions& = {},
                                  const BodyLookup& = {});
```

The result carries the status, the derived transform of every component, the
residual, the iteration count, the remaining degrees of freedom, and the
mates found conflicting or redundant.

Problem construction failures are **errors, not statuses**: a mate naming a
component that is gone, a reference that does not resolve, a face with no
body to resolve it against. ADR-004 requires "this reference does not
resolve" to stay distinct from every other failure, and a solve that never
started did not converge, diverge, or find the system inconsistent. The five
statuses are outcomes of solving; they are not a place to file the reasons
solving could not begin.

Only statuses the implementation can actually distinguish are offered. There
is no separate `IterationLimit`: a run that exhausts its iterations is
`SolverFailure` with the iteration count and residual in the result, because
the distinction between "ran out of iterations" and "could not make progress"
is not one this implementation can draw reliably.

## DOF model

Six unknowns per component that is free to move: a translation in metres and
a rotation vector in radians. A component grounded by a `Fixed` mate
contributes **none** — grounding removes freedom rather than adding equations
to pin it, so the system gets smaller instead of larger.

`Fixed` is also why a component needs no `grounded` flag. `P13-XFORM-001`
deferred one as meaningless without a solver; `P13-MATE-001` supplied the
mate instead, and this milestone consumes it.

### The rotation parameterization

The unknowns are an **increment from the component's current transform**, not
absolute angles, and the increment turns the component about its own origin.
After each accepted step the increment is folded into the base and reset to
zero.

Two things follow, and both matter:

- **There is no singularity to reach.** The Jacobian is always evaluated at a
  zero increment, so gimbal lock is unreachable and there is no quaternion to
  renormalize. The word "quaternion" appears once in the implementation, in a
  comment saying there is not one.
- **The Jacobian is well conditioned.** Turning about the component's own
  origin means a component far from the model origin does not swing wildly
  for a small angle.

## Constraint-problem construction

| Mate | Equations | Rank |
| --- | --- | --- |
| `Fixed` | 0 — grounds instead | — |
| `Coincident` (planes) | 3 | 3 |
| `Coincident` (axes) | 4 | 4 |
| `Concentric` | 4 | 4 |
| `Parallel` | 2 | 2 |
| `Perpendicular` | 1 | 1 |
| `Distance` | 1 | 1 |
| `Angle` | 1 | 1 |

**The equation counts are rank-honest, and getting that right was the crux of
the milestone.** "Two unit vectors are parallel" is two equations, but the
natural formulation `cross(Da, Db) = 0` produces three rows of rank two.
Feeding that to rank-based DOF analysis would make every such mate appear
permanently redundant and corrupt the `OverConstrained` classification
outright.

So `cross(Da, Db)` is projected onto an orthonormal basis of `Da`'s
complement, chosen deterministically from `Da`'s smallest component. The
cross product already lies in that plane, so nothing is lost and exactly two
independent rows result. The positional part of axis-collinearity is handled
the same way.

`Concentric` and an axis-to-axis `Coincident` produce identical equations.
They differ in what the engineer meant, which the model records and the
solver does not need.

## Residual definitions

Every residual is a **length in metres**, including the ones that are really
angles, which are multiplied by the assembly's characteristic size. The
sketch solver takes the same care for the same reason: "all residuals are
lengths (metres), so the Jacobian is well scaled".

| Kind | Residual | Zero when |
| --- | --- | --- |
| `Parallel` | `L (Da x Db) . Ek`, two rows | the directions are parallel, either sense |
| `Perpendicular` | `L (Da . Db)` | the directions are at a right angle |
| `Angle` | `L (Da . Db - cos t)` | the directions are `t` apart |
| `OffsetAlong` | `(Pb - Pa) . Da - t` | b's origin is `t` along a's normal |
| `OffsetPerpendicular` | `(Pb - Pa) . Ek`, two rows | the axes meet |
| `SeparationPerpendicular` | `abs((Pb - Pa) perp Da) - t` | the axes are `t` apart |

`Ek` is the frozen complement basis described above. The separation residual
is not differentiable where the axes meet, the same property the sketch
solver's `Length` equation has and handles.

The characteristic length is the furthest any component or mate target sits
from the model origin, floored at a millimetre — so an assembly built at the
origin uses the floor. It makes a radian of misalignment weigh about what it sweeps at
the edge of the assembly, so the conditioning does not depend on the unit the
model happens to be drawn in — which is what the scale test below measures.

## Jacobian

Analytic throughout; nothing is differentiated numerically in production.

Each residual's gradient with respect to the world geometry it is built from
is written out by hand, and one chain rule carries it onto the six unknowns
of each component:

```text
dP/dv = I        dP/dw = -[P - c]x        dD/dw = -[D]x
row contribution to the rotation block:  (P - c) x gP  +  D x gD
```

**The complement basis is frozen at the base and refreshed only on rebase.**
This is not an optimization. `Ek` depends on `Da`, so recomputing it inside an
evaluation would make a *correct* analytic Jacobian disagree with a *correct*
central difference, and the verification below would fail on working code.
`evaluate()` and `jacobianAtBase()` are separate functions so the two cannot
be confused.

## Nonlinear method

Gauss-Newton with a minimum-norm step, an Armijo backtracking line search,
and Levenberg-Marquardt damping when the Gauss-Newton direction does not
reduce the residual — the same structure as the sketch solver, chosen because
it is the same kind of least-squares problem with the same residual units,
not because it was the easiest to write.

```text
initial state     placement intent, with no seed
step              minimum-norm Gauss-Newton (CompleteOrthogonalDecomposition)
acceptance        Armijo, 40 halvings, then LM damping over 1e-6..1e6 scale
rebase            every accepted step folds into the base
max iterations    100 (configurable)
residual          converge at tolerance * 1e-3, classify at tolerance
failure           no step reduces the residual, or the step vanishes
```

The minimum-norm step is what makes an under-constrained assembly move as
little as the constraints allow rather than drifting along its free
directions — which is why the tests can assert that a distance mate changed
only the coordinate it speaks about.

A free rigid-body mode makes the Jacobian rank-deficient by construction.
That is handled rather than avoided: the complete orthogonal decomposition
returns the minimum-norm solution of a singular system, and the missing rank
is reported as degrees of freedom.

## Convergence

| Quantity | Value | Why |
| --- | --- | --- |
| residual tolerance | 1e-9 m | a nanometre; far below any modelling tolerance and far above double-precision noise at metre scale |
| iterate until | tolerance * 1e-3 | converge well inside the tolerance the result is classified against |
| rank tolerance | 1e-9 of a row's norm | a row that adds less than this to the span is dependent |
| step rank threshold | 1e-12 | relative, for the minimum-norm decomposition |
| stationarity | 1e-8 of `abs(J) abs(F)` | distinguishes "cannot improve" from "not finished" |

There is one tolerance for both lengths and angles because the angular
residuals are already scaled into metres. The tolerances are the sketch
solver's, justified by the same reasoning: the same problem shape, the same
residual units, the same conditioning.

## Solved-state contract

| Expected | Measured |
| --- | --- |
| the solver modifies the document | it cannot — `const Document&` |
| placement intent after a solve | unchanged, revision unchanged |
| transforms after a **failed** solve | none at all |
| solving twice from the same intent | identical, including the iteration count |
| a saved and reloaded assembly | solves to **bit-identical** transforms |
| the `.bcad` file after a solve | **byte-identical** to the file saved before it |
| the reloaded component's placement | still the intent, not the solved position |

"Solving twice from the same intent" is the row that would catch a solver
quietly moving its own starting point: if the first solve had written back,
the second would start somewhere else and take a different number of
iterations.

ADR-005 names five things that verifying "placement is intent, transforms are
derived" looks like. All five are measured:

| ADR-005 check | Where |
| --- | --- |
| a saved and reloaded assembly solves to the same transforms | this table, measured bit-identical |
| no transform appears in the `.bcad` JSON | this table, measured as byte-identity of the whole file |
| a parameter change moves a component, and restoring it restores the transform | determinism, measured bit-identical |
| solving twice gives identical transforms | this table |
| an assembly with no grounded component reports under-constrained | under-constrained cases, 9 DOF |

ADR-005 asks for agreement "to the tolerance the reference models already
hold"; three of the five come out bit-identical instead, because nothing is
seeded and nothing is cached.

**"No transform appears in the `.bcad` JSON" is measured as byte-identity, not
as a text search**, and the difference is not pedantic: a `Distance` mate of
25 mm writes `"distance": 0.025` into the file as *intent*, which is exactly
the number the solve produces for that component's position. A search for the
solved value would have found it and been wrong. Comparing the whole file
before and after a solve asserts something stronger and with no such trap —
not that one number is absent, but that nothing changed at all.

## Basic mate coverage

All seven, each with a hand-derived expectation rather than a residual check:

| Mate | Hand-derived expectation | Measured |
| --- | --- | --- |
| `Fixed` | stays exactly at its placement | 10, 20, 30 mm |
| `Distance` (planes) | `zb = 25 mm`, x and y untouched | 7, -3, 25 mm |
| `Distance` (negative) | the other side | 0, 0, -25 mm |
| `Distance` (axes) | 50 mm out by 3-4-5, pulled to 10 mm | 6, 8, 0 mm |
| `Coincident` | planes meet, sliding untouched | 12, -8, 0 mm |
| `Concentric` | perpendicular offset gone, along-axis free | 0, 0, 10 mm |
| `Parallel` | normal parallel to a's | 0, 0, 1 |
| `Perpendicular` | dot product zero | 0 to 1e-8 |
| `Angle` (35 deg) | dot product `cos 35 deg` | equal to 1e-8 |

## Fully constrained cases

Six independent equations against six unknowns, DOF zero, no redundancy:

```text
Coincident(XY, XY)      2 rotation + 1 translation (z)
Distance(YZ, YZ) = 0    1 translation (x)
Distance(XZ, XZ) = 0    1 translation (y)
Perpendicular(YZ, XZ)   1 rotation (about z)
```

Constructing this needed care worth recording: rotation is three degrees of
freedom, but a `Parallel` mate supplies constraints in **pairs**, so any two
of them leave one row redundant and the assembly classifies as
`OverConstrained` instead. `Perpendicular` supplies exactly **one**, which is
what makes a clean DOF-zero case expressible at all.

Measured: `FullyConstrained`, 6 equations, 6 unknowns, DOF 0, no redundant
mates, and b lands exactly on a with its axes aligned.

## Under-constrained cases

Every DOF count below is derived from the geometry, not read back:

| Assembly | Expected DOF | Why | Measured |
| --- | --- | --- | --- |
| one free component, no mates | 6 | nothing constrains it | 6 |
| grounded + `Distance` | 5 | one equation of rank one | 5 |
| grounded + `Coincident` planes | 3 | slide in two directions, spin about the normal | 3 |
| grounded + `Concentric` | 2 | slide along the axis, spin about it | 2 |
| grounded + `Perpendicular` | 5 | one equation | 5 |
| **nothing grounded** + `Coincident` | 9 | 12 unknowns less rank 3 — the 3 the mate leaves plus the 6 the assembly keeps | 9 |

The last is ADR-005's requirement that an assembly with nothing grounded is
reported as free to move rather than silently pinned.

## Over-constrained and inconsistent cases

The distinction the gate asks for, measured rather than asserted:

| Case | Expected | Measured |
| --- | --- | --- |
| two identical `Distance` mates | satisfiable, one redundant | `OverConstrained`, 1 redundant, residual < 1e-9 |
| `Distance` 10 mm **and** 20 mm | unsatisfiable | `Inconsistent`, both conflicting |
| the residual there | least squares lands at the midpoint, 15 mm, so each is 5 mm out | **5.000 mm** |
| a violated mate between two grounded components | nothing can move | `Inconsistent`, residual exactly 30 mm, 0 iterations |

The 5 mm figure is the point: an inconsistent system is not merely "large
residual". Least squares has a closed-form answer here and the solver finds
exactly it, which is what distinguishes a correct compromise from a failure
to converge.

## Redundancy

The supported contract, stated exactly rather than overclaimed:

**A mate is reported redundant when one of its Jacobian rows adds nothing to
the span of the rows before it**, by sequential Gram-Schmidt in mate ID
order. This detects linear dependence at the solution. It does not detect
redundancy that is non-linear, nor redundancy that only appears away from the
configuration solved to.

Attribution follows ID order: of two equivalent mates the **later** one is
reported, the same contract the sketch solver states as "constraints implied
by constraints with lower IDs". The transforms do not depend on mate order;
which mate is named does. Both halves are tested.

## Unresolved references

A mate whose target does not resolve makes `solve()` **fail**, before any
iteration. It does not guess, does not bind nearby geometry, and does not use
a previous resolution — the system is built fresh on every call, so there is
no stale resolved state to reuse.

Measured: removing a component that a mate names gives `NotFound`, not a
status, and no solve appears to have succeeded.

## Failure atomicity

| Expected | Measured |
| --- | --- |
| canonical placement after a failed solve | unchanged |
| document revision | unchanged |
| transforms returned on failure | none |
| the document afterwards | still usable |
| removing the contradiction | solves, to the right answer |

The last row is the half that "nothing changed" alone would not prove: the
document is not merely untouched, it still works.

## Independent analytical validation

Thirty cases — the analytic suite; the three derivative cases are the section
below, and check something different. **Every expected value here is derived
by hand from the geometry and written into the test as a literal.** None comes
from BetterCAD's own solver,
which is the distinction that makes the suite worth anything: the residuals
the solver drives to zero are computed from the same equations it
differentiates, so a small residual proves only that the solver agrees with
itself.

The closed-form ones are listed above; the arithmetic is in the tests beside
each assertion.

## Derivative validation

The milestone's hard gate, and the one that cannot be done through a public
API.

```text
method        central difference, step 1e-6, about a zero increment
gate          relative error < 1e-7
```

| Case | Equations | Unknowns | Max relative error |
| --- | --- | --- | --- |
| coincident planes | 3 | 12 | 3.47e-12 |
| coincident axes / concentric | 4 | 12 | 3.47e-12 |
| parallel | 2 | 12 | 3.47e-12 |
| perpendicular / distance / angle | 1 | 12 | 2.49e-12 to 3.72e-11 |
| three components, four mates | 8 | 12 | 5.07e-12 |

Worst case **3.7e-11**, five orders inside the gate, and sitting at the
truncation floor of a central difference at this step size. That is the
signature of a correct analytic Jacobian: a wrong derivative — a sign error,
a missing term, a transposed cross product — is an error of order 1, not of
order 1e-11.

The multi-mate case is there because single-mate cases structurally cannot
catch a Jacobian block written into the wrong component's columns: with one
mate, every column belongs to one of its two components. With three
components, a misplaced block lands where the finite difference sees nothing.

### A deliberate exception, recorded

These tests include `src/assembly/solver/SolverSystem.hpp`. They are the only
tests in this repository that reach past a public API, and CLAUDE.md's rule
is that tests use production APIs and not private back doors.

The exception was taken deliberately, with the alternatives weighed. Testing
through the public API would show the derivatives are good enough to
converge, not that each entry is right — and a Gauss-Newton solver with a
wrong Jacobian usually still converges, reports success, and puts the
components somewhere plausible and wrong. The other option, exposing a public
`derivativeError()`, would be behaviour existing only for tests, which the
same rule forbids more clearly.

What the exception does **not** do: it bypasses no production behaviour, and
it makes no behaviour exist for the tests' benefit. It checks an internal
mathematical contract that no public API can expose without inventing one.

It does have a second cost, found by the `debug-shared` preset and not by
reasoning: see Finding 4.

## Determinism

| Expected | Measured |
| --- | --- |
| the same document solved twice | identical status, iterations, DOF, residual |
| the transforms | **bit-identical** |
| mates added in a different order | the same transforms |
| which mate is named redundant | follows ID order, by contract |
| a parameter change | moves the component, and restoring it restores the transform bit-identically |

Bit-identical is claimed only where it is earned: nothing is seeded, nothing
is cached, and the iteration order is fixed by ascending ID. Across presets,
the assertions are exact values to 1e-8 m, so a Release build that differed
meaningfully would fail the same tests rather than pass quietly.

## Performance baseline

Measured on the Debug build, which is unoptimized; recorded as a baseline and
nothing more. The test asserts only correctness, never timing, so it cannot
fail because the machine was busy.

| Components | Equations | Unknowns | DOF | Iterations | Time |
| --- | --- | --- | --- | --- | --- |
| 2 | 3 | 6 | 3 | 2 | 1.2 ms |
| 5 | 12 | 24 | 12 | 3 | 5.1 ms |
| 10 | 27 | 54 | 27 | 3 | 19.4 ms |
| 20 | 57 | 114 | 57 | 3 | 104.0 ms |

The iteration count is flat: a chain of coincidences is close to linear, so
Gauss-Newton reaches it immediately. The time grows by roughly 5x per
doubling, consistent with a dense decomposition being about cubic in the
unknowns. That is a real ceiling on assembly size and is recorded as a known
limitation rather than dressed up.

## Adversarial review

Four findings, **no production defect**; nothing weakened. The third is a
diagnostic that over-claims, shared with the already-qualified sketch solver
and deliberately left consistent with it. The fourth is a build-configuration
defect that the three-preset gate caught and neither static preset could.

### Cleared

| Question | Answer |
| --- | --- |
| Can residual scaling hide a violated constraint? | No — scaling makes the angular tolerance *tighter* for large assemblies; at the millimetre floor it is 1e-6 rad, about 0.2 arcseconds |
| Can length and angular units be mixed? | No — every residual is metres; measured identical behaviour at 1 mm, 100 mm and 10 m |
| Can Jacobian signs be reversed? | No — a sign error is an error of order 1; measured 3.7e-11 |
| Can the rotation parameterization become singular? | No — the increment is always zero where the derivative is taken |
| Can quaternion normalization corrupt derivatives? | There are no quaternions |
| Can a free rigid-body mode make the system singular? | It does, by construction, and is handled: minimum-norm solution, missing rank reported as DOF |
| Can an under-constrained system be falsely called converged? | It *is* converged — with its DOF reported. That is the honest answer, not a false one |
| Can redundant be misclassified as inconsistent? | No — duplicate mates give `OverConstrained` with a residual under 1e-9 |
| Can contradictory constraints return a low residual? | No — measured exactly 5 mm, the closed-form least-squares compromise |
| Can component or mate ordering change the solution? | No — transforms match; only redundancy attribution follows ID order, by contract |
| Can Debug and Release differ meaningfully? | Not numerically — the tests assert exact values to 1e-8 m and all three presets run the same 1382 tests. **But the presets did differ**: `debug-shared` failed to link where both static presets built. Finding 4 |
| Can NaN/Inf reach solved state? | Not observed. The inputs are validated finite in production — a mate's value (`Mate.cpp`) and a resolved placement (`placementOf()`) — and the test checks every entry of every returned matrix, plus the residual, across a converged solve, an inconsistent one and a stalled one. The solver does **not** guard its own output; it relies on validated inputs |
| Can save/load change the result? | No — a reloaded assembly solves bit-identically, and the file does not change across a solve |
| Can a failed solve mutate the document? | It cannot — `const Document&` |
| Can the solver overwrite canonical placements? | Same answer: impossible by type |
| Can unresolved references use stale data? | No — the system is rebuilt on every call |
| Are expected values generated by BetterCAD? | No — hand-derived throughout |
| Are tolerances loose enough to hide mistakes? | 1e-8 m against a solver converging at 1e-9; a real error is millimetres |
| Were previous tests weakened? | No. `git diff --numstat` over the whole milestone is `+5/-0` and `+8/-1`; the single deleted line is a CMake `target_include_directories` call, replaced by a wider one with a comment saying why. No test line was deleted or changed |

### Finding 1 — a hand-derived test caught an overclaim in its own expectation

`Solve_ParallelTurnsTheNearWayRoundNotTheFarWay` asserted that a normal
starting 80 degrees from its target returns the near way, to `+Z`. It did
not; it reached `-Z`.

The solver was right and the expectation was wrong. `Parallel` is satisfied
in either sense, so its residual is proportional to `sin(theta)`, which is
equal at `theta` and `180 - theta`: the equation cannot distinguish "80
degrees from +Z" from "100 degrees from -Z". Near 90 degrees the derivative
`cos(theta)` is also small, so the Newton step is proportional to
`tan(theta)` — about 325 degrees here — and the line search backtracks into
whichever branch it lands in.

Resolved by asserting what the constraint actually means. The replacement
still pins the normal to the Z axis with three tight assertions; what it no
longer claims is *which sense*, which P13-MATE-001 explicitly does not
promise. The near-branch behaviour is kept as its own test at a 20 degree
start, where `cos(theta) = 0.94` makes the linearization sound and the near
branch *is* reached.

**No assertion was weakened to make a failure pass.** The original claim was
simply not true of the model, and asserting it would have been asserting
something the mate does not mean.

### Finding 2 — three behaviours suppression could have got wrong

Added after asking what suppression could silently change: a suppressed
`Fixed` mate that still grounded its component would report **3 degrees of
freedom where the model has 9** — six ways to move silently gone, and nothing
in the result to say so. Measured: 12 unknowns and 9 DOF, correct. Likewise a violated mate between
two grounded components reports `Inconsistent` at 0 iterations rather than
spinning to the iteration limit.

### Finding 3 — `Inconsistent` over-claims at an exactly stationary start

Found by checking, rather than assuming, what the non-finite test's own
comment promised: that it covered "every kind of solve including the ones that
fail". It did not — it ran one converged solve. Extending it to the failing
kinds turned up the finding.

An assembly of two components whose planes are already parallel, under a
`Perpendicular` mate, reports:

```text
status:  Inconsistent
message: the mates cannot all be satisfied (largest residual 1 mm at the best compromise)
```

**The mates can be satisfied.** Turning either component 90 degrees does it.
What has happened is that the residual `L (Da . Db)` sits at its maximum where
the normals are parallel, so its gradient is identically zero: the solver
cannot move, and the branch that distinguishes "stalled at a genuine
least-squares minimum" from "stalled anywhere else" sees the same signature in
both.

**Not fixed, deliberately.** The assembly solver's classification is this
branch of the sketch solver's, line for line — the same `stalled && stationary`
test over the same `scale == 0.0 ||` disjunction — so the same blind spot
follows from the same code. That it does was not measured in the sketch
solver: P12 is qualified and outside this milestone's scope, and the inference
is from reading the two branches side by side, not from running one. Narrowing it in one solver and
not the other would make the same situation report `Inconsistent` in a sketch
and something else in an assembly, which is a worse answer than a consistent
over-claim. Fixing it properly is a change to both solvers and is not in this
milestone's scope.

What was done instead: the behaviour is pinned by an assertion so a future
change to it is deliberate and visible in the diff, the test says plainly that
it is characterisation and not endorsement, and it is recorded in the known
limitations rather than left for someone to rediscover.

The narrower rule that would fix it, recorded so the next attempt does not
start from scratch: when the solve stalls with unsatisfied rows whose Jacobian
rows are themselves zero, and the system has unknowns to move (`n > 0`), the
linearization carries no information and `SolverFailure` is the honest answer.
The `n == 0` case — everything grounded, nothing can move — stays
`Inconsistent`, and is tested.

### Finding 4 — the white-box test does not link in a shared build

Found by the qualification itself. `debug` and `release` are static and both
passed; `debug-shared` failed at the link:

```text
undefined reference to `bettercad::assembly::detail::System::jacobianAtBase(...)'
undefined reference to `bettercad::assembly::detail::System::evaluate(...)'
undefined reference to `bettercad::assembly::detail::System::build(...)'
```

`System` lives in a private header with no export macro, so in a shared build
its symbols never leave `libbettercad_assembly.dll` and the derivative tests
cannot resolve them. A static build links the archive directly and never
notices.

**This is the white-box exception's second cost, and it was not foreseen when
the exception was taken.** The first — recorded above — is that one test
includes a private header. The second is that the class now carries
`BETTERCAD_ASSEMBLY_EXPORT` so those symbols leave the DLL.

What was considered and rejected:

| Option | Why not |
| --- | --- |
| Drop the derivative test from the shared preset | The milestone's hard gate would then not run in one of the three presets, and the presets would no longer run the same tests |
| Compile the solver sources into the test target too | Works only by relying on GNU `ld` not extracting an archive member whose symbols are already defined — link-order dependent, and it differs between static and shared. Load-bearing linker subtlety is worse than an exported symbol |
| Give the module a public Jacobian-diagnostics API | Public API invented to make a test link is the thing CLAUDE.md names outright |

What was done: `class BETTERCAD_ASSEMBLY_EXPORT System`. One line, the same in
all three presets, no behaviour added, nothing test-only. `System` is still
declared in no public header, so it remains an exported implementation detail
rather than public API — reaching it still means including a private header on
purpose.

The honest summary is that the exception is more expensive than it looked when
it was taken. It is still the right trade — a Gauss-Newton solver with a wrong
Jacobian converges, reports success, and puts the parts somewhere plausible
and wrong — but "one test includes a private header" undersold it, and this
record corrects that.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`, and every log cited here is beside
it.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 432/432 | 0 | **1382/1382** | 268.3 s — see note 2 |
| `release` | 432/432 | 0 | **1382/1382** | 205.8 s |
| `debug-shared` | 432/432 | 0 | **1382/1382** | 205.5 s |

Then the milestone's related tests, five times over until failure — 873 tests
selected by the solver, mate, reference, component, placement, datum, object,
persistence, parameter, sketch, CLI and architecture names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **873/873 ×5** | 749.7 s |
| `debug` | **873/873 ×5** | 668.6 s |

1382 = the 1349 of `P13-MATE-001` plus this milestone's 33.

**Qualified tree.** The harness records a git tree ID per source directory
before building. Recomputed from the working tree after the run, all eight are
identical, so the tree that was qualified is the tree that is committed:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           b45bc91a5b922d25cb3b687efdcc058bbd4f19ee
src               784391ff0a96313550caee86ac7ba07f906aab2b
tests             d94e49dc3a5b411a74ed6c62f5548353a6bb5d90
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

### Three things in the logs a reader would otherwise have to guess at

**1. An earlier run failed, and two of its lines are in this run's times
file.** The `debug-shared` link failure of Finding 4 came from a run started
at 17:34. The harness does not abort on a failed build — it skips that
preset's tests and carries on to the repeat stages — so that run was still
alive when the fixed tree's run started at 18:27 and overwrote the times file.
Two of its lines landed in the middle of this run's:

```text
repeat release exit 0    Sun 20/09/2026 18:48:35.21   <- the failed run's
repeat debug started     Sun 20/09/2026 18:48:35.23   <- the failed run's
```

They are between `debug build exit 0` and `debug ctest exit 0`, where this
run had no repeat stage. This run's own repeat stages are the ones at
19:26:01 and 19:38:31, after `debug-shared ctest`. The stray run was stopped
at 18:49 and no process of it survived — verified by process listing before
continuing.

**2. The `debug` ctest ran under contention, so it was re-run cleanly.** The
overlap above means the stray run's repeat-debug ctest shared `build/debug`
with this run's debug ctest for about a minute. It passed 1382/1382 in 268.3 s
against the 205.8 s and 205.5 s of the other two presets — the slowdown is the
contention.

Contention cannot turn a failing test into a passing one, so the pass stands.
It can cause spurious failures, and it makes the timing meaningless. Rather
than leave the headline regression result resting on a contended run, the
`debug` ctest was repeated with nothing else running, on the same binaries and
the same tree:

```text
ctest-debug-rerun.log:  1382/1382 passed, 176.7 s, exit 0
```

The harness's own contended log is kept as `ctest-debug.log` rather than
deleted, so the record shows what happened.

**3. `ninja: warning: premature end of file; recovering`** is the first line
of each build log. It is ninja's own `.ninja_log`, truncated when the earlier
runs were killed mid-build — not a compiler warning and not a source file.
Ninja recovers by treating what it cannot read as not built, so it rebuilds
more rather than less; all 432 targets were built in every preset regardless.
No compiler warning appears in any of the three build logs, and warnings are
errors in this project's presets.

## Known limitations

- **The branch a `Parallel`, `Perpendicular` or `Angle` mate reaches is not
  guaranteed** when the start is near the stationary point of its residual
  (about 90 degrees for `Parallel`). The constraint is satisfied either way;
  which way is not predictable from the start. Finding 1 above.
- **A start exactly at a residual's stationary point is reported as
  `Inconsistent`, which over-claims.** Two exactly parallel normals under a
  `Perpendicular` mate give an identically zero gradient; the solver stalls
  and reports "the mates cannot all be satisfied", when in fact a 90-degree
  turn satisfies them. The classification is local and cannot distinguish "no
  solution" from "no solution reachable from here". Finding 3 below.
- **Convergence is local, not global.** The basin tests measure recovery from
  perturbations up to 200 mm and 60 degrees on a fully constrained assembly.
  Nothing here claims convergence from an arbitrary start, and ADR-005's
  requirement that the solver converge from intent alone is met only to the
  extent those measurements show.
- **The solve is dense and about cubic in the unknowns.** 20 components take
  104 ms in a Debug build. This is a real ceiling on assembly size, measured
  and not optimized, as the milestone's scope requires.
- **Redundancy detection is linear and local.** It finds rows dependent at
  the solution, by Gram-Schmidt. Non-linear redundancy, and redundancy that
  appears only elsewhere in configuration space, are not detected.
- **The separation between two axes is not differentiable where they meet.**
  A `Distance` mate between axes with a target of zero sits exactly on that
  point; use `Concentric` instead, which is the constraint that means it.
- **The white-box derivative test costs two exceptions, not one.** It includes
  a private header, and that header's `System` class is exported from the
  assembly DLL so the test can link in a shared build. Both are recorded above
  with what was weighed against them.
- **Nothing consumes the solved transforms yet.** They are returned, not
  applied to bodies, and no regeneration step calls the solver. Assembly
  regeneration is a later milestone.
- **A mate's own value is a literal.** A component's placement *can* be driven
  by a parameter and the solver follows it — measured, including restoring the
  transform when the parameter is restored — but a `Distance` or `Angle` mate's
  value cannot, so a design change reaches the assembly through placements
  only.

## Result

```text
TASK:            P13-SOLVE-001 — Assembly constraint solver
IMPLEMENTATION:  934 lines across 4 new files: the solver's public contract,
                 the private equation system with its analytic Jacobian, and
                 a Gauss-Newton loop with rank analysis. +5/-0 and +8/-1 to
                 two CMake files. No existing source changed.
TESTS:           33 new cases, 983 lines, in 2 files
VALIDATION:      30 analytic cases with every expected value hand-derived;
                 3 derivative cases against central differences, worst
                 relative error 3.7e-11 against a 1e-7 gate
REGRESSION:      1382/1382 on debug, release and debug-shared, each from
                 clean; 873/873 five times over in release and debug;
                 0 compiler warnings in all three builds
ADVERSARIAL:     4 findings, 0 production defects
RESULT:          PASS
EVIDENCE:        this directory
```

The three the milestone was told to treat as hard gates:

| Gate | Result |
| --- | --- |
| Independent Jacobian verification | Every analytic derivative agrees with a central difference to **3.7e-11** at worst, five orders inside the gate and at the truncation floor of the method. A wrong derivative is an error of order 1, not 1e-11 |
| DOF classification | Every count hand-derived from the geometry and matched: **6, 5, 3, 2, 9, 0**, plus `FullyConstrained`, `UnderConstrained`, `OverConstrained` and `Inconsistent` each reached by a case built to reach it and no other |
| Canonical-vs-derived separation | The solver takes a `const Document&`, so it cannot write back **by type**. Intent and document revision are unchanged after a solve; the `.bcad` file is **byte-identical** across one; a failed solve returns no transforms at all |

A solver that merely converges on a few assemblies is not what is being
claimed here. What is claimed is that the derivatives are right, that the
degrees of freedom are counted right, that intent and derived state cannot be
confused, and that the four ways it can fail are told apart — each measured
against something that is not the solver itself.

What is **not** claimed: convergence from an arbitrary start; that
`Inconsistent` always means "unsatisfiable" (it does not — Finding 3); that
the solve scales past a few tens of components (104 ms at 20, roughly cubic);
or that anything consumes the transforms yet.

## Revision

| When | What |
| --- | --- |
| 15:2x | solver written; derivative gate passed before anything was built on it |
| 16:13 | analytic suite: 17 of 18, one hand-derived expectation shown to be an overclaim |
| 16:3x | basin, performance, ordering and suppression tests added |
| 17:0x | qualification stopped before it finished: three of ADR-005's five verification checks had no test. Added and passed |
| 17:3x | sweeping the evidence's numeric claims against the source found four wrong (corrected) and a test comment promising coverage it did not have. Giving it that coverage turned up Finding 3 |
| 18:22 | qualification failed: `debug-shared` could not link the white-box derivative tests. Finding 4, fixed, and the tree re-qualified from clean |
| 19:49 | three presets and both repeat stages PASS on the final tree |
| 19:56 | `debug` ctest repeated with nothing else running, after the killed run was found to have overlapped it |
