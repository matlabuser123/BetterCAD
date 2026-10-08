# P17-BC-001 — the P17-LOAD-001 requalification

## What changed, and why

P17-LOAD-001 needed "a `FaceName`, resolved through P16's mapping, or the
reason it could not be". It wrote that in an anonymous namespace inside
`src/structural/StructuralLoad.cpp`. P17-BC-001 needs **exactly the same three
checks** for a restraint.

Copying them would have put the mapping *semantics* in two places, so a future
change to P16's mapping states would have had two homes and one of them would
have been missed. So the resolution moved to a shared helper and both consumers
call it:

```text
NEW   include/bettercad/structural/StructuralTarget.hpp
NEW   src/structural/StructuralTarget.cpp
          structural::resolveFaceTarget(map, FaceName) -> facets, or the reason
          structural::faceTargetProblem(map, FaceName) -> TargetProblem, or none

CHANGED   src/structural/StructuralLoad.cpp
          its private `resolveTarget` now maps TargetProblem onto LoadProblem
          and keeps its own diagnostics
```

**Behaviour is unchanged.** The three checks are the same checks in the same
order, lifted verbatim:

```text
boundaryFacetsOf fails      selector malformed on its own terms
!fullyResolved()            names no face of the body as it is now
facets.empty()              resolved, and nothing attributed
```

What is **not** shared is the diagnostics. A load "has nothing to act on" and a
restraint "has nothing to constrain", and each message carries its own
identity. `TargetProblem` is deliberately neutral and each consumer maps it onto
its own problem enum with its own wording. P17-LOAD-001's messages are byte-for-
byte what they were.

## Why this requires a requalification

`src/structural/StructuralLoad.cpp` is production code in a milestone that is
already `[x]`. Modifying a qualified shared path silently is what the workflow
forbids, and P17-BC-001's brief says so explicitly:

> If implementing P17-BC changes production code used by the already-qualified
> P17-LOAD-001, then affected P17-LOAD evidence is stale. Required: rerun
> P17-LOAD targeted qualification and document the requalification.

## What was re-run

```text
targeted, debug-ext, during implementation
    23/23 P17-LOAD-001 tests PASS, 100%, 61.88 s
    unchanged from the counts P17-LOAD-001's own evidence records

targeted, debug-ext, with the shared helper in place and the whole
P17-BC suite added
    185/185 PASS over the structural, meshing-mapping and compile-fail
    selection (StructuralBC_, StructuralLoad_, StructuralDof_,
    StructuralData_, Tet4, StructuralMaterial_, StructuralAnalysis,
    structbc, structdof, structids, Mesh_, Mapping_, GeometryMeshMap),
    370.21 s

mutation probes
    every probe ran the P17-BC AND P17-LOAD suites together, 55 tests, so a
    mutation of the SHARED resolver is killed by both milestones' tests --
    which is the point of sharing it. M3 (unresolved target used anyway) is
    killed by 5 tests drawn from both

three-preset unfiltered qualification
    see FREEZE.md -- the full suite in debug-ext, release-ext and
    debug-shared-ext, which re-establishes P17-LOAD-001 formally
```

## Forward note

A dated note has been added to `docs/verification/P17-LOAD-001/README.md`
recording that its `StructuralLoad.cpp` was refactored by P17-BC-001 and where
the requalification evidence is. Its own `FREEZE.md` fingerprint is left
exactly as it was: that fingerprint is the tree P17-LOAD-001 was qualified on,
and editing it would be falsifying a record. The note points forward instead.

## One asymmetry, recorded rather than tidied

`StructuralConstraints.cpp` calls `resolveFaceTarget` **once** on the path that
succeeds and asks `faceTargetProblem` only to classify a failure.
`StructuralLoad.cpp` still asks both unconditionally, so it resolves a face
twice per load.

That is a real inefficiency and it is deliberate: the authorized change to
P17-LOAD-001 was the **rewiring**, and the minimum change that achieves it is
the one that was made. Tightening its call pattern would be a second,
unauthorized edit to a qualified file, and the scope rule does not bend for a
tidiness improvement. The cost is one extra mapping lookup and one facet-vector
copy per face load — a handful per model, on no hot path.

It belongs to whichever milestone next has reason to touch that file.
