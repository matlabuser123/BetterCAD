# P17-ASSEMBLY-001 — adversarial review

```text
RESULT: PASS
```

Twenty-eight attacks, from brief section 130, asked against the final diff.
**Five** found real defects in my own work; all five were fixed, and two of
them were found by mutation probes rather than by reading. The rest are
answered with the mechanism that closes them.

## The five defects this review found

### D1 — the symmetry assertion claimed exact zero

The first draft asserted `max |K - K^T| == 0.0`, reasoning that the scatter
writes `Ke(a,b)` and `Ke(b,a)` from the same symmetric source in the same pass.

That reasoning was wrong. `computeTet4Stiffness` forms
`Ke(a,b) = sum_k B[k][a] * (DB)[k][b]`, so `Ke(a,b)` and `Ke(b,a)` are
*different sums of different products* — equal in exact arithmetic because `D`
is symmetric, equal to within rounding in floating point. The measured element
asymmetry is 9.537e-07 against a `|Ke|` of 1.131e+10, which is a few ulps.

Fixed by **deriving** the bound instead of asserting a constant: the test
measures the elements' own asymmetry and the worst contributor count, and
checks the global error against their product. It came out at exactly twice the
element asymmetry against a bound of twenty-two times it. A tolerance with a
reason, as CLAUDE.md requires.

### D2 — the single-contributor regime does not exist, and the test required it

The draft asserted that some global entry is reached by exactly one element —
the stand-in for the one-element fixture that cannot be built. It failed at
`0 > 0`: on a Tet4 mesh of a solid, every pair of nodes sharing a tetrahedron
shares more than one, so the minimum contributor count is two.

Fixed by checking the sum **in every contributor-count group** instead — fifteen
groups from 2 to 22 contributors — and recording the distribution. Strictly
stronger than the claim it replaced, and the absence of the single-contributor
case is now stated rather than assumed away.

### D3 — the large-mesh test asserted an arbitrary ratio

`dense > 50 * sparse` came out at thirty-two and failed. The threshold, not the
code, was deciding the verdict — exactly what brief section 77 warns against.

Fixed by measuring the **scaling**: two sizes of the same model, with entries
per row (33 → 40) staying bounded while `Ndof` grew six-fold, plus the exact
structural bound `nnz <= 144 * tets`. Neither contains a tuned number. The
storage figures (1.56 MiB against 49.61 MiB) are now reported, not asserted.

### D4 — the suite could not see a post-hoc symmetrisation

**Found by mutation probe M8.** Inserting an averaging pass over `(i,j)` and
`(j,i)` survived every test, because the two values already agree to 1e-17.
That is the brief's automatic failure "K is symmetrised after assembly to hide
error" being invisible.

Fixed by adding the detector: a symmetrised matrix is *exactly* symmetric on
every mesh and an untouched one cannot be, when its elements are asymmetric and
several write each entry. The test now asserts a strictly positive error, with
both premises asserted first — and only on the fixture where that is sound,
because RM-MESH-01 at six tetrahedra legitimately gives exactly zero. M8 is now
killed.

### D5 — the out-of-range read was checked on a vector of zeros

**Found by mutation probe M14.** `force()[Ndof]` was checked on an *unloaded*
model, where row 0 is zero too, so a mutation that wrapped to `row % size`
returned zero and passed.

Fixed: the check now runs on a vector whose row 0 is non-zero, at three
out-of-range positions. M14 is now killed.

D4 and D5 are the same family as P17-BC-001's D4 — an assertion whose subject
was not what it appeared to be — and both were found by probing rather than by
reading, which is the clearest argument in this milestone for running the
probes at all.

## The twenty-eight attacks

### Can duplicate sparse contributions be overwritten instead of summed?

No. The symbolic pass gives exactly **one slot per (row, column)** and every
contribution does `+=` into it, so a duplicate is a sum by construction rather
than by a library's reduction policy. Fifteen contributor-count groups, from 2
to 22 contributors per entry, each checked against an independent sum. The
probe that replaces `+=` with `=` is killed by nine tests.

### Can a shared-node Ke contribution be lost?

No. The pattern is checked **both ways** against an independently computed set
of contributing pairs, so a missing pair is visible before any value is; and
the dense oracle compares every cell in both directions. The probe that drops
one local row is killed by nine tests.

### Can local Tet4 DOF ordering differ from the global P17-DOF ordering?

No. `elementDegreesOfFreedom` is public for exactly this: the test asks
P17-DOF what index each (node, component) has and checks it is what that local
position holds, for all twelve, and asserts `localDofIndex` agrees. Five
`static_assert`s sit beside the definition.

### Can raw NodeId be used as a matrix index?

No, and the probe that tries is the broadest kill in the set: **24 of 26**
tests. Searched over the implementation with comments stripped, there is no
`3 * node`, no `- 1` and no row computed from a handle. Every row comes from
`FreeEquationMap::equationOf`, which ADR-037 made a zero-based position for
precisely this reason.

### Can K dimensions depend on max NodeId instead of node count?

No. `rows()` is `freeCount()` of the empty-constraint numbering, which is
`3 * nodeCount()`. Asserted on every fixture, including meshes whose handles
are sparse — and the raw-handle probe, which is what that defect looks like, is
killed.

### Can F dimensions differ from K?

No. Both are asserted equal to `3 * nodeCount()` on every fixture, and the
superposition test compares the two vectors' sizes before comparing entries.

### Can unordered element traversal change floating sums?

It would, which is why the order is frozen and recorded.
`elementOrder()` is asserted to equal the mesh's own ascending-`ElementId`
order; the probe that records it reversed is killed by four tests. No
unordered container appears anywhere in the module.

### Can unordered triplet reduction change values between presets?

Not applicable, and deliberately so: there is no triplet staging and no
reduction step. ADR-038 records that this was one of the reasons for the
symbolic pass — Eigen documents `setFromTriplets` as summing duplicates but
not as summing them in a specified order.

### Can sparse duplicate reduction be library-order dependent?

No library is involved. `bettercad_structural` links no linear algebra; the
search for `Eigen` over the implementation returns zero.

### Can parallel assembly race on shared K entries?

There is no parallel assembly. Searched: `thread`, `atomic`, `omp` and
`parallel` each appear zero times. Recorded as *race-sensitive assembly
avoided by design*.

### Can atomics make results scheduling-dependent?

No atomics. Same search.

### Can assembly add an epsilon diagonal to hide singularity?

No, and the probe that scales the diagonal by 1.000001 is killed by eight
tests — the rigid-body modes being the detector, since a regularised matrix has
no null space.

### Can free-body rigid modes disappear?

No. All six are verified at 1e-17 relative on the unit fixture and the three
translations at 1e-17 on RM-MESH-04. The nullity is exactly six, with the sixth
and seventh eigenvalues more than four orders of magnitude apart so the
threshold does not decide the count.

### Can post-hoc symmetrisation hide a scatter bug?

It could, until D4 was fixed. The detector is now in place and M8 is killed.

### Can one invalid Tet be silently skipped?

No. `computeTet4Kinematics` and `computeTet4Stiffness` refusals are propagated
with the `ElementId`, and the probe that disables the check survives only
because no degenerate element can reach assembly — possession of a
`StructuralModel` is the evidence. Recorded as unreachable, with the branch
kept because a future input path would make it live. The probe that skips a
*valid* element is killed by eleven tests.

### Can one invalid load be silently skipped?

No. A load naming a node the numbering lacks fails rather than being dropped.
Also unreachable through the production API, for the matching reason:
`PreparedLoads` possession proves every target resolved against this mesh.

### Can partial K/F be published after a later failure?

No. All working state lives in one local struct and the system is constructed
in one step through its only private constructor, after every validation. There
is no default constructor at all, so a half-built system is unrepresentable —
three build-failure cases enforce it.

### Can P17-ASSEMBLY recompute traction or pressure instead of consuming P17-LOAD?

No. Searched with comments stripped: `pressure`, `traction`, `facet`,
`density`, `gravity` — zero occurrences each. The pressure and gravity tests
compare the assembled `F` against the *prepared* field entry by entry as well
as against the analytical resultant, so a second integration would disagree.

### Can P17-ASSEMBLY recompute Ke instead of using P17-ELEM?

No. `matrixB`, `matrixD`, `detJ`, `lame`, `shear` — zero occurrences each. The
only element algebra is the two `computeTet4*` calls.

### Can P17-ASSEMBLY renumber DOFs independently?

No. It builds the numbering through `buildMeshDofMap` and the row space through
`buildFreeEquationMap`, both P17-DOF's, and carries the numbering on the system
so a consumer never rebuilds one. There is no equation counter and no index
arithmetic.

### Can a material change incorrectly leave K reusable?

No. `AssemblySource` carries the material identity and revision, and the test
asserts the source moves on a modulus edit. The entrywise check that `K(2E)`
is exactly `2 K(E)` is the stronger half: the dependency is real, not nominal.

### Can a load change incorrectly leave F reusable?

The assembled `F` is rebuilt from the prepared loads on every call and nothing
caches it — there is no cache in this milestone at all. The test asserts two
load cases give different `F` and identical `K`.

### Can a restraint-only edit unnecessarily force a K rebuild?

This is the one the brief asks to get *right* rather than merely safe, and it
is why `AssemblySource` has six fields and not eight. `StructuralResultSource`
carries `analysisRevision`, which P17-DATA-001 deliberately made cover loads,
restraints **and** solver settings in one counter — so stamping the system with
it would report a system stale after an edit that cannot change it.
`AssemblySource` carries no analysis field, and the test asserts a restraint
edit leaves `K`, `F` **and** the source untouched.

The alternative — splitting the analysis revision — would mean a second counter
in a qualified data model, which P17-BC-001 deliberately did not add. Recorded
in the header as the reason, not left as a silence.

### Can an M1 load vector be assembled with M2 stiffness because NodeIds repeat?

No. `loads.describes(mesh)` compares the `MeshStamp` **and** the node count,
for the reason `MeshDofMap::describes` records. The test remeshes the same
unchanged model — so the handles genuinely do overlap — and the assembly is
refused. The probe that disables the check is killed.

### Can 32-bit sparse indices overflow?

The index is 64-bit and the bound is a compile-time proof in the header, not a
runtime branch. `-Wconversion -Wsign-conversion -Werror` with zero warnings is
what stops a narrowing slipping in. The probe is not expressible at any size
this suite can mesh, and that is recorded rather than reported as a kill.

### Can a large production assembly accidentally allocate a dense Ndof squared matrix?

No. The only allocations are `rowStart` (Ndof+1), `inner` and `values` (nnz),
`force` (Ndof) and the symbolic pattern (O(nnz)). `largestSymmetryError` walks
stored entries with a binary search for each counterpart, so even the
validation does not densify. Measured: 1.56 MiB against 49.61 MiB for a dense
equivalent, with entries per row bounded as the mesh grows.

### Can zero targeted tests run while qualification says PASS?

No. The selection was counted before it was trusted:

```text
ctest -N -R "StructuralSystem_|structasm"     Total Tests: 32
    20 unit + 6 reference + 6 compile-fail
```

and the final regression is **unfiltered** in all three presets. P17-DOF-001
found a real instance of the opposite failure, so the counts are derived from
inside the selection rather than from reading the filter.

### Can qualification use stale binaries or a different source tree?

No. Each preset is configured, has **every build output removed**, is rebuilt,
and is tested only after a successful build; the no-op rebuild check proves the
binaries CTest ran are the ones just produced. The harness reads the eight
component hashes itself before the first build and again after the last test
run. See [FREEZE.md](FREEZE.md).

## What was NOT found

No hidden global state; no behaviour that exists only for tests; no
architectural boundary crossed — `bettercad_structural` links exactly what it
linked before, and the layering test passes; no widened scope — no solve, no
constraint application, no reduced system, no post-processing, no persistence,
no command, no GUI.

**And no predecessor production file was modified.** `git status` over `src`
and `include` shows two new files and a one-line addition to
`src/structural/CMakeLists.txt`. So brief section 146's requalification
requirement has nothing to act on: P17-ELEM, P17-LOAD, P17-DOF and P17-BC are
untouched, and their suites pass unchanged inside the full run.
