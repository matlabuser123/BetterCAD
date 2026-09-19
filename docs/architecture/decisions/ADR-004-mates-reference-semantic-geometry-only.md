# ADR-004 — A mate may reference only semantic geometry

Status: Accepted
Date: 2026-09-19

## Context

A mate holds two pieces of geometry in a relationship — coincident faces,
concentric axes, a distance between planes. It must therefore name geometry
on a part, and that name has to survive the part being edited. An assembly
whose mates break whenever a part parameter changes is not an assembly.

BetterCAD has two kinds of reference, and they behave differently on exactly
this point:

**Semantic names survive.** `FaceName{ObjectId feature, FaceSelector face}`
(`include/bettercad/core/document/References.hpp:113`) names the face by the
feature that generated it and its role there. The propagation rule is stated
at `include/bettercad/core/geometry/Faces.hpp:130`: names "are carried by the
boolean operations through the kernel's history: a face split in two carries
its name on both parts, faces merged into one carry all their names, a face
the operation removed carries none." And decisively:

> A name is never moved to a face because of its geometry.

Resolution failure is loud, never a guess
(`include/bettercad/features/FaceReferences.hpp:26`): "when no face carries
the name, the reference fails, and no other face is taken because it lies
where the named face used to be."

**Geometric signatures do not survive.** `geometry::FaceSignature` matches a
plane in model space (`Faces.hpp:62`). Its own header says the face may grow
or shrink within its plane and still match, but "if the plane moves (e.g. the
block gets taller and its top face rises), the reference matches no face."

`P12-REF-001` measured what that costs. Three of six production reference
models placed a hole or a fillet on a signature written as a literal that
happened to equal a driving parameter. They regenerated at the size they were
authored at and failed at every other:

```text
InspectionPort: hole: the placement face (plane through (0, 0, 40) mm facing
(0, 0, 1)) matches no face of the body
```

That is BetterCAD behaving correctly — the failure is the contract working —
but the models were wrong, and no test caught it because no test drove those
parameters. [ADR-001](ADR-001-reference-model-datum-placement.md) records the
fix and the standing constraint it produced.

**Datums are the third kind, and the most stable.** `PlaneReference` and
`AxisReference` (`References.hpp:126`, `:147`) name a principal plane, a
datum plane, a coordinate system's principal plane or a named face, and
resolve through `features::resolvePlane` / `resolveAxis`
(`include/bettercad/features/Datums.hpp:216`) with a full error taxonomy.
Datums are parameter-driven by construction
(`src/features/datum/DatumResolution.cpp:109`), so they *move with* the model
rather than being invalidated by it.

## Constraints

- Invariant: "never permanently assume `Face 7 today = Face 7 after
  regeneration`" (`CLAUDE.md`, Architecture Rules).
- Never silently select a different face or edge after regeneration.
- `HoleDefinition.face` accepts only a `FaceSignature`, not a `FaceName`
  (recorded as a known limitation of `P12`).

## Options

1. **Datums and semantic face names only.** A mate references a
   `PlaneReference`, an `AxisReference`, or a `FaceName`. `FaceSignature` is
   refused at validation.
2. **Also allow `FaceSignature`.** Any planar or cylindrical face becomes
   mateable, including hole bores and fillet surfaces.
3. **Datums only.** Not even `FaceName`; parts must publish explicit mating
   datums.
4. **Defer** to `P13-MATE-001`.

## Decision

Option 1. A mate may reference:

```text
PlaneReference   principal plane, datum plane, coordinate-system plane,
                 or a named face of a feature
AxisReference    principal axis, datum axis, coordinate-system axis
FaceName         a face named by the feature that generated it
```

and may **not** reference a `FaceSignature`. Validation refuses it; this is
not a runtime warning.

## Rationale

Option 2 reproduces a defect this project has already measured, and
multiplies it. In a part, a broken signature breaks that part. In an
assembly, it breaks every assembly that instances the part, and the failure
surfaces far from the parameter that caused it. `P12-REF-001` found that
class of defect only because an adversarial review drove parameters no test
drove; at assembly scale the same defect is harder to find and worse when
found. Allowing it would be choosing a known failure mode for convenience.

Option 3 is the most disciplined and was seriously considered: mating to
published datums is what careful assembly practice does, and it would make
every mate parameter-driven by construction. It was rejected because it makes
the twelve existing reference models unmateable until each gains datums, and
because `FaceName` already meets the actual requirement — it is semantic, it
rides the kernel history, and it fails loudly. Refusing it would be
stricter than the evidence demands.

Option 4 was rejected because this is precisely the decision that constrains
the rest of the phase. `P13-MATE-001` cannot design its data model without
knowing what a mate may point at, and deferring it would mean discovering the
constraint after the code exists.

The rule that falls out is simple enough to state in one line, which is a
good sign: **a mate may reference anything that moves with the model, and
nothing that merely sits where the model used to be.**

## Consequences

- **You cannot mate to a hole's face.** Holes accept only a `FaceSignature`
  today, so a bore is not nameable. Mating a bolt to a hole means mating to a
  datum axis published for it, or to the face the hole was drilled from.
  Giving `HoleDefinition` a `FaceName` option would lift this, and is a
  feature change outside `P13`.
- Parts intended for assembly should publish mating datums. This is a
  modelling convention the assembly reference models will have to
  demonstrate, and it is the same discipline
  [ADR-001](ADR-001-reference-model-datum-placement.md) already imposed.
- A mate's `dependencies()` includes the objects its references name, via
  `referencedObjects()` (`References.hpp:142`), which is what makes a mate
  rebuild when the datum it uses moves.
- Mate validation gains a distinct error for "this reference kind is not
  allowed in a mate", separate from "this reference does not resolve". The
  first is a modelling mistake, the second a regeneration failure, and
  collapsing them would hide which one occurred.
- Fillet and chamfer surfaces are equally unnameable, for the same reason.

## Verification

When `P13-MATE-001` implements this: a mate constructed with a
`FaceSignature` is refused at validation with a distinguishable error; a mate
to a datum plane survives a parameter change that moves the plane, and the
solved transform follows it; a mate to a `FaceName` survives a parameter
change that resizes the face within its plane; and a mate whose named face is
removed by a later feature fails loudly and blocks, with no substituted face.
