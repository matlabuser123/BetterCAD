# P17-VIZ-001 — the measured evidence for what is built

Every table here is produced by a test in
`tests/renderer/StructuralResultViewTests.cpp`, run in `debug-ext`. The
fixture is a **real solve** in every case: `RecoveredFields` has a private
constructor with one friend, so there is no way to synthesise a result, and
every number below came through `solveStructuralAnalysis`.

Fixture: 40 × 30 × 20 mm steel block, 20 mm global target with a 6 mm local
control on one side face, end cap pushed +1000 N along z against a fixed start
cap.

## Channel table

`StructuralViz_EveryChannelHasOneDescriptorAndOneDomain` and
`StructuralViz_EveryChannelClassifiesItsWholeDomain`.

| channel | domain | internal unit | source member | may be negative |
| --- | --- | --- | --- | --- |
| DisplacementMagnitude | Node | m | `NodalDisplacement::magnitude` | no |
| DisplacementX / Y / Z | Node | m | `NodalDisplacement::displacement.{x,y,z}` | yes |
| VonMises | Element | Pa | `ElementFields::vonMises` | no |
| StressXX / YY / ZZ | Element | Pa | `ElementFields::stress.{xx,yy,zz}` | yes |
| StressXY / YZ / ZX | Element | Pa | `ElementFields::stress.{xy,yz,zx}` | yes |
| PrincipalStress1 / 2 / 3 | Element | Pa | `ElementFields::principalStress.sigma{1,2,3}` | yes |
| HydrostaticStress | Element | Pa | `ElementFields::hydrostatic` | yes |

```text
15 channels, every name distinct, every descriptor self-consistent
exactly 2 are norms and cannot be negative:
    DisplacementMagnitude, VonMises
the 4 nodal channels report unit "m"; the 11 element channels report "Pa"
```

**Every value is a read of a stored member.** A grep of the production files
with comments stripped finds no von Mises formula, no eigensolver, no
deviatoric invariant, no `sqrt` of a stress and no nodal average:

```text
radiusRatio= aspectRatio= cross( std::sqrt abs( .erase( swap( remesh
-> 0 matches in src/renderer/StructuralResultView.cpp
-> 0 matches in src/renderer/StructuralInspection.cpp
```

## Classification cardinality

`StructuralViz_EveryChannelClassifiesItsWholeDomain`, for all 15 channels.

| | result |
| --- | --- |
| expected == domain size | PASS for every channel |
| classified == expected | PASS for every channel |
| classified > 0 | PASS for every channel |
| skipped | 0 for every channel |
| nonFinite | 0 for every channel |
| `complete()` | true for every channel |
| extreme identity set for the channel's own domain, the other kind left invalid | PASS |

The P16 trap is closed in the type: `ScalarField::complete()` requires
`expected > 0 && classified == expected && skipped == 0 && nonFinite == 0`, and
`field()` **refuses** rather than returning a partial field:

```text
EmptyClassificationDomain    a zero-entity domain is a question nobody asked,
                             not a uniform field
IncompleteClassification     rendering the values that did classify, as though
                             the field were whole, is the "silently skip" defect
NonFiniteValue               refused, never mapped to a colour
ResultSourceMismatch         refused before anything is read
```

`StructuralViz_ExtremeNavigationIsGlobalAndDeterministic` additionally proves
an incomplete field and an empty field each have **no extreme**, which is the
same trap one layer up.

## Range and extremes, against an independent sweep

`StructuralViz_RangeAndExtremesMatchAnIndependentSweep`. The oracle iterates
the source arrays in the test and computes min/max/argmin/argmax itself; the
production classifier is never asked for its own expected values.

| channel | check | result |
| --- | --- | --- |
| DisplacementMagnitude | min, max, argmin, argmax vs the test's own sweep | exact match |
| DisplacementMagnitude | max vs `fields.largestDisplacementMagnitude()` | exact match |
| DisplacementMagnitude | max vs `outcome.largestDisplacement` | exact match |
| VonMises | max, argmax vs the test's own sweep | exact match |
| VonMises | max vs `fields.largestVonMises()` and `outcome.largestVonMises` | exact match |
| VonMises | minimum >= 0 (it is a norm) | PASS |

Three independent statements of the same number agree for each.

### Signed channels keep their negatives

`StructuralViz_ASignedChannelKeepsItsNegativeValues`. Poisson contraction makes
the transverse displacements and several stress components negative under the
fixture's axial load.

```text
channels swept: DisplacementX, DisplacementY, StressXX, StressYY, StressZZ,
                PrincipalStress3, HydrostaticStress
channels found with a negative minimum: > 0   (asserted non-vacuous)
for any channel whose values are ALL negative, the maximum is asserted
negative -- which is the defect an accumulator seeded at 0.0 produces
```

The seeding rule is in the implementation and is the reason: the extremum is
seeded **from the first value**, never from `0.0`. This project has been caught
by that once already, when a peak principal stress under inward pressure came
back as its own initialiser.

### Channels are not transposed

`StructuralViz_ChannelsAreNotTransposed`. Each of the 11 element channels is
read back against the exact source member it claims to come from, element by
element, with **exact** equality — it is a read, so any difference is a wrong
member.

```text
tau_yz and tau_zx are written out separately and in that order, because a
field with equal shears passes either way and a transposition would hide
```

Principal stresses are additionally asserted to remain descending,
`sigma1 >= sigma2 >= sigma3`, as P17-POST produced them and not re-sorted.

## The deformed display

`StructuralViz_DeformedPositionsAreTheUndeformedPlusScaledDisplacement`.

| check | result |
| --- | --- |
| scale 0 reproduces the undeformed buffer **exactly**, bit for bit | PASS |
| scale 1 equals `undeformed + u` per component, against the test's own oracle | PASS, all vertices, non-vacuity asserted |
| scale 100 multiplies the **offset** by 100, not the position | PASS, with "something actually moved" asserted |
| scale 20 after scale 10 equals scale 20 directly — **not cumulative** | PASS |
| 20 toggles between 0 and 1 are idempotent | PASS |
| NaN, +Inf, -Inf, -1.0, -0.5 refused | PASS, `InvalidArgument` |
| 0.0 accepted | PASS |

The cumulative-deformation bug is **unrepresentable**, not merely tested:
`deformedPositions` returns a fresh buffer derived from `MeshView::positions()`
every call, so there is no accumulator to corrupt and no in-place update API.

## Source guards

`StructuralViz_RefusesAResultFromADifferentMesh`. The body is unchanged and the
mesh is coarsened, so the new mesh **restarts its NodeIds and ElementIds from
1** — numeric ids repeat, which is the adversarial case.

```text
the new mesh's first node really is NodeId 1                 asserted (premise)
the old result view does not describe the new mesh           PASS
field() on the new mesh refuses, naming result_source_mismatch PASS
deformedPositions() on the new mesh refuses                   PASS
a view cannot be built from the new mesh + the old fields     PASS
```

## Immutability

`StructuralViz_RenderingChangesNothingItReads`. Every visualisation operation
the milestone offers is run — five deformation scales and all 15 channels — and
the inputs are compared before and after.

| fingerprint | result |
| --- | --- |
| every mesh node coordinate, and the `MeshStamp` | byte-identical |
| every nodal displacement and magnitude, and four element channels | byte-identical |
| the `AssemblySource` | identical |
| `MeshView::positions()`, the view's own undeformed buffer | byte-identical |

The last row is the one that catches an in-place deformation: if the deformed
buffer were an update rather than a copy, that is what would have moved.

## Currentness

`StructuralViz_StaleResultIsNeverDrawableAsCurrent`. Both authorities are
consumed and neither is restated; there is no GUI stale flag.

| edit | `mesh` (P16) | `analysis` (P17-DATA) | drawable as current | stale reason |
| --- | --- | --- | --- | --- |
| none | Current | SolvedCurrent | **yes** | — |
| no result at all | Current | Ready | no | — |
| **material** | **Current** | **SolvedStale** | no | Material |
| **restraint** | **Current** | **SolvedStale** | no | Analysis |
| geometry | StaleGeometry | — | no | — |
| remesh | Current | SolvedStale | no | Mesh |

The two middle rows are the distinction the brief singles out: a material or
restraint edit leaves the **mesh current** and the **result stale**, and
`resultStaleOnACurrentMesh()` answers it directly so a UI cannot send the user
to remesh. `resultMayDrawAsCurrent()` is true for `SolvedCurrent` and nothing
else.

## Inspection

`StructuralViz_InspectionReportsExactSourceValues` and
`StructuralViz_RestraintComesFromTheRestraintsNotTheDisplacement`.

```text
every node     canonical position, displacement and stored magnitude,
               exact, for ALL nodes of the result
every element  strain, stress, both principal sets, von Mises and
               hydrostatic, exact, for ALL elements
reactions      force and component mask exact for every restrained node;
               an UNRESTRAINED node has NO reaction rather than a zero one,
               with the number of refusals asserted > 0
restraint      read from the PREPARED RESTRAINTS, never from the
               displacement -- a node can sit at exactly zero displacement
               without being restrained, and that case is swept
               restraints from a different mesh are refused, not ignored
```

## Overlays

`StructuralViz_LoadGlyphsReadThePreparedContributions`,
`StructuralViz_RestraintGlyphsDistinguishTheirComponents`,
`StructuralViz_ReactionArrowsKeepTheQualifiedSign`,
`StructuralViz_ArrowScaleIsPresentationOnly`.

The overlays carry **no load physics**: `LoadContribution` already holds the
`LoadId`, the kind, the element count, the area and the resultant, so this
layer never sees a surface normal and never re-integrates.

| check | measured |
| --- | --- |
| positive pressure on the +z end cap | resultant **along −z** — inward, P17-LOAD's frozen convention |
| its magnitude | \|F\| = p·A to 1e-9 |
| negative pressure | resultant along +z — suction, signed and not clamped |
| a global +x traction on the +z face | resultant along +x, z-component 0 to 1e-9 — **not** aligned to the normal |
| nodal forces | ascending, no repeats, each exact, summing to 1000 N to 1e-9 |
| a zero load | still reported, `hasDirection == false` — no invented axis |
| fixed support | `isFixed()`, 3 components, DOFs == nodes × 3 |
| Ux / Uy / Uz only | not fixed, 1 component, the right one, DOFs == nodes |
| two restraints | each keeps its own `RestraintId` and mask |
| reaction arrows | exact per node, ascending, summing to **−1000 N** to 1e-9 — opposing the load, sign **not** reversed |
| the summed reaction vs `outcome.forceBalance.reaction` | agrees to 1e-12 |
| arrow length | proportional; −75 and +75 give the same length; 0 gives 0, not a minimum glyph |
| arrow scale ×100 | length ×100 exactly; reaction arrows byte-identical before and after |
| bad scale / reference / magnitude | refused |


## The public numerical kernel

Added by the adversarial review — see
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) Finding 1.
`StructuralViz_AChannelWithNoValueIsAbsentNotZero`.

| check | result |
| --- | --- |
| channels served by the nodal reader | **4** |
| channels served by the element reader | **11** |
| served by both | **0** — an element value is never averaged onto the nodes |
| served by neither | **0** — no dead `default:` arm remains |
| the two sums vs `allResultChannels().size()` | 4 + 11 == **15**, over the whole enum |
| every channel's `has_value()` vs `descriptorOf(channel).domain` | agrees, all 15 |
| a node that did not move, `DisplacementX` | present, `== 0.0` exactly |
| the same node, `VonMises` | **absent** — distinguishable from the zero above |
| `magnitude` read vs `hypot` of the components | **differs** (4.0e-6 vs 3.742e-6), so a re-norm cannot pass |
| `vonMises` read vs the invariant of the six components | **differs**, so a re-derivation cannot pass |
| the 11 element reads, exact `==` on unequal components | all pass; `yz == 55`, `zx == 66`, so a transposition cannot pass |

### The defect it closes, measured

The nodal reader was returned to answering `0.0` where it has nothing to say —
the original code's behaviour:

```text
nodal channelValue returns 0.0 instead of std::nullopt

  1 of 20 tests failed:
      unit.StructuralViz_AChannelWithNoValueIsAbsentNotZero
  the other 19 all PASSED
```

The new case is the only test that notices, which is the measurement of the gap
it closes rather than a claim about it.

## The two shared-build traps, checked before the freeze

Both are recorded traps that fail **only** in `debug-shared-ext`, so they are
checked by inspection in advance rather than discovered three hours into a
qualification.

```text
export macro on a header-defined constexpr or inline
  grep -nE "BETTERCAD_RENDERER_EXPORT" the two headers | grep -E "constexpr|inline"
      0 matches

  26 export macros in all (11 in StructuralResultView.hpp, 15 in
  StructuralInspection.hpp), every one on an out-of-line declaration.

a non-exported data struct with an out-of-line member
  the only out-of-line member definitions in either .cpp belong to
  StructuralResultView::{of, describes, domainSize, field, deformedPositions},
  and that class carries the macro at CLASS level:
      class BETTERCAD_RENDERER_EXPORT StructuralResultView {

  the 11 plain structs -- ChannelDescriptor, ScalarField, ExtremeEntity,
  StructuralDisplayState, NodeResultInspection, ElementResultInspection,
  ReactionInspection, LoadGlyph, NodalLoadArrow, RestraintGlyph,
  ReactionArrow -- define every member in the header, so none needs the macro.
      0 out-of-line definitions each
```

## Totals

```text
47 targeted cases, 47 passing        tests/renderer/StructuralResultViewTests.cpp
                                     37 headless + 10 tagged [frame] for a GPU
0 warnings                           every build, -Werror on
```

This file grew with the milestone, so a count here is the count NOW and not
the count when a section above it was written. The figure was 20 while the
adversarial review was being resolved and 33 at the b04a9e1 qualification;
both appear in context elsewhere and are correct there.

The regression set and the three-preset qualification are run after the
mutation probe set, not before it — the probes change the source, and a
qualification whose tree then moves is void.

## The brief's explicit prohibitions, checked against the source

Each of these is a stated rule for this milestone rather than a general
preference, so each is checked rather than asserted. **Comments are stripped
before counting**, because every one of these files states the rule it obeys in
a comment and an unqualified grep therefore finds the rule itself — the mistake
an earlier revision of UNIT_BOUNDARY.md made.

| prohibition | check | result |
| --- | --- | --- |
| no second von Mises formula, eigensolver or deviatoric invariant | `grep -nE "sqrt\|eigen\|Eigen\|deviator\|invariant\|pow\("` over both `.cpp` files | **0** in code; the 2 matches are the comment stating the rule |
| no silent averaging of an element value to the nodes | `grep -niE "average\|smooth\|interpolat\|extrapolat"` | **0** in code; 1 comment |
| no invented rotational nodal reaction DOF | `grep -ciE "\b(moment\|torque\|rotation\|rotational)\b"` over all four files, comments stripped | **0, 0, 0, 0** |
| no metres-to-millimetres conversion in this layer | `grep` for `1000` and for `toModel`, comments stripped | **0** |
| no geometric recogniser for an unnamed face | `grep` for a cylinder test, axis fit, radius or coordinate tolerance | **0** |

The word-boundary form of the rotational check matters: the first attempt used
bare `rx|ry|rz` alternatives, which matched inside `eve`**`ry`**,
`geomet`**`ry`** and `restating`, and reported ten hits that were ordinary
prose. A grep that can match inside a word is not a measurement.

`ReactionInspection` carries exactly three fields, which is the positive form
of the same claim:

```cpp
meshing::NodeId node{};
Force3D force{};
structural::RestraintComponents constrained{};
```

Three translational components and the mask that produced them. No moment, and
no fourth-to-sixth component for a UI to populate with a guess.
