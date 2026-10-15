# P17-REFMOD-001 — mutation testing

```text
14 probes: 14 KILLED, 0 survived, 0 equivalent
           11 on the models, the catalog and the oracles
            3 on PRODUCTION formulas, to show the suite sees the implementation
```

Each probe is one textual edit, applied by a script that builds debug-ext,
runs the named Catch2 filter, records the result and restores the file (the
restore was checked with `git` and by text search before the qualification
was launched; the qualification then cleans and rebuilds every preset from
the restored tree). Logs are in the session's scratch directory and the
verdict lines were transcribed from them.

## The probes

| # | File | Mutation | Filter | Verdict | What killed it |
| --- | --- | --- | --- | --- | --- |
| M01 | StructuralModels.cpp | RM-FEA-02's Uz roller becomes a FIXED base | `[rm02]` | killed, 4 | the affine field (`worst < kExact`), the per-element strain, the Ux roller's reaction, `U = F delta / 2` |
| M02 | StructuralModels.cpp | RM-FEA-04's pressure sign flipped in the builder | `[rm04]` | killed, 1 | the declaration guard `pressure == kBlockPressurePa` -- the physics gates behind it were not reached; see M13 for the same sign through the physics |
| M03 | Analytic.hpp | the ORACLE's shear stress becomes `2 mu gamma` | `[rm01]` | killed, 4 | single-tet stress, single-tet internal forces, patch stress, patch `K u` |
| M04 | StructuralModels.cpp | RM-FEA-06's placed document is built on the base frame | `[rm06]` | killed, 5 | the vacuity guard (first node moved > 1 mm) FIRST, then the boundary gap, the tet correspondence, the centroid match and the rotated displacements |
| M05 | StructuralModels.cpp | RM-FEA-07 refines BOTH caps | `[rm07]` | killed, 1 | the declaration guard `local.size() == 1`; the mirror physics was not reached, which is why M14 exists |
| M06 | StructuralModels.cpp | RM-FEA-08's second Uz restraint becomes FIXED, so the model solves | `[rm08],[suite]` | killed, 2 | the declared-outcome gate (`outcome.has_value() == expectSolve`), and `constrainedDegreesOfFreedom == 8` |
| M07 | StructuralModels.cpp | RM-FEA-10's second load targets the real top face | `[rm10],[suite]` | killed, 2 | the declared-outcome gate, and `status() == Rejected` |
| M08 | StructuralReferenceModels.hpp | RM-FEA-09 declared a refusal | `[suite]` | killed, 2 | `refusals == 2`, and the declared-outcome gate |
| M09 | StructuralModels.cpp | gravity points UP | `[rm05]` | killed, 1 | `g.z < 0` -- the direction guard, read from the analysis's own load |
| M10 | Analytic.hpp | the ORACLE's mean self-weight stress loses its `/ 2` | `[rm05]` | killed, 1 | the exact mean-stress identity |
| M11 | **src/structural/StructuralLoad.cpp** | a facet's consistent nodal force becomes `A t / 2` | `[rm04],[rm02]` | killed, 7 | the axial bar's affine field, strain, reaction and energy; the pressure block's applied resultant, reaction and mean stress |
| M12 | **src/structural/Tet4Element.cpp** | D carries `2 mu` on the shear diagonal | `[rm01],[rm02]` | killed, 5 | single-tet stress and internal forces, the rigid-rotation energy, patch stress and `K u` |
| M13 | **src/structural/StructuralLoad.cpp** | a pressure acts OUTWARD | `[rm04]` | killed, 3 | the applied resultant, the reaction and the mean stress, all by sign |
| M14 | StructuralModels.cpp | the local control is on the WRONG cap in both documents | `[rm07]` | killed, 2 | the band physics: `atRoot.rootBand > 10 x none.rootBand` and its mirror -- the refinement went to the other end |

## Three things the table says

**The production probes are the ones that matter, and all three died on
closed forms.** M11, M12 and M13 are the defects the brief names as the
hardest to see -- a factor in the consistent load, a factor in the shear
stiffness, a sign in the pressure -- and each was caught by a number computed
from the models' dimensions and the document's material, never from the code
it was mutated in. M12 in particular is the halved/doubled engineering shear
that `Tet4Element.hpp` warns "leaves the normal terms right".

**Two kills were by a declaration guard rather than by physics, and that is
recorded rather than dressed up.** M02 died on `pressure == kBlockPressurePa`
and M05 on `local.size() == 1`, both of which are the suite checking that the
builder built what the catalog says. The physics behind them is reached by
M13 (the same sign, through the load integration) and M14 (the same
wrong-region defect, through the bands), which is why those two probes were
added.

**The oracle is not dead weight.** M03 and M10 mutate `Analytic.hpp`, not the
code under test, and the suite fails -- so the comparisons are between two
independent computations, and a change to either side is visible. A suite
whose oracle could be edited without consequence would be comparing the
implementation with itself.

## The restore

After M13 the script rebuilt the tree (exit 0); M14 ran afterwards and was
restored by the same mechanism. Before the qualification was launched,
`git diff --stat` over `src/`, `include/` and `apps/` was empty, the working
tree listed exactly the twelve entries this milestone adds or modifies, and
the M14 line was back to `m.refined()` by text search. The qualification then
cleaned and rebuilt every preset from that tree, which is the restore that
counts.
