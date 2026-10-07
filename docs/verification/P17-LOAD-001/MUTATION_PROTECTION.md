# P17-LOAD-001 — mutation protection

```text
SUBJECT:  the highest-risk load-integration and load-authority errors,
          introduced deliberately, to establish that the suite would CATCH
          them rather than that it passes
PROBES:   13
KILLED:   12
SURVIVED:  1   M13, a no-op for a stated structural reason
```

Harness: `loadmut/run.sh` in the session scratchpad, the same shape the
previous two milestones used. Each probe restores the pristine sources first,
applies **one** literal substitution verified to be present exactly once,
rebuilds `bettercad_tests`, runs the 23 load tests, and restores. The run ends
with a **restore build** and a re-run — `post-restore: 100% tests passed out of
23` — because a restored source is not a restored binary.

## Results

| Probe | Mutation | Verdict | Tests failed |
| --- | --- | --- | --- |
| M1 | `A/3` becomes `A/2` in the facet nodal force | **KILLED** | 7 of 23 |
| M2 | the whole facet force assigned to corner 0 | **KILLED** | 2 of 23 |
| M3 | pressure sign flipped (`+p n` instead of `-p n`) | **KILLED** | 3 of 23 |
| M4 | pressure takes the normal's component magnitudes | **KILLED** | 3 of 23 |
| M5 | traction projected onto the facet normal | **KILLED** | 4 of 23 |
| M6 | shared-node contributions overwritten, not added | **KILLED** | 10 of 23 |
| M7 | `std::map` replaced by `std::unordered_map` | **KILLED** | 2 of 23 |
| M8 | an unresolved target accepted as zero load | **KILLED** | 2 of 23 |
| M9 | the missing-density check bypassed | **KILLED** | 1 of 23, by a crash |
| M10 | a nodal force ignores its `MeshStamp` | **KILLED** | 1 of 23 |
| M11 | the area vector not halved | **KILLED** | 8 of 23 |
| M12 | gravity's `V/4` becomes `V/2` | **KILLED** | 2 of 23 |
| M13 | gravity uses `abs(signedVolume)` | *SURVIVED* | — |

Full output in `loadmut/results.txt`. Four are worth drawing out.

### M2 preserves the total force exactly, and is caught anyway

Putting the whole facet force on one corner keeps `sum F` **identical** — the
three thirds and the single whole are the same total. So the force-resultant
assertions cannot see it, and only two tests do:

```text
StructuralLoad_ConvergesToTheAnalyticLoadAsTheSurfaceIsRefined
StructuralLoad_ResolvesACanonicalFaceTargetAgainstAReferenceMesh
```

both through the **moment**. That is the whole argument for the brief's
insistence on first-moment conservation: a wrong distribution with a right
total is the failure mode a force check is blind to, and the moment is what
sees it. Recorded here because it is the single best justification in the
milestone for a test that would otherwise look redundant.

### M6 is caught by ten tests, which says the accumulation is load-bearing

Overwriting instead of adding breaks almost everything — the resultants, the
superposition, the closed-surface cancellation, the gravity weight. A node on a
meshed face is shared by several facets, so overwriting discards most of the
load. The breadth of the kill is the evidence that `Accumulator::add` really is
on the path for every load type.

### M9 kills by CRASHING, and that is reported as such

```text
2675 - StructuralLoad_GravityWeighsTheMeshedBodyAndNeedsADensity (Exit code 0xc0000409)
```

With the density check bypassed, `material.density()` is `nullopt` and `->si()`
on it is undefined behaviour; the process dies on `__fastfail`. The test does
catch the mutation, and the honest description is that it catches it by the
program terminating rather than by an assertion. A reader should not read
"KILLED" there as "a test asserted the right thing" — what it establishes is
that the check is reachable and that nothing downstream tolerates its absence.
The *behaviour under a missing density* is asserted properly by the unmutated
test, which requires `DensityMissing` and the diagnostic naming it.

### M11 catches a factor of two in eight places

Dropping the `0.5` from the area vector doubles every area, so it breaks the
analytical triangle test, both resultants, the convergence, the closed surface
and the reference-mesh integration. It is listed because the half is the single
easiest thing to omit — the cross product of two edges spans the
parallelogram — and because the test that names it explicitly
(`FacetAreaVectorIsHalfTheEdgeCrossProduct`) asserts the wrong answer as well
as the right one.

## The survivor

### M13 — `abs(signedVolume)` in the gravity integration is a no-op

Replacing `signedVolume(...)` with `std::abs(signedVolume(...))` changed
nothing, and it cannot: by the time `prepareStructuralLoads` runs, its argument
is a `StructuralModel`, and possession of one proves P16 validated the mesh —
which refuses any tetrahedron whose signed volume is not positive. `abs` of a
positive number is that number.

**This is the third milestone in which a defensive `abs` survived for exactly
this reason.** P17-ELEM-001's M7 was the same shape, and its resolution applies
here too: the mutation that *is* reachable is the one at the **predicate**,
where orientation is actually decided, and that one is killed —
`Tet4Element_RejectsAnInvertedTetrahedronRatherThanReorderingIt` and
P17-ELEM-001's M1 and M11 cover it.

So the survival is evidence about the architecture rather than a hole in the
suite: ADR-036's possession gate makes downstream defensive checks
unreachable. A test contrived to kill M13 would have to construct a
`StructuralModel` over an inverted mesh, which is precisely what that type
exists to make impossible.

It is recorded rather than removed. The `abs` is **not** in the production code
— the production line is the plain signed volume, with a comment saying why —
so there is nothing defensive to delete; the probe simply demonstrates that
adding one would be inert.

## Coverage of the claims

```text
A t / 3 per corner, not A/2, not all on one      M1, M2
the area vector is halved                        M11
pressure is -p n, inward for positive p          M3
pressure honours the oriented normal             M4
traction is global and does not follow it        M5
shared nodes ACCUMULATE                          M6
the emitted order is deterministic               M7
an unresolved target is refused, not zeroed      M8
a missing density is refused, never defaulted    M9
a nodal force is bound to its mesh generation    M10
gravity gives each node V/4                      M12
abs() cannot mask an inversion                   M13 is inert because the gate
                                                 is upstream; P17-ELEM's M1
                                                 kills it where it is reachable
```

Twelve claims, thirteen probes, twelve killed and one inert with its reason
measured rather than argued.
