# P16-MAP-001 — mutation testing harness

```text
PURPOSE:  break the correspondence on purpose, and see whether the suite
          notices. A mutation that SURVIVES marks a gap in the tests.
```

Each entry in `mutations.json` names **the file it applies to**, because the
provenance this milestone builds runs through four of them:

```text
src/core/geometry/occt/OcctMesh.cpp          the kernel's per-face grouping
src/meshing/SurfaceMesh.cpp                  carried through unification + sort
src/meshing/VolumeMesh.cpp                   carried to the boundary by position
src/meshing/GeometryMeshMap.cpp              inverted, joined to names, queried
include/bettercad/meshing/GeometryMeshMap.hpp the completeness predicate
```

`mutate.sh` applies one at a time, rebuilds, runs the suite, records the
verdict, and restores every pristine source at the end — including after a
failure, so a mutation cannot be left in the tree.

```text
mutate.sh       the loop: apply, rebuild, run, record, restore
apply.py        applies one mutation by index, or restores the snapshot. Every
                file goes back to pristine first, so the only difference from
                the committed tree is the one mutation. A pattern that is
                absent, or that occurs more than once, is refused rather than
                guessed at.
mutations.json  the 15 mutations
results.txt     the run
```

The harness also writes `pristine/` (its snapshot of the four sources),
`build.log` and `run.log` while it works. Those are transient and are not
committed: the snapshot would duplicate source inside `docs/`, and the two logs
hold only the last mutation's output. `results.txt` is the record.

## Why the whole `[meshing]` suite and not only `[map]`

An attribution mistake can break the surface or the volume contracts the
mapping is built on, and that is a kill worth seeing rather than hiding. Two of
the three provenance mutations are caught largely by `P16-SURF-001`'s and
`P16-VOL-001`'s own tests, which is exactly the point: the chain is held by
more than this milestone's tests.

## Reading the verdicts

```text
killed (assertions: ...)   the suite failed, with the count. The intended
                           outcome.
killed by the COMPILER     the mutant did not build. WEAKER evidence: it shows
                           the code would not compile, not that the tests would
                           notice.
*** SURVIVED ***           a gap. To be diagnosed, not explained away.
```

## Three guards deliberately absent from the list

A mutation against a branch that **cannot fire through the public API** has a
predetermined verdict: it survives, and reports a gap that is not one.
Including such a mutation is misleading and omitting it silently is worse, so
the three are named here and examined in `ADVERSARIAL_REVIEW.md`.

```text
triangulate's lockstep pairing check
    verifies that BRepBuilderAPI_Copy preserves face order, which it does. A
    guard against a FUTURE OCCT change, like a static_assert.

owningTetrahedraOf's "exactly one owner" branch
    a boundary facet is by definition a face of exactly one tetrahedron, and
    the only way to hold a Triangle3 handle is to get it from a VolumeMesh,
    whose triangles are its boundary. The branch states the invariant and
    gives the diagnostic if a future mesh source breaks it.

the facesWithoutFacets counter
    triangulate refuses a face it could not mesh, so every CAD face of a
    meshable body has at least one triangle. The counter and its issue exist so
    that an attribution chain which LOST a face is reported rather than passing
    as complete; the predicate that consumes it IS tested, directly.
```

Two of these were found the hard way: they survived the first run, and
diagnosing them is what established that they are invariant guards rather than
test gaps. The third survivor of that run was a real gap and was fixed -- see
`ADVERSARIAL_REVIEW.md`.

## Running it

From the repository root, with `BETTERCAD_BUILD_ROOT` set:

```text
bash docs/verification/P16-MAP-001/qualification/mutation/mutate.sh
```

It builds and tests in `debug-ext` only. Whether a single preset can
distinguish a mutation is a different question, and the three-preset
qualification is where that is answered.
