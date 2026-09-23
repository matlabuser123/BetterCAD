# ADR-021 — An assembly view is one hidden-line problem, not one per component

```text
Status:    Accepted
Date:      2026-09-23
Milestone: P14-ASM-001
Builds on: ADR-002 (assemblies live in the document)
           ADR-005 (a component is drawn where the solver put it)
           ADR-011 (drawing intent is canonical, projection is derived)
           ADR-013 (view orientation and drawing scale)
           ADR-019 (hidden-line removal is exact, not polygonal)
```

## Context

`P14-VIEW-001` draws **one** object: a feature, or one component moved to its
solved transform. `P14-HLR-001` classifies visible against hidden for **one**
body, and left a checklist item open in as many words — "validate assemblies
with occlusion" — because occlusion *between* components was unreachable when
a view drew one of them.

This milestone has to lift that, and the shape of the lift decides whether the
drawings are right:

```text
correctness   a component in front must hide the one behind it, and a
              drawing that gets this wrong looks entirely plausible
provenance    a projected line must still say which OCCURRENCE it came from,
              or dimensions, balloons and stable references have nothing to
              attach to later
identity      the same part placed three times is three occurrences, and
              collapsing them is how a BOM comes out wrong
```

There is a hard constraint from the architecture: OCCT may only be reached
from `src/core/geometry/occt`, so whatever is chosen, the kernel call stays in
`geometry` (layer 0) and `drawing` (layer 4) consumes a BetterCAD type.

And one from P13: **the active occurrence set already exists.**
`assembly::activeComponents()` applies the document's active configuration and
both layers of suppression. Any second answer to "what is in this assembly"
would be a second source of truth about it.

## Options

### 1. Fuse the occurrences into one body, then run the existing HLR

Boolean-fuse every active occurrence's transformed body and hand the result to
`hiddenLineDrawing(body, basis)` unchanged.

```text
+ no new geometry API at all; every later stage works as it does today
- PROVENANCE IS DESTROYED. A fuse produces one shape; nothing in it says
  which occurrence a face came from, and ADR-012 already records that this
  codebase has no stable edge name to recover it with
- it changes the geometry. A fuse removes the faces where two components
  touch and merges coplanar ones, so two bolted plates would lose the line
  between them -- a line a drawing is required to show
- a boolean over a large assembly is slow and can fail, and a failed fuse
  fails the whole drawing for a reason that has nothing to do with drawing
- components that merely touch or interfere would be silently welded
```

### 2. Run HLR per occurrence and overlay the results

Call the existing one-body routine once per occurrence and concatenate.

```text
+ trivial; provenance is free, because each call is one occurrence
+ parallelisable
- IT IS WRONG, and wrong in the way that looks right. Each call classifies a
  component against ITSELF, so a component hidden entirely behind another
  comes back fully visible. Every rear component would be drawn in solid
  lines over the front one
- no amount of post-processing repairs it: the depth information needed to
  decide which of two components is in front was never computed
```

### 3. One HLR problem, every occurrence added to it, results extracted per shape

`HLRBRep_Algo::Add()` takes each occurrence's transformed body, `Hide()` runs
once over all of them, and `HLRBRep_HLRToShape`'s per-shape overloads
(`VCompound(S)`, `HCompound(S)`, …) return each occurrence's own edges.

```text
+ occlusion between components is what the algorithm computes, not something
  assembled afterwards. It is the same computation that already decides
  whether a part hides itself
+ provenance survives: each extraction is asked for one shape, so the edges
  come back already attributed
+ the one-body case becomes this with one body, so there is ONE pipeline and
  the part path cannot drift from the assembly path
+ no geometry is altered: no fuse, no merge, nothing welded
- the kernel's cost is superlinear in the number of faces, and a big
  assembly is one big problem rather than many small ones
- every occurrence must be present before the algorithm runs, so an assembly
  that cannot produce one body cannot produce a partial drawing (which
  ADR-005 and section 4 of this milestone's brief require anyway)
```

## Decision

**Option 3.** An assembly view is one hidden-line problem containing every
active occurrence, and each occurrence's lines are extracted separately so the
drawing knows which occurrence drew them.

The decisive argument against option 2 is that it does not compute the thing
the milestone exists to compute. The decisive argument against option 1 is
that it answers the occlusion question correctly and destroys the identity
question in the process — and identity is what `P14-BOM-001`, `P14-STREF-001`
and every dimension on an assembly drawing will need.

Option 3's cost is real and is accepted rather than hidden: a large assembly is
one large kernel problem, and nothing here caches it. That is recorded as a
known limitation, with the same reasoning `P14-HLR-001` gave — a cache needs an
invalidation rule decided deliberately, and ADR-011 and ADR-014 make drawing
geometry derived.

## Consequences

```text
+ geometry::hiddenLineDrawing gains an overload taking a span of bodies, and
  the single-body one calls it. ProjectedEdge gains `source`, the index of
  the body it came from, so the kernel's answer is attributed at the point it
  is read rather than guessed at later.
+ drawing::DrawnEdge gains an optional ComponentId. It is empty for a view of
  a part, set for every line of an assembly view, and it survives the
  coincident-line merge: when a front component's visible edge and a rear
  one's hidden edge draw the same line, the surviving line keeps the
  occurrence that won under ISO 128, and the loser's occurrence is not
  silently inherited.
+ A view says what it looks at with an explicit ViewSubject -- Object or
  Assembly -- rather than by leaving a field invalid. A file that means the
  whole assembly says so; a file written before this milestone has no such
  key and reads as Object, which is what it was.
+ WHICH occurrences are active is asked of assembly::activeComponents() on
  every call, so configuration and suppression have exactly one
  implementation and a drawing cannot disagree with the solver about what is
  in the assembly.
- An assembly view fails as a whole when any ACTIVE occurrence lacks a solved
  transform or a body. There is no partial assembly drawing, because a
  drawing missing a component looks exactly like a drawing of a smaller
  machine.
- The kernel cost is one problem over the whole assembly, and nothing caches
  it. A view drawn N times runs it N times.
```
