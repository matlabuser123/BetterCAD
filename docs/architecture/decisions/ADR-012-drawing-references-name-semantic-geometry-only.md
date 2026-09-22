# ADR-012 — A drawing may reference only what ADR-004 already permits

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-004 (mates reference semantic geometry only)
           ADR-011 (drawing intent is canonical)
```

## Context

A dimension on a drawing points at the model. It has to keep pointing at the
same thing when the model changes, or say plainly that it cannot — because
the failure it is exposed to is the worst one in the phase: **a dimension
that still resolves, to the wrong geometry.** Nothing reports that. The
drawing regenerates, the number changes, the sheet prints, and a part is made
to a dimension nobody chose.

An engineer dimensioning a part reaches for an **edge**. So the first
question this milestone asked was what a stable edge reference costs here.
The answer is that there is not one.

`geometry::EdgeSignature` (`include/bettercad/core/geometry/Edges.hpp:55-65`)
names an edge by the curve it lies on — a line's point and direction, a
circle's centre, axis and radius. Its own header states the position without
hedging (`Edges.hpp:18-23`):

> "BetterCAD has **no persistent topological naming yet** (a later
> milestone). Until then, features refer to an edge of a body by the curve
> the edge lies on, never by the kernel's enumeration order or object
> identity."

and the failure envelope (`Edges.hpp:38-47`):

> "If the supporting curve itself moves or changes size (e.g. the box gets
> taller, moving its top edges), **the reference matches no edge**… This is
> geometric matching, not persistent topological naming."

A box getting taller is not an exotic edit. It is the ordinary parametric
change a drawing exists to track. An `EdgeSignature` breaks on exactly the
event a drawing must survive.

`ADR-004` has already ruled on this class of reference, for mates, and the
rule is quoted in the code that enforces it
(`include/bettercad/core/document/MateReference.hpp:16-19`):

> "A mate may reference anything that moves with the model, and nothing that
> merely sits where the model used to be."

That is why a mate refuses a `FaceSignature`. An `EdgeSignature` fails the
same test for the same reason. `P12-REF-001` measured the cost of getting
this wrong: three of six production models had placed geometry on a signature
written as a literal that happened to equal a driving parameter, and each
worked at the size it was authored at and failed at every other.

What *is* stable is `FaceName` — a face named by the feature that generated
it and the role it plays there, carried through booleans by the kernel's
history, with the guarantee that "a name is never moved to a face because of
its geometry" (`include/bettercad/features/Faces.hpp:134`). Datums are stable
because they are definitions. Document objects and component occurrences are
stable because they have identity.

## Options

1. **ADR-004's vocabulary, unchanged.** A drawing reference may name a datum
   plane or axis, a `FaceName`, a document object, or a component occurrence.
   Nothing else.
2. **ADR-004's vocabulary plus `EdgeSignature`**, on the argument that a
   broken signature becomes *unresolved* rather than wrong, and unresolved is
   honest.
3. **Build persistent topological naming now**, so edges can be named
   stably, and then allow edge references.

## Decision

**Option 1.** A drawing reference may name:

```text
a datum plane or datum axis          (PlaneReference, AxisReference)
a named face of a feature            (FaceName)
a document object                    (ObjectId, via ObjectReference)
a component occurrence               (ComponentId)
```

and may not name a `geometry::EdgeSignature`, a `geometry::FaceSignature`, a
topology index, a position in a feature's own reference list, or anything
whose identity is the kernel's enumeration order.

A dimension an engineer would describe as "between these two edges" is
expressed as a dimension between the two **named faces** that meet at those
edges, or to a datum. The edge is where the faces intersect; the faces are
what the model can name.

## Rationale

Option 2 is the one that needed real argument, because "it degrades to
unresolved" sounds safe. It is not, for two reasons.

The first is frequency. Unresolved is an acceptable outcome for a rare,
destructive edit — deleting the feature that made the face. It is not an
acceptable outcome for *changing a dimension*, which is the single most
common thing anyone does to a parametric model and the one case a drawing
absolutely must survive. A reference type that breaks on the common edit
turns "my drawing still works" into a coin toss.

The second is that it does not always degrade to unresolved. `Edges.hpp:38-47`
gives two failure modes, not one: no match, **and ambiguity** where several
edges lie on the same curve after a cut splits one in two. `findEdges` is
documented as requiring exactly one match. Ambiguity resolved by any rule at
all — first, nearest, longest — is precisely the silent wrong answer this ADR
exists to prevent.

Option 3 is semantic topology. `ROADMAP.md` carries it as `P21`, and the core
rule it records is the one this ADR is applying: "wrong target is worse than
unresolved target". `Id.hpp:149-153` already declares `EdgeId`, `FaceId` and
`VertexId` with the note that "persistent naming across regenerations is
future work"; they are used nowhere outside `tests/core/IdTests.cpp`. Building
that inside `P14-ARCH-001` would be building a phase inside a milestone, and
`P14` would inherit an unqualified identity scheme underneath every dimension
on every drawing. That is the wrong order.

## Consequences

- **Some dimensions an engineer will want are not expressible in `P14`.** A
  dimension to a fillet's tangent edge, to a chamfer's cut edge, or to any
  edge that is not the intersection of two nameable faces, has no spelling.
  This is a real capability gap, it is recorded rather than worked around,
  and it closes when semantic topology (`P21`) lands — at which point this
  ADR is the one to amend.
- A drawing's reference states reuse `P13-REF-001`'s, unchanged: resolved,
  unresolved, and the document-mismatch case that ADR-010 makes unreachable
  for now.
- Recovery is inherited, not invented: when the intended target returns under
  its own identity, the reference resolves again to the same geometry,
  because the reference itself never changed. `P13-STREF-001` measured this
  for mates and the mechanism is shared.
- A drawing object's `dependencies()` is built the way a mate's is —
  `referencedObjects(...)` of each reference plus every driving
  `ParameterId`, de-duplicated — so the graph dirties a view when the
  geometry beneath it moves.
- Because `AxisReference` has no `validate()` or `referencedObjects()`
  overload today (they are open-coded in `src/core/document/MateReference.cpp:108-112`),
  a drawing that references axes will want them promoted rather than
  open-coded a second time.

## Rejected alternatives, and what would make them right

**Allowing `EdgeSignature` (Option 2)** would be right if edges were named
rather than matched — which is Option 3 — or if drawings were only ever made
of models that never change, which is not a drawing system.

**Building persistent topological naming (Option 3)** is right, and is `P21`.
When it exists, a drawing reference gains edge and vertex targets and this ADR
is amended to say so. Nothing in this decision blocks that: the reference
types are a closed enumeration, and adding a case to it is additive in the
file format (ADR-010) and in the graph.

## Verification

For `P14`: a dimension follows its named faces through a parameter change and
reports the new value; a dimension whose target feature is deleted becomes
explicitly unresolved and names what it wanted; a geometrically identical face
introduced where the target used to be is **not** adopted — the test that
matters, modelled on `StableReference_NeverRebindsToASimilarFaceThatTookItsPlace`;
the intended target returning restores the reference to the same geometry; and
constructing a drawing reference from an `EdgeSignature` or a `FaceSignature`
does not compile.
