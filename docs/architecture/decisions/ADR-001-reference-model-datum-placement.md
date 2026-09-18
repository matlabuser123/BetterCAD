# ADR-001 — Reference models are datumed so geometric references never move

Status: Accepted
Date: 2026-09-19

## Context

BetterCAD places some features by **semantic name** (`FaceName` /
`FaceSelector`, e.g. `FaceRole::EndCap`) and others by **geometric
signature** (`geometry::FaceSignature`, `geometry::lineSignature` — a plane
or line in model space and a side). Shell, draft and sketch attachment take
the first. Holes and fillets take only the second.

`geometry::FaceSignature` is documented, deliberately, as geometric matching
and not persistent topological naming: a face may grow or shrink within its
plane and still match, but **if the plane moves, the reference matches no
face**, and that is reported rather than guessed around.

P12-REF-001 built six production reference models. Three of them placed a
hole or a fillet on a signature written as a literal that happened to equal a
driving parameter — `z = 40` where 40 was `height`, `x = 90` where 90 was
`width`. Those models regenerated correctly at the size they were authored
at and **failed outright at any other**, and no test drove those parameters,
so nothing noticed until the adversarial review. Reproduced:

```text
InspectionPort: hole: the placement face (plane through (0, 0, 40) mm facing
(0, 0, 1)) matches no face of the body
```

## Constraints

- `FaceSignature`'s contract is correct and must not change here: silently
  binding to a different face after regeneration is the failure mode the
  project explicitly forbids. Failing loudly is the right behaviour.
- Adding semantic face naming to `HoleDefinition` and the fillet features is
  a feature change, outside this milestone's authorization.
- Reference models are permanent, and are the suite's evidence that features
  compose. A reference model that breaks when its own parameters change is
  not evidence of anything.

## Options

1. **Add `FaceName` support to holes and fillets.** Correct long-term, but a
   feature change in a milestone authorized only to build models, and it
   would need its own persistence, validation and qualification.
2. **Accept that those parameters are not drivable** and document it.
   Cheapest, but leaves production reference models with parameters that
   destroy them — the opposite of what the models exist to demonstrate.
3. **Datum the models so the planes carrying geometric references never
   move.** Choose the model's origin and extrude directions so that every
   face a hole or fillet is placed on is invariant under every parameter,
   and reach the faces that *do* move with the references that follow them
   (named faces, driven datums).

## Decision

Option 3, as a standing constraint on reference models: **a reference model
must be datumed so that every plane or edge named by a geometric signature is
invariant under all of the model's parameters.**

Concretely, in `GearboxCover` the cover now hangs *below* its outside face —
`z = 0` is the outside for every height — and the parting face, which does
move, is reached by a datum plane driven by `-height`, with the shell's open
face reached by a named cap. In `RibbedBracket` the filleted corner is the
one at `x = 0` rather than at `x = width`, and the rib sits on a datum driven
by `width/2`.

## Rationale

Option 3 costs nothing at runtime, needs no new capability, and produces
models that are *better engineering documents*: a cast cover really is
dimensioned from its outside tooling face, and choosing the invariant corner
is what a careful modeller does anyway. It also leaves option 1 open — when
semantic naming reaches holes and fillets, these models keep working and can
be simplified.

Option 1 would have been right if the milestone had been authorized to change
the feature set, or if some model genuinely could not be datumed to satisfy
the constraint. Neither was true.

Option 2 was rejected because a parameter that destroys the model is a defect
whether or not it is documented.

## Consequences

- Every future reference model must pick its origin before its features, and
  say in a comment why the invariant face is the one it is.
- This does **not** generalise to user models: a user can place a hole on any
  face, and will hit the same limit. That limit is now recorded, with an
  executable demonstration, in `P12-REF-001`'s evidence.
- It follows that a reference model **cannot be relocated by moving its own
  datum**. `ReferenceModel_RibbedBracketIsBuiltOnItsFrame` records exactly
  this: the sketches and the rib follow the frame, the hole and the fillet do
  not, and the regeneration fails atomically and says why.

## Verification

- `ReferenceModel_GearboxCoverFollowsItsHeight` — drives `height` up and down
  and checks the closed form at each; fails without this decision.
- `ReferenceModel_RibbedBracketFollowsItsWidth` — drives `width`.
- `ReferenceModel_RibbedBracketIsBuiltOnItsFrame` — records the boundary.
- Evidence: [docs/verification/P12-REF-001/](../../verification/P12-REF-001/README.md),
  "Adversarial review" findings 1 and 6.
