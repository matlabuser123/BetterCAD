# P17-VALID-001 — the solve entry point, and what "cannot be bypassed" honestly means

The brief asks that every solve validate its input. The gates to do that all
existed before this milestone. **What did not exist was anything that ran
them.**

## The gap, measured

Before this milestone, `grep` for a production caller of the solver found none:

```text
solveStructuralSystem   called from tests only
recoverFields           called from tests only
recoverSupportReactions called from tests only
StructuralResult::create called from tests only
requireStructuralModel  called from tests, and from currentResultSource
```

Every gate was correct and every gate was optional. That is exactly the shape
of the limitation P16 recorded for meshes — "nothing FORCES the holder of a
mesh to ask whether it is stale" — one layer up. ADR-036 answered P16's version
with possession rather than documentation, and this answers the solver's
version the same way: with the only path that exists.

## What the entry point is

```cpp
Result<StructuralSolveOutcome>
solveStructuralAnalysis(const Document&, const features::Regenerator&,
                        const meshing::Mesher&, AnalysisId,
                        const SolverSettings& = {},
                        const Point3D& origin = Point3D{},
                        const EquilibriumTolerance& = {});
```

It takes a DOCUMENT and an identifier. It does not take a mesh, a
`VolumeMesh`, a `StructuralModel`, a `GlobalStructuralSystem` or a
`SolvedSystem`, and two compile-failure cases prove there is no overload that
does:

```text
SOLVE_ENTRY_TAKES_A_MESH      solveStructuralAnalysis(*held, analysis)
QUALITY_ENTRY_TAKES_A_MESH    validateStructuralMeshQuality(mesh)
```

The second matters for a different reason: the quality function takes a
`MeshQualityReport`, so a P17 function that could compute a radius ratio does
not exist. One definition of every metric lives in `src/meshing/MeshQuality.cpp`
and the signature is what keeps it that way.

## The order, and one implementation of it

```text
1  validateStructuralAnalysisForSolve   -- and STOP on Rejected
2  resolveStructuralMaterial            -- for the analysis MODE
3  buildMeshDofMap
4  prepareStructuralRestraints          -- against the CURRENT mesh
5  prepareStructuralLoads               -- against the CURRENT mesh
6  assembleStructuralSystem
7  solveStructuralSystem                -- settings, sources, pivot, residual
8  recoverFields                        -- finiteness
9  recoverSupportReactions              -- FORCE and MOMENT equilibrium
10 currentResultSource                  -- provenance, from P17-DATA
11 StructuralResult::create             -- cardinality and finiteness
```

Step 1 CALLS the validator rather than repeating its checks, so the two cannot
disagree about what a solve requires. The cost is that steps 2 to 5 re-prepare
what validation already prepared — a few lookups and two O(nodes) passes, which
is the price ADR-036 already chose to pay for re-preparing rather than holding.

Step 10 is deliberately not assembled field by field here.
`currentResultSource` calls `requireStructuralModel` itself and adds the
analysis and material revisions, so a result's identity is built by the one
place that owns currency. A `StructuralResultSource` constructed in this file
would be a second opinion about what a result depends on.

**Nothing is published unless every step passed.** There is no partial
outcome: a failure returns an error and no result, because a result that claims
to be a solution and is not is worse than no result at all.

## The honest limit of "cannot be bypassed"

Stated plainly, because the claim is easy to overstate.

The stages below this entry point REMAIN PUBLIC, and they must: P17's
analytical validation needs them, because a structural fixture cannot pose a
system whose answer is known in closed form — the smallest mesh a
`StructuralModel` can be built from has dozens of degrees of freedom. So a
caller determined to assemble and solve by hand still can.

What they get if they do is a `SolvedSystem` with every one of ITS gates
applied: the settings check, source agreement, finiteness, the pivot ratio and
an independent residual. What they cannot do is obtain a `StructuralResult` for
a document without going through `currentResultSource`, which calls
`requireStructuralModel` and inherits every input gate.

```text
CLAIMED      this is the only production path from a Document to a
             StructuralResult, and it validates
NOT CLAIMED  that the compiler forbids every alternative route to a number
```

A caller could still construct a `StructuralResultSource` by hand and pass it
to `StructuralResult::create`, since that struct is plain data. Closing that
would mean making result provenance a possession-gated type of its own, which
is a P17-DATA decision and not this milestone's to take.

## What the outcome carries, and why each field is there

```text
validation      the input report, INCLUDING WARNINGS. A successful solve does
                not discard them: a result computed on a mesh outside the
                qualified envelope is still a result that should be shown
                with the warning. `WarnsWhenNothingIsLoaded` asserts the
                warning survives into the outcome.
result          the published StructuralResult
residual        P17-SOLVE's independent residual metrics, as IT measured them
pivotRatio      min|D| / max|D| -- the sufficiency verdict
strainEnergy    (1/2) u^T K u
freeEquations   the rows actually factorised
forceBalance    P17-REACTION's force equilibrium, at the caller's tolerance
momentBalance   and the moment equilibrium, about the caller's origin
largestDisplacement, largestVonMises   P17-POST's extremes
```

Each is READ from the stage that owns it. Nothing in this module recomputes a
residual, a pivot ratio, an energy or a balance, and no second tolerance is
defined beside any of them.

`origin` is explicit rather than defaulted-and-forgotten because equilibrium
holds about EVERY origin and a caller reporting about a support wants that one
— and because a P17-REACTION-001 mutation proved that with everything at the
global origin, `x - O == x` and an origin-ignoring path is a no-op.

## Tests that hold this file's claims

```text
SolvesAWellPosedAnalysisEndToEnd
    the whole chain from a Document. The result describes the mesh, its
    arrays match the node and element counts, the reactions are non-empty,
    resultCurrency says Current, every gate's measurement is finite and
    positive, equilibrium holds -- AND the reaction really is -1000 N against
    an applied +1000 N, so the balance cannot be a pair of zeros.

RefusesAStaleMeshThroughThePublicEntry
    the gap P16 could not close, closed, with all three premises asserted.

PublishesNothingWhenAGateRefuses
    the residual gate and the equilibrium gate, each reached by tightening
    it below its measured value, each producing no result.

DoesNotClaimSixDegreesOfFreedomIsSufficient
    validation accepts, the factorisation refuses. The asymmetry in one test
    because either half alone looks like the forbidden rule.

StopsAtTheFirstRefusingStage
    a model wrong in three ways reports ONE finding, naming the cause.
```
